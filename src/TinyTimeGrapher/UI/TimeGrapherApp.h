#pragma once
#include <functional>
#include <vector>

#include "TinyMaterialDesign.h"
#include "TinyTimeGrapher/TimeGrapherEngine.h"
#include "TinyTimeGrapher/UI/Icons.h"
#include "TinyTimeGrapher/UI/Pages/GraphPage.h"
#include "TinyTimeGrapher/UI/Pages/GrapherPage.h"
#include "TinyTimeGrapher/UI/Pages/LivePage.h"
#include "TinyTimeGrapher/UI/Pages/SettingsPage.h"
#include "TinyTimeGrapher/UI/Pages/TrendsPage.h"
#include "TinyTimeGrapher/UI/Theme.h"

namespace tinytimegrapher {

/**
 * @brief The Time Grapher touch GUI on top of a TimeGrapherEngine, built
 * with TinyMaterialDesign.
 *
 * The app provides the frame: the app bar with a menu (Start / Stop and
 * Shut down) and the navigation bar. Each tab is its own GrapherPage (a
 * tinymd::Page, switched by a tinymd::PageNavigator):
 *
 * - LivePage: KPI tiles, input level and the volume chart
 * - GraphPage: the timegrapher trace (tick / tock dots)
 * - TrendsPage: rate, beat error and amplitude over time
 * - SettingsPage: beat rate, lift angle, analysis window, filter, gain,
 *   sensitivity, calibration, clock correction (see setClockCalibrator()),
 *   theme, demo mode and reset to defaults
 *
 * Further pages can be added with addPage() before begin().
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
  enum Page { kLive = 0, kGraph = 1, kTrends = 2, kSettings = 3 };

  /// Called (debounced: 2 s after the last change) when the user has
  /// changed a setting, e.g. to persist it.
  std::function<void(const GrapherConfig& config, bool dark)> onSettingsChanged;

  /// Called when the user has confirmed "Shut down" in the menu, after the
  /// measurement was stopped and pending settings were reported to
  /// onSettingsChanged. Without a callback the menu has no "Shut down".
  std::function<void()> onShutdown;

  TimeGrapherApp(tinygpu::LCDBoard& board, TimeGrapherEngine& engine)
      : darkTheme_(timeGrapherDarkTheme<RGB_T>()),
        lightTheme_(timeGrapherLightTheme<RGB_T>()),
        app_(board, darkTheme_),
        ctx_(engine, app_),
        live_(ctx_),
        graph_(ctx_),
        trends_(ctx_),
        settings_(ctx_),
        navigator_(app_.screen(), nav_) {
    pages_ = {&live_, &graph_, &trends_, &settings_};
    ctx_.onConfigChanged = [this]() { configChanged(); };
    ctx_.showPage = [this](int page) { showPage(page); };
    ctx_.setDark = [this](bool dark) {
      setDark(dark);
      configChanged();
    };
    ctx_.isDark = [this]() { return dark_; };
    ctx_.requestRefresh = [this]() { forceRefresh_ = true; };
  }

  /// Adds a page (before begin()).
  void addPage(GrapherPage<RGB_T>& page) { pages_.push_back(&page); }

  /// Builds the GUI and starts the board (display + touch).
  void begin(bool darkTheme = true) {
    dark_ = darkTheme;
    app_.begin();
    if (!darkTheme) app_.screen().setTheme(lightTheme_);
    ctx_.cfg = ctx_.engine.config();
    ctx_.top = kBarHeight;
    ctx_.height = height() - kBarHeight - kNavHeight;
    buildFrame();
    nav_ = tinymd::NavigationBar<RGB_T>(tinymd::Bounds(0, height() - kNavHeight, width(), kNavHeight));
    for (GrapherPage<RGB_T>* page : pages_) navigator_.addPage(*page);
    navigator_.onPageChanged = [this](int) { forceRefresh_ = true; };
    navigator_.begin(tinymd::Bounds(0, ctx_.top, width(), ctx_.height));  // builds the pages
    built_ = true;
  }

  /// Call from loop(): handles touch input and redraws when needed.
  void update() {
    const uint32_t now = millis();
    bool redraw = navigator_.tick(now);
    if (now - lastRefreshMs_ >= refreshIntervalMs_) {
      const uint32_t rev = ctx_.engine.revision();
      if (rev != lastRevision_ || forceRefresh_) {
        lastRevision_ = rev;
        lastRefreshMs_ = now;
        forceRefresh_ = false;
        pages_[navigator_.current()]->refresh();
        redraw = true;
      }
    }
    if (redraw) app_.screen().invalidate();
    if (settingsChanged_ && now - settingsChangedMs_ > 2000) {
      settingsChanged_ = false;
      if (onSettingsChanged) onSettingsChanged(ctx_.cfg, dark_);
    }
    app_.update();
  }

  /// Minimum time between two redraws caused by new measurement data.
  void setRefreshIntervalMs(uint32_t ms) { refreshIntervalMs_ = ms; }

  void showPage(int index) {
    if (!built_) return;
    navigator_.show(index);
    forceRefresh_ = true;
  }

  bool isDark() const { return dark_; }
  void setDark(bool dark) {
    dark_ = dark;
    app_.screen().setTheme(dark ? darkTheme_ : lightTheme_);
    forceRefresh_ = true;
  }

  /// Shows the state of the clock correction (e.g. by NTPClockSync) in the
  /// Settings.
  void setClockCalibrator(const ClockCalibrator& calibrator) {
    ctx_.clock = &calibrator;
    if (built_) settings_.clockChanged();
  }

  tinymd::App<RGB_T>& app() { return app_; }
  PageContext<RGB_T>& context() { return ctx_; }
  LivePage<RGB_T>& livePage() { return live_; }
  GraphPage<RGB_T>& graphPage() { return graph_; }
  TrendsPage<RGB_T>& trendsPage() { return trends_; }
  SettingsPage<RGB_T>& settingsPage() { return settings_; }

 private:
  static constexpr int32_t kBarHeight = 40;
  static constexpr int32_t kNavHeight = 56;

  tinymd::MaterialTheme<RGB_T> darkTheme_;
  tinymd::MaterialTheme<RGB_T> lightTheme_;
  tinymd::App<RGB_T> app_;
  PageContext<RGB_T> ctx_;
  LivePage<RGB_T> live_;
  GraphPage<RGB_T> graph_;
  TrendsPage<RGB_T> trends_;
  SettingsPage<RGB_T> settings_;
  std::vector<GrapherPage<RGB_T>*> pages_;
  tinymd::NavigationBar<RGB_T> nav_;
  tinymd::PageNavigator<RGB_T> navigator_;

  // frame
  tinymd::AppBar<RGB_T> appBar_;
  tinymd::IconButton<RGB_T> menuButton_;
  tinymd::Menu<RGB_T> menu_;
  tinymd::ListItem<RGB_T> startItem_, shutdownItem_;
  tinymd::Dialog<RGB_T> shutdownDialog_;
  tinymd::Button<RGB_T> shutdownCancel_, shutdownConfirm_;

  bool built_ = false;
  bool dark_ = true;
  bool forceRefresh_ = true;
  bool settingsChanged_ = false;
  uint32_t settingsChangedMs_ = 0;
  uint32_t refreshIntervalMs_ = 100;
  uint32_t lastRefreshMs_ = 0;
  uint32_t lastRevision_ = 0;

  int32_t width() const { return static_cast<int32_t>(app_.width()); }
  int32_t height() const { return static_cast<int32_t>(app_.height()); }

  /// Applies the edited configuration and updates all pages.
  void configChanged() {
    ctx_.engine.setConfig(ctx_.cfg);
    for (GrapherPage<RGB_T>* page : pages_) page->configChanged();
    settingsChanged_ = true;
    settingsChangedMs_ = millis();
    forceRefresh_ = true;
  }

  void buildFrame() {
    appBar_ = tinymd::AppBar<RGB_T>(tinymd::Bounds(0, 0, width(), kBarHeight), "Time Grapher");
    menuButton_ = tinymd::IconButton<RGB_T>(appBar_.trailingRect(), tinymd::drawMenu<RGB_T>);
    menuButton_.onClick = [this]() { openMenu(); };
    appBar_.trailing = &menuButton_;
    app_.screen().addFixedWidget(appBar_);
    buildMenu();
  }

  void buildMenu() {
    constexpr int32_t menuW = 150;
    constexpr int32_t itemH = 48;
    menu_ = tinymd::Menu<RGB_T>(tinymd::Bounds(width() - menuW - 4, kBarHeight, menuW, 2 * itemH));
    startItem_ = tinymd::ListItem<RGB_T>(menu_.itemRect(0, itemH), "Start", drawPlayIcon<RGB_T>);
    startItem_.onClick = [this]() {
      app_.screen().dismissDialog();
      TimeGrapherEngine& engine = ctx_.engine;
      if (engine.isRunning()) {
        engine.stop();
      } else {
        engine.start();
      }
      forceRefresh_ = true;
    };
    menu_.addItem(startItem_);
    shutdownItem_ = tinymd::ListItem<RGB_T>(menu_.itemRect(1, itemH), "Shut down", drawPowerIcon<RGB_T>);
    shutdownItem_.onClick = [this]() {
      app_.screen().dismissDialog();
      app_.screen().presentDialog(shutdownDialog_);
    };
    menu_.addItem(shutdownItem_);
    menu_.onOutsideTap = [this]() { app_.screen().dismissDialog(); };

    const int32_t dialogH = 150;
    shutdownDialog_ =
        tinymd::Dialog<RGB_T>(tinymd::Bounds(16, (height() - dialogH) / 2, width() - 32, dialogH), "Shut down?",
                              "The measurement is stopped and the device is switched off.");
    shutdownCancel_ = tinymd::Button<RGB_T>(shutdownDialog_.actionRect(0, 2), "Cancel", tinymd::ButtonVariant::kText);
    shutdownCancel_.onClick = [this]() { app_.screen().dismissDialog(); };
    shutdownConfirm_ = tinymd::Button<RGB_T>(shutdownDialog_.actionRect(1, 2), "Shut down");
    shutdownConfirm_.onClick = [this]() {
      app_.screen().dismissDialog();
      shutdown();
    };
    shutdownDialog_.addAction(shutdownCancel_);
    shutdownDialog_.addAction(shutdownConfirm_);
  }

  /// Opens the menu with the items for the current state.
  void openMenu() {
    const bool running = ctx_.engine.isRunning();
    startItem_.setTitle(running ? "Stop" : "Start");
    startItem_.setIcon(running ? drawStopIcon<RGB_T> : drawPlayIcon<RGB_T>);
    shutdownItem_.visible = static_cast<bool>(onShutdown);
    const int items = shutdownItem_.visible ? 2 : 1;
    menu_.bounds.h = items * startItem_.bounds.h;
    app_.screen().presentDialog(menu_);
  }

  void shutdown() {
    ctx_.engine.stop();
    if (settingsChanged_) {
      settingsChanged_ = false;
      if (onSettingsChanged) onSettingsChanged(ctx_.cfg, dark_);
    }
    if (onShutdown) onShutdown();
  }
};

}  // namespace tinytimegrapher
