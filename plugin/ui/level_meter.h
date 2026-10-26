#ifndef EVERYTHINGEQ_PLUGIN_UI_LEVEL_METER_H_
#define EVERYTHINGEQ_PLUGIN_UI_LEVEL_METER_H_

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>

#include "IControl.h"

namespace eeq {

// Vertical segmented peak meter (ABOUT.md §4): green below -12 dBFS, yellow
// up to 0 dBFS, red above, with the name underneath.  The scale runs to +6 dBFS
// because float signals can exceed full scale.  A numeric readout on top shows
// the recent peak; once any peak exceeds 0 dBFS it latches (red) until clicked
// or ResetClip() is called (e.g. on transport start).
//
// UI thread only.  Feed it per-channel linear peaks gathered since the last
// call with AddPeaks(); ballistics are computed from wall-clock time.
class LevelMeter final : public iplug::igraphics::IControl {
 public:
  static constexpr int kMaxChannels = 4;
  static constexpr float kMinDb = -60.f;
  static constexpr float kMaxDb = 6.f;
  static constexpr float kYellowDb = -12.f;
  static constexpr float kRedDb = 0.f;
  static constexpr float kFallDbPerSec = 20.f;
  static constexpr float kHoldSec = 1.5f;

  LevelMeter(const iplug::igraphics::IRECT& bounds, const char* label,
             int num_channels)
      : IControl(bounds),
        label_(label),
        num_channels_(std::clamp(num_channels, 1, kMaxChannels)) {
    level_db_.fill(kMinDb);
    hold_db_.fill(kMinDb);
    hold_age_.fill(0.f);
    active_.fill(true);
    SetTooltip("Click to reset the peak readout");
  }

  void SetChannelActive(int ch, bool active) {
    if (ch < num_channels_ && active_[ch] != active) {
      active_[ch] = active;
      SetDirty(false);
    }
  }

  void AddPeaks(const float* linear_peaks) {
    const auto now = std::chrono::steady_clock::now();
    float dt = last_update_.time_since_epoch().count()
                   ? std::chrono::duration<float>(now - last_update_).count()
                   : 0.f;
    dt = std::min(dt, 0.25f);
    last_update_ = now;

    for (int ch = 0; ch < num_channels_; ++ch) {
      const float db =
          linear_peaks[ch] > 0.f ? 20.f * std::log10(linear_peaks[ch]) : -200.f;
      level_db_[ch] =
          std::max(db, std::max(level_db_[ch] - kFallDbPerSec * dt, -200.f));
      if (db >= hold_db_[ch]) {
        hold_db_[ch] = db;
        hold_age_[ch] = 0.f;
      } else if ((hold_age_[ch] += dt) > kHoldSec) {
        hold_db_[ch] = std::max(hold_db_[ch] - kFallDbPerSec * dt, -200.f);
      }
      if (db > kRedDb) clipped_ = true;
      if (clipped_) latched_db_ = std::max(latched_db_, db);
    }
    SetDirty(false);
  }

  void ResetClip() {
    clipped_ = false;
    latched_db_ = -200.f;
    hold_db_.fill(-200.f);
    SetDirty(false);
  }

