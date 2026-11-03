#pragma once
/**
 * @file NTPClockSync.h
 * @brief Measures the error of the audio clock with TinyNTP. Not part of
 * TinyTimeGrapher.h: include it after TinyNTP.h when you use it.
 */
#include <Arduino.h>

#include "TinyNTP.h"
#include "TinyTimeGrapher/ClockCalibrator.h"
#include "TinyTimeGrapher/TimeGrapherEngine.h"

namespace tinytimegrapher {

/**
 * @brief Compares the audio clock of a TimeGrapherEngine with the time of an
 * NTP server (TinyNTP) and corrects the measured rate with the result.
 *
 * Every intervalMs, update() asks the server for the time and adds it, with
 * the audio time of the engine, to a ClockCalibrator. As soon as its
 * result is precise enough (or a stored result was restored), it is applied
 * with TimeGrapherEngine::setClockCorrectionPpm().
 *
 * update() blocks until the server answers (at most timeoutMs), so call it
 * from its own task, not from the GUI loop:
 *
 *   NTPClockSync<WiFiUDP> clockSync(engine, calibrator);
 *   // network task:
 *   clockSync.update();
 */
template <typename UDPAPI>
class NTPClockSync {
 public:
  NTPClockSync(TimeGrapherEngine& engine, ClockCalibrator& calibrator, const char* server = NTP_HOST_POOL,
               uint32_t timeoutMs = 1000)
      : engine_(engine), calibrator_(calibrator), ntp_(server, 123, timeoutMs) {}

  /// Time between two requests.
  uint32_t intervalMs = 30000;
  /// Answers with a longer round trip are ignored: the time of the server
  /// is only known within half of the round trip.
  uint32_t maxRoundTripMs = 60;

  /// Requests the time when intervalMs has passed. Returns true if a sample
  /// was added.
  bool update() {
    if (hasRequested_ && millis() - lastRequestMs_ < intervalMs) return false;
    hasRequested_ = true;
    lastRequestMs_ = millis();
    return sync();
  }

  /// Requests the time now. Returns true if a sample was added.
  bool sync() {
    const uint32_t start = millis();
    const bool ok = ntp_ ? ntp_.update() : ntp_.begin();
    roundTripMs_ = millis() - start;
    if (!ok || roundTripMs_ > maxRoundTripMs) return false;
    // right after update() the time of the client is the time of the server
    const double reference = static_cast<double>(ntp_.getTimeMs()) / 1000.0;
    const double audio = engine_.audioTimeSec();
    if (!calibrator_.addSample(reference, audio)) return false;
    apply();
    return true;
  }

  /// Applies the result of the calibrator (also a stored one) to the engine.
  void apply() {
    float ppm, uncertainty;
    if (calibrator_.best(ppm, uncertainty)) engine_.setClockCorrectionPpm(ppm);
  }

  /// Round trip of the last request in ms.
  uint32_t roundTripMs() const { return roundTripMs_; }

  tiny_ntp::TinyNTPClient<UDPAPI>& ntp() { return ntp_; }

 private:
  TimeGrapherEngine& engine_;
  ClockCalibrator& calibrator_;
  tiny_ntp::TinyNTPClient<UDPAPI> ntp_;
  bool hasRequested_ = false;
  uint32_t lastRequestMs_ = 0;
  uint32_t roundTripMs_ = 0;
};

}  // namespace tinytimegrapher
