#pragma once
#include "TinyMaterialDesign.h"

namespace tinytimegrapher {

/// Gauge icon (Live tab): half arc with a needle.
template <typename RGB_T>
void drawGaugeIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color,
                   uint8_t thickness = 2) {
  using tinymd::toPx;
  const int32_t radius = r.w * 2 / 5;
  const int32_t cx = r.centerX();
  const int32_t cy = r.y + r.h * 2 / 3;
  target.drawArc(toPx(cx), toPx(cy), toPx(radius), 180.0f, 360.0f, color, thickness);
  tinymd::drawLineThick(target, cx, cy, cx + radius * 2 / 3, cy - radius * 2 / 3, color, thickness);
  target.fillCircle(toPx(cx), toPx(cy), 2, color);
}

/// Two dotted vertical lines (Graph tab): the timegrapher trace.
template <typename RGB_T>
void drawTraceIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color,
                   uint8_t thickness = 2) {
  (void)thickness;
  using tinymd::toPx;
  for (int i = 0; i < 4; ++i) {
    const int32_t y = r.y + 3 + i * (r.h - 6) / 3;
    target.fillCircle(toPx(r.x + r.w / 3 + i / 2), toPx(y), 2, color);
    target.fillCircle(toPx(r.x + r.w * 2 / 3 + i), toPx(y), 2, color);
  }
}

/// Zig-zag line (Trends tab).
template <typename RGB_T>
void drawTrendIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color,
                   uint8_t thickness = 2) {
  const tinymd::Bounds in = r.inset(r.w / 6);
  const int32_t xs[] = {in.x, in.x + in.w / 3, in.x + in.w * 2 / 3, in.right()};
  const int32_t ys[] = {in.bottom(), in.y + in.h / 3, in.y + in.h * 2 / 3, in.y};
  for (int i = 0; i < 3; ++i) tinymd::drawLineThick(target, xs[i], ys[i], xs[i + 1], ys[i + 1], color, thickness);
}

/// Three slider lines with knobs (Settings tab).
template <typename RGB_T>
void drawTuneIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color,
                  uint8_t thickness = 2) {
  using tinymd::toPx;
  const tinymd::Bounds in = r.inset(r.w / 6);
  const int32_t knobs[] = {in.w * 2 / 3, in.w / 4, in.w / 2};
  for (int i = 0; i < 3; ++i) {
    const int32_t y = in.y + 1 + i * (in.h - 2) / 2;
    tinymd::drawLineThick(target, in.x, y, in.right(), y, color, thickness);
    target.fillCircle(toPx(in.x + knobs[i]), toPx(y), 3, color);
  }
}

}  // namespace tinytimegrapher
