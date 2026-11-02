#pragma once
#include <math.h>

#include <string>
#include <vector>

#include "TinyTimeGrapher/Models.h"
#include "TinyTimeGrapher/UI/Theme.h"

namespace tinytimegrapher {

/**
 * @brief Standard timegrapher trace.
 *
 * One row = one balance oscillation (two beats). Beats are placed by their
 * phase in the row, so a steady watch gives two vertical dotted lines (tick,
 * tock); a slope shows the rate error and a shifted tock line shows beat
 * error. The newest tick is at the bottom, anchored at 25 % of the width.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class TimegrapherChart : public tinymd::Widget<RGB_T> {
 public:
  TimegrapherChart() = default;
  TimegrapherChart(tinymd::Bounds bounds, const char* title) : title_(title) { this->bounds = bounds; }

  /// Number of rows (oscillations) shown.
  int rows = 40;

  /// Number of ticks needed to fill the chart.
  size_t ticksNeeded() const { return static_cast<size_t>(rows) * 2 + 4; }

  void setSubtitle(const char* subtitle) { subtitle_ = subtitle; }

  /// Ticks oldest first; bph = 0 if not known (28800 is assumed).
  void setData(const std::vector<Tick>& ticks, int bph) {
    ticks_ = ticks;
    bph_ = bph;
  }

  void draw(tinygpu::ISurface<RGB_T>& target) override {
    using tinymd::toPx;
    const tinymd::MaterialTheme<RGB_T>& t = this->theme();
    const tinymd::Bounds& b = this->bounds;
    const Accents<RGB_T> accents;
    drawCardFrame(target, b, t);
    tinygpu::IFont<RGB_T>& small = *t.typography.label;
    tinygpu::IFont<RGB_T>& body = *t.typography.body;
    drawText(target, body, b.x + 8, b.y + 6, title_.c_str(), t.colors.onSurface, t.colors.surface);
    drawText(target, small, b.x + 8, b.y + 18, subtitle_.c_str(), t.colors.onSurfaceVariant, t.colors.surface);

    // legend at the bottom
    const int32_t legendY = b.bottom() - 6 - static_cast<int32_t>(small.getHeight(1));
    target.fillCircle(toPx(b.x + 12), toPx(legendY + 3), 3, accents.cyan);
    drawText(target, small, b.x + 18, legendY, "tick", t.colors.onSurfaceVariant, t.colors.surface);
    target.fillCircle(toPx(b.x + 52), toPx(legendY + 3), 3, accents.amber);
    drawText(target, small, b.x + 58, legendY, "tock", t.colors.onSurfaceVariant, t.colors.surface);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d bph", bph_ > 0 ? bph_ : 28800);
    drawText(target, small, b.right() - 8 - static_cast<int32_t>(small.measureTextWidth(buf)), legendY, buf,
             t.colors.onSurfaceVariant, t.colors.surface);

    const tinymd::Bounds plot(b.x + 6, b.y + 30, b.w - 12, legendY - 4 - (b.y + 30));
    if (plot.w < 8 || plot.h < 8) return;
    const RGB_T grid = t.colors.outline;
    target.fillRect(toPx(plot.x + plot.w / 4), toPx(plot.y), 1, toPx(plot.h), grid);
    target.fillRect(toPx(plot.x + plot.w * 3 / 4), toPx(plot.y), 1, toPx(plot.h), grid);
    const RGB_T faint = tinymd::blend(t.colors.surface, grid, 0.5f);
    for (int i = 0; i <= 4; ++i) {
      const int32_t y = plot.y + (plot.h - 1) * i / 4;
      target.fillRect(toPx(plot.x), toPx(y), toPx(plot.w), 1, faint);
    }

    // anchor on the newest tick (even beat) so tick and tock keep their side
    // and colour on every redraw
    const Tick* newest = nullptr;
    for (size_t i = ticks_.size(); i-- > 0;) {
      if (ticks_[i].beat % 2 == 0) {
        newest = &ticks_[i];
        break;
      }
    }
    if (newest == nullptr) {
      const char* msg = "Waiting for ticks...";
      drawText(target, body, plot.centerX() - static_cast<int32_t>(body.measureTextWidth(msg)) / 2,
               plot.centerY() - 4, msg, t.colors.onSurfaceVariant, t.colors.surface);
      return;
    }
    const double rowLen = 2 * 3600.0 / (bph_ > 0 ? bph_ : 28800);
    const int32_t radius = plot.h / rows >= 5 ? 2 : 1;
    for (const Tick& tick : ticks_) {
      const double u = (tick.timeSec - newest->timeSec) / rowLen;
      const double k = floor(u + 0.25);
      const double row = -k;
      if (row >= rows || row < 0) continue;
      const double x = u - k + 0.25;
      const int32_t px = plot.x + static_cast<int32_t>(x * plot.w);
      const int32_t py = plot.y + static_cast<int32_t>(plot.h * (1.0 - (row + 0.5) / rows));
      const RGB_T color = tick.beat % 2 == 0 ? accents.cyan : accents.amber;
      target.fillRect(toPx(px - radius), toPx(py - radius), toPx(2 * radius + 1), toPx(2 * radius + 1), color);
    }
  }

 private:
  std::string title_;
  std::string subtitle_;
  std::vector<Tick> ticks_;
  int bph_ = 0;
};

}  // namespace tinytimegrapher
