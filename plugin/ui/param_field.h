#ifndef EVERYTHINGEQ_PLUGIN_UI_PARAM_FIELD_H_
#define EVERYTHINGEQ_PLUGIN_UI_PARAM_FIELD_H_

#include <algorithm>
#include <string>

#include "IControl.h"
#include "plugin/ui/theme.h"

namespace eeq {

// A compact "LABEL [ value ]" control bound to one parameter (ABOUT.md
// section 4).  Continuous parameters: drag vertically (Shift = fine) or use
// the wheel; double-click to type a value.  Enums: click for a menu.  Bools:
// click to toggle (drawn as a button).  Option-click resets to the default.
// Disabled fields are drawn at the inactive opacity and ignore the mouse.
//
// The bound parameter can change (e.g. when another band is selected), see
// Rebind().
class ParamField final : public iplug::igraphics::IControl {
 public:
  enum class Style { kField, kToggle };

  ParamField(const iplug::igraphics::IRECT& bounds, int param_id,
             std::string label, float label_width = 70.f,
             Style style = Style::kField)
      : IControl(bounds, param_id),
        label_(std::move(label)),
        label_width_(label_width),
        style_(style) {}

  void SetLabel(const std::string& label) {
    if (label != label_) {
      label_ = label;
      SetDirty(false);
    }
  }

  // Bind to another parameter and show its current value.
  void Rebind(int param_id) {
    if (param_id == GetParamIdx()) return;
    SetParamIdx(param_id);
    if (const iplug::IParam* p = GetParam()) {
      SetValue(p->GetNormalized());
    }
    SetDirty(false);
  }

  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    const float a = IsDisabled() ? theme::kInactiveOpacity : 1.f;
    auto fade = [a](const ig::IColor& c) {
      return ig::IColor(static_cast<int>(c.A * a), c.R, c.G, c.B);
    };
    const iplug::IParam* p = GetParam();
    WDL_String value;
    if (p) p->GetDisplayWithLabel(value);

    if (style_ == Style::kToggle) {
      const bool on = GetValue() >= 0.5;
      const ig::IColor on_color = label_ == "BYP" ? theme::kWarn : theme::kOn;
      g.FillRoundRect(fade(on ? on_color : theme::kPanelRaised), mRECT, 3.f);
      g.DrawRoundRect(fade(theme::kBorder), mRECT, 3.f);
      g.DrawText(theme::Text(11.f, fade(on ? theme::kBackground : theme::kText),
                             ig::EAlign::Center),
                 label_.c_str(), mRECT);
      return;
    }

    const ig::IRECT label_rect = mRECT.GetFromLeft(label_width_);
    const ig::IRECT box = mRECT.GetReducedFromLeft(label_width_);
    g.DrawText(theme::Text(10.f, fade(theme::kTextDim)), label_.c_str(),
               label_rect);
    g.FillRoundRect(fade(mMouseIsOver && !IsDisabled() ? theme::kPanelRaised
                                                       : theme::kPanel),
                    box, 3.f);
    g.DrawRoundRect(fade(theme::kBorder), box, 3.f);
    const bool is_enum = p && p->NDisplayTexts() > 0;
    if (is_enum) {
      // Menu affordance.
      const float x = box.R - 9.f, y = box.MH();
      g.FillTriangle(fade(theme::kTextDim), x - 3.f, y - 1.5f, x + 3.f,
                     y - 1.5f, x, y + 2.f);
    }
    g.DrawText(theme::Text(11.f, fade(theme::kText)), value.Get(),
               box.GetPadded(-6.f).GetReducedFromRight(is_enum ? 8.f : 0.f));
  }

  void OnMouseDown(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override {
    const iplug::IParam* p = GetParam();
    if (!p) return;
    if (mod.A) {
      SetValueToDefault(GetValIdxForPos(x, y));
      SetDirty(true);
      return;
    }
    if (style_ == Style::kToggle || p->Type() == iplug::IParam::kTypeBool) {
      SetValue(GetValue() >= 0.5 ? 0.0 : 1.0);
      SetDirty(true);
      return;
    }
    if (p->NDisplayTexts() > 0) {
      PromptUserInput(mRECT.GetReducedFromLeft(label_width_));
    }
  }

  void OnMouseDblClick(float x, float y,
                       const iplug::igraphics::IMouseMod& mod) override {
    const iplug::IParam* p = GetParam();
    if (p && p->NDisplayTexts() == 0 && style_ == Style::kField) {
      PromptUserInput(mRECT.GetReducedFromLeft(label_width_));
    }
  }

  void OnMouseDrag(float x, float y, float dx, float dy,
                   const iplug::igraphics::IMouseMod& mod) override {
    if (!IsContinuous()) return;
    const double gearing = mod.S ? 2000.0 : 200.0;
    SetValue(std::clamp(GetValue() - dy / gearing, 0.0, 1.0));
    SetDirty(true);
  }

  void OnMouseWheel(float x, float y, const iplug::igraphics::IMouseMod& mod,
                    float d) override {
    if (!IsContinuous()) return;
    const double step = mod.S ? 0.001 : 0.01;
    SetValue(std::clamp(GetValue() + d * step, 0.0, 1.0));
    SetDirty(true);
  }

 private:
  bool IsContinuous() const {
    const iplug::IParam* p = GetParam();
    return p && style_ == Style::kField && p->NDisplayTexts() == 0 &&
           p->Type() == iplug::IParam::kTypeDouble;
  }

  std::string label_;
  float label_width_;
  Style style_;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_PARAM_FIELD_H_
