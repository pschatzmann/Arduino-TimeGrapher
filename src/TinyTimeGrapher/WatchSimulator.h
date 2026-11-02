#pragma once
#include <math.h>
#include <stdint.h>

#include <random>
#include <vector>

#include "TinyTimeGrapher/TinyTimeGrapherConfig.h"

namespace tinytimegrapher {

/**
 * @brief Generates the sound of a mechanical watch: two decaying 4 kHz
 * bursts (unlock, drop) per beat plus noise - for demos and tests without a
 * real watch.
 *
 *   WatchSimulator watch(48000);
 *   watch.bph = 21600;
 *   watch.rateSPerDay = 12;   // the simulated watch gains 12 s/day
 *   watch.beatErrorMs = 0.8;
 *   watch.generate(buffer, frames, channels);
 */
class WatchSimulator {
 public:
  explicit WatchSimulator(int sampleRate)
      : sampleRate_(sampleRate), burst_(static_cast<size_t>(0.004 * sampleRate)), nextBeat_(0.25 * sampleRate) {
    for (size_t k = 0; k < burst_.size(); ++k) {
      const double t = static_cast<double>(k) / sampleRate;
      burst_[k] = static_cast<float>(exp(-t / 0.0008) * sin(2 * kPi * 4000 * t));
    }
  }

  int bph = 28800;
  /// Rate deviation of the simulated watch.
  double rateSPerDay = 0.0;
  /// Offset of the odd beats.
  double beatErrorMs = 0.0;
  /// Time between the two sounds of a beat (gives the amplitude).
  double lockDropMs = 7.0;
  /// Peak level of the first sound (the second one has 70 %).
  float level = 0.5f;
  /// Standard deviation of the added noise.
  float noise = 0.002f;

  /// Fills `out` with `count` mono samples in the range -1..1.
  void generate(float* out, size_t count) {
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    const double period = 3600.0 / bph / (1.0 + rateSPerDay / 86400.0) * sampleRate_;
    for (size_t i = 0; i < count; ++i, ++index_) {
      // schedule the bursts of the next beat
      while (nextBeat_ <= static_cast<double>(index_) + burst_.size()) {
        const double start = nextBeat_ + (beat_ % 2 == 1 ? beatErrorMs / 1000.0 * sampleRate_ : 0.0);
        addBurst(static_cast<int64_t>(start), level);
        addBurst(static_cast<int64_t>(start + lockDropMs / 1000.0 * sampleRate_), level * 0.7f);
        beat_++;
        nextBeat_ += period;
      }
      float v = noise * gauss(random_);
      for (size_t b = 0; b < active_.size();) {
        const int64_t k = index_ - active_[b].start;
        if (k >= static_cast<int64_t>(burst_.size())) {
          active_[b] = active_.back();
          active_.pop_back();
          continue;
        }
        if (k >= 0) v += active_[b].amp * burst_[static_cast<size_t>(k)];
        ++b;
      }
      out[i] = v;
    }
  }

  /// Fills `out` with `frames` frames of interleaved 16 bit PCM (the same
  /// signal on all channels).
  void generate(int16_t* out, size_t frames, int channels = 1) {
    float buf[128];
    size_t pos = 0;
    while (pos < frames) {
      const size_t n = frames - pos < 128 ? frames - pos : 128;
      generate(buf, n);
      for (size_t i = 0; i < n; ++i) {
        float v = buf[i] < -1.0f ? -1.0f : (buf[i] > 1.0f ? 1.0f : buf[i]);
        const int16_t s = static_cast<int16_t>(lrintf(v * 32767.0f));
        for (int c = 0; c < channels; ++c) out[(pos + i) * channels + c] = s;
      }
      pos += n;
    }
  }

 private:
  struct Burst {
    int64_t start;
    float amp;
  };
  int sampleRate_;
  std::vector<float> burst_;
  std::vector<Burst> active_;
  std::mt19937 random_{1};
  int64_t index_ = 0;
  int64_t beat_ = 0;
  double nextBeat_ = 0.0;

  void addBurst(int64_t start, float amp) { active_.push_back(Burst{start, amp}); }
};

}  // namespace tinytimegrapher
