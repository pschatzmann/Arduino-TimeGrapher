#pragma once
#include <math.h>
#include <stdio.h>

#include "TinyTimeGrapher/UI/Icons.h"
#include "TinyTimeGrapher/UI/Pages/GrapherPage.h"

namespace tinytimegrapher {

/**
 * @brief Live tab: measurement state, input level, the KPI tiles and the
 * volume chart.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class LivePage : public GrapherPage<RGB_T> {
  using Base = GrapherPage<RGB_T>;
  using Base::kMargin;

 public:
  explicit LivePage(PageContext<RGB_T>& ctx) : Base(ctx, "Live", drawGaugeIcon<RGB_T>) {}

  void build() override {
    const int32_t inner = this->inner();
    int32_t y = this->pageTop() + 6;
    status_ = tinymd::Label<RGB_T>(tinymd::Bounds(kMargin, y, inner, 12), "");
    this->add(status_);
    y += 16;
    levelBar_ = tinymd::LinearProgressIndicator<RGB_T>(tinymd::Bounds(kMargin, y, inner, 4));
    this->add(levelBar_);
    y += 10;

    static const char* const captions[kTileCount] = {"RATE",   "BEAT ERR", "AMPLITUDE", "BEAT RATE", "TICK/TOCK",
                                                     "JITTER", "SNR",      "GAIN",      "WINDOW"};
    constexpr int32_t gap = 6;
    constexpr int32_t tileH = 46;
    const int32_t tileW = (inner - 2 * gap) / 3;
    for (int i = 0; i < kTileCount; ++i) {
      const int32_t x = kMargin + (i % 3) * (tileW + gap);
      tiles_[i] = tinymd::StatTile<RGB_T>(tinymd::Bounds(x, y + (i / 3) * (tileH + gap), tileW, tileH), captions[i]);
      this->add(tiles_[i]);
    }
    y += 3 * tileH + 2 * gap + 8;

    threshold_ = this->initVolumeChart(volume_, tinymd::Bounds(kMargin, y, inner, 120), "Volume (dB)");
    volume_.setFooter("Peaks above the threshold are ticks");
    this->add(volume_);
  }

  void refresh() override {
    TimeGrapherEngine& engine = this->engine();
    const Accents<RGB_T>& a = this->accents();
    const GrapherConfig& cfg = this->cfg();
    const bool running = engine.isRunning();
    const Kpis k = engine.kpis();
    char buf[48];
    char v[16];
    const int sec = static_cast<int>(engine.sessionSec());
    if (running) {
      snprintf(buf, sizeof(buf), "%s  %02d:%02d", k.locked() ? "Measuring" : "Listening for ticks", sec / 60,
               sec % 60);
    } else {
      snprintf(buf, sizeof(buf), "%s", k.locked() ? "Stopped" : "Stopped - start in the menu");
    }
    status_.setText(buf);
    status_.setColor(running ? a.emerald : this->theme().colors.onSurfaceVariant);
    // the input is analyzed all the time (see the monitor in the Settings),
    // but the Live page only shows it during a measurement
    const double level = running ? engine.level() * 20 : 0.0;
    levelBar_.setValue(static_cast<float>(level > 1 ? 1 : level));

    setTile(0, formatValue(v, sizeof(v), k.rateSPerDay, 1), "s/day",
            !hasValue(k.rateSPerDay) ? a.cyan : (fabs(k.rateSPerDay) < 15 ? a.emerald : a.amber));
    setTile(1, formatValue(v, sizeof(v), k.beatErrorMs, 2), "ms", beatErrorColor(k.beatErrorMs));
    setTile(2, formatValue(v, sizeof(v), k.amplitudeDeg, 0), "deg", a.cyan);
    char sub[20] = "bph";
    if (hasValue(k.measuredBph)) snprintf(sub, sizeof(sub), "meas %.0f", k.measuredBph);
    if (k.nominalBph > 0) {
      snprintf(v, sizeof(v), "%d", k.nominalBph);
    } else {
      snprintf(v, sizeof(v), "--");
    }
    setTile(3, v, sub, a.purple);
    setTile(4, formatValue(v, sizeof(v), k.tickTockRatio, 2), "ratio", a.purple);
    setTile(5, formatValue(v, sizeof(v), k.jitterMs, 2), "ms", a.blue);
    setTile(6, formatValue(v, sizeof(v), k.snrDb, 0), "dB", a.blue);
    setTile(7, formatValue(v, sizeof(v), engine.gainDb(), 0), cfg.agc ? "dB auto" : "dB", a.blue);
    snprintf(v, sizeof(v), "%d", cfg.windowSec);
    setTile(8, v, "s", a.blue);

    if (running) {
      this->refreshVolume(volume_, threshold_);
    } else {
      this->clearVolume(volume_, threshold_);
    }
  }

 private:
  static constexpr int kTileCount = 9;

  tinymd::Label<RGB_T> status_;
  tinymd::LinearProgressIndicator<RGB_T> levelBar_;
  tinymd::StatTile<RGB_T> tiles_[kTileCount];
  tinymd::LineChart<RGB_T> volume_;
  int threshold_ = 0;

  void setTile(int index, const char* value, const char* sub, RGB_T accent) {
    tiles_[index].setValue(value);
    tiles_[index].setSub(sub);
    tiles_[index].setAccent(accent);
  }

  RGB_T beatErrorColor(double v) const {
    const Accents<RGB_T>& a = this->accents();
    if (!hasValue(v)) return a.cyan;
    return v < 0.4 ? a.emerald : (v < 1.0 ? a.amber : a.rose);
  }
};

}  // namespace tinytimegrapher
