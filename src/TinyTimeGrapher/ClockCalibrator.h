#pragma once
#include <math.h>
#include <stdint.h>

#include <mutex>
#include <vector>

namespace tinytimegrapher {

/**
 * @brief Measures the error of the audio sample clock against a reference
 * clock (e.g. NTP).
 *
 * The rate of a watch is measured against the clock of the audio codec: an
 * error of 1 ppm gives 0.0864 s/day. Feed pairs of reference time and audio
 * time (TimeGrapherEngine::audioTimeSec()) every few seconds or minutes;
 * a linear fit of their difference gives the clock error in ppm and its
 * uncertainty, which shrinks with the measured time span (with a network
 * time source, about one hour for 1 ppm).
 *
 * A result from an earlier calibration (e.g. stored in the flash) can be
 * restored with setStored(): it is used until the new measurement is more
 * precise.
 *
 * Thread safe: the samples can be added from a network task while the GUI
 * reads the result.
 */
class ClockCalibrator {
 public:
  /// Samples kept; when full, every second one is dropped (the time span is
  /// kept, the spacing doubles).
  static constexpr size_t kCapacity = 64;

  /// Measurements with a lower uncertainty are used (see best()).
  float maxUncertaintyPpm = 1.0f;
  /// Minimum time span of the measurement in seconds.
  float minSpanSec = 300.0f;
  /// A sample that is further off the fit is ignored (dropped audio or a
  /// slow network answer); after 3 such samples in a row the measurement
  /// starts again.
  float maxResidualMs = 40.0f;

  /// Adds a pair of reference time and audio time (both in seconds). Returns
  /// false if the sample was ignored.
  bool addSample(double referenceSec, double audioSec) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (samples_.empty()) {
      ref0_ = referenceSec;
      offset0_ = audioSec - referenceSec;
    }
    Sample s;
    s.x = referenceSec - ref0_;
    s.y = (audioSec - referenceSec - offset0_) * 1000.0;  // ms
    if (!samples_.empty() && s.x <= samples_.back().x) return false;
    if (samples_.size() >= 5 && fabs(s.y - predict(s.x)) > maxResidualMs) {
      if (++rejected_ >= 3) clearLocked();
      return false;
    }
    rejected_ = 0;
    if (samples_.size() >= kCapacity) {
      size_t j = 0;
      for (size_t i = 0; i < samples_.size(); i += 2) samples_[j++] = samples_[i];
      samples_.resize(j);
    }
    samples_.push_back(s);
    fit();
    return true;
  }

  /// Starts a new measurement (the stored result is kept).
  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    clearLocked();
  }

  /// Result of an earlier calibration.
  void setStored(float ppm, float uncertaintyPpm) {
    std::lock_guard<std::mutex> lock(mutex_);
    storedPpm_ = ppm;
    storedUncertainty_ = uncertaintyPpm;
    hasStored_ = true;
  }

  /// Clock error of the current measurement: positive if the audio clock
  /// runs fast. NaN as long as there are less than 3 samples.
  float ppm() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ppm_;
  }
  /// Standard error of ppm().
  float uncertaintyPpm() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return uncertainty_;
  }
  /// Time span of the current measurement in seconds.
  float spanSec() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_.empty() ? 0.0f : static_cast<float>(samples_.back().x - samples_.front().x);
  }
  size_t sampleCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_.size();
  }

  /// True if the current measurement is precise enough (maxUncertaintyPpm,
  /// minSpanSec).
  bool isMeasured() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return measuredLocked();
  }

  /// The clock error to apply: the current measurement if it is precise
  /// enough and better than the stored one, else the stored result. Returns
  /// false if there is neither.
  bool best(float& ppm, float& uncertaintyPpm, bool* fromMeasurement = nullptr) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool measured = measuredLocked() && (!hasStored_ || uncertainty_ <= storedUncertainty_);
    if (fromMeasurement != nullptr) *fromMeasurement = measured;
    if (measured) {
      ppm = ppm_;
      uncertaintyPpm = uncertainty_;
      return true;
    }
    if (hasStored_) {
      ppm = storedPpm_;
      uncertaintyPpm = storedUncertainty_;
      return true;
    }
    return false;
  }

 private:
  struct Sample {
    double x;  // reference time since the first sample (s)
    double y;  // change of the offset audio - reference (ms)
  };

  mutable std::mutex mutex_;
  std::vector<Sample> samples_;
  double ref0_ = 0;
  double offset0_ = 0;
  double slope_ = 0;  // ms per s = 1000 ppm
  double intercept_ = 0;
  float ppm_ = NAN;
  float uncertainty_ = NAN;
  int rejected_ = 0;
  bool hasStored_ = false;
  float storedPpm_ = 0;
  float storedUncertainty_ = 0;

  bool measuredLocked() const {
    return samples_.size() >= 6 && samples_.back().x - samples_.front().x >= minSpanSec &&
           uncertainty_ <= maxUncertaintyPpm;
  }

  double predict(double x) const { return intercept_ + slope_ * x; }

  void clearLocked() {
    samples_.clear();
    rejected_ = 0;
    slope_ = intercept_ = 0;
    ppm_ = uncertainty_ = NAN;
  }

  void fit() {
    const size_t n = samples_.size();
    if (n < 2) return;
    double mx = 0, my = 0;
    for (const Sample& s : samples_) {
      mx += s.x;
      my += s.y;
    }
    mx /= n;
    my /= n;
    double sxx = 0, sxy = 0;
    for (const Sample& s : samples_) {
      sxx += (s.x - mx) * (s.x - mx);
      sxy += (s.x - mx) * (s.y - my);
    }
    if (sxx <= 0) return;
    slope_ = sxy / sxx;
    intercept_ = my - slope_ * mx;
    if (n < 3) return;
    double sse = 0;
    for (const Sample& s : samples_) sse += (s.y - predict(s.x)) * (s.y - predict(s.x));
    // at least 1 ms of noise: the reference has millisecond resolution
    const double sigma = fmax(sqrt(sse / (n - 2)), 1.0);
    ppm_ = static_cast<float>(slope_ * 1000.0);
    uncertainty_ = static_cast<float>(sigma / sqrt(sxx) * 1000.0);
  }
};

}  // namespace tinytimegrapher
