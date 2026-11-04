#pragma once
#include <stdio.h>

#include <vector>

#include "TinyTimeGrapher/UI/Icons.h"
#include "TinyTimeGrapher/UI/Pages/GrapherPage.h"
#include "TinyTimeGrapher/UI/TimegrapherChart.h"

namespace tinytimegrapher {

/**
 * @brief Graph tab: the timegrapher trace (tick / tock dots) with rate, beat
 * error and amplitude as subtitle.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class GraphPage : public GrapherPage<RGB_T> {
  using Base = GrapherPage<RGB_T>;

 public:
  explicit GraphPage(PageContext<RGB_T>& ctx) : Base(ctx, "Graph", drawTraceIcon<RGB_T>) {}

  void build() override {
    trace_ = TimegrapherChart<RGB_T>(
        tinymd::Bounds(4, this->pageTop() + 4, this->width() - 8, this->pageHeight() - 8), "Timegrapher");
    trace_.rows = 40;
    this->add(trace_);
  }

  void refresh() override {
    const Kpis k = this->engine().kpis();
    this->engine().ticks(ticks_, trace_.ticksNeeded());
    trace_.setData(ticks_, k.nominalBph > 0 ? k.nominalBph : this->cfg().bph);
    char rate[16], be[16], amp[16], buf[64];
    snprintf(buf, sizeof(buf), "%s s/d  %s ms  %s deg", formatValue(rate, sizeof(rate), k.rateSPerDay, 1),
             formatValue(be, sizeof(be), k.beatErrorMs, 2), formatValue(amp, sizeof(amp), k.amplitudeDeg, 0));
    trace_.setSubtitle(buf);
  }

  TimegrapherChart<RGB_T>& chart() { return trace_; }

 private:
  TimegrapherChart<RGB_T> trace_;
  std::vector<Tick> ticks_;
};

}  // namespace tinytimegrapher