  void OnMouseDown(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override {
    ResetClip();
  }

  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    const ig::IText label_text(12.f, ig::COLOR_LIGHT_GRAY, "Roboto-Regular",
                               ig::EAlign::Center);
    const ig::IText scale_text(8.f, ig::COLOR_GRAY, "Roboto-Regular",
                               ig::EAlign::Far, ig::EVAlign::Middle);

    ig::IRECT r = mRECT;
    // Peak readout on top, name underneath the bars.
    const ig::IRECT readout_rect = r.ReduceFromTop(18.f).GetPadded(-1.f);
    const ig::IRECT label_rect = r.ReduceFromBottom(16.f);
    r.ReduceFromTop(8.f);  // Room for the +6 label.
    r.ReduceFromBottom(4.f);
    const ig::IRECT scale_rect = r.ReduceFromLeft(13.f);
    bars_rect_ = r;

    g.DrawText(label_text, label_.c_str(), label_rect);

    // Scale ticks.
    for (float db : {6.f, 0.f, -6.f, -12.f, -24.f, -36.f, -48.f, -60.f}) {
      const float y = DbToY(db);
      WDL_String s;
      s.SetFormatted(8, "%d", static_cast<int>(-db));  // Magnitude only.
      if (db > 0.f) s.Set("+6");
      g.DrawText(scale_text, s.Get(),
                 ig::IRECT(scale_rect.L, y - 5.f, scale_rect.R - 2.f, y + 5.f));
      g.DrawLine(ig::COLOR_GRAY.WithOpacity(0.5f), scale_rect.R - 1.5f, y,
                 scale_rect.R, y);
    }

    // Bars.
    constexpr float kSegmentPitch = 3.f;
    constexpr float kSegmentGap = 1.f;
    constexpr float kBarGap = 2.f;
    const float bar_width =
        (bars_rect_.W() - kBarGap * (num_channels_ - 1)) / num_channels_;
    for (int ch = 0; ch < num_channels_; ++ch) {
      const float x0 = bars_rect_.L + ch * (bar_width + kBarGap);
      const ig::IRECT bar(x0, bars_rect_.T, x0 + bar_width, bars_rect_.B);
      g.FillRect(ig::IColor(255, 18, 18, 20), bar);
      if (!active_[ch]) continue;
      for (float top = bars_rect_.B - kSegmentPitch; top >= bars_rect_.T - 0.5f;
           top -= kSegmentPitch) {
        const float segment_db = YToDb(top + 0.5f * kSegmentPitch);
        const bool lit = segment_db <= level_db_[ch];
        g.FillRect(
            ZoneColor(segment_db).WithOpacity(lit ? 1.f : 0.12f),
            ig::IRECT(bar.L, top, bar.R, top + kSegmentPitch - kSegmentGap));
      }
      if (hold_db_[ch] > kMinDb) {
        const float y = DbToY(std::min(hold_db_[ch], kMaxDb));
        g.FillRect(ZoneColor(hold_db_[ch]),
                   ig::IRECT(bar.L, y - 1.f, bar.R, y + 1.f));
      }
    }

    // Peak readout.
    float shown_db = -200.f;
    for (int ch = 0; ch < num_channels_; ++ch) {
      if (active_[ch]) shown_db = std::max(shown_db, hold_db_[ch]);
    }
    if (clipped_) shown_db = latched_db_;
    g.FillRoundRect(
        clipped_ ? ig::IColor(255, 200, 30, 30) : ig::IColor(255, 30, 30, 34),
        readout_rect, 2.f);
    WDL_String s;
    if (shown_db <= -100.f) {
      s.Set("-inf");
    } else {
      s.SetFormatted(16, "%.1f", shown_db);
    }
    g.DrawText(
        ig::IText(10.f, ig::COLOR_WHITE, "Roboto-Regular", ig::EAlign::Center),
        s.Get(), readout_rect);
  }

 private:
  float DbToY(float db) const {
    const float u =
        (std::clamp(db, kMinDb, kMaxDb) - kMinDb) / (kMaxDb - kMinDb);
    return bars_rect_.B - u * bars_rect_.H();
  }
  float YToDb(float y) const {
    return kMinDb + (bars_rect_.B - y) / bars_rect_.H() * (kMaxDb - kMinDb);
  }

  static iplug::igraphics::IColor ZoneColor(float db) {
    if (db > kRedDb) return iplug::igraphics::IColor(255, 235, 50, 45);
    if (db > kYellowDb) return iplug::igraphics::IColor(255, 235, 200, 40);
    return iplug::igraphics::IColor(255, 60, 200, 90);
  }

  std::string label_;
  int num_channels_ = 0;
  std::array<float, kMaxChannels> level_db_{};
  std::array<float, kMaxChannels> hold_db_{};
  std::array<float, kMaxChannels> hold_age_{};
  std::array<bool, kMaxChannels> active_{};
  bool clipped_ = false;
  float latched_db_ = -200.f;
  iplug::igraphics::IRECT bars_rect_;
  std::chrono::steady_clock::time_point last_update_{};
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_LEVEL_METER_H_
