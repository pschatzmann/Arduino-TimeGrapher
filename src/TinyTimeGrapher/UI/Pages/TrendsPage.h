#pragma once
#include <math.h>
#include <stdio.h>

#include <string>
#include <vector>

#include "TinyTimeGrapher/UI/Icons.h"
#include "TinyTimeGrapher/UI/Pages/GrapherPage.h"

namespace tinytimegrapher {

/**
 * @brief Trends tab: rate, beat error and amplitude over the last 1, 5 or
 * 30 minutes (or the whole session) and a button to reset the session.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class TrendsPage : public GrapherPage<RGB_T> {
  using Base = GrapherPage<RGB_T>;
  using Base::kMargin;

 public:
  explicit TrendsPage(PageContext<RGB_T>& ctx) : Base(ctx, "Trends", drawTrendIcon<RGB_T>) {}

  void build() override {
    const Accents<RGB_T>& a = this->accents();
    const int32_t inner = this->inner();
    int32_t y = this->pageTop() + 6;
    window_ = tinymd::SegmentedButton<RGB_T>(tinymd::Bounds(kMargin, y, inner, 28));
    for (const char* s : {"1 min", "5 min", "30 min", "All"}) window_.addSegment(s);
    window_.setSelected(windowIndex_, true);
    window_.onChange = [this](uint32_t mask) {
      for (int i = 0; i < kWindowCount; ++i) {
        if (mask & (1u << i)) windowIndex_ = i;
      }
      this->ctx_.refreshSoon();
    };
    this->add(window_);
    y += 36;

    static const char* const titles[kChartCount] = {"Rate (s/day)", "Beat error (ms)", "Amplitude (deg)"};
    const RGB_T colors[kChartCount] = {a.cyan, a.amber, a.emerald};
    for (int i = 0; i < kChartCount; ++i) {
      charts_[i] = tinymd::LineChart<RGB_T>(tinymd::Bounds(kMargin, y, inner, 130), titles[i]);
      charts_[i].addSeries(colors[i]);
      this->add(charts_[i]);
      y += 138;
    }
    resetButton_ = tinymd::Button<RGB_T>(tinymd::Bounds(kMargin, y, inner, 32), "Reset session",
                                         tinymd::ButtonVariant::kOutlined);
    resetButton_.onClick = [this]() {
      this->engine().resetSession();
      this->ctx_.refreshSoon();
    };
    this->add(resetButton_);
  }

  void refresh() override {
    static const size_t windows[kWindowCount] = {60, 300, 1800, TTG_MAX_HISTORY};
    this->engine().history(history_, windows[windowIndex_]);
    for (int i = 0; i < kChartCount; ++i) {
      values_.resize(history_.size());
      for (size_t j = 0; j < history_.size(); ++j) {
        const KpiPoint& p = history_[j];
        values_[j] = i == 0 ? p.rateSPerDay : (i == 1 ? p.beatErrorMs : p.amplitudeDeg);
      }
      charts_[i].setValues(0, values_);
      charts_[i].setFooter(summary(values_).c_str());
    }
  }

 private:
  static constexpr int kWindowCount = 4;
  static constexpr int kChartCount = 3;

  tinymd::SegmentedButton<RGB_T> window_;
  int windowIndex_ = 1;
  tinymd::LineChart<RGB_T> charts_[kChartCount];
  tinymd::Button<RGB_T> resetButton_;
  // scratch buffers, kept to avoid allocations on every refresh
  std::vector<KpiPoint> history_;
  std::vector<float> values_;

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
