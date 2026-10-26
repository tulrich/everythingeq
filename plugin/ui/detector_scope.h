#ifndef EVERYTHINGEQ_PLUGIN_UI_DETECTOR_SCOPE_H_
#define EVERYTHINGEQ_PLUGIN_UI_DETECTOR_SCOPE_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "IControl.h"
#include "plugin/ui/editor_model.h"
#include "plugin/ui/theme.h"

namespace eeq {

// Scrolling detector history for every group: each column covers a fixed
// time slice and keeps the trigger range and the peak control value D.
class DetectorHistory {
 public:
  static constexpr double kColumnSeconds = 0.01;
  static constexpr int kColumns = 240;  // 2.4 s.

  struct Column {
    float trigger_min = 0.f;
    float trigger_max = 0.f;
    float control = 0.f;
    bool valid = false;
  };

  void Add(const DetectorFrame& frame, double sample_rate) {
    for (int g = 0; g < kNumGroups; ++g) {
      Column& c = pending_[g];
      if (!c.valid) {
        c = {frame.trigger_min[g], frame.trigger_max[g], frame.control_max[g],
             true};
      } else {
        c.trigger_min = std::min(c.trigger_min, frame.trigger_min[g]);
        c.trigger_max = std::max(c.trigger_max, frame.trigger_max[g]);
        c.control = std::max(c.control, frame.control_max[g]);
      }
    }
    pending_samples_ += frame.samples;
    if (pending_samples_ >= kColumnSeconds * sample_rate) {
      head_ = (head_ + 1) % kColumns;
      for (int g = 0; g < kNumGroups; ++g) {
        columns_[g][head_] = pending_[g];
        pending_[g].valid = false;
      }
      pending_samples_ = 0;
    }
  }

  // Column i of group g, oldest (0) to newest (kColumns - 1).
  const Column& at(int g, int i) const {
    return columns_[g][(head_ + 1 + i) % kColumns];
  }

 private:
  std::array<std::array<Column, kColumns>, kNumGroups> columns_{};
  std::array<Column, kNumGroups> pending_{};
  int pending_samples_ = 0;
  int head_ = 0;
};

// The detector scope (ABOUT.md section 3.B, "trigger meter and threshold
// handle"): the selected group's trigger level over time against the
// effective threshold T_eff and its knee, with the group's output D below.
// Dragging vertically moves T_eff by editing Threshold (alpha < 0.5) or Rise.
class DetectorScope final : public iplug::igraphics::IControl {
 public:
  DetectorScope(const iplug::igraphics::IRECT& bounds, EditorModel& model,
                const DetectorHistory& history, const int& group)
      : IControl(bounds), model_(model), history_(history), group_(group) {
    SetTooltip(
        "Detector: the white trace is what the detector sees (level at "
        "Alpha 0, rise above the recent average at Alpha 1). The group fires "
        "when it crosses the blue threshold line, ramping across the shaded "
        "knee. The strip below is the group output D (0-100%). Drag up/down "
        "to move the threshold.");
  }

  bool IsDirty() override { return true; }  // Scrolls continuously.

  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    const ParamValues v = model_.Values();
    Settings s = Read(v);
    const ig::IRECT trig = TriggerRect();
    const ig::IRECT dstrip = mRECT.GetFromBottom(kStrip);
    g.FillRoundRect(theme::kBackground, mRECT, 3.f);

    // Active region (above or below the threshold), knee band, threshold.
    const float ty = DbToY(s.teff, s, trig);
    const ig::IRECT active = s.below ? ig::IRECT(trig.L, ty, trig.R, trig.B)
                                     : ig::IRECT(trig.L, trig.T, trig.R, ty);
    g.FillRect(theme::kAccent.WithOpacity(0.06f), active);
    if (s.knee > 0.0) {
      const float k0 = DbToY(s.teff + s.knee / 2, s, trig);
      const float k1 = DbToY(s.teff - s.knee / 2, s, trig);
      g.FillRect(theme::kAccent.WithOpacity(0.15f),
                 ig::IRECT(trig.L, k0, trig.R, k1));
    }

    // Trigger trace: one vertical range per column.
    const float col_w = trig.W() / DetectorHistory::kColumns;
    g.PathClear();
    bool started = false;
    for (int i = 0; i < DetectorHistory::kColumns; ++i) {
      const DetectorHistory::Column& c = history_.at(group_, i);
      if (!c.valid) continue;
      const float x = trig.L + (i + 0.5f) * col_w;
      const float y0 = DbToY(c.trigger_max, s, trig);
      const float y1 = DbToY(c.trigger_min, s, trig);
      g.DrawLine(theme::kText.WithOpacity(0.35f), x, y0, x,
                 std::max(y1, y0 + 1));
      if (!started) {
        g.PathMoveTo(x, y0);
        started = true;
      } else {
        g.PathLineTo(x, y0);
      }
    }
    if (started) g.PathStroke(theme::kText.WithOpacity(0.85f), 1.f);
    g.DrawLine(theme::kAccent, trig.L, ty, trig.R, ty, nullptr, 1.5f);

