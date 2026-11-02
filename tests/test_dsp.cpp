// Port of the Android-TimeGrapher DspTest: runs the tick detector and the KPI
// calculator against synthetic watch signals.
#include <math.h>
#include <stdio.h>

#include <random>
#include <vector>

#include "TinyTimeGrapher/KpiCalculator.h"
#include "TinyTimeGrapher/TickDetector.h"
#include "TinyTimeGrapher/TimeGrapherEngine.h"

using namespace tinytimegrapher;

static const int fs = 48000;
static int failures = 0;

static void assertNear(const char* name, double expected, double actual, double tolerance) {
  const bool ok = isfinite(actual) && fabs(expected - actual) <= tolerance;
  printf("%s %-45s expected %10.3f +- %-6.2f got %10.3f\n", ok ? "PASS" : "FAIL", name, expected,
         tolerance, actual);
  if (!ok) failures++;
}

static void burst(std::vector<float>& buf, double t, float amp) {
  const int s0 = static_cast<int>(t * fs);
  for (int k = 0; k < static_cast<int>(0.004 * fs); ++k) {
    const double tt = static_cast<double>(k) / fs;
    const size_t idx = static_cast<size_t>(s0 + k);
    if (idx < buf.size()) buf[idx] += static_cast<float>(amp * exp(-tt / 0.0008) * sin(2 * kPi * 4000 * tt));
  }
}

/// Synthetic watch: two decaying 4 kHz bursts (unlock, drop) per beat plus noise.
static std::vector<float> synth(int bph, int seconds, double periodScale = 1.0, double oddShiftMs = 0.0,
                                double ldMs = 7.0) {
  std::vector<float> out(static_cast<size_t>(seconds * fs));
  std::mt19937 rnd(1);
  std::normal_distribution<double> gauss(0.0, 1.0);
  for (float& v : out) v = static_cast<float>(gauss(rnd) * 0.002);
  const double period = 3600.0 / bph * periodScale;
  for (int n = 0;; ++n) {
    const double t = 0.5 + n * period + (n % 2 == 1 ? oddShiftMs / 1000.0 : 0.0);
    if (t + 0.05 >= seconds) break;
    burst(out, t, 0.5f);
    burst(out, t + ldMs / 1000.0, 0.35f);
  }
  return out;
}

static std::vector<Tick> detect(const std::vector<float>& sig, bool agc = false, float gainDb = 0.0f) {
  std::vector<Tick> ticks;
  TickDetector det(fs, 2000, 8000, 4.0f, [&](const Tick& t) { ticks.push_back(t); });
  det.agc = agc;
  det.setManualGainDb(gainDb);
  for (size_t pos = 0; pos < sig.size(); pos += 1024) {
    const size_t n = sig.size() - pos < 1024 ? sig.size() - pos : 1024;
    det.process(sig.data() + pos, n);
  }
  return ticks;
}

static std::vector<float> weak() {
  std::vector<float> s = synth(28800, 20);
  for (float& v : s) v *= 0.004f;
  return s;
}

static Kpis kpis(const std::vector<Tick>& ticks, const GrapherConfig& cfg) {
  KpiCalculator calc;
  return calc.compute(ticks.data(), ticks.size(), cfg, kNoValue, 0.0);
}

