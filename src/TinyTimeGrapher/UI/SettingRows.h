#pragma once
#include <math.h>

#include <functional>
#include <string>

#include "TinyTimeGrapher/UI/Theme.h"

namespace tinytimegrapher {

/**
 * @brief Settings row: caption and formatted value on one line, a Slider
 * below. The value is quantized to `step`.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class ValueSlider : public tinymd::Widget<RGB_T> {
 public:
  using Formatter = std::function<void(float value, char* buf, size_t len)>;

  static constexpr int32_t kHeight = 40;

  ValueSlider() = default;
  ValueSlider(const char* caption, float minValue, float maxValue, float step, Formatter format)
      : caption_(caption), step_(step), format_(std::move(format)), slider_(tinymd::Bounds(), minValue, maxValue) {
    slider_.onChange = [this](float v) {
      const float q = quantize(v);
      if (q == value_) return;
      value_ = q;
      if (onChange) onChange(value_);
    };
  }

  std::function<void(float)> onChange;

  float value() const { return value_; }
  void setValue(float value) {
    value_ = quantize(value);
    slider_.setValue(value_);
  }

  void setTheme(const tinymd::MaterialTheme<RGB_T>& theme) override {
    tinymd::Widget<RGB_T>::setTheme(theme);
    slider_.setTheme(theme);
  }

  void draw(tinygpu::ISurface<RGB_T>& target) override {
    const tinymd::MaterialTheme<RGB_T>& t = this->theme();
    tinygpu::IFont<RGB_T>& small = *t.typography.label;
    tinygpu::IFont<RGB_T>& body = *t.typography.body;
    const tinymd::Bounds& b = this->bounds;
    const RGB_T captionColor = this->enabled ? t.colors.onSurfaceVariant
                                             : tinymd::blend(t.colors.onSurfaceVariant, t.colors.background, 0.5f);
    drawText(target, small, b.x, b.y + 3, caption_.c_str(), captionColor, t.colors.background);
    char buf[24] = "";
    if (format_) format_(value_, buf, sizeof(buf));
    drawText(target, body, b.right() - static_cast<int32_t>(body.measureTextWidth(buf)), b.y + 2, buf,
             this->enabled ? t.colors.onSurface : captionColor, t.colors.background);
    syncSlider();
    slider_.enabled = this->enabled;
    slider_.draw(target);
  }

  bool onGesture(const tinygpu::GestureEvent& event) override {
    syncSlider();
    return slider_.onGesture(event);
  }

  bool isDraggableAt(int32_t x, int32_t y) const override {
    (void)x;
    return y >= this->bounds.y + kTextHeight;
  }

 private:
  static constexpr int32_t kTextHeight = 14;
  std::string caption_;
  float step_ = 1.0f;
  float value_ = 0.0f;
  Formatter format_;
  tinymd::Slider<RGB_T> slider_;

  float quantize(float v) const { return step_ > 0 ? roundf(v / step_) * step_ : v; }

  /// The slider band below the text line - the bounds move while a parent
  /// container scrolls, so this is recalculated before every use.
  void syncSlider() {
    const tinymd::Bounds& b = this->bounds;
    slider_.bounds = tinymd::Bounds(b.x + 9, b.y + kTextHeight + 2, b.w - 18, 18);
  }
};

/**
 * @brief Settings row: caption on the left, Switch on the right; tapping
 * anywhere on the row toggles it.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class SwitchRow : public tinymd::Widget<RGB_T> {
 public:
  static constexpr int32_t kHeight = 32;

  SwitchRow() = default;
  explicit SwitchRow(const char* caption) : caption_(caption) {}

  std::function<void(bool)> onChange;

  bool value() const { return switch_.value(); }
  void setValue(bool value) { switch_.setValue(value); }

  void setTheme(const tinymd::MaterialTheme<RGB_T>& theme) override {
    tinymd::Widget<RGB_T>::setTheme(theme);
    switch_.setTheme(theme);
  }

  void draw(tinygpu::ISurface<RGB_T>& target) override {
    const tinymd::MaterialTheme<RGB_T>& t = this->theme();
    const tinymd::Bounds& b = this->bounds;
    tinygpu::IFont<RGB_T>& body = *t.typography.body;
    drawText(target, body, b.x, b.centerY() - static_cast<int32_t>(body.getHeight(1)) / 2, caption_.c_str(),
             t.colors.onSurface, t.colors.background);
    switch_.bounds = tinymd::Bounds(b.right() - 44, b.centerY() - 12, 44, 24);
    switch_.draw(target);
  }

  bool onGesture(const tinygpu::GestureEvent& event) override {
    if (!tinymd::isTapGesture(event.type)) return false;
    switch_.setValue(!switch_.value());
    if (onChange) onChange(switch_.value());
    return true;
  }

 private:
  std::string caption_;
  tinymd::Switch<RGB_T> switch_;
};

}  // namespace tinytimegrapher
