#pragma once
#include <math.h>
#include <stdint.h>

#include <memory>
#include <mutex>
#include <vector>

#include "TinyTimeGrapher/HistoryBuffer.h"
#include "TinyTimeGrapher/KpiCalculator.h"
#include "TinyTimeGrapher/Models.h"
#include "TinyTimeGrapher/TickDetector.h"
#include "TinyTimeGrapher/TinyTimeGrapherConfig.h"
#include "TinyTimeGrapher/WatchSimulator.h"

namespace tinytimegrapher {

/**
 * @brief The measurement engine: feed it the microphone samples and read
 * back the ticks, KPIs, volume trace and KPI history.
 *
 * The audio is always analyzed once begin() was called, so the volume trace
 * (and the detection threshold) can be shown while no measurement is
 * running. start() begins a new measurement session: only then are the
 * beats collected and the KPIs calculated (once per second of audio).
 *
 * With GrapherConfig::demo the provided audio is replaced by the sound of a
 * simulated watch (see demoWatch()), so the input still sets the pace.
 *
 * All public methods are thread safe, so the audio can be provided from a
 * separate task while the GUI reads the results.
 *
 *   TimeGrapherEngine engine;
 *   engine.begin(48000);
 *   engine.start();
 *   // audio task:  engine.write(pcm, frames, channels);
 *   // GUI:         Kpis k = engine.kpis();
 */
class TimeGrapherEngine {
 public:
  TimeGrapherEngine() = default;

  /// Starts the analysis of audio with the indicated sample rate.
  bool begin(int sampleRate, const GrapherConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    return beginLocked(sampleRate);
  }

  /// Starts the analysis of audio with the indicated sample rate, keeping
  /// the current configuration.
  bool begin(int sampleRate) {
    std::lock_guard<std::mutex> lock(mutex_);
    return beginLocked(sampleRate);
  }

  /// Stops the analysis and releases the buffers.
  void end() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    detector_.reset();
    ticks_.resize(0);
    history_.resize(0);
    volume_.resize(0);
    sampleRate_ = 0;
  }

  int sampleRate() const { return sampleRate_; }

  /// Replaces the configuration. Changes to the filter apply immediately
  /// while idle and on the next start() while measuring.
  void setConfig(const GrapherConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool filterChanged = config.lowHz != config_.lowHz || config.highHz != config_.highHz;
    const bool sourceChanged = config.demo != config_.demo;
    config_ = config;
    if (sampleRate_ > 0) {
      if (sourceChanged && running_) {
        // real and simulated beats must not end up in the same session
        resetSessionLocked();
        createDetector();
      } else if (filterChanged && !running_) {
        createDetector();
      }
    }
    revision_++;
  }

