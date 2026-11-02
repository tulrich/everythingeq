#ifndef EVERYTHINGEQ_PLUGIN_UI_BAND_RIBBON_H_
#define EVERYTHINGEQ_PLUGIN_UI_BAND_RIBBON_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "IControl.h"
#include "model/response_curves.h"
#include "model/slot_mapping.h"
#include "plugin/ui/editor_model.h"
#include "plugin/ui/theme.h"

namespace eeq {

// The band ribbon over the graph (ABOUT.md section 4): one cell per band
// slot showing its color and a large icon of its type's curve; click to
// select, click the selected band for its type menu.  The dot at the right
// of each cell toggles the band's bypass (when shown).  Off bands show a
// dash, bypassed bands are dimmed and struck through.
//
// Shift-click selects a range of bands and Command-click adds or removes
// one; the selection is where pastes land and what a power dot toggles.
// Right-click (or Control-click) a band for Copy, Paste and the rest.
class BandRibbon final : public iplug::igraphics::IControl {
 public:
  struct Actions {
    std::function<void(int)> copy;  // Copy this band.
    std::function<bool()> can_paste;
    // Paste a section (BandSection) onto these bands.
    std::function<void(int, const std::vector<int>&)> paste;
  };

  BandRibbon(const iplug::igraphics::IRECT& bounds, EditorModel& model,
             Actions actions)
      : IControl(bounds),
        model_(model),
        actions_(std::move(actions)),
        menu_(""),
        context_menu_("") {
    SetTooltip(
        "Bands: click to select, click the selected band to change its type. "
        "Shift-click selects a range, Command-click adds a band; right-click "
        "for Copy and Paste. The dot turns a band on or off (bypass).");
  }

  // The selected bands, in order (the edited band and any added to it).
  std::vector<int> Selection() const {
    std::vector<int> bands;
    const int primary = model_.SelectedBand();
    for (int b = 0; b < kNumBands; ++b) {
      if (b == primary || extra_[b]) bands.push_back(b);
    }
    return bands;
  }
  void SelectAll() {
    extra_.fill(true);
    SetDirty(false);
  }

  void SetShowPower(bool show) { show_power_ = show; }

  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    const ParamValues v = model_.Values();
    const int selected = model_.SelectedBand();
    for (int b = 0; b < kNumBands; ++b) {
      const ig::IRECT cell = Cell(b);
      const int type =
          static_cast<int>(std::lround(v[BandParamId(b, BandField::kType)]));
      const bool off = type == 0;
      const bool bypassed = v[BandParamId(b, BandField::kBypass)] >= 0.5;
      const bool is_selected = b == selected;
      const bool in_selection = is_selected || extra_[b];
      g.FillRoundRect(in_selection ? theme::kPanelRaised : theme::kPanel, cell,
                      3.f);
      g.DrawRoundRect(is_selected    ? theme::kAccent
                      : in_selection ? theme::kAccent.WithOpacity(0.5f)
                                     : theme::kBorder,
                      cell, 3.f, nullptr, in_selection ? 1.5f : 1.f);
      const ig::IColor color = theme::BandColor(b);
      const float a = off || bypassed ? 0.35f : 1.f;
      g.FillRect(color.WithOpacity(a), cell.GetFromLeft(4.f).GetVPadded(-4.f));
      ig::IRECT content = cell.GetReducedFromLeft(7.f);
      if (show_power_) {
        const ig::IRECT power = PowerRect(b);
        content.R = power.L - 2.f;
        if (hover_power_ == b) {
          g.FillRoundRect(theme::kText.WithOpacity(0.1f), power, 3.f);
        }
        g.DrawLine(theme::kGrid, power.L, power.T + 3.f, power.L,
                   power.B - 3.f);
        const float cx = power.MW(), cy = power.MH();
        if (!off && !bypassed) g.FillCircle(color, cx, cy, 5.f);
        g.DrawCircle(off        ? theme::kBorder
                     : bypassed ? theme::kTextDim
                                : color,
                     cx, cy, 4.f, nullptr, 2.f);
      }
      // The type's curve, as large as the cell allows; Off is a dash.
      const ig::IRECT icon = content.GetPadded(-4.f, -5.f, -4.f, -5.f);
      if (off) {
        g.DrawLine(theme::kBorder, icon.MW() - 6.f, icon.MH(), icon.MW() + 6.f,
                   icon.MH(), nullptr, 1.5f);
      } else {
        DrawIcon(g, static_cast<BandType>(type), icon, color.WithOpacity(a),
                 1.75f);
      }
      if (bypassed && !off) {
        g.DrawLine(theme::kTextDim, content.L + 2.f, content.B - 5.f,
                   content.R - 2.f, content.T + 5.f);
      }
    }
  }

