#pragma once
#include <string>

#include "TinyTimeGrapher/UI/Theme.h"

namespace tinytimegrapher {

/**
 * @brief Small tile showing one KPI: caption, value (in the accent color)
 * and a unit / sub text.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class StatTile : public tinymd::Widget<RGB_T> {
 public:
  StatTile() = default;
  StatTile(tinymd::Bounds bounds, const char* caption, const char* sub = "") : caption_(caption), sub_(sub) {
    this->bounds = bounds;
  }

  void setCaption(const char* caption) { caption_ = caption; }
  void setValue(const char* value) { value_ = value; }
  void setSub(const char* sub) { sub_ = sub; }
  void setAccent(RGB_T color) { accent_ = color; }

  void draw(tinygpu::ISurface<RGB_T>& target) override {
    using tinymd::toPx;
    const tinymd::MaterialTheme<RGB_T>& t = this->theme();
    const tinymd::Bounds& b = this->bounds;
    const size_t radius = toPx(t.shape.medium);
    target.fillRoundRect(toPx(b.x), toPx(b.y), toPx(b.w), toPx(b.h), radius, t.colors.surface);
    target.drawRoundRect(toPx(b.x), toPx(b.y), toPx(b.w), toPx(b.h), radius, t.colors.outline);

    tinygpu::IFont<RGB_T>& small = *t.typography.label;
    tinygpu::IFont<RGB_T>& big = *t.typography.title;
    const int32_t x = b.x + 5;
    drawText(target, small, x, b.y + 4, caption_.c_str(), t.colors.onSurfaceVariant, t.colors.surface);
    // shrink to the body font if the value does not fit
    tinygpu::IFont<RGB_T>& valueFont =
        static_cast<int32_t>(big.measureTextWidth(value_.c_str())) <= b.w - 8 ? big : *t.typography.body;
    const int32_t valueY = b.y + 4 + static_cast<int32_t>(small.getHeight(1)) + 3;
    drawText(target, valueFont, x, valueY, value_.c_str(), accent_, t.colors.surface);
    drawText(target, small, x, b.bottom() - 4 - static_cast<int32_t>(small.getHeight(1)), sub_.c_str(),
             t.colors.onSurfaceVariant, t.colors.surface);
  }

 private:
  std::string caption_;
  std::string value_ = "--";
  std::string sub_;
  RGB_T accent_ = Accents<RGB_T>().cyan;
};

/**
 * @brief Text that is wrapped at word boundaries to the widget width.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class TextBlock : public tinymd::Widget<RGB_T> {
 public:
  TextBlock() = default;
  TextBlock(tinymd::Bounds bounds, const char* text) : text_(text) { this->bounds = bounds; }

  void setText(const char* text) { text_ = text; }

  void draw(tinygpu::ISurface<RGB_T>& target) override {
    const tinymd::MaterialTheme<RGB_T>& t = this->theme();
    tinygpu::IFont<RGB_T>& font = *t.typography.label;
    const int32_t lineHeight = static_cast<int32_t>(font.getHeight(1)) + 3;
    int32_t y = this->bounds.y;
    std::string line;
    std::string word;
    auto flush = [&]() {
      drawText(target, font, this->bounds.x, y, line.c_str(), t.colors.onSurfaceVariant, t.colors.background);
      y += lineHeight;
      line.clear();
    };
    for (size_t i = 0; i <= text_.size(); ++i) {
      const char c = i < text_.size() ? text_[i] : ' ';
      if (c != ' ') {
        word += c;
        continue;
      }
      if (word.empty()) continue;
      const std::string candidate = line.empty() ? word : line + " " + word;
      if (!line.empty() && static_cast<int32_t>(font.measureTextWidth(candidate.c_str())) > this->bounds.w) {
        flush();
        line = word;
      } else {
        line = candidate;
      }
      word.clear();
    }
    if (!line.empty()) flush();
  }

  /// Height needed to show the whole text with the indicated font.
  static int32_t heightFor(const char* text, int32_t width, tinygpu::IFont<RGB_T>& font) {
    const int32_t lineHeight = static_cast<int32_t>(font.getHeight(1)) + 3;
    int32_t lines = 0;
    std::string line, word, s(text);
    for (size_t i = 0; i <= s.size(); ++i) {
      const char c = i < s.size() ? s[i] : ' ';
      if (c != ' ') {
        word += c;
        continue;
      }
      if (word.empty()) continue;
      const std::string candidate = line.empty() ? word : line + " " + word;
      if (!line.empty() && static_cast<int32_t>(font.measureTextWidth(candidate.c_str())) > width) {
        lines++;
        line = word;
      } else {
        line = candidate;
      }
      word.clear();
    }
    if (!line.empty()) lines++;
    return lines * lineHeight;
  }

 private:
  std::string text_;
};

}  // namespace tinytimegrapher