  GrapherConfig config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
  }

  /// Starts a new measurement session.
  void start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sampleRate_ <= 0) return;
    resetSessionLocked();
    createDetector();
    volume_.clear();  // the trace of the session starts empty
    running_ = true;
    revision_++;
  }

  /// Stops the measurement; the results of the session stay available.
  void stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    revision_++;
  }

  bool isRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
  }

  /// Clears the ticks, KPIs and history of the current session.
  void resetSession() {
    std::lock_guard<std::mutex> lock(mutex_);
    resetSessionLocked();
    revision_++;
  }

  /// Processes mono float samples in the range -1..1.
  void write(const float* samples, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!detector_) return;
    if (config_.demo && demo_) {
      float buf[kChunk];
      for (size_t pos = 0; pos < count; pos += kChunk) {
        const size_t n = count - pos < kChunk ? count - pos : kChunk;
        demo_->generate(buf, n);
        processLocked(buf, n);
      }
    } else {
      processLocked(samples, count);
    }
  }

  /// The simulated watch of the demo mode (see GrapherConfig::demo): by
  /// default 28800 bph, +4 s/day, 0.3 ms beat error. Only valid after
  /// begin(); change its parameters while the demo mode is off.
  WatchSimulator& demoWatch() { return *demo_; }

 private:
  void processLocked(const float* samples, size_t count) {
    size_t pos = 0;
    while (pos < count) {
      // split at the block boundary of the volume trace
      size_t n = static_cast<size_t>(blockLen_ - blockFill_);
      if (n > count - pos) n = count - pos;
      detector_->process(samples + pos, n);
      pos += n;
      samples_ += n;
      blockFill_ += static_cast<int>(n);
      if (blockFill_ >= blockLen_) {
        blockFill_ = 0;
        endOfBlock();
      }
    }
  }


 public:
  /// Processes interleaved 16 bit PCM data with `channels` channels; the
  /// channel `channel` is analyzed.
  void write(const int16_t* in, size_t frames, int channels = 1, int channel = 0) {
    float fin[kChunk];
    size_t pos = 0;
    while (pos < frames) {
      const size_t n = frames - pos < kChunk ? frames - pos : kChunk;
      for (size_t i = 0; i < n; ++i) fin[i] = in[(pos + i) * channels + channel] / 32768.0f;
      write(fin, n);
      pos += n;
    }
  }

  /// The KPIs of the last second of a measurement.
  Kpis kpis() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return kpis_;
  }

  /// Smoothed input level (0..1) of the filtered and amplified signal.
  double level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return detector_ ? detector_->level() : 0.0;
  }

  /// Gain currently applied to the filtered signal in dB.
  double gainDb() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return detector_ ? detector_->gainDb() : 0.0;
  }

  /// Copies the newest (up to `maxCount`) ticks, oldest first.
  void ticks(std::vector<Tick>& result, size_t maxCount = TTG_MAX_TICKS) const {
    std::lock_guard<std::mutex> lock(mutex_);
    copyNewest(ticks_, result, maxCount);
  }

  /// Copies the newest (up to `maxCount`) KPI history points, oldest first.
  void history(std::vector<KpiPoint>& result, size_t maxCount = TTG_MAX_HISTORY) const {
    std::lock_guard<std::mutex> lock(mutex_);
    copyNewest(history_, result, maxCount);
  }

  /// Number of KPI history points (seconds) of the session.
  size_t historySize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return history_.size();
  }

  /// Copies the volume trace (block peaks, linear 0..1, newest last,
  /// TTG_VOLUME_RATE points per second) and returns the current detection
  /// threshold on the same scale.
  float volume(std::vector<float>& result) const {
    std::lock_guard<std::mutex> lock(mutex_);
    copyNewest(volume_, result, volume_.capacity());
    return threshold_;
  }

  /// Changes whenever new data is available (a volume point, a KPI or a
  /// state change), so that a GUI can redraw only when needed.
  uint32_t revision() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return revision_;
  }

  /// Seconds of audio received since begin(), measured with the audio
  /// clock (see ClockCalibrator).
  double audioTimeSec() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sampleRate_ > 0 ? static_cast<double>(samples_) / sampleRate_ : 0.0;
  }

  /// Error of the audio clock in ppm (positive: runs fast), e.g. measured
  /// with a ClockCalibrator. The rate is corrected with it, except in the
  /// demo mode.
  void setClockCorrectionPpm(double ppm) {
    std::lock_guard<std::mutex> lock(mutex_);
    clockPpm_ = ppm;
  }
  double clockCorrectionPpm() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clockPpm_;
  }

  /// Seconds of audio measured in the current session.
  double sessionSec() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sampleRate_ > 0 ? static_cast<double>(samples_ - sessionStart_) / sampleRate_ : 0.0;
  }

 private:
  static constexpr size_t kChunk = 128;

  mutable std::mutex mutex_;
  GrapherConfig config_;
  int sampleRate_ = 0;
  bool running_ = false;
  std::unique_ptr<TickDetector> detector_;
  std::unique_ptr<WatchSimulator> demo_;
  KpiCalculator calculator_;
  Kpis kpis_;
  HistoryBuffer<Tick> ticks_;
  HistoryBuffer<KpiPoint> history_;
  HistoryBuffer<float> volume_;
  std::vector<Tick> window_;
  float threshold_ = 0.0f;
  uint32_t revision_ = 0;
  double clockPpm_ = 0.0;

  int64_t samples_ = 0;
  int64_t sessionStart_ = 0;
  int64_t detectorStart_ = 0;
  int64_t lastKpiSec_ = 0;
  int blockLen_ = 1;
  int blockFill_ = 0;

  /// Newest tick of the session and the learned beat interval, for the beat
  /// numbering.
  bool hasLastTick_ = false;
  Tick lastTick_;
  double beatUnit_ = kNoValue;

  bool beginLocked(int sampleRate) {
    if (sampleRate <= 0) return false;
    if (sampleRate != sampleRate_ || ticks_.capacity() == 0) {
      sampleRate_ = sampleRate;
      ticks_.resize(TTG_MAX_TICKS);
      history_.resize(TTG_MAX_HISTORY);
      volume_.resize(TTG_VOLUME_POINTS);
      window_.reserve(TTG_MAX_TICKS);
      std::unique_ptr<WatchSimulator> watch(new WatchSimulator(sampleRate));
      if (demo_) {
        // keep the parameters of the previous simulator
        watch->bph = demo_->bph;
        watch->rateSPerDay = demo_->rateSPerDay;
        watch->beatErrorMs = demo_->beatErrorMs;
        watch->lockDropMs = demo_->lockDropMs;
        watch->level = demo_->level;
        watch->noise = demo_->noise;
      } else {
        watch->bph = 28800;
        watch->rateSPerDay = 4.0;
        watch->beatErrorMs = 0.3;
        watch->level = 0.05f;
      }
      demo_ = std::move(watch);
        blockLen_ = sampleRate / TTG_VOLUME_RATE;
      if (blockLen_ < 1) blockLen_ = 1;
      blockFill_ = 0;
      running_ = false;
      resetSessionLocked();
    }
    createDetector();
    return true;
  }

  void createDetector() {
    detectorStart_ = samples_;
    detector_.reset(new TickDetector(sampleRate_, config_.lowHz, config_.highHz, config_.sensitivity,
                                     [this](const Tick& t) { onTick(t); }));
    applyDetectorConfig();
  }

  void applyDetectorConfig() {
    detector_->agc = config_.agc;
    detector_->setManualGainDb(config_.gainDb);
    detector_->sensitivity = config_.sensitivity;
  }

  void resetSessionLocked() {
    ticks_.clear();
    history_.clear();
    kpis_ = Kpis();
    hasLastTick_ = false;
    beatUnit_ = kNoValue;
    sessionStart_ = samples_;
    lastKpiSec_ = 0;
  }

  void endOfBlock() {
    applyDetectorConfig();
    volume_.push(detector_->takeBlockPeak());
    threshold_ = static_cast<float>(detector_->threshold());
    revision_++;
    if (!running_) return;
    const int64_t sec = (samples_ - sessionStart_) / sampleRate_;
    if (sec > lastKpiSec_) {
      lastKpiSec_ = sec;
      publishKpis(static_cast<double>(sec));
    }
  }

  void onTick(const Tick& raw) {
    if (!running_) return;
    Tick t = raw;
    // the detector counts from its creation: make the time session relative
    t.timeSec += static_cast<double>(detectorStart_ - sessionStart_) / sampleRate_;
    const int bph = kpis_.nominalBph > 0 ? kpis_.nominalBph : config_.bph;
    const double unit = bph > 0 ? 3600.0 / bph : beatUnit_;
    int64_t steps = 1;
    if (hasLastTick_ && hasValue(unit)) {
      steps = llround((t.timeSec - lastTick_.timeSec) / unit);
      if (steps < 1) steps = 1;
    }
    if (hasLastTick_ && steps == 1) beatUnit_ = t.timeSec - lastTick_.timeSec;
    t.beat = (hasLastTick_ ? lastTick_.beat : -1) + steps;
    lastTick_ = t;
    hasLastTick_ = true;
    ticks_.push(t);
  }

  void publishKpis(double sec) {
    window_.clear();
    for (size_t i = 0; i < ticks_.size(); ++i) {
      if (ticks_[i].timeSec >= sec - config_.windowSec) window_.push_back(ticks_[i]);
    }
    const double snr = ticks_.empty() ? kNoValue : detector_->snrDb(ticks_.back().peak);
    // the simulated watch is not measured with the audio clock
    kpis_ = calculator_.compute(window_.data(), window_.size(), config_, snr, detector_->level(),
                                config_.demo ? 0.0 : clockPpm_);
    KpiPoint p;
    p.timeSec = static_cast<float>(sec);
    p.rateSPerDay = static_cast<float>(kpis_.rateSPerDay);
    p.beatErrorMs = static_cast<float>(kpis_.beatErrorMs);
    p.amplitudeDeg = static_cast<float>(kpis_.amplitudeDeg);
    history_.push(p);
  }

  template <typename T>
  static void copyNewest(const HistoryBuffer<T>& source, std::vector<T>& result, size_t maxCount) {
    const size_t n = source.size() < maxCount ? source.size() : maxCount;
    result.resize(n);
    const size_t first = source.size() - n;
    for (size_t i = 0; i < n; ++i) result[i] = source[first + i];
  }
};

}  // namespace tinytimegrapher