  void OnMouseDown(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override {
    for (int b = 0; b < kNumBands; ++b) {
      if (!Cell(b).Contains(x, y)) continue;
      const ParamValues v = model_.Values();
      const int type =
          static_cast<int>(std::lround(v[BandParamId(b, BandField::kType)]));
      const int primary = model_.SelectedBand();
      // On the Mac, iPlug reports Command-click as left+right and
      // Control-click as mod.C.
      const bool context = (mod.R && !mod.L) || mod.C;
      const bool toggle = mod.L && mod.R;
      if (context) {
        OpenContextMenu(b);
      } else if (show_power_ && PowerRect(b).Contains(x, y)) {
        TogglePower(b, v);
      } else if (mod.S) {
        for (int i = std::min(b, primary); i <= std::max(b, primary); ++i) {
          extra_[i] = true;
        }
      } else if (toggle) {
        if (b != primary) extra_[b] = !extra_[b];
      } else if (b == primary) {
        OpenTypeMenu(b, type);
      } else {
        extra_.fill(false);
        model_.SelectBand(b);
      }
      SetDirty(false);
      return;
    }
  }

  void OnMouseOver(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override {
    IControl::OnMouseOver(x, y, mod);
    hover_power_ = -1;
    for (int b = 0; b < kNumBands && show_power_; ++b) {
      if (PowerRect(b).Contains(x, y)) hover_power_ = b;
    }
  }
  void OnMouseOut() override {
    IControl::OnMouseOut();
    hover_power_ = -1;
  }

  void OnPopupMenuSelection(iplug::igraphics::IPopupMenu* menu, int) override {
    if (!menu || menu_band_ < 0) return;
    const int chosen = menu->GetChosenItemIdx();
    if (chosen < 0) return;
    if (menu == &context_menu_) return OnContextChoice(chosen);
    const int id = BandParamId(menu_band_, BandField::kType);
    model_.SetValueGesture(id, chosen / ParamTable()[id].max);
  }

  bool IsDirty() override { return true; }  // Cheap; follows param changes.

 private:
  static constexpr int kNumTypes = static_cast<int>(BandType::kVintageHigh) + 1;
  static constexpr int kIconPoints = 24;
  static constexpr double kIconRangeDb = 15.0;  // Half height.

  // The curve of a band of `type` at typical settings (a boost for types
  // with a gain), 20 Hz - 20 kHz, in dB.
  static const std::array<float, kIconPoints>& IconCurve(BandType type) {
    static const auto curves = [] {
      std::array<std::array<float, kIconPoints>, kNumTypes> c{};
      const std::vector<double> freqs = DisplayFrequencies(kIconPoints);
      for (int t = 1; t < kNumTypes; ++t) {
        BandParams bp;
        bp.type = static_cast<BandType>(t);
        if (const auto m = SlotMappingFor(bp.type, Slot::kX)) bp.x = m->mid;
        if (const auto m = SlotMappingFor(bp.type, Slot::kZ)) bp.z = m->mid;
        if (const auto m = SlotMappingFor(bp.type, Slot::kY)) {
          bp.y = std::string(m->unit) == "dB" ? 12.0 : m->max * 0.5;
        }
        if (bp.type == BandType::kTilt) bp.z = 6.0;
        if (bp.type == BandType::kBp) bp.y = 6.0;
        for (int i = 0; i < kIconPoints; ++i) {
          const double mag = std::abs(BandParamsResponse(
              bp, 0.0, 0.0, 1.0, freqs[i], 48000.0, nullptr));
          c[t][i] = static_cast<float>(20.0 * std::log10(std::max(mag, 1e-6)));
        }
      }
      return c;
    }();
    return curves[static_cast<int>(type)];
  }

 public:
  // A small plot of a band type's curve at typical settings.
  static void DrawIcon(iplug::igraphics::IGraphics& g, BandType type,
                       const iplug::igraphics::IRECT& r,
                       const iplug::igraphics::IColor& color,
                       float thickness = 1.25f) {
    const std::array<float, kIconPoints>& c = IconCurve(type);
    g.DrawLine(theme::kGrid, r.L, r.MH(), r.R, r.MH());
    // The curve, clipped to the icon: an LP or HP leaves through the bottom
    // instead of running along it.
    auto point = [&](int i) {
      return std::pair<float, float>(r.L + r.W() * i / (kIconPoints - 1),
                                     static_cast<float>(c[i] / kIconRangeDb));
    };
    auto y_of = [&](float u) { return r.MH() - u * 0.5f * r.H(); };
    g.PathClear();
    bool pen = false;
    for (int i = 1; i < kIconPoints; ++i) {
      auto [x0, u0] = point(i - 1);
      auto [x1, u1] = point(i);
      // Clip the segment to |u| <= 1.
      float t0 = 0.f, t1 = 1.f;
      for (const float bound : {-1.f, 1.f}) {
        const float d0 = bound > 0.f ? bound - u0 : u0 - bound;
        const float d1 = bound > 0.f ? bound - u1 : u1 - bound;
        if (d0 < 0.f && d1 < 0.f) t1 = -1.f;  // Wholly outside.
        if (d0 < 0.f && d1 >= 0.f) t0 = std::max(t0, d0 / (d0 - d1));
        if (d0 >= 0.f && d1 < 0.f) t1 = std::min(t1, d0 / (d0 - d1));
      }
      if (t0 > t1) {
        pen = false;
        continue;
      }
      const float xa = x0 + (x1 - x0) * t0, ua = u0 + (u1 - u0) * t0;
      const float xb = x0 + (x1 - x0) * t1, ub = u0 + (u1 - u0) * t1;
      if (!pen || t0 > 0.f) g.PathMoveTo(xa, y_of(ua));
      g.PathLineTo(xb, y_of(ub));
      pen = t1 >= 1.f;
    }
    g.PathStroke(color, thickness);
  }

 private:
  iplug::igraphics::IRECT Cell(int band) const {
    return mRECT.SubRectHorizontal(kNumBands, band).GetHPadded(-2.f);
  }
  iplug::igraphics::IRECT PowerRect(int band) const {
    return Cell(band).GetFromRight(kPowerWidth);
  }

  void OpenTypeMenu(int band, int current) {
    const std::vector<std::string>& labels =
        ParamTable()[BandParamId(band, BandField::kType)].enum_labels;
    menu_.Clear();
    for (size_t i = 0; i < labels.size(); ++i) {
      menu_.AddItem(labels[i].c_str(), -1,
                    static_cast<int>(i) == current
                        ? iplug::igraphics::IPopupMenu::Item::kChecked
                        : iplug::igraphics::IPopupMenu::Item::kNoFlags);
    }
    menu_band_ = band;
    GetUI()->CreatePopupMenu(*this, menu_, Cell(band));
  }

  // The bands a context action on `band` applies to: the selection if the
  // band is in it, else just the band.
  std::vector<int> TargetsFor(int band) const {
    const std::vector<int> sel = Selection();
    for (int b : sel) {
      if (b == band) return sel;
    }
    return {band};
  }

  // Bypass every target to the opposite of the clicked band's state.
  void TogglePower(int band, const ParamValues& v) {
    if (std::lround(v[BandParamId(band, BandField::kType)]) == 0) return;
    const bool bypass = v[BandParamId(band, BandField::kBypass)] < 0.5;
    ParamValues next = v;
    for (int b : TargetsFor(band)) {
      if (std::lround(v[BandParamId(b, BandField::kType)]) == 0) continue;
      next[BandParamId(b, BandField::kBypass)] = bypass ? 1.0 : 0.0;
    }
    model_.SetValuesGesture(next);
  }

  enum ContextItem {
    kCopy,
    kPasteAll,
    kPasteShape,
    kPasteDynamics,
    kSep1,
    kBypassItem,
    kTurnOff,
    kSep2,
    kSelectAll,
  };

  void OpenContextMenu(int band) {
    namespace ig = iplug::igraphics;
    const std::vector<int> targets = TargetsFor(band);
    const std::string where = targets.size() > 1
                                  ? std::to_string(targets.size()) + " Bands"
                                  : "Band " + std::to_string(band + 1);
    const int paste_flags = actions_.can_paste && actions_.can_paste()
                                ? ig::IPopupMenu::Item::kNoFlags
                                : ig::IPopupMenu::Item::kDisabled;
    context_menu_.Clear();
    context_menu_.AddItem(("Copy Band " + std::to_string(band + 1)).c_str());
    context_menu_.AddItem(("Paste to " + where).c_str(), -1, paste_flags);
    context_menu_.AddItem(("Paste Shape to " + where).c_str(), -1, paste_flags);
    context_menu_.AddItem(("Paste Dynamics to " + where).c_str(), -1,
                          paste_flags);
    context_menu_.AddSeparator();
    context_menu_.AddItem(("Bypass " + where).c_str());
    context_menu_.AddItem(("Turn Off " + where).c_str());
    context_menu_.AddSeparator();
    context_menu_.AddItem("Select All Bands");
    menu_band_ = band;
    GetUI()->CreatePopupMenu(*this, context_menu_, Cell(band));
  }

  void OnContextChoice(int item) {
    const std::vector<int> targets = TargetsFor(menu_band_);
    const ParamValues v = model_.Values();
    ParamValues next = v;
    switch (item) {
      case kCopy:
        if (actions_.copy) actions_.copy(menu_band_);
        break;
      case kPasteAll:
      case kPasteShape:
      case kPasteDynamics:
        if (actions_.paste) actions_.paste(item - kPasteAll, targets);
        break;
      case kBypassItem:
        for (int b : targets) next[BandParamId(b, BandField::kBypass)] = 1.0;
        model_.SetValuesGesture(next);
        break;
      case kTurnOff:
        for (int b : targets) next[BandParamId(b, BandField::kType)] = 0.0;
        model_.SetValuesGesture(next);
        break;
      case kSelectAll:
        SelectAll();
        break;
      default:
        break;
    }
    SetDirty(false);
  }

  static constexpr float kPowerWidth = 22.f;
  EditorModel& model_;
  Actions actions_;
  std::array<bool, kNumBands> extra_{};
  iplug::igraphics::IPopupMenu menu_;
  iplug::igraphics::IPopupMenu context_menu_;
  int menu_band_ = -1;
  bool show_power_ = true;
  int hover_power_ = -1;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_BAND_RIBBON_H_
