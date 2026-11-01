#ifndef EVERYTHINGEQ_PLUGIN_UI_BAND_RIBBON_H_
#define EVERYTHINGEQ_PLUGIN_UI_BAND_RIBBON_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

#include "IControl.h"
#include "model/response_curves.h"
#include "model/slot_mapping.h"
#include "plugin/ui/editor_model.h"
#include "plugin/ui/theme.h"

namespace eeq {

// The top band ribbon (ABOUT.md section 4): one cell per band slot showing
// its number, an icon of its type's curve and a short type name (dropped as
// the cells narrow); click to select, click the selected band for its type
// menu.  The dot at the right of each cell toggles the band's bypass (when
// shown).  Off bands read "--", bypassed bands are dimmed and struck through.
class BandRibbon final : public iplug::igraphics::IControl {
 public:
  BandRibbon(const iplug::igraphics::IRECT& bounds, EditorModel& model)
      : IControl(bounds), model_(model), menu_("") {
    SetTooltip(
        "Bands: click to select; click the selected band to change its type. "
        "The dot turns a band on or off (bypass).");
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
      g.FillRoundRect(is_selected ? theme::kPanelRaised : theme::kPanel, cell,
                      3.f);
      g.DrawRoundRect(is_selected ? theme::kAccent : theme::kBorder, cell, 3.f,
                      nullptr, is_selected ? 1.5f : 1.f);
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
      const ig::IColor text_color =
          off || bypassed ? theme::kTextDim : theme::kText;
      const std::string number = std::to_string(b + 1);
      g.DrawText(theme::Text(10.f, theme::kTextDim), number.c_str(),
                 content.ReduceFromLeft(9.f));
      if (!off && content.W() >= kIconWidth + 2.f) {
        DrawIcon(g, static_cast<BandType>(type),
                 content.ReduceFromLeft(kIconWidth).GetVPadded(-6.f),
                 color.WithOpacity(a));
        content.ReduceFromLeft(4.f);
      }
      if (content.W() >= 34.f || off) {
        g.DrawText(theme::Text(10.f, text_color), ShortName(type), content);
      }
      if (bypassed && !off) {
        g.DrawLine(theme::kTextDim, cell.L + 10.f, cell.MH(), cell.R - 6.f,
                   cell.MH());
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
      if (show_power_ && PowerRect(b).Contains(x, y)) {
        if (type != 0) {
          const int id = BandParamId(b, BandField::kBypass);
          model_.SetValueGesture(id, v[id] >= 0.5 ? 0.0 : 1.0);
        }
      } else if (b == model_.SelectedBand()) {
        OpenTypeMenu(b, type);
      } else {
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
    const int id = BandParamId(menu_band_, BandField::kType);
    model_.SetValueGesture(id, chosen / ParamTable()[id].max);
  }

  bool IsDirty() override { return true; }  // Cheap; follows param changes.

 private:
  static constexpr int kNumTypes = static_cast<int>(BandType::kPultecHigh) + 1;
  static constexpr float kIconWidth = 20.f;
  static constexpr int kIconPoints = 24;
  static constexpr double kIconRangeDb = 15.0;  // Half height.

  static const char* ShortName(int type) {
    static const char* const kNames[] = {
        "--",  "Peak",  "Lo Shelf", "Hi Shelf",  "Band Shelf",
        "LP",  "HP",    "Notch",    "BP",        "Tilt",
        "F-M", "Vowel", "K-Wt",     "Pultec Lo", "Pultec Hi"};
    return kNames[std::clamp(type, 0, static_cast<int>(std::size(kNames)) - 1)];
  }

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

  static void DrawIcon(iplug::igraphics::IGraphics& g, BandType type,
                       const iplug::igraphics::IRECT& r,
                       const iplug::igraphics::IColor& color) {
    const std::array<float, kIconPoints>& c = IconCurve(type);
    g.DrawLine(theme::kGrid, r.L, r.MH(), r.R, r.MH());
    g.PathClear();
    for (int i = 0; i < kIconPoints; ++i) {
      const float x = r.L + r.W() * i / (kIconPoints - 1);
      const float u =
          static_cast<float>(std::clamp(c[i] / kIconRangeDb, -1.0, 1.0));
      const float y = r.MH() - u * 0.5f * r.H();
      if (i == 0) {
        g.PathMoveTo(x, y);
      } else {
        g.PathLineTo(x, y);
      }
    }
    g.PathStroke(color, 1.25f);
  }

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

  static constexpr float kPowerWidth = 22.f;
  EditorModel& model_;
  iplug::igraphics::IPopupMenu menu_;
  int menu_band_ = -1;
  bool show_power_ = true;
  int hover_power_ = -1;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_BAND_RIBBON_H_