int main() {
  // weak signal is detected with and without gain
  assertNear("weak signal: ticks without gain", 156, detect(weak()).size(), 5);
  assertNear("weak signal: ticks with 30 dB gain", 156, detect(weak(), false, 30).size(), 5);

  // threshold follows sensitivity
  auto threshold = [](float sens) {
    TickDetector det(fs, 2000, 8000, sens, [](const Tick&) {});
    std::vector<float> sig = synth(28800, 5);
    det.process(sig.data(), sig.size());
    return det.threshold();
  };
  assertNear("threshold(8) / threshold(4)", 2.0, threshold(8) / threshold(4), 0.1);

  // AGC amplifies a weak signal but not noise
  assertNear("agc: weak signal ticks", 156, detect(weak(), true).size(), 8);
  {
    std::mt19937 rnd(2);
    std::normal_distribution<double> gauss(0.0, 1.0);
    std::vector<float> noise(30 * fs);
    for (float& v : noise) v = static_cast<float>(gauss(rnd) * 0.0005);
    assertNear("agc: noise ticks", 0, detect(noise, true).size(), 3);
  }

  // detects all beats: 8 beats per second over ~19.5 s
  assertNear("detects all beats", 156, detect(synth(28800, 20)).size(), 3);

  {
    GrapherConfig cfg;
    cfg.bph = 28800;
    Kpis k = kpis(detect(synth(28800, 20)), cfg);
    assertNear("zero rate when nominal", 0.0, k.rateSPerDay, 1.0);
    assertNear("nominal bph", 28800, k.nominalBph, 0);
  }
  {
    Kpis k = kpis(detect(synth(28800, 20, 1.001)), GrapherConfig());
    assertNear("slow watch: bph", 28800, k.nominalBph, 0);
    assertNear("slow watch: negative rate", -86.3, k.rateSPerDay, 1.5);
  }
  {
    // odd beats shifted by 1.5 ms -> offset between even and odd of 1.5 ms
    Kpis k = kpis(detect(synth(21600, 20, 1.0, 1.5)), GrapherConfig());
    assertNear("beat error", 1.5, k.beatErrorMs, 0.2);
  }
  {
    // 52 / (2 sin(pi * 0.007 / 0.25)) = ~296 deg
    Kpis k = kpis(detect(synth(28800, 20, 1.0, 0.0, 7.0)), GrapherConfig());
    assertNear("amplitude from lock/drop", 296, k.amplitudeDeg, 10);
  }

  // the engine end to end with 16 bit stereo PCM
  {
    std::vector<float> sig = synth(21600, 15, 1.0, 1.0);
    std::vector<int16_t> pcm(sig.size() * 2);
    for (size_t i = 0; i < sig.size(); ++i) {
      pcm[2 * i] = static_cast<int16_t>(lrintf(sig[i] * 32767.0f));
      pcm[2 * i + 1] = 0;
    }
    TimeGrapherEngine engine;
    GrapherConfig cfg;
    cfg.agc = false;
    engine.begin(fs, cfg);
    engine.start();
    for (size_t pos = 0; pos < sig.size(); pos += 512) {
      const size_t n = sig.size() - pos < 512 ? sig.size() - pos : 512;
      engine.write(pcm.data() + 2 * pos, n, 2);
    }
    Kpis k = engine.kpis();
    std::vector<Tick> ticks;
    engine.ticks(ticks);
    std::vector<KpiPoint> hist;
    engine.history(hist);
    std::vector<float> vol;
    engine.volume(vol);
    assertNear("engine: bph", 21600, k.nominalBph, 0);
    assertNear("engine: rate", 0.0, k.rateSPerDay, 1.0);
    assertNear("engine: beat error", 1.0, k.beatErrorMs, 0.2);
    assertNear("engine: history points", 15, hist.size(), 1);
    assertNear("engine: volume points", TTG_VOLUME_POINTS, vol.size(), 0);
    assertNear("engine: beat numbering", ticks.size() - 1, ticks.back().beat - ticks.front().beat, 1);
  }

  // demo mode: silent input is replaced by the simulated watch
  {
    TimeGrapherEngine engine;
    GrapherConfig cfg;
    cfg.demo = true;
    engine.begin(fs, cfg);
    engine.demoWatch().bph = 21600;
    engine.demoWatch().rateSPerDay = 10.0;
    engine.start();
    std::vector<int16_t> silence(512, 0);
    for (int i = 0; i < 15 * fs / 512; ++i) engine.write(silence.data(), silence.size());
    Kpis k = engine.kpis();
    assertNear("demo: bph", 21600, k.nominalBph, 0);
    assertNear("demo: rate", 10.0, k.rateSPerDay, 1.0);
  }

  printf("%s (%d failures)\n", failures == 0 ? "OK" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
