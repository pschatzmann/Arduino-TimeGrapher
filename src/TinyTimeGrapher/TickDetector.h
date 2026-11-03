#pragma once
#include <math.h>

#include <algorithm>
#include <functional>
#include <vector>

#include "AudioToolsConfig.h"
#include "AudioTools/CoreAudio/AudioBasic/Collections.h"
#include "AudioTools/CoreAudio/AudioFilter/Filter.h"
#include "TinyTimeGrapher/Models.h"
#include "TinyTimeGrapher/TinyTimeGrapherConfig.h"

namespace tinytimegrapher {

/**
 * @brief Streaming tick detector: band-pass, rectify, smooth, adaptive
 * threshold above a slowly tracked noise floor.
 *
 * The band-pass is a second-order Butterworth high-pass followed by a
 * low-pass, both from arduino-audio-tools (direct form I).
 *
 * After the first threshold crossing a short capture window records the
 * beat's sub-peaks so the lock-to-drop time (needed for the amplitude) can
 * be measured.
 *
 * The output signal (see process()) can be cleaned with a noise gate (see
 * setNoiseGateDb()) that attenuates everything between the beats.
 */
class TickDetector {
 public:
  using TickCallback = std::function<void(const Tick&)>;

  TickDetector(int sampleRate, int lowHz, int highHz, float sens, TickCallback onTick)
      : sensitivity(sens),
        sampleRate_(sampleRate),
        hp_(clampCutoff(lowHz, sampleRate), sampleRate),
        lp_(clampCutoff(highHz, sampleRate), sampleRate),
        smoothAlpha_(1.0 - exp(-1.0 / (0.0002 * sampleRate))),  // ~0.2 ms
        floorAlpha_(1.0 - exp(-1.0 / (1.0 * sampleRate))),      // ~1 s
        captureLen_(static_cast<int>(0.030 * sampleRate)),
        minSeparation_(static_cast<int>(0.0015 * sampleRate)),
        warmup_(static_cast<int64_t>(0.5 * sampleRate)),
        agcRelease_(exp(-1.0 / (3.0 * sampleRate))),       // ~3 s peak hold decay
        agcRise_(1.0 - exp(-1.0 / (1.0 * sampleRate))),    // ~1 s gain increase
        gateAttack_(1.0 - exp(-1.0 / (0.0005 * sampleRate))),  // ~0.5 ms
        gateRelease_(1.0 - exp(-1.0 / (0.002 * sampleRate))),  // ~2 ms
        gateHoldLen_(static_cast<int>(0.010 * sampleRate)),
        capture_(static_cast<size_t>(captureLen_)),
        gateDelay_(static_cast<size_t>(0.002 * sampleRate) + 1),  // 2 ms look-ahead
        onTick_(std::move(onTick)) {}

  /// Threshold as a multiple of the noise floor; may be changed while running.
  float sensitivity;

  /// Automatic gain control keeps the filtered peaks near a fixed level;
  /// otherwise the manual gain (see setManualGainDb()) applies.
  bool agc = false;

  void setManualGainDb(float db) {
    if (db != manualGainDb_) {
      manualGainDb_ = db;
      manualGain_ = static_cast<dsp_t>(pow(10.0, db / 20.0));
    }
  }

  /// Attenuates the output signal between the beats by `db` (e.g. 40) to
  /// suppress the noise; 0 (default) switches the gate off. The gate opens
  /// when a beat is detected and stays open for its capture window plus 10
  /// ms. While the gate is active the output is delayed by 2 ms, so the gate
  /// is already open when the onset of a beat arrives. Only the output is
  /// affected - not the detection.
  void setNoiseGateDb(float db) {
    gateClosedGain_ = db > 0 ? static_cast<dsp_t>(pow(10.0, -db / 20.0)) : 1;
  }

  /// Gain currently applied to the filtered signal, in dB.
  double gainDb() const { return 20 * log10(static_cast<double>(gain_)); }

  /// Current smoothed noise floor (same scale as the level).
  double noiseFloor() const { return noiseFloor_; }
  /// Smoothed level with a slow decay, for the UI meter.
  double level() const { return level_; }

