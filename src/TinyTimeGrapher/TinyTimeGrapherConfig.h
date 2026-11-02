#pragma once

/// Number type of the per-sample signal processing. float keeps the
/// detector cheap on MCUs without a double precision FPU (e.g. ESP32-S3);
/// define TTG_DSP_DOUBLE to use double instead.
namespace tinytimegrapher {
#ifdef TTG_DSP_DOUBLE
using dsp_t = double;
#else
using dsp_t = float;
#endif

constexpr double kPi = 3.14159265358979323846;
}  // namespace tinytimegrapher

/// Number of recent beats kept for the KPI window and the timegrapher chart
/// (36000 bph x 30 s analysis window = 300 beats).
#ifndef TTG_MAX_TICKS
#define TTG_MAX_TICKS 400
#endif

/// Number of KPI history points (one per second) kept for the trend charts.
#ifndef TTG_MAX_HISTORY
#define TTG_MAX_HISTORY 3600
#endif

/// Number of points in the rolling volume trace (50 per second).
#ifndef TTG_VOLUME_POINTS
#define TTG_VOLUME_POINTS 500
#endif

/// Volume trace points per second.
#ifndef TTG_VOLUME_RATE
#define TTG_VOLUME_RATE 50
#endif
