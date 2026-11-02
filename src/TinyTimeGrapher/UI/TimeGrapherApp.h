#pragma once
#include <math.h>
#include <stdio.h>

#include <functional>
#include <string>
#include <vector>

#include "TinyMaterialDesign.h"
#include "TinyTimeGrapher/TimeGrapherEngine.h"
#include "TinyTimeGrapher/UI/Icons.h"
#include "TinyTimeGrapher/UI/SettingRows.h"
#include "TinyTimeGrapher/UI/StatTile.h"
#include "TinyTimeGrapher/UI/Theme.h"
#include "TinyTimeGrapher/UI/TimegrapherChart.h"

namespace tinytimegrapher {

/**
 * @brief The complete Time Grapher touch GUI on top of a TimeGrapherEngine,
 * built with TinyMaterialDesign.
 *
 * - App bar with the Start / Stop button
 * - Live: KPI tiles, input level and the volume chart
 * - Graph: the timegrapher trace (tick / tock dots)
 * - Trends: rate, beat error and amplitude over time
 * - Settings: beat rate, lift angle, analysis window, filter, gain,
 *   sensitivity, calibration, theme and demo mode
 *
 *   TimeGrapherEngine engine;
 *   TimeGrapherApp<RGB565> gui(board, engine);
 *
 *   void setup() {
 *     engine.begin(48000);
 *     gui.begin();
 *   }
 *   void loop() { gui.update(); }
 *
 * The audio is provided to the engine separately (see TimeGrapherEngine).
 * update() only redraws when the engine has new data, at most every
 * setRefreshIntervalMs() milliseconds.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class TimeGrapherApp {
 public:
  enum Page { kLive = 0, kGraph = 1, kTrends = 2, kSettings = 3, kPageCount = 4 };

  /// Called (debounced: 2 s after the last change) when the user has
  /// changed a setting, e.g. to persist it.
  std::function<void(const GrapherConfig& config, bool dark)> onSettingsChanged;

  TimeGrapherApp(tinygpu::LCDBoard& board, TimeGrapherEngine& engine)
      : engine_(engine),
        darkTheme_(timeGrapherDarkTheme<RGB_T>()),
        lightTheme_(timeGrapherLightTheme<RGB_T>()),
        app_(board, darkTheme_),
        lift_("Lift angle", 30, 60, 1, [](float v, char* b, size_t n) { snprintf(b, n, "%.0f deg", v); }),
        windowSec_("Analysis window", 5, 30, 1, [](float v, char* b, size_t n) { snprintf(b, n, "%.0f s", v); }),
        low_("Filter low cut", 500, 6000, 100, [](float v, char* b, size_t n) { snprintf(b, n, "%.0f Hz", v); }),
        high_("Filter high cut", 4000, 16000, 100,
              [](float v, char* b, size_t n) { snprintf(b, n, "%.0f Hz", v); }),
        agc_("Automatic gain"),
        gain_("Amplification", 0, 40, 1, [](float v, char* b, size_t n) { snprintf(b, n, "%+.0f dB", v); }),
        sens_("Sensitivity (threshold x noise)", 2, 12, 0.1f,
              [](float v, char* b, size_t n) { snprintf(b, n, "%.1f", v); }),
        cal_("Calibration offset", -60, 60, 1, [](float v, char* b, size_t n) { snprintf(b, n, "%+.0f s/d", v); }),
        dark_("Dark theme"),
        demo_("Demo mode") {}

  /// Builds the GUI and starts the board (display + touch).
  void begin(bool darkTheme = true) {
    dark_.setValue(darkTheme);
    app_.begin();
    if (!darkTheme) app_.screen().setTheme(lightTheme_);
    cfg_ = engine_.config();
    buildFrame();
    buildLive();
    buildGraph();
    buildTrends();
    buildSettings();
    showPage(kLive);
  }

  /// Call from loop(): handles touch input and redraws when needed.
  void update() {
    const uint32_t now = millis();
    if (now - lastRefreshMs_ >= refreshIntervalMs_) {
      const uint32_t rev = engine_.revision();
      if (rev != lastRevision_ || forceRefresh_) {
        lastRevision_ = rev;
        lastRefreshMs_ = now;
        forceRefresh_ = false;
        refresh();
        app_.screen().invalidate();
      }
    }
    if (settingsChanged_ && now - settingsChangedMs_ > 2000) {
      settingsChanged_ = false;
      if (onSettingsChanged) onSettingsChanged(cfg_, dark_.value());
    }
    app_.update();
  }

  /// Minimum time between two redraws caused by new measurement data.
  void setRefreshIntervalMs(uint32_t ms) { refreshIntervalMs_ = ms; }

  void showPage(int page) {
    page_ = page;
    for (int i = 0; i < kPageCount; ++i) pages_[i].visible = i == page;
    nav_.setSelectedIndex(page);
    forceRefresh_ = true;
  }

  bool isDark() const { return dark_.value(); }
  void setDark(bool dark) {
    dark_.setValue(dark);
    app_.screen().setTheme(dark ? darkTheme_ : lightTheme_);
  }

  tinymd::App<RGB_T>& app() { return app_; }

 private:
  static constexpr int32_t kBarHeight = 40;
  static constexpr int32_t kNavHeight = 56;
  static constexpr int32_t kMargin = 8;
  static constexpr const char* kDemoNote =
      "Measures a simulated watch instead of the microphone - to try the timegrapher without a watch.";

  TimeGrapherEngine& engine_;
  tinymd::MaterialTheme<RGB_T> darkTheme_;
  tinymd::MaterialTheme<RGB_T> lightTheme_;
  tinymd::App<RGB_T> app_;
  GrapherConfig cfg_;
  Accents<RGB_T> accents_;

  // frame
  tinymd::AppBar<RGB_T> appBar_;
  tinymd::Button<RGB_T> startButton_;
  tinymd::NavigationBar<RGB_T> nav_;
  tinymd::Container<RGB_T> pages_[kPageCount];

  // live
  tinymd::Label<RGB_T> status_;
  tinymd::LinearProgressIndicator<RGB_T> levelBar_;
  StatTile<RGB_T> tiles_[9];
  tinymd::LineChart<RGB_T> volume_;
  int volumeThreshold_ = 0;

  // graph
  TimegrapherChart<RGB_T> trace_;

  // trends
  tinymd::SegmentedButton<RGB_T> trendWindow_;
  tinymd::LineChart<RGB_T> trends_[3];
  tinymd::Button<RGB_T> resetButton_;
  int trendWindowIndex_ = 1;

  // settings
  tinymd::Label<RGB_T> bphCaption_;
  tinymd::Chip<RGB_T> bphChips_[kStandardBphCount + 1];
  ValueSlider<RGB_T> lift_, windowSec_, low_, high_;
  SwitchRow<RGB_T> agc_;
  ValueSlider<RGB_T> gain_, sens_;
  TextBlock<RGB_T> sensNote_;
  tinymd::LineChart<RGB_T> monitor_;
  int monitorThreshold_ = 0;
  ValueSlider<RGB_T> cal_;
  TextBlock<RGB_T> calNote_;
  SwitchRow<RGB_T> dark_;
  SwitchRow<RGB_T> demo_;
  TextBlock<RGB_T> demoNote_;
  std::vector<std::string> bphLabels_;

  int page_ = kLive;
  bool forceRefresh_ = true;
  bool settingsChanged_ = false;
  uint32_t settingsChangedMs_ = 0;
  uint32_t refreshIntervalMs_ = 100;
  uint32_t lastRefreshMs_ = 0;
  uint32_t lastRevision_ = 0;

  // scratch buffers, kept to avoid allocations on every refresh
  std::vector<Tick> ticks_;
  std::vector<KpiPoint> history_;
  std::vector<float> levels_;
  std::vector<float> values_;

  int32_t width() const { return static_cast<int32_t>(app_.width()); }
  int32_t pageTop() const { return kBarHeight; }
  int32_t pageHeight() const { return static_cast<int32_t>(app_.height()) - kBarHeight - kNavHeight; }

  // ---------------------------------------------------------------- building

  void buildFrame() {
    const int32_t w = width();
    appBar_ = tinymd::AppBar<RGB_T>(tinymd::Bounds(0, 0, w, kBarHeight), "Time Grapher");
    startButton_ = tinymd::Button<RGB_T>(tinymd::Bounds(w - 78, 6, 72, 28), "Start");
    startButton_.onClick = [this]() {
      if (engine_.isRunning()) {
        engine_.stop();
      } else {
        engine_.start();
      }
      forceRefresh_ = true;
    };
    appBar_.trailing = &startButton_;
    app_.screen().addFixedWidget(appBar_);

    nav_ = tinymd::NavigationBar<RGB_T>(
        tinymd::Bounds(0, static_cast<int32_t>(app_.height()) - kNavHeight, w, kNavHeight));
    nav_.addDestination(drawGaugeIcon<RGB_T>, "Live");
    nav_.addDestination(drawTraceIcon<RGB_T>, "Graph");
    nav_.addDestination(drawTrendIcon<RGB_T>, "Trends");
    nav_.addDestination(drawTuneIcon<RGB_T>, "Settings");
    nav_.onChange = [this](int index) { showPage(index); };
    app_.screen().addFixedWidget(nav_);

    // all pages share the same bounds, so the screen itself never scrolls;
    // each page scrolls on its own
    for (int i = 0; i < kPageCount; ++i) {
      pages_[i].bounds = tinymd::Bounds(0, pageTop(), w, pageHeight());
      app_.screen().addWidget(pages_[i]);
    }
  }

  void buildLive() {
    tinymd::Container<RGB_T>& page = pages_[kLive];
    const int32_t w = width();
    int32_t y = pageTop() + 6;
    status_ = tinymd::Label<RGB_T>(tinymd::Bounds(kMargin, y, w - 2 * kMargin, 12), "");
    page.addChild(status_);
    y += 16;
    levelBar_ = tinymd::LinearProgressIndicator<RGB_T>(tinymd::Bounds(kMargin, y, w - 2 * kMargin, 4));
    page.addChild(levelBar_);
    y += 10;

    static const char* const captions[9] = {"RATE",   "BEAT ERR", "AMPLITUDE", "BEAT RATE", "TICK/TOCK",
                                            "JITTER", "SNR",      "GAIN",      "WINDOW"};
    constexpr int32_t gap = 6;
    constexpr int32_t tileH = 46;
    const int32_t tileW = (w - 2 * kMargin - 2 * gap) / 3;
    for (int i = 0; i < 9; ++i) {
      const int32_t x = kMargin + (i % 3) * (tileW + gap);
      tiles_[i] = StatTile<RGB_T>(tinymd::Bounds(x, y + (i / 3) * (tileH + gap), tileW, tileH), captions[i]);
      page.addChild(tiles_[i]);
    }
    y += 3 * tileH + 2 * gap + 8;

    initVolumeChart(volume_, volumeThreshold_, tinymd::Bounds(kMargin, y, w - 2 * kMargin, 120), "Volume (dB)");
    volume_.setFooter("Peaks above the threshold are ticks");
    page.addChild(volume_);
  }

  void initVolumeChart(tinymd::LineChart<RGB_T>& chart, int& thresholdLine, tinymd::Bounds bounds,
                       const char* title) {
    chart = tinymd::LineChart<RGB_T>(bounds, title);
    chart.addSeries(accents_.cyan, 1);
    chart.setYRange(-80, 0);
    chart.setGridValues({0, -20, -40, -60});
    chart.setVisiblePoints(TTG_VOLUME_POINTS);
    thresholdLine = chart.addReferenceLine(-80, accents_.amber);
  }

  void buildGraph() {
    trace_ = TimegrapherChart<RGB_T>(tinymd::Bounds(4, pageTop() + 4, width() - 8, pageHeight() - 8), "Timegrapher");
    trace_.rows = 40;
    pages_[kGraph].addChild(trace_);
  }

  void buildTrends() {
    tinymd::Container<RGB_T>& page = pages_[kTrends];
    const int32_t w = width();
    int32_t y = pageTop() + 6;
    trendWindow_ = tinymd::SegmentedButton<RGB_T>(tinymd::Bounds(kMargin, y, w - 2 * kMargin, 28));
    trendWindow_.addSegment("1 min");
    trendWindow_.addSegment("5 min");
    trendWindow_.addSegment("30 min");
    trendWindow_.addSegment("All");
    trendWindow_.setSelected(trendWindowIndex_, true);
    trendWindow_.onChange = [this](uint32_t mask) {
      for (int i = 0; i < 4; ++i) {
        if (mask & (1u << i)) trendWindowIndex_ = i;
      }
      forceRefresh_ = true;
    };
    page.addChild(trendWindow_);
    y += 36;

    static const char* const titles[3] = {"Rate (s/day)", "Beat error (ms)", "Amplitude (deg)"};
    const RGB_T colors[3] = {accents_.cyan, accents_.amber, accents_.emerald};
    for (int i = 0; i < 3; ++i) {
      trends_[i] = tinymd::LineChart<RGB_T>(tinymd::Bounds(kMargin, y, w - 2 * kMargin, 130), titles[i]);
      trends_[i].addSeries(colors[i]);
      page.addChild(trends_[i]);
      y += 138;
    }
    resetButton_ =
        tinymd::Button<RGB_T>(tinymd::Bounds(kMargin, y, w - 2 * kMargin, 32), "Reset session",
                              tinymd::ButtonVariant::kOutlined);
    resetButton_.onClick = [this]() {
      engine_.resetSession();
      forceRefresh_ = true;
    };
    page.addChild(resetButton_);
  }

  void buildSettings() {
    tinymd::Container<RGB_T>& page = pages_[kSettings];
    const int32_t w = width();

    bphCaption_ = tinymd::Label<RGB_T>(tinymd::Bounds(), "Beat rate (bph)");
    page.addChild(bphCaption_);
    bphLabels_.clear();
    bphLabels_.push_back("Auto");
    for (int b : kStandardBph) bphLabels_.push_back(std::to_string(b));
    for (int i = 0; i <= kStandardBphCount; ++i) {
      bphChips_[i] = tinymd::Chip<RGB_T>(tinymd::Bounds(), bphLabels_[i].c_str(), true);
      bphChips_[i].onClick = [this, i]() {
        cfg_.bph = i == 0 ? 0 : kStandardBph[i - 1];
        updateBphChips();
        configChanged();
      };
      page.addChild(bphChips_[i]);
    }
    updateBphChips();

    addSlider(page, lift_, cfg_.liftAngleDeg, [this](float v) { cfg_.liftAngleDeg = v; });
    addSlider(page, windowSec_, cfg_.windowSec, [this](float v) { cfg_.windowSec = static_cast<int>(v); });
    addSlider(page, low_, cfg_.lowHz, [this](float v) { cfg_.lowHz = static_cast<int>(v); });
    addSlider(page, high_, cfg_.highHz, [this](float v) { cfg_.highHz = static_cast<int>(v); });

    agc_.setValue(cfg_.agc);
    agc_.onChange = [this](bool v) {
      cfg_.agc = v;
      gain_.enabled = !v;
      configChanged();
    };
    page.addChild(agc_);
    addSlider(page, gain_, cfg_.gainDb, [this](float v) { cfg_.gainDb = v; });
    gain_.enabled = !cfg_.agc;
    addSlider(page, sens_, cfg_.sensitivity, [this](float v) { cfg_.sensitivity = v; });
    sensNote_ = TextBlock<RGB_T>(tinymd::Bounds(),
                                 "Lower sensitivity detects quieter ticks but picks up more noise.");
    page.addChild(sensNote_);

    initVolumeChart(monitor_, monitorThreshold_, tinymd::Bounds(0, 0, w - 2 * kMargin, 110), "Input level (dB)");
    page.addChild(monitor_);

    addSlider(page, cal_, cfg_.calibrationSPerDay, [this](float v) { cfg_.calibrationSPerDay = v; });
    calNote_ = TextBlock<RGB_T>(tinymd::Bounds(),
                                "Added to the measured rate to compensate the clock of the audio "
                                "codec. Calibrate against a reference watch.");
    page.addChild(calNote_);

    dark_.onChange = [this](bool v) {
      app_.screen().setTheme(v ? darkTheme_ : lightTheme_);
      configChanged();
    };
    page.addChild(dark_);

    demo_.setValue(cfg_.demo);
    demo_.onChange = [this](bool v) {
      cfg_.demo = v;
      configChanged();
    };
    page.addChild(demo_);
    demoNote_ = TextBlock<RGB_T>(tinymd::Bounds(), kDemoNote);
    page.addChild(demoNote_);
    layoutSettings();
  }

  void addSlider(tinymd::Container<RGB_T>& page, ValueSlider<RGB_T>& slider, float value,
                 std::function<void(float)> apply) {
    slider.setValue(value);
    slider.onChange = [this, apply](float v) {
      apply(v);
      configChanged();
    };
    page.addChild(slider);
  }

  /// Positions the settings rows from top to bottom, skipping hidden ones.
  void layoutSettings() {
    const int32_t w = width();
    const int32_t inner = w - 2 * kMargin;
    int32_t y = pageTop() + 6;
    auto place = [&](tinymd::Widget<RGB_T>& widget, int32_t h, bool show = true) {
      widget.visible = show;
      if (!show) {
        // invisible children still count for the content height
        widget.bounds = tinymd::Bounds(kMargin, pageTop(), 0, 0);
        return;
      }
      widget.bounds = tinymd::Bounds(kMargin, y, inner, h);
      y += h + 6;
    };
    auto placeNote = [&](TextBlock<RGB_T>& note, const char* text, bool show = true) {
      const int32_t h = TextBlock<RGB_T>::heightFor(text, inner, *app_.theme().typography.label);
      place(note, h, show);
    };

    place(bphCaption_, 12);
    constexpr int32_t chipGap = 4;
    const int32_t chipW = (inner - 3 * chipGap) / 4;
    for (int i = 0; i <= kStandardBphCount; ++i) {
      bphChips_[i].bounds = tinymd::Bounds(kMargin + (i % 4) * (chipW + chipGap), y + (i / 4) * 30, chipW, 26);
    }
    y += 2 * 30 + 6;
    place(lift_, ValueSlider<RGB_T>::kHeight);
    place(windowSec_, ValueSlider<RGB_T>::kHeight);
    place(low_, ValueSlider<RGB_T>::kHeight);
    place(high_, ValueSlider<RGB_T>::kHeight);
    place(agc_, SwitchRow<RGB_T>::kHeight);
    place(gain_, ValueSlider<RGB_T>::kHeight);
    place(sens_, ValueSlider<RGB_T>::kHeight);
    placeNote(sensNote_, "Lower sensitivity detects quieter ticks but picks up more noise.");
    place(monitor_, 110);
    place(cal_, ValueSlider<RGB_T>::kHeight);
    placeNote(calNote_,
              "Added to the measured rate to compensate the clock of the audio codec. Calibrate against a "
              "reference watch.");
    place(dark_, SwitchRow<RGB_T>::kHeight);
    place(demo_, SwitchRow<RGB_T>::kHeight);
    placeNote(demoNote_, kDemoNote);
    app_.screen().invalidate();
  }

  void updateBphChips() {
    for (int i = 0; i <= kStandardBphCount; ++i) {
      const int bph = i == 0 ? 0 : kStandardBph[i - 1];
      bphChips_[i].setSelected(bph == cfg_.bph);
    }
  }

  void configChanged() {
    engine_.setConfig(cfg_);
    settingsChanged_ = true;
    settingsChangedMs_ = millis();
    forceRefresh_ = true;
  }

  // ------------------------------------------------------------- refreshing

  void refresh() {
    const bool running = engine_.isRunning();
    startButton_.setLabel(running ? "Stop" : "Start");
    startButton_.setVariant(running ? tinymd::ButtonVariant::kTonal : tinymd::ButtonVariant::kFilled);
    switch (page_) {
      case kLive:
        refreshLive(running);
        break;
      case kGraph:
        refreshGraph();
        break;
      case kTrends:
        refreshTrends();
        break;
      case kSettings:
        refreshVolume(monitor_, monitorThreshold_);
        break;
    }
  }

  void refreshLive(bool running) {
    const Kpis k = engine_.kpis();
    char buf[48];
    char v[16];
    const int sec = static_cast<int>(engine_.sessionSec());
    if (running) {
      snprintf(buf, sizeof(buf), "%s  %02d:%02d", k.locked() ? "Measuring" : "Listening for ticks", sec / 60,
               sec % 60);
    } else {
      snprintf(buf, sizeof(buf), "%s", k.locked() ? "Stopped" : "Stopped - press Start");
    }
    status_.setText(buf);
    status_.setColor(running ? accents_.emerald : app_.theme().colors.onSurfaceVariant);
    const double level = engine_.level() * 20;
    levelBar_.setValue(static_cast<float>(level > 1 ? 1 : level));

    setTile(0, formatValue(v, sizeof(v), k.rateSPerDay, 1), "s/day",
            !hasValue(k.rateSPerDay) ? accents_.cyan
                                     : (fabs(k.rateSPerDay) < 15 ? accents_.emerald : accents_.amber));
    setTile(1, formatValue(v, sizeof(v), k.beatErrorMs, 2), "ms", beatErrorColor(k.beatErrorMs));
    setTile(2, formatValue(v, sizeof(v), k.amplitudeDeg, 0), "deg", accents_.cyan);
    char sub[20] = "bph";
    if (hasValue(k.measuredBph)) snprintf(sub, sizeof(sub), "meas %.0f", k.measuredBph);
    if (k.nominalBph > 0) {
      snprintf(v, sizeof(v), "%d", k.nominalBph);
    } else {
      snprintf(v, sizeof(v), "--");
    }
    setTile(3, v, sub, accents_.purple);
    setTile(4, formatValue(v, sizeof(v), k.tickTockRatio, 2), "ratio", accents_.purple);
    setTile(5, formatValue(v, sizeof(v), k.jitterMs, 2), "ms", accents_.blue);
    setTile(6, formatValue(v, sizeof(v), k.snrDb, 0), "dB", accents_.blue);
    setTile(7, formatValue(v, sizeof(v), engine_.gainDb(), 0), cfg_.agc ? "dB auto" : "dB", accents_.blue);
    snprintf(v, sizeof(v), "%d", cfg_.windowSec);
    setTile(8, v, "s", accents_.blue);

    refreshVolume(volume_, volumeThreshold_);
  }

  void setTile(int index, const char* value, const char* sub, RGB_T accent) {
    tiles_[index].setValue(value);
    tiles_[index].setSub(sub);
    tiles_[index].setAccent(accent);
  }

  RGB_T beatErrorColor(double v) const {
    if (!hasValue(v)) return accents_.cyan;
    return v < 0.4 ? accents_.emerald : (v < 1.0 ? accents_.amber : accents_.rose);
  }

  void refreshVolume(tinymd::LineChart<RGB_T>& chart, int thresholdLine) {
    const float threshold = engine_.volume(levels_);
    for (float& l : levels_) l = toDb(l);
    chart.setValues(0, levels_);
    chart.setReferenceLine(thresholdLine, toDb(threshold));
  }

  static float toDb(float v) {
    if (v <= 0.0f) return -80.0f;
    const float d = 20.0f * log10f(v);
    return d < -80.0f ? -80.0f : (d > 0.0f ? 0.0f : d);
  }

  void refreshGraph() {
    const Kpis k = engine_.kpis();
    engine_.ticks(ticks_, trace_.ticksNeeded());
    trace_.setData(ticks_, k.nominalBph > 0 ? k.nominalBph : cfg_.bph);
    char rate[16], be[16], amp[16], buf[64];
    snprintf(buf, sizeof(buf), "%s s/d  %s ms  %s deg", formatValue(rate, sizeof(rate), k.rateSPerDay, 1),
             formatValue(be, sizeof(be), k.beatErrorMs, 2), formatValue(amp, sizeof(amp), k.amplitudeDeg, 0));
    trace_.setSubtitle(buf);
  }

  void refreshTrends() {
    static const size_t windows[4] = {60, 300, 1800, TTG_MAX_HISTORY};
    engine_.history(history_, windows[trendWindowIndex_]);
    for (int i = 0; i < 3; ++i) {
      values_.resize(history_.size());
      for (size_t j = 0; j < history_.size(); ++j) {
        const KpiPoint& p = history_[j];
        values_[j] = i == 0 ? p.rateSPerDay : (i == 1 ? p.beatErrorMs : p.amplitudeDeg);
      }
      trends_[i].setValues(0, values_);
      trends_[i].setFooter(summary(values_).c_str());
    }
  }

  static std::string summary(const std::vector<float>& values) {
    double lo = INFINITY, hi = -INFINITY, sum = 0;
    size_t n = 0;
    for (float v : values) {
      if (!isfinite(v)) continue;
      lo = v < lo ? v : lo;
      hi = v > hi ? v : hi;
      sum += v;
      n++;
    }
    if (n == 0) return "No data yet";
    char buf[64];
    snprintf(buf, sizeof(buf), "min %.2f avg %.2f max %.2f (%u s)", lo, sum / n, hi,
             static_cast<unsigned>(values.size()));
    return buf;
  }
};

}  // namespace tinytimegrapher