    // D strip.
    g.FillRect(theme::kPanel, dstrip);
    for (int i = 0; i < DetectorHistory::kColumns; ++i) {
      const DetectorHistory::Column& c = history_.at(group_, i);
      if (!c.valid || c.control <= 0.001f) continue;
      const float x = dstrip.L + i * col_w;
      g.FillRect(theme::kAccent.WithOpacity(0.75f),
                 ig::IRECT(x, dstrip.B - c.control * dstrip.H(),
                           x + col_w + 0.5f, dstrip.B));
    }

    // Labels.
    char buf[64];
    const ig::IText small = theme::Text(9.f, theme::kTextDim);
    std::snprintf(buf, sizeof(buf), "%+.0f", s.hi);
    g.DrawText(small, buf,
               ig::IRECT(trig.L + 2, trig.T, trig.L + 40, trig.T + 11));
    std::snprintf(buf, sizeof(buf), "%+.0f", s.lo);
    g.DrawText(small, buf,
               ig::IRECT(trig.L + 2, trig.B - 11, trig.L + 40, trig.B));
    std::snprintf(buf, sizeof(buf), "%s %.1f dB",
                  s.alpha >= 0.5 ? "Rise" : "Thresh", s.teff);
    g.DrawText(theme::Text(9.f, theme::kAccent, ig::EAlign::Far), buf,
               ig::IRECT(trig.R - 90, ty - 12, trig.R - 3, ty - 1));
    std::snprintf(
        buf, sizeof(buf), "D %d%%",
        static_cast<int>(std::lround(model_.LiveControls()[group_] * 100)));
    g.DrawText(theme::Text(9.f, theme::kText, ig::EAlign::Far), buf,
               dstrip.GetHPadded(-3.f));
    g.DrawRoundRect(theme::kBorder, mRECT, 3.f);
  }

  void OnMouseDown(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override {
    const Settings s = Read(model_.Values());
    drag_id_ = GroupParamId(
        group_, s.alpha >= 0.5 ? GroupField::kRise : GroupField::kThreshold);
    drag_weight_ = s.alpha >= 0.5 ? s.alpha : 1.0 - s.alpha;
    model_.BeginEdit(drag_id_);
  }

  void OnMouseDrag(float x, float y, float dx, float dy,
                   const iplug::igraphics::IMouseMod& mod) override {
    if (drag_id_ < 0) return;
    const Settings s = Read(model_.Values());
    const iplug::igraphics::IRECT trig = TriggerRect();
    // Move T_eff by the dragged distance in dB; the edited parameter moves
    // by that over its weight in T_eff = (1 - a) * Threshold + a * Rise.
    const double ddb =
        -dy / trig.H() * (s.hi - s.lo) * (mod.S ? 0.1 : 1.0) / drag_weight_;
    const ParamSpec& spec = ParamTable()[drag_id_];
    const double plain =
        std::clamp(model_.Values()[drag_id_] + ddb, spec.min, spec.max);
    model_.SetEdit(drag_id_, (plain - spec.min) / (spec.max - spec.min));
  }

  void OnMouseUp(float x, float y,
                 const iplug::igraphics::IMouseMod& mod) override {
    if (drag_id_ >= 0) model_.EndEdit(drag_id_);
    drag_id_ = -1;
  }

 private:
  static constexpr float kStrip = 14.f;

  struct Settings {
    double alpha, teff, knee, lo, hi;
    bool below;
  };

  Settings Read(const ParamValues& v) const {
    Settings s;
    auto at = [&](GroupField f) { return v[GroupParamId(group_, f)]; };
    s.alpha = at(GroupField::kAlpha);
    s.teff = (1.0 - s.alpha) * at(GroupField::kThreshold) +
             s.alpha * at(GroupField::kRise);
    s.knee = at(GroupField::kKnee);
    s.below = std::lround(at(GroupField::kMode)) == 1;
    // Level (dBFS) at alpha 0 blends into rise (dB above average) at 1.
    s.lo = (1.0 - s.alpha) * -72.0 + s.alpha * -24.0;
    s.hi = (1.0 - s.alpha) * 0.0 + s.alpha * 36.0;
    return s;
  }

  iplug::igraphics::IRECT TriggerRect() const {
    return mRECT.GetReducedFromBottom(kStrip + 1.f).GetPadded(-1.f);
  }

  static float DbToY(double db, const Settings& s,
                     const iplug::igraphics::IRECT& r) {
    const double u = std::clamp((db - s.lo) / (s.hi - s.lo), 0.0, 1.0);
    return r.B - static_cast<float>(u) * r.H();
  }

  EditorModel& model_;
  const DetectorHistory& history_;
  const int& group_;
  int drag_id_ = -1;
  double drag_weight_ = 1.0;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_DETECTOR_SCOPE_H_
