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
 * The app provides the frame: the app bar with a menu (Start / Stop, Reset
 * session, WiFi, Light / Dark theme and Shut down), the navigation bar and
 * the on-screen keyboard. Each tab is its own GrapherPage (a tinymd::Page,
 * switched by a tinymd::PageNavigator):
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
 * With setWifi() the app has a WiFi page (tinymd::WifiLoginPage: networks,
 * SSID, password) in the menu, e.g. for the clock correction with NTP. With
 * setWifiStartupPrompt(true) it is also shown on startup when the device has
 * no stored credentials or cannot connect in time.
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
        navigator_(app_.screen(), nav_),
        wifiPage_(keyboard_, app_.screen()) {
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

  /// The WiFi of the device (before begin()): adds the WiFi page to the menu.
  void setWifi(tinymd::WifiControl& wifi) { wifi_ = &wifi; }

  /// Shows the WiFi page on startup when there are no stored credentials,
  /// or they do not connect within setWifiStartupTimeoutMs() (default: off).
  void setWifiStartupPrompt(bool prompt) { wifiPrompt_ = prompt; }

  /// Time on startup until the WiFi page is shown when the stored
  /// credentials do not connect.
  void setWifiStartupTimeoutMs(uint32_t ms) { wifiTimeoutMs_ = ms; }

  /// Builds the GUI and starts the board (display + touch).
  void begin(bool darkTheme = true) {
    dark_ = darkTheme;
    app_.begin();
    if (!darkTheme) app_.screen().setTheme(lightTheme_);
    ctx_.cfg = ctx_.engine.config();
    ctx_.top = kBarHeight;
    ctx_.height = height() - kBarHeight - kNavHeight;
    keyboard_.bounds = tinymd::Bounds(0, height() - kKeyboardHeight, width(), kKeyboardHeight);
    keyboard_.visible = false;
    buildFrame();
    nav_ = tinymd::NavigationBar<RGB_T>(tinymd::Bounds(0, height() - kNavHeight, width(), kNavHeight));
    for (GrapherPage<RGB_T>* page : pages_) navigator_.addPage(*page);
    navigator_.onPageChanged = [this](int) {
      keyboard_.hide();
      hideWifi();
      forceRefresh_ = true;
    };
    navigator_.begin(tinymd::Bounds(0, ctx_.top, width(), ctx_.height));  // builds the pages
    // a tab also leaves the WiFi page when it was the current page before
    nav_.onChange = [this](int index) { showPage(index); };
    if (wifi_ != nullptr) {
      // not a tab: shown instead of the pages by showWifi()
      wifiPage_.setWifi(wifi_);
      wifiPage_.onDone = [this]() { showPage(navigator_.current() >= 0 ? navigator_.current() : kLive); };
      wifiPage_.bounds = tinymd::Bounds(0, ctx_.top, width(), ctx_.height);
      wifiPage_.visible = false;
      app_.screen().addWidget(wifiPage_);
      wifiPage_.build();
    }
    app_.screen().addFixedWidget(keyboard_);  // last: on top of everything
    beginMs_ = millis();
    built_ = true;
  }

  /// Call from loop(): handles touch input and redraws when needed.
  void update() {
    const uint32_t now = millis();
    checkWifiOnStartup(now);
    bool redraw = wifiVisible_ ? wifiPage_.tick(now) : navigator_.tick(now);
    if (now - lastRefreshMs_ >= refreshIntervalMs_) {
      const uint32_t rev = ctx_.engine.revision();
      if (rev != lastRevision_ || forceRefresh_) {
        lastRevision_ = rev;
        lastRefreshMs_ = now;
        forceRefresh_ = false;
        if (!wifiVisible_) pages_[navigator_.current()]->refresh();
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
    hideWifi();
    navigator_.show(index);
    forceRefresh_ = true;
  }

  /// Shows the WiFi page (if setWifi() was called).
  void showWifi() {
    if (!built_ || wifi_ == nullptr) return;
    keyboard_.hide();
    for (int i = 0; i < navigator_.count(); ++i) navigator_.page(i).visible = false;
    nav_.clearSelection();  // a tap on any tab leaves the WiFi page
    wifiPage_.visible = true;
    wifiVisible_ = true;
    wifiPage_.onShow();
    forceRefresh_ = true;
    app_.screen().invalidate();
  }

  bool isWifiShown() const { return wifiVisible_; }

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
  tinymd::WifiLoginPage<RGB_T>& wifiPage() { return wifiPage_; }

 private:
  static constexpr int32_t kBarHeight = 40;
  static constexpr int32_t kNavHeight = 56;
  static constexpr int32_t kKeyboardHeight = 170;
  static constexpr int32_t kMenuItemHeight = 44;

  tinymd::MaterialTheme<RGB_T> darkTheme_;
  tinymd::MaterialTheme<RGB_T> lightTheme_;
  tinymd::App<RGB_T> app_;
  tinymd::Keyboard<RGB_T> keyboard_;
  PageContext<RGB_T> ctx_;
  LivePage<RGB_T> live_;
  GraphPage<RGB_T> graph_;
  TrendsPage<RGB_T> trends_;
  SettingsPage<RGB_T> settings_;
  std::vector<GrapherPage<RGB_T>*> pages_;
  tinymd::NavigationBar<RGB_T> nav_;
  tinymd::PageNavigator<RGB_T> navigator_;
  tinymd::WifiLoginPage<RGB_T> wifiPage_;
  tinymd::WifiControl* wifi_ = nullptr;
  bool wifiVisible_ = false;
  bool wifiPrompt_ = false;
  bool wifiChecked_ = false;
  uint32_t wifiTimeoutMs_ = 15000;
  uint32_t beginMs_ = 0;

  // frame
  tinymd::AppBar<RGB_T> appBar_;
  tinymd::IconButton<RGB_T> menuButton_;
  tinymd::Menu<RGB_T> menu_;
  tinymd::ListItem<RGB_T> startItem_, resetItem_, wifiItem_, themeItem_, shutdownItem_;
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

  void hideWifi() {
    if (!wifiVisible_) return;
    wifiVisible_ = false;
    wifiPage_.visible = false;
    wifiPage_.onHide();
  }

  /// Shows the WiFi page once on startup (setWifiStartupPrompt()): at once
  /// without stored credentials, otherwise when they fail or do not connect
  /// in time.
  void checkWifiOnStartup(uint32_t now) {
    if (wifiChecked_ || wifi_ == nullptr || !wifiPrompt_) return;
    const tinymd::WifiControl::State state = wifi_->state();
    if (state == tinymd::WifiControl::State::kConnected) {
      wifiChecked_ = true;
      return;
    }
    const bool failed = state == tinymd::WifiControl::State::kFailed;
    const bool timeout = now - beginMs_ >= wifiTimeoutMs_;
    if (!wifi_->hasCredentials() || failed || timeout) {
      wifiChecked_ = true;
      showWifi();
    }
  }

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

  void addMenuItem(tinymd::ListItem<RGB_T>& item, const char* title, tinymd::IconPainter<RGB_T> icon,
                   std::function<void()> action) {
    item = tinymd::ListItem<RGB_T>(menu_.itemRect(0, kMenuItemHeight), title, icon);
    item.onClick = [this, action]() {
      app_.screen().dismissDialog();
      action();
    };
    menu_.addItem(item);
  }

  void buildMenu() {
    constexpr int32_t menuW = 160;
    menu_ = tinymd::Menu<RGB_T>(tinymd::Bounds(width() - menuW - 4, kBarHeight, menuW, kMenuItemHeight));
    addMenuItem(startItem_, "Start", drawPlayIcon<RGB_T>, [this]() {
      TimeGrapherEngine& engine = ctx_.engine;
      if (engine.isRunning()) {
        engine.stop();
      } else {
        engine.start();
      }
      forceRefresh_ = true;
    });
    addMenuItem(resetItem_, "Reset session", drawResetIcon<RGB_T>, [this]() {
      ctx_.engine.resetSession();
      forceRefresh_ = true;
    });
    addMenuItem(wifiItem_, "WiFi", tinymd::drawWifi<RGB_T>, [this]() { showWifi(); });
    addMenuItem(themeItem_, "Light theme", drawThemeIcon<RGB_T>, [this]() {
      setDark(!dark_);
      settings_.onShow();  // updates its theme switch
      configChanged();
    });
    addMenuItem(shutdownItem_, "Shut down", drawPowerIcon<RGB_T>,
                [this]() { app_.screen().presentDialog(shutdownDialog_); });
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
    themeItem_.setTitle(dark_ ? "Light theme" : "Dark theme");
    wifiItem_.visible = wifi_ != nullptr;
    shutdownItem_.visible = static_cast<bool>(onShutdown);
    // the visible items from the top
    int count = 0;
    for (tinymd::ListItem<RGB_T>* item : {&startItem_, &resetItem_, &wifiItem_, &themeItem_, &shutdownItem_}) {
      if (item->visible) item->bounds = menu_.itemRect(count++, kMenuItemHeight);
    }
    menu_.bounds.h = count * kMenuItemHeight;
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
