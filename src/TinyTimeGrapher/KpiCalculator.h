#pragma once
#include <math.h>

#include <algorithm>
#include <vector>

#include "TinyTimeGrapher/Models.h"
#include "TinyTimeGrapher/TinyTimeGrapherConfig.h"

namespace tinytimegrapher {

/**
 * @brief Calculates the KPIs from the ticks inside the analysis window.
 *
 * Rate comes from a linear fit of the tick times against the
 * (miss-tolerant) beat index, beat error from the offset between even and
 * odd beats. The working buffers are kept between calls so that the
 * once-per-second calculation does not allocate memory once it has run.
 */
class KpiCalculator {
 public:
  static constexpr size_t kMinTicks = 8;

  /// Snaps a measured beat rate to the nearest standard rate if it is
  /// within 5 %, otherwise rounds it.
  static int snapBph(double measured) {
    int best = kStandardBph[0];
    for (int b : kStandardBph) {
      if (fabs(b - measured) < fabs(best - measured)) best = b;
    }
    return fabs(best - measured) / best < 0.05 ? best : static_cast<int>(lround(measured));
  }

  /// Median interval between consecutive ticks, or NaN if there are less
  /// than 3 ticks.
  double medianInterval(const Tick* ticks, size_t n) {
    if (n < 3) return kNoValue;
    work_.clear();
    for (size_t i = 1; i < n; ++i) work_.push_back(ticks[i].timeSec - ticks[i - 1].timeSec);
    std::sort(work_.begin(), work_.end());
    return work_[work_.size() / 2];
  }

  /// KPIs from the `n` ticks (oldest first). clockPpm is the error of the
  /// clock the tick times were measured with (positive: runs fast).
  Kpis compute(const Tick* ticks, size_t n, const GrapherConfig& config, double snrDb, double level,
               double clockPpm = 0.0) {
    Kpis k;
    k.snrDb = snrDb;
    k.level = level;
    if (n < kMinTicks) return k;
    const double median = medianInterval(ticks, n);
    if (!hasValue(median) || median <= 0) return k;
    const int bph = config.bph > 0 ? config.bph : snapBph(3600.0 / median);
    const double nominal = 3600.0 / bph;

    // beat index relative to the first tick, tolerant against missed beats
    const double t0 = ticks[0].timeSec;
    idx_.resize(n);
    xs_.resize(n);
    ys_.resize(n);
    double mx = 0, my = 0;
    for (size_t i = 0; i < n; ++i) {
      idx_[i] = lround((ticks[i].timeSec - t0) / nominal);
      xs_[i] = static_cast<double>(idx_[i]);
      ys_[i] = ticks[i].timeSec - t0;
      mx += xs_[i];
      my += ys_[i];
    }
    mx /= n;
    my /= n;
    double sxx = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) {
      sxx += (xs_[i] - mx) * (xs_[i] - mx);
      sxy += (xs_[i] - mx) * (ys_[i] - my);
    }
    if (sxx <= 0) return k;
    const double slope = sxy / sxx;
    const double intercept = my - slope * mx;

    // a clock that runs fast makes the beats look too long
    const double clock = 1.0 + clockPpm * 1e-6;
    k.measuredBph = 3600.0 * clock / slope;
    k.nominalBph = bph;
    k.rateSPerDay = (nominal * clock / slope - 1.0) * 86400.0 + config.calibrationSPerDay;

    // residuals split by parity
    double sumE = 0, sumO = 0;
    size_t nE = 0, nO = 0;
    res_.resize(n);
    for (size_t i = 0; i < n; ++i) {
      res_[i] = ys_[i] - (intercept + slope * xs_[i]);
      if (isEven(idx_[i])) {
        sumE += res_[i];
        nE++;
      } else {
        sumO += res_[i];
        nO++;
      }
    }
    if (nE > 0 && nO > 0) {
      const double mE = sumE / nE;
      const double mO = sumO / nO;
      k.beatErrorMs = fabs(mO - mE) * 1000.0;
      double ss = 0;
      for (size_t i = 0; i < n; ++i) {
        const double d = res_[i] - (isEven(idx_[i]) ? mE : mO);
        ss += d * d;
      }
      k.jitterMs = sqrt(ss / n) * 1000.0;
    }

    double peakE = 0, peakO = 0;
    for (size_t i = 0; i < n; ++i) {
      if (isEven(idx_[i])) {
        peakE += ticks[i].peak;
      } else {
        peakO += ticks[i].peak;
      }
    }
    if (nE > 0 && nO > 0 && peakO > 0) k.tickTockRatio = (peakE / nE) / (peakO / nO);

    work_.clear();
    for (size_t i = 0; i < n; ++i) {
      if (hasValue(ticks[i].lockDropMs)) work_.push_back(ticks[i].lockDropMs);
    }
    if (work_.size() >= kMinTicks / 2) {
      std::sort(work_.begin(), work_.end());
      k.amplitudeDeg = amplitudeDeg(work_[work_.size() / 2], bph, config.liftAngleDeg);
    }
    return k;
  }

  /// A = L / (2 sin(pi t / T)), T = full balance period = 7200 / bph
  /// seconds, t = unlock to drop time. NaN if outside of 90..360 degrees.
  static double amplitudeDeg(double lockDropMs, int bph, double liftDeg) {
    const double period = 7200.0 / bph;
    const double s = sin(kPi * (lockDropMs / 1000.0) / period);
    if (s <= 0.0) return kNoValue;
    const double a = liftDeg / (2 * s);
    return (a >= 90.0 && a <= 360.0) ? a : kNoValue;
  }

 private:
  std::vector<long> idx_;
  std::vector<double> xs_;
  std::vector<double> ys_;
  std::vector<double> res_;
  std::vector<double> work_;

  static bool isEven(long i) { return (i % 2) == 0; }
};

}  // namespace tinytimegrapher