  /// Detection threshold currently in force (same scale as the level).
  double threshold() const { return std::max<double>(floor_ * sensitivity, kMinThreshold); }

  /// Largest filtered level since the last call, for the volume chart.
  float takeBlockPeak() {
    const float p = static_cast<float>(blockPeak_);
    blockPeak_ = 0;
    return p;
  }

  /// Processes one sample in the range -1..1 and returns the filtered,
  /// amplified signal (clipped to -1..1).
  float process(float sample) {
    step(sample);
    const dsp_t o = lastOut_;
    return static_cast<float>(o > 1 ? 1 : (o < -1 ? -1 : o));
  }

  /// Processes `count` samples; when `out` is given it receives the
  /// filtered, amplified signal (clipped to -1..1).
  void process(const float* samples, size_t count, float* out = nullptr) {
    for (size_t i = 0; i < count; ++i) {
      const float o = process(samples[i]);
      if (out != nullptr) out[i] = o;
    }
  }

  /// Signal to noise ratio of a beat with the indicated peak.
  double snrDb(double peak) const { return 20 * log10(peak / std::max<double>(floor_, 1e-6)); }

  /// Root mean square of the samples.
  static double rms(const float* samples, size_t count) {
    double s = 0.0;
    for (size_t i = 0; i < count; ++i) s += samples[i] * samples[i];
    return sqrt(s / std::max<size_t>(1, count));
  }

 private:
  static constexpr dsp_t kMinThreshold = 0.0001;
  static constexpr dsp_t kAgcTarget = 0.5;
  static constexpr dsp_t kAgcMaxGain = 1000.0;  // 60 dB
  /// Only raise the gain while peaks stand this far above the noise floor.
  static constexpr dsp_t kAgcGate = 8.0;

  const int sampleRate_;
  audio_tools::HighPassFilterDF1<dsp_t> hp_;
  audio_tools::LowPassFilterDF1<dsp_t> lp_;
  const dsp_t smoothAlpha_;
  const dsp_t floorAlpha_;
  const int captureLen_;
  const int minSeparation_;
  const int64_t warmup_;
  const dsp_t agcRelease_;
  const dsp_t agcRise_;
  const dsp_t gateAttack_;
  const dsp_t gateRelease_;
  const int gateHoldLen_;
  std::vector<dsp_t> capture_;
  std::vector<dsp_t> gateDelay_;
  size_t gateDelayPos_ = 0;
  TickCallback onTick_;

  float manualGainDb_ = 0.0f;
  dsp_t manualGain_ = 1.0;
  dsp_t gain_ = 1.0;
  dsp_t agcEnv_ = 0.0;

  int64_t index_ = 0;
  dsp_t smooth_ = 0.0;
  dsp_t floor_ = 0.0;
  bool capturing_ = false;
  int64_t captureStart_ = 0;

  dsp_t noiseFloor_ = 0.0;
  dsp_t level_ = 0.0;
  dsp_t blockPeak_ = 0.0;
  dsp_t lastOut_ = 0.0;
  dsp_t gateClosedGain_ = 1;  // 1: gate off
  dsp_t gateGain_ = 1;
  int gateHold_ = 0;

  static float clampCutoff(int hz, int sampleRate) {
    return std::min(std::max(static_cast<float>(hz), 10.0f), sampleRate * 0.45f);
  }

  void step(dsp_t x) {
    const dsp_t f = lp_.process(hp_.process(x));
    const dsp_t y = f * applyGain(fabs(f));
    smooth_ += smoothAlpha_ * (fabs(y) - smooth_);
    level_ = std::max<dsp_t>(smooth_, level_ * static_cast<dsp_t>(0.9999));
    if (smooth_ > blockPeak_) blockPeak_ = smooth_;
    const dsp_t threshold = std::max<dsp_t>(floor_ * sensitivity, kMinThreshold);

    if (capturing_) {
      const int k = static_cast<int>(index_ - captureStart_);
      capture_[k] = smooth_;
      trackFloorClamped();
      if (k == captureLen_ - 1) finishCapture(threshold);
    } else {
      if (index_ < warmup_) {
        floor_ += floorAlpha_ * 4 * (smooth_ - floor_);
      } else if (smooth_ > threshold) {
        capturing_ = true;
        captureStart_ = index_;
        capture_[0] = smooth_;
        trackFloorClamped();
      } else {
        floor_ += floorAlpha_ * (smooth_ - floor_);
      }
      noiseFloor_ = floor_;
    }
    if (gateClosedGain_ < 1) {
      // look-ahead: the gate works on the output delayed by 2 ms
      const dsp_t delayed = gateDelay_[gateDelayPos_];
      gateDelay_[gateDelayPos_] = y;
      gateDelayPos_ = (gateDelayPos_ + 1) % gateDelay_.size();
      lastOut_ = delayed * noiseGate();
    } else {
      lastOut_ = y;
    }
    index_++;
  }

