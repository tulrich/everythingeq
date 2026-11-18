#ifndef EVERYTHINGEQ_PLUGIN_UI_DETECTOR_SCOPE_H_
#define EVERYTHINGEQ_PLUGIN_UI_DETECTOR_SCOPE_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "IControl.h"
#include "dsp/bands/vowel.h"
#include "dsp/dynamics/spectral_envelope.h"
#include "dsp/dynamics/tracker.h"
#include "model/slot_mapping.h"
#include "plugin/ui/editor_model.h"
#include "plugin/ui/eq_graph.h"
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
                const DetectorHistory& history, const EqGraph& graph,
                const int& group)
      : IControl(bounds),
        model_(model),
        history_(history),
        graph_(graph),
        group_(group) {
    SetTooltip(
        "Detector: the white trace is what the detector sees (level at "
        "Alpha 0, rise above the recent average at Alpha 1). The group fires "
        "when it crosses the blue threshold line, ramping across the shaded "
        "knee. The strip below is the group output D (0-100%), grey when no "
        "band uses this group, with the gain its bands apply now. Drag "
        "up/down to move the threshold. Detect Spectrum adds the input's "
        "16 band levels (100 Hz - 8 kHz) at the right, measured as each "
        "band's trigger: lit where they cross the threshold.");
  }

  bool IsDirty() override { return true; }  // Scrolls continuously.

  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    const ParamValues v = model_.Values();
    Settings s = Read(v);
    const ig::IRECT full = TriggerRect();
    const ig::IRECT dstrip = mRECT.GetFromBottom(kStrip);
    g.FillRoundRect(theme::kBackground, mRECT, 3.f);
    // Spectrum: the band levels take the right part; the trace the rest.
    const bool spectral = s.detect == DetectMode::kSpectrum;
    const ig::IRECT trig =
        spectral ? full.GetReducedFromRight(full.W() * kBarsShare) : full;

    // Active region (above or below the threshold), knee band, threshold.
    const bool level = UsesGainComputer(s.detect);
    const float ty = DbToY(s.teff, s, trig);
    const ig::IRECT active_rect = s.below
                                      ? ig::IRECT(trig.L, ty, trig.R, trig.B)
                                      : ig::IRECT(trig.L, trig.T, trig.R, ty);
    const ig::IRECT active_full =
        ig::IRECT(full.L, active_rect.T, full.R, active_rect.B);
    if (level) g.FillRect(theme::kAccent.WithOpacity(0.06f), active_full);
    if (level && s.knee > 0.0) {
      const float k0 = DbToY(s.teff + s.knee / 2, s, trig);
      const float k1 = DbToY(s.teff - s.knee / 2, s, trig);
      g.FillRect(theme::kAccent.WithOpacity(0.15f),
                 ig::IRECT(full.L, k0, full.R, k1));
    }
    if (spectral) DrawBands(g, s, full.GetFromRight(full.W() * kBarsShare));

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
    if (level)
      g.DrawLine(theme::kAccent, full.L, ty, full.R, ty, nullptr, 1.5f);

    // D strip.
    const bool active = graph_.group_active(group_);
    const ig::IColor d_color = active ? theme::kAccent.WithOpacity(0.75f)
                                      : theme::kTextDim.WithOpacity(0.35f);
    g.FillRect(theme::kPanel, dstrip);
    for (int i = 0; i < DetectorHistory::kColumns; ++i) {
      const DetectorHistory::Column& c = history_.at(group_, i);
      if (!c.valid || c.control <= 0.001f) continue;
      const float x = dstrip.L + i * col_w;
      g.FillRect(d_color, ig::IRECT(x, dstrip.B - c.control * dstrip.H(),
                                    x + col_w + 0.5f, dstrip.B));
    }

    // Labels.
    char buf[64];
    const ig::IText dim_text = theme::Text(9.f, theme::kTextDim);
    std::snprintf(buf, sizeof(buf), "%+.0f", s.hi);
    g.DrawText(dim_text, buf,
               ig::IRECT(trig.L + 2, trig.T, trig.L + 40, trig.T + 11));
    std::snprintf(buf, sizeof(buf), "%+.0f", s.lo);
    g.DrawText(dim_text, buf,
               ig::IRECT(trig.L + 2, trig.B - 11, trig.L + 40, trig.B));
    if (level) {
      std::snprintf(buf, sizeof(buf), "%s %.1f dB",
                    s.alpha >= 0.5 ? "Rise" : "Thresh", s.teff);
      g.DrawText(theme::Text(9.f, theme::kAccent, ig::EAlign::Far), buf,
                 ig::IRECT(trig.R - 90, ty - 12, trig.R - 3, ty - 1));
    } else {
      // The tracked vowel or pitch, dimmed while held (unvoiced input).
      const EditorModel::Tracked t = model_.LiveTracking()[group_];
      const std::string text =
          TrackedText(s.detect, t.value) + (t.voiced ? "" : "  (held)");
      g.DrawText(theme::Text(11.f, t.voiced ? theme::kAccent : theme::kTextDim,
                             ig::EAlign::Far),
                 text.c_str(),
                 ig::IRECT(trig.R - 140, trig.T + 2, trig.R - 4, trig.T + 15));
    }
    std::snprintf(
        buf, sizeof(buf), "D %d%%",
        static_cast<int>(std::lround(model_.LiveControls()[group_] * 100)));
    g.DrawText(theme::Text(9.f, theme::kText, ig::EAlign::Far), buf,
               dstrip.GetHPadded(-3.f));
    if (active) {
      std::snprintf(
          buf, sizeof(buf), "%+.1f dB",
          std::round(graph_.group_gain_db(group_) * 10.f) / 10.f + 0.f);
      g.DrawText(theme::Text(9.f, theme::kGain, ig::EAlign::Far), buf,
                 dstrip.GetHPadded(-3.f).GetReducedFromRight(36.f));
    } else {
      g.DrawText(theme::Text(9.f, theme::kTextDim, ig::EAlign::Far),
                 "no bands: set a band's Dyn Range",
                 dstrip.GetHPadded(-3.f).GetReducedFromRight(36.f));
    }
    g.DrawRoundRect(theme::kBorder, mRECT, 3.f);
  }

  void OnMouseDown(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override {
    const Settings s = Read(model_.Values());
    if (!UsesGainComputer(s.detect)) return;  // No threshold to drag.
    drag_id_ = GroupParamId(
        group_, s.alpha >= 0.5 ? GroupField::kRise : GroupField::kThreshold);
    drag_weight_ = s.alpha >= 0.5 ? s.alpha : 1.0 - s.alpha;
    drag_plain_ = model_.Values()[drag_id_];
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
    // Accumulate unsnapped, so small moves add up across the detents.
    const ParamSpec& spec = ParamTable()[drag_id_];
    drag_plain_ = std::clamp(drag_plain_ + ddb, spec.min, spec.max);
    model_.SetEdit(drag_id_, (drag_plain_ - spec.min) / (spec.max - spec.min));
  }

  void OnMouseUp(float x, float y,
                 const iplug::igraphics::IMouseMod& mod) override {
    if (drag_id_ >= 0) model_.EndEdit(drag_id_);
    drag_id_ = -1;
  }

  // "AH 2.10" (Vowel) or "220 Hz A3" (Pitch) for a tracked control value.
  static std::string TrackedText(DetectMode mode, double value) {
    char buf[32];
    if (mode == DetectMode::kVowel) {
      return FormatSlotValue(*SlotMappingFor(BandType::kVowel, Slot::kX),
                             value * kVowelMorphMax);
    }
    static const char* const kNotes[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                         "F#", "G",  "G#", "A",  "A#", "B"};
    const double hz = Tracker::ValueToPitch(value);
    const int midi =
        static_cast<int>(std::lround(69.0 + 12.0 * std::log2(hz / 440.0)));
    std::snprintf(buf, sizeof(buf), "%.0f Hz %s%d", hz,
                  kNotes[(midi % 12 + 12) % 12], midi / 12 - 1);
    return buf;
  }

 private:
  static constexpr float kStrip = 14.f;
  static constexpr float kBarsShare = 0.36f;  // Of the width, Spectrum.

  struct Settings {
    double alpha, teff, knee, lo, hi;
    bool below;
    DetectMode detect;
  };

  Settings Read(const ParamValues& v) const {
    Settings s;
    auto at = [&](GroupField f) { return v[GroupParamId(group_, f)]; };
    s.alpha = at(GroupField::kAlpha);
    s.teff = (1.0 - s.alpha) * at(GroupField::kThreshold) +
             s.alpha * at(GroupField::kRise);
    s.knee = at(GroupField::kKnee);
    s.below = std::lround(at(GroupField::kMode)) == 1;
    s.detect = static_cast<DetectMode>(std::lround(at(GroupField::kDetect)));
    // Level (dBFS) at alpha 0 blends into rise (dB above average) at 1.
    s.lo = (1.0 - s.alpha) * -72.0 + s.alpha * -24.0;
    s.hi = (1.0 - s.alpha) * 0.0 + s.alpha * 36.0;
    return s;
  }

  // A Spectrum group's band levels as its per-band trigger sees them (level
  // less alpha times the spectrum's mean), on the trace's scale.
  void DrawBands(iplug::igraphics::IGraphics& g, const Settings& s,
                 const iplug::igraphics::IRECT& r) const {
    namespace ig = iplug::igraphics;
    const SpectralLevels levels = model_.LiveSpectra()[group_];
    double peak = SpectralEnvelope::kFloorDb, mean = 0.0;
    for (float l : levels) peak = std::max(peak, static_cast<double>(l));
    for (float l : levels) {
      mean += std::max(static_cast<double>(l),
                       peak - SpectralEnvelope::kMeanFloorDb);
    }
    mean /= kSpectralBands;
    g.DrawLine(theme::kBorder, r.L, r.T, r.L, r.B);
    const ig::IRECT bars = r.GetHPadded(-3.f);  // The trace's scale.
    const float w = bars.W() / kSpectralBands;
    for (int i = 0; i < kSpectralBands; ++i) {
      const double trigger = levels[i] - s.alpha * mean;
      const bool fires = s.below ? trigger < s.teff : trigger > s.teff;
      const float x = bars.L + i * w;
      const float y = DbToY(trigger, s, bars);
      g.FillRect(fires ? theme::kAccent : theme::kText.WithOpacity(0.35f),
                 ig::IRECT(x + 0.5f, y, x + w - 0.5f, bars.B));
    }
    g.DrawText(theme::Text(8.f, theme::kTextDim), "100",
               ig::IRECT(bars.L, bars.T, bars.L + 20, bars.T + 10));
    g.DrawText(theme::Text(8.f, theme::kTextDim, ig::EAlign::Far), "8k",
               ig::IRECT(bars.R - 20, bars.T, bars.R, bars.T + 10));
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
  const EqGraph& graph_;
  const int& group_;
  int drag_id_ = -1;
  double drag_weight_ = 1.0;
  double drag_plain_ = 0.0;  // The dragged value, unsnapped.
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_DETECTOR_SCOPE_H_
