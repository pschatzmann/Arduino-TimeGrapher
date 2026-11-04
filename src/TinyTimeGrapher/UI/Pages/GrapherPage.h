#pragma once
#include <math.h>

#include <functional>
#include <vector>

#include "TinyMaterialDesign.h"
#include "TinyTimeGrapher/ClockCalibrator.h"
#include "TinyTimeGrapher/TimeGrapherEngine.h"
#include "TinyTimeGrapher/UI/Theme.h"

namespace tinytimegrapher {

/**
 * @brief What the pages share: the engine, the configuration that is edited
 * and the actions of the app. Owned by TimeGrapherApp.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
struct PageContext {
  PageContext(TimeGrapherEngine& engine, tinymd::App<RGB_T>& app) : engine(engine), app(app) {}

  TimeGrapherEngine& engine;
  tinymd::App<RGB_T>& app;
  /// The configuration as edited by the GUI (applied with configChanged()).
  GrapherConfig cfg;
  Accents<RGB_T> accents;
  /// The clock correction (e.g. by NTPClockSync), nullptr if none.
  const ClockCalibrator* clock = nullptr;
  /// Area of the pages (between app bar and navigation bar).
  int32_t top = 0;
  int32_t height = 0;

  /// Applies cfg to the engine and lets all pages update.
  std::function<void()> onConfigChanged;
  std::function<void(int page)> showPage;
  std::function<void(bool dark)> setDark;
  std::function<bool()> isDark;
  /// Requests a refresh of the page with new data.
  std::function<void()> requestRefresh;

  void configChanged() {
    if (onConfigChanged) onConfigChanged();
  }
  void refreshSoon() {
    if (requestRefresh) requestRefresh();
  }
};

/**
 * @brief Base class of the tabs: a tinymd::Page (a scrollable Container with
 * title and icon, shown by a tinymd::PageNavigator) that builds its own
 * widgets and updates them from the TimeGrapherEngine.
 *
 * The navigator calls build() once (after the page was added to the screen,
 * so it has a theme), tick() regularly while it is shown; the app calls
 * refresh() when new data is available while the page is shown and
 * configChanged() when the configuration was changed by any page.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class GrapherPage : public tinymd::Page<RGB_T> {
 public:
  GrapherPage(PageContext<RGB_T>& ctx, const char* title, tinymd::IconPainter<RGB_T> icon)
      : tinymd::Page<RGB_T>(title, icon), ctx_(ctx) {}

  /// Updates the widgets from the engine.
  virtual void refresh() {}
  /// The configuration (ctx.cfg) was changed.
  virtual void configChanged() {}

 protected:
  static constexpr int32_t kMargin = 8;
  /// Right margin of the scrolling pages: keeps the controls out of the
  /// touch zone of the scrollbar.
  static constexpr int32_t kRightMargin = tinymd::Container<RGB_T>::kScrollbarTouchWidth;

  PageContext<RGB_T>& ctx_;

  TimeGrapherEngine& engine() { return ctx_.engine; }
  GrapherConfig& cfg() { return ctx_.cfg; }
  const Accents<RGB_T>& accents() const { return ctx_.accents; }
  const tinymd::MaterialTheme<RGB_T>& theme() const { return ctx_.app.theme(); }
  int32_t width() const { return static_cast<int32_t>(ctx_.app.width()); }
  int32_t screenHeight() const { return static_cast<int32_t>(ctx_.app.height()); }
  /// Width of the content (keeps the scrollbar free).
  int32_t inner() const { return width() - kMargin - kRightMargin; }
  int32_t pageTop() const { return ctx_.top; }
  int32_t pageHeight() const { return ctx_.height; }

  /// Positions widgets from top to bottom; hidden ones get empty bounds.
  struct Placer {
    int32_t x, y, w, top;
    void place(tinymd::Widget<RGB_T>& widget, int32_t h, bool show = true, int32_t gap = 6) {
      widget.visible = show;
      if (!show) {
        widget.bounds = tinymd::Bounds(x, top, 0, 0);  // still counts for the content height
        return;
      }
      widget.bounds = tinymd::Bounds(x, y, w, h);
      y += h + gap;
    }
  };

  /// Placer over the whole width of the page, starting at its top.
  Placer pagePlacer(int32_t gap = 6) const { return Placer{kMargin, pageTop() + gap, inner(), pageTop()}; }

  void add(tinymd::Widget<RGB_T>& widget) { this->addChild(widget); }

  /// Sets up a chart of the input volume in dB with a reference line for the
  /// detection threshold; returns the index of that line.
  int initVolumeChart(tinymd::LineChart<RGB_T>& chart, tinymd::Bounds bounds, const char* title) {
    chart = tinymd::LineChart<RGB_T>(bounds, title);
    chart.addSeries(accents().cyan, 1);
    chart.setYRange(-80, 0);
    chart.setGridValues({0, -20, -40, -60});
    chart.setVisiblePoints(TTG_VOLUME_POINTS);
    return chart.addReferenceLine(-80, accents().amber);
  }

  /// Fills a chart created by initVolumeChart() with the current volume.
  void refreshVolume(tinymd::LineChart<RGB_T>& chart, int thresholdLine) {
    const float threshold = engine().volume(levels_);
    for (float& l : levels_) l = toDb(l);
    chart.setValues(0, levels_);
    chart.setReferenceLine(thresholdLine, toDb(threshold));
  }

  /// Empties a chart created by initVolumeChart().
  void clearVolume(tinymd::LineChart<RGB_T>& chart, int thresholdLine) {
    levels_.clear();
    chart.setValues(0, levels_);
    chart.setReferenceLine(thresholdLine, -80.0f);
  }

  static float toDb(float v) {
    if (v <= 0.0f) return -80.0f;
    const float d = 20.0f * log10f(v);
    return d < -80.0f ? -80.0f : (d > 0.0f ? 0.0f : d);
  }

 private:
  // scratch buffer, kept to avoid allocations on every refresh
  std::vector<float> levels_;
};

}  // namespace tinytimegrapher