  /// Gain of the noise gate for the current sample: open while a beat is
  /// captured (plus the hold time), else the closed gain - with a short
  /// attack and release so the gate does not click.
  dsp_t noiseGate() {
    if (gateClosedGain_ >= 1) return 1;
    if (capturing_) {
      gateHold_ = gateHoldLen_;
    } else if (gateHold_ > 0) {
      gateHold_--;
    }
    const dsp_t target = (capturing_ || gateHold_ > 0) ? 1 : gateClosedGain_;
    gateGain_ += (target > gateGain_ ? gateAttack_ : gateRelease_) * (target - gateGain_);
    return gateGain_;
  }

  /// Keeps the floor following the noise while a beat is being captured.
  /// Without it a floor that falls behind a rising level (e.g. AGC gain
  /// increasing) would never recover, because every capture ends in a new
  /// trigger. The input is clamped so the beat itself hardly moves the floor.
  void trackFloorClamped() {
    floor_ += floorAlpha_ * (std::min<dsp_t>(smooth_, std::max<dsp_t>(3 * floor_, 1e-6)) - floor_);
  }

  dsp_t applyGain(dsp_t a) {
    if (!agc) {
      gain_ = manualGain_;
      agcEnv_ = 0.0;
      return gain_;
    }
    agcEnv_ = std::max<dsp_t>(a, agcEnv_ * agcRelease_);
    dsp_t wanted = kAgcTarget / std::max<dsp_t>(agcEnv_, 1e-9);
    wanted = std::min<dsp_t>(std::max<dsp_t>(wanted, 1.0), kAgcMaxGain);
    // pure noise must not be amplified up to the target, it would then
    // trigger the detector at random
    const bool signal = index_ >= warmup_ && agcEnv_ * gain_ > kAgcGate * floor_;
    const dsp_t before = gain_;
    if (wanted < gain_) {
      gain_ = wanted;
    } else if (signal) {
      gain_ += agcRise_ * (wanted - gain_);
    }
    // the floor is in amplified units; a gain change must not look like a
    // noise change
    if (gain_ != before) floor_ *= gain_ / before;
    return gain_;
  }

  void finishCapture(dsp_t threshold) {
    capturing_ = false;
    int p1 = 0;
    for (int k = 0; k < captureLen_; ++k) {
      if (capture_[k] > capture_[p1]) p1 = k;
    }
    int p2 = -1;
    for (int k = 0; k < captureLen_; ++k) {
      if (abs(k - p1) < minSeparation_) continue;
      if (p2 < 0 || capture_[k] > capture_[p2]) p2 = k;
    }
    // the second sound must stand out as a real local peak above the threshold
    const bool second = p2 >= 0 && capture_[p2] > threshold && isLocalPeak(p2);
    Tick tick;
    tick.lockDropMs = second ? static_cast<float>(abs(p2 - p1) * 1000.0 / sampleRate_) : NAN;
    // beat time = onset of the sound; the capture started at the first crossing
    tick.timeSec = static_cast<double>(captureStart_) / sampleRate_;
    tick.peak = static_cast<float>(capture_[p1]);
    if (onTick_) onTick_(tick);
  }

  bool isLocalPeak(int k) const {
    const int lo = std::max(0, k - 3);
    const int hi = std::min(captureLen_ - 1, k + 3);
    for (int j = lo; j <= hi; ++j) {
      if (capture_[j] > capture_[k]) return false;
    }
    return true;
  }
};

}  // namespace tinytimegrapher
