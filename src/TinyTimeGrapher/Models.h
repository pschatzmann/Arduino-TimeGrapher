#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace tinytimegrapher {

/// Value used for "not available" in every floating point KPI field.
constexpr double kNoValue = NAN;

/// True if `v` holds a real value (i.e. is not kNoValue / NaN / inf).
inline bool hasValue(double v) { return isfinite(v); }

/// One detected beat. `timeSec` is derived from the audio sample counter,
/// not the wall clock.
struct Tick {
  double timeSec = 0.0;
  float peak = 0.0f;
  /// Time between the two main sounds of the beat (unlock to drop) in ms,
  /// or NaN if not resolvable.
  float lockDropMs = NAN;
  /// Running beat number since the session start (missed beats counted);
  /// even = tick, odd = tock.
  int64_t beat = 0;
};

/// The key figures of a measurement. Fields are NaN (see hasValue()) while
/// they can not be determined.
struct Kpis {
  double rateSPerDay = kNoValue;
  double beatErrorMs = kNoValue;
  double amplitudeDeg = kNoValue;
  double measuredBph = kNoValue;
  /// 0 = not known yet.
  int nominalBph = 0;
  double tickTockRatio = kNoValue;
  double jitterMs = kNoValue;
  double snrDb = kNoValue;
  double level = 0.0;

  bool locked() const { return hasValue(rateSPerDay); }
};

/// One sample of the KPIs over time (one per second while running).
struct KpiPoint {
  float timeSec = 0.0f;
  float rateSPerDay = NAN;
  float beatErrorMs = NAN;
  float amplitudeDeg = NAN;
};

/// All user settings of the timegrapher.
struct GrapherConfig {
  /// 0 = auto-detect from the tick interval.
  int bph = 0;
  float liftAngleDeg = 52.0f;
  int windowSec = 10;
  int lowHz = 2000;
  int highHz = 8000;
  float sensitivity = 4.0f;
  /// Automatic gain control; when off, gainDb is applied as a fixed
  /// amplification.
  bool agc = true;
  float gainDb = 0.0f;
  float calibrationSPerDay = 0.0f;
  /// Measure a simulated watch instead of the provided audio (see
  /// TimeGrapherEngine::demoWatch()).
  bool demo = false;
};

/// The standard beat rates of mechanical movements.
constexpr int kStandardBph[] = {14400, 18000, 19800, 21600, 25200, 28800, 36000};
constexpr int kStandardBphCount = sizeof(kStandardBph) / sizeof(kStandardBph[0]);

}  // namespace tinytimegrapher
