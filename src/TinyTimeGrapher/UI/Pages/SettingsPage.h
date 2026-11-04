#pragma once
#include <math.h>
#include <stdio.h>

#include <functional>
#include <string>
#include <vector>

#include "TinyTimeGrapher/UI/Icons.h"
#include "TinyTimeGrapher/UI/Pages/GrapherPage.h"

namespace tinytimegrapher {

/**
 * @brief Settings tab: beat rate, lift angle, analysis window, filter, gain,
 * sensitivity (with an input level monitor), calibration, clock correction
 * (see TimeGrapherApp::setClockCalibrator()), theme, demo mode and reset to
 * defaults. All changes are applied at once.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class SettingsPage : public GrapherPage<RGB_T> {
  using Base = GrapherPage<RGB_T>;
  using typename Base::Placer;
  using Base::kMargin;

 public:
  explicit SettingsPage(PageContext<RGB_T>& ctx)
      : Base(ctx, "Settings", drawTuneIcon<RGB_T>),
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

  void build() override {
    bphCaption_ = tinymd::Label<RGB_T>(tinymd::Bounds(), "Beat rate (bph)");
    this->add(bphCaption_);
    bphLabels_.clear();
    bphLabels_.push_back("Auto");
    for (int b : kStandardBph) bphLabels_.push_back(std::to_string(b));
    for (int i = 0; i <= kStandardBphCount; ++i) {
      bphChips_[i] = tinymd::Chip<RGB_T>(tinymd::Bounds(), bphLabels_[i].c_str(), true);
      bphChips_[i].onClick = [this, i]() {
        this->cfg().bph = i == 0 ? 0 : kStandardBph[i - 1];
        updateBphChips();
        this->ctx_.configChanged();
      };
      this->add(bphChips_[i]);
    }

    addSlider(lift_, [this](float v) { this->cfg().liftAngleDeg = v; });
    addSlider(windowSec_, [this](float v) { this->cfg().windowSec = static_cast<int>(v); });
    addSlider(low_, [this](float v) { this->cfg().lowHz = static_cast<int>(v); });
    addSlider(high_, [this](float v) { this->cfg().highHz = static_cast<int>(v); });

    agc_.onChange = [this](bool v) {
      this->cfg().agc = v;
      gain_.enabled = !v;
      this->ctx_.configChanged();
    };
    this->add(agc_);
    addSlider(gain_, [this](float v) { this->cfg().gainDb = v; });
    addSlider(sens_, [this](float v) { this->cfg().sensitivity = v; });
    sensNote_ = tinymd::TextBlock<RGB_T>(kSensNote);
    this->add(sensNote_);

    monitorThreshold_ = this->initVolumeChart(monitor_, tinymd::Bounds(), "Input level (dB)");
    this->add(monitor_);

    addSlider(cal_, [this](float v) { this->cfg().calibrationSPerDay = v; });
    calNote_ = tinymd::TextBlock<RGB_T>(kCalNote);
    this->add(calNote_);
    clockNote_ = tinymd::TextBlock<RGB_T>("");
    this->add(clockNote_);

    dark_.onChange = [this](bool v) {
      if (this->ctx_.setDark) this->ctx_.setDark(v);
    };
    this->add(dark_);

    demo_.onChange = [this](bool v) {
      this->cfg().demo = v;
      this->ctx_.configChanged();
    };
    this->add(demo_);
    demoNote_ = tinymd::TextBlock<RGB_T>(kDemoNote);
    this->add(demoNote_);

    // reset to defaults, after a confirmation
    defaultsButton_ = tinymd::Button<RGB_T>(tinymd::Bounds(), "Reset to defaults", tinymd::ButtonVariant::kOutlined);
    defaultsButton_.onClick = [this]() { this->ctx_.app.screen().presentDialog(defaultsDialog_); };
    this->add(defaultsButton_);
    const int32_t dialogH = 150;
    defaultsDialog_ = tinymd::Dialog<RGB_T>(
        tinymd::Bounds(16, (this->screenHeight() - dialogH) / 2, this->width() - 32, dialogH), "Reset settings?",
        "All settings are set back to their default values.");
    defaultsCancel_ = tinymd::Button<RGB_T>(defaultsDialog_.actionRect(0, 2), "Cancel", tinymd::ButtonVariant::kText);
    defaultsCancel_.onClick = [this]() { this->ctx_.app.screen().dismissDialog(); };
    defaultsConfirm_ = tinymd::Button<RGB_T>(defaultsDialog_.actionRect(1, 2), "Reset");
    defaultsConfirm_.onClick = [this]() {
      this->ctx_.app.screen().dismissDialog();
      resetToDefaults();
    };
    defaultsDialog_.addAction(defaultsCancel_);
    defaultsDialog_.addAction(defaultsConfirm_);

    syncWidgets();
  }

  void onShow() override {
    // the theme can also be changed by TimeGrapherApp::setDark()
    if (this->ctx_.isDark) dark_.setValue(this->ctx_.isDark());
  }

  void refresh() override {
    this->refreshVolume(monitor_, monitorThreshold_);
    refreshClock();
  }

  /// Updates all widgets from the configuration (ctx.cfg).
  void syncWidgets() {
    const GrapherConfig& cfg = this->cfg();
    updateBphChips();
    lift_.setValue(cfg.liftAngleDeg);
    windowSec_.setValue(cfg.windowSec);
    low_.setValue(cfg.lowHz);
    high_.setValue(cfg.highHz);
    agc_.setValue(cfg.agc);
    gain_.setValue(cfg.gainDb);
    gain_.enabled = !cfg.agc;
    sens_.setValue(cfg.sensitivity);
    cal_.setValue(cfg.calibrationSPerDay);
    if (this->ctx_.isDark) dark_.setValue(this->ctx_.isDark());
    demo_.setValue(cfg.demo);
    layout();
  }

  /// Shows the new state of the clock correction (ctx.clock).
  void clockChanged() {
    refreshClock();
    layout();
  }

 private:
  static constexpr const char* kSensNote = "Lower sensitivity detects quieter ticks but picks up more noise.";
  static constexpr const char* kCalNote =
      "Added to the measured rate to compensate the clock of the audio codec. Calibrate against a reference "
      "watch.";
  static constexpr const char* kDemoNote =
      "Measures a simulated watch instead of the microphone - to try the timegrapher without a watch.";

  tinymd::Label<RGB_T> bphCaption_;
  tinymd::Chip<RGB_T> bphChips_[kStandardBphCount + 1];
  std::vector<std::string> bphLabels_;
  tinymd::SliderRow<RGB_T> lift_, windowSec_, low_, high_;
  tinymd::SwitchRow<RGB_T> agc_;
  tinymd::SliderRow<RGB_T> gain_, sens_;
  tinymd::TextBlock<RGB_T> sensNote_;
  tinymd::LineChart<RGB_T> monitor_;
  int monitorThreshold_ = 0;
  tinymd::SliderRow<RGB_T> cal_;
  tinymd::TextBlock<RGB_T> calNote_;
  tinymd::TextBlock<RGB_T> clockNote_;
  std::string clockText_;
  tinymd::SwitchRow<RGB_T> dark_;
  tinymd::SwitchRow<RGB_T> demo_;
  tinymd::TextBlock<RGB_T> demoNote_;
  tinymd::Button<RGB_T> defaultsButton_;
  tinymd::Dialog<RGB_T> defaultsDialog_;
  tinymd::Button<RGB_T> defaultsCancel_;
  tinymd::Button<RGB_T> defaultsConfirm_;

  void addSlider(tinymd::SliderRow<RGB_T>& slider, std::function<void(float)> apply) {
    slider.onChange = [this, apply](float v) {
      apply(v);
      this->ctx_.configChanged();
    };
    this->add(slider);
  }

  /// Sets all settings (including the theme) back to their defaults.
  void resetToDefaults() {
    this->cfg() = GrapherConfig();
    if (this->ctx_.setDark) this->ctx_.setDark(true);
    syncWidgets();
    this->ctx_.configChanged();
  }

  void updateBphChips() {
    for (int i = 0; i <= kStandardBphCount; ++i) {
      const int bph = i == 0 ? 0 : kStandardBph[i - 1];
      bphChips_[i].setSelected(bph == this->cfg().bph);
    }
  }

  int32_t noteHeight(const char* text) const {
    // like tinymd::TextBlock::heightFor(), but for any text (also before it is set)
    tinygpu::IFont<RGB_T>& font = *this->theme().typography.label;
    return tinymd::TextBlock<RGB_T>::lineCount(text, this->inner(), font) *
           (static_cast<int32_t>(font.getHeight(1)) + 3);
  }

  /// Positions the rows from top to bottom, skipping hidden ones.
  void layout() {
    const int32_t inner = this->inner();
    Placer p = this->pagePlacer();
    p.place(bphCaption_, 12);
    constexpr int32_t chipGap = 4;
    const int32_t chipW = (inner - 3 * chipGap) / 4;
    for (int i = 0; i <= kStandardBphCount; ++i) {
      bphChips_[i].bounds = tinymd::Bounds(kMargin + (i % 4) * (chipW + chipGap), p.y + (i / 4) * 30, chipW, 26);
    }
    p.y += 2 * 30 + 6;
    p.place(lift_, tinymd::SliderRow<RGB_T>::kHeight);
    p.place(windowSec_, tinymd::SliderRow<RGB_T>::kHeight);
    p.place(low_, tinymd::SliderRow<RGB_T>::kHeight);
    p.place(high_, tinymd::SliderRow<RGB_T>::kHeight);
    p.place(agc_, tinymd::SwitchRow<RGB_T>::kHeight);
    p.place(gain_, tinymd::SliderRow<RGB_T>::kHeight);
    p.place(sens_, tinymd::SliderRow<RGB_T>::kHeight);
    p.place(sensNote_, noteHeight(kSensNote));
    p.place(monitor_, 110);
    p.place(cal_, tinymd::SliderRow<RGB_T>::kHeight);
    p.place(calNote_, noteHeight(kCalNote));
    p.place(clockNote_, noteHeight(clockText_.c_str()), this->ctx_.clock != nullptr);
    p.place(dark_, tinymd::SwitchRow<RGB_T>::kHeight);
    p.place(demo_, tinymd::SwitchRow<RGB_T>::kHeight);
    p.place(demoNote_, noteHeight(kDemoNote));
    p.place(defaultsButton_, 32);
    this->ctx_.app.screen().invalidate();
  }

  /// Updates the state of the clock correction; relayouts when the text
  /// needs a different height.
  void refreshClock() {
    const ClockCalibrator* clock = this->ctx_.clock;
    if (clock == nullptr) return;
    char buf[160];
    int len = 0;
    float ppm, uncertainty;
    bool measured = false;
    if (clock->best(ppm, uncertainty, &measured)) {
      len = snprintf(buf, sizeof(buf), "Clock correction (NTP): %+.1f ppm = %+.2f s/day (+-%.2f)%s", ppm,
                     ppm * 0.0864f, uncertainty * 0.0864f, measured ? "" : ", stored");
    } else {
      len = snprintf(buf, sizeof(buf), "Clock correction (NTP): none yet");
    }
    if (!measured) {
      const float minutes = clock->spanSec() / 60.0f;
      const float current = clock->uncertaintyPpm();
      if (isfinite(current)) {
        snprintf(buf + len, sizeof(buf) - len, ". Measuring for %.0f min: +-%.1f ppm", minutes, current);
      } else {
        snprintf(buf + len, sizeof(buf) - len, ". Waiting for the time server");
      }
    }
    if (clockText_ == buf) return;
    const bool relayout = noteHeight(clockText_.c_str()) != noteHeight(buf);
    clockText_ = buf;
    clockNote_.setText(buf);
    if (relayout) layout();
  }
};

}  // namespace tinytimegrapher
