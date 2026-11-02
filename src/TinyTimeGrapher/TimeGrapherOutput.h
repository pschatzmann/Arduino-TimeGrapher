#pragma once
#include "AudioTools.h"
#include "TinyTimeGrapher/TimeGrapherEngine.h"

namespace tinytimegrapher {

/**
 * @brief arduino-audio-tools output that feeds the written 16 bit PCM data
 * into a TimeGrapherEngine, e.g. via a StreamCopy from the microphone:
 *
 *   TimeGrapherEngine engine;
 *   TimeGrapherOutput grapherOut(engine);
 *   StreamCopy copier(grapherOut, mic);
 *
 *   grapherOut.begin(mic.audioInfo());
 *   // loop / audio task: copier.copy();
 *
 * The sample rate and the channel count come from the AudioInfo; the
 * first channel is analyzed.
 */
class TimeGrapherOutput : public audio_tools::AudioOutput {
 public:
  explicit TimeGrapherOutput(TimeGrapherEngine& engine) : engine_(engine) {}

  bool begin(audio_tools::AudioInfo info) override {
    setAudioInfo(info);
    return begin();
  }

  bool begin() override {
    if (cfg.bits_per_sample != 16) {
      LOGE("TimeGrapherOutput: only 16 bits per sample are supported");
      return false;
    }
    is_active = engine_.begin(static_cast<int>(cfg.sample_rate));
    return is_active;
  }

  void setAudioInfo(audio_tools::AudioInfo info) override {
    audio_tools::AudioOutput::setAudioInfo(info);
    if (is_active) engine_.begin(static_cast<int>(info.sample_rate));
  }

  size_t write(const uint8_t* data, size_t len) override {
    if (!is_active || cfg.channels == 0) return len;
    const size_t frames = len / (sizeof(int16_t) * cfg.channels);
    engine_.write(reinterpret_cast<const int16_t*>(data), frames, cfg.channels);
    return len;
  }

 private:
  TimeGrapherEngine& engine_;
};

}  // namespace tinytimegrapher
