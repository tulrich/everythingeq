#ifndef EVERYTHINGEQ_PLUGIN_UI_BAND_RIBBON_H_
#define EVERYTHINGEQ_PLUGIN_UI_BAND_RIBBON_H_

#include <cmath>
#include <string>

#include "IControl.h"
#include "plugin/ui/editor_model.h"
#include "plugin/ui/theme.h"

namespace eeq {

// The top band ribbon (ABOUT.md section 4): one cell per band slot showing
// its number and type; click to select.  Off bands read "--", bypassed bands
// are dimmed and struck through.
class BandRibbon final : public iplug::igraphics::IControl {
 public:
  BandRibbon(const iplug::igraphics::IRECT& bounds, EditorModel& model)
      : IControl(bounds), model_(model) {}

  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    const ParamValues v = model_.Values();
    const int selected = model_.SelectedBand();
    const std::vector<std::string>& types =
        ParamTable()[BandParamId(0, BandField::kType)].enum_labels;
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
      const std::string text =
          "B" + std::to_string(b + 1) + ": " + (off ? "--" : types[type]);
      const ig::IText t =
          theme::Text(11.f, off || bypassed ? theme::kTextDim : theme::kText,
                      ig::EAlign::Center);
      g.DrawText(t, text.c_str(), cell.GetReducedFromLeft(4.f));
      if (bypassed && !off) {
        g.DrawLine(theme::kTextDim, cell.L + 10.f, cell.MH(), cell.R - 6.f,
                   cell.MH());
      }
    }
  }

  void OnMouseDown(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override {
    for (int b = 0; b < kNumBands; ++b) {
      if (Cell(b).Contains(x, y)) {
        model_.SelectBand(b);
        SetDirty(false);
        return;
      }
    }
  }

  bool IsDirty() override { return true; }  // Cheap; follows param changes.

 private:
  iplug::igraphics::IRECT Cell(int band) const {
    return mRECT.SubRectHorizontal(kNumBands, band).GetHPadded(-2.f);
  }

  EditorModel& model_;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_BAND_RIBBON_H_
