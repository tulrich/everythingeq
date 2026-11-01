#ifndef EVERYTHINGEQ_PLUGIN_UI_PARAM_FIELD_H_
#define EVERYTHINGEQ_PLUGIN_UI_PARAM_FIELD_H_

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "IControl.h"
#include "plugin/ui/theme.h"

namespace eeq {

// A compact "LABEL [ value ]" control bound to one parameter (ABOUT.md
// section 4).  Continuous parameters: drag vertically (Shift = fine) or use
// the wheel; double-click to type a value.  Enums: click for a menu.  Bools:
// click to toggle (drawn as a button).  Integer parameters whose choices
// depend on context get a menu from SetMenuLabels().  Option-click resets to
// the default.
// The dial style draws a continuous parameter as a labeled knob (same
// gestures); the segments style shows an enum's choices side by side.
// Disabled fields are drawn at the inactive opacity and ignore the mouse.
//
// The bound parameter can change (e.g. when another band is selected), see
// Rebind().
class ParamField final : public iplug::igraphics::IControl {
 public:
  enum class Style { kField, kToggle, kDial, kSegments };

  ParamField(const iplug::igraphics::IRECT& bounds, int param_id,
             std::string label, float label_width = 70.f,
             Style style = Style::kField)
      : IControl(bounds, param_id),
        label_(std::move(label)),
        label_width_(label_width),
        style_(style),
        menu_("") {
    // iPlug2 controls have menus and text entry disabled by default.
    DisablePrompt(false);
  }

  // Choices for an integer parameter, shown as a menu (index = value).
  void SetMenuLabels(std::vector<std::string> labels) {
    if (labels != menu_labels_) {
      menu_labels_ = std::move(labels);
      SetDirty(false);
    }
  }

  // A continuous parameter with this many detents across its range moves
  // one detent per wheel notch and a few pixels per detent when dragged
  // (0: the default speeds).
  void SetDetents(int count) { detents_ = count; }

  // Accent color of the dial arc and selected segment.
  void SetColor(const iplug::igraphics::IColor& color) {
    if (color_ != color) {
      color_ = color;
      SetDirty(false);
    }
  }

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

    if (style_ == Style::kDial) return DrawDial(g, fade, value.Get());
    if (style_ == Style::kSegments) return DrawSegments(g, fade);
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
    const bool is_enum = HasMenu();
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
    if (style_ == Style::kSegments) {
      const int n = p->NDisplayTexts();
      const iplug::igraphics::IRECT box =
          mRECT.GetReducedFromLeft(label_width_);
      if (n <= 0 || box.W() <= 0.f) return;
      const int i =
          std::clamp(static_cast<int>((x - box.L) / box.W() * n), 0, n - 1);
      SetValue(p->ToNormalized(i));
      SetDirty(true);
      return;
    }
    if (style_ == Style::kToggle || p->Type() == iplug::IParam::kTypeBool) {
      SetValue(GetValue() >= 0.5 ? 0.0 : 1.0);
      SetDirty(true);
      return;
    }
    if (!menu_labels_.empty()) {
      const int current = static_cast<int>(std::lround(p->Value()));
      menu_.Clear();
      for (size_t i = 0; i < menu_labels_.size(); ++i) {
        menu_.AddItem(menu_labels_[i].c_str(), -1,
                      static_cast<int>(i) == current
                          ? iplug::igraphics::IPopupMenu::Item::kChecked
                          : iplug::igraphics::IPopupMenu::Item::kNoFlags);
      }
      GetUI()->CreatePopupMenu(*this, menu_,
                               mRECT.GetReducedFromLeft(label_width_));
      return;
    }
    if (p->NDisplayTexts() > 0) {
      PromptUserInput(EntryRect());
    }
  }

  void OnPopupMenuSelection(iplug::igraphics::IPopupMenu* menu,
                            int val_idx) override {
    if (menu_labels_.empty()) {
      IControl::OnPopupMenuSelection(menu, val_idx);
      return;
    }
    const iplug::IParam* p = GetParam();
    if (!menu || !p) return;
    const int chosen = menu->GetChosenItemIdx();
    if (chosen < 0) return;
    SetValue(p->ToNormalized(chosen));
    SetDirty(true);
  }

  void OnMouseDblClick(float x, float y,
                       const iplug::igraphics::IMouseMod& mod) override {
    const iplug::IParam* p = GetParam();
    if (p && !HasMenu() &&
        (style_ == Style::kField || style_ == Style::kDial)) {
      PromptUserInput(EntryRect());
    }
  }

  void OnMouseDrag(float x, float y, float dx, float dy,
                   const iplug::igraphics::IMouseMod& mod) override {
    if (!IsContinuous()) return;
    // Pixels for the full range.
    const double range_px =
        detents_ > 0 ? std::clamp(detents_ * kPixelsPerDetent, 200.0, 640.0)
                     : 200.0;
    Nudge(-dy / (mod.S ? 10.0 * range_px : range_px));
  }

  void OnMouseWheel(float x, float y, const iplug::igraphics::IMouseMod& mod,
                    float d) override {
    if (!IsContinuous()) return;
    const double step = detents_ > 0 ? 1.0 / detents_ : mod.S ? 0.001 : 0.01;
    Nudge(d * step);
  }

 private:
  iplug::igraphics::IRECT EntryRect() const {
    return style_ == Style::kDial ? mRECT.GetFromBottom(14.f)
                                  : mRECT.GetReducedFromLeft(label_width_);
  }

  template <typename Fade>
  void DrawDial(iplug::igraphics::IGraphics& g, const Fade& fade,
                const char* value) {
    namespace ig = iplug::igraphics;
    const iplug::IParam* p = GetParam();
    g.DrawText(theme::Text(9.f, fade(theme::kTextDim), ig::EAlign::Center),
               label_.c_str(), mRECT.GetFromTop(11.f));
    g.DrawText(theme::Text(10.f, fade(theme::kText), ig::EAlign::Center), value,
               mRECT.GetFromBottom(13.f));
    const ig::IRECT knob =
        mRECT.GetReducedFromTop(12.f).GetReducedFromBottom(14.f);
    const float r = std::max(4.f, std::min(knob.W(), knob.H()) / 2.f - 2.f);
    const float cx = knob.MW(), cy = knob.MH();
    constexpr float kA0 = -135.f, kSweep = 270.f;
    const float v = static_cast<float>(GetValue());
    const float d = p ? static_cast<float>(p->GetDefault(true)) : 0.f;
    const float a = kA0 + kSweep * v, a_def = kA0 + kSweep * d;
    g.DrawArc(fade(theme::kPanelRaised), cx, cy, r, kA0, kA0 + kSweep, nullptr,
              3.f);
    if (std::abs(a - a_def) > 0.5f) {
      g.DrawArc(fade(color_), cx, cy, r, std::min(a, a_def), std::max(a, a_def),
                nullptr, 3.f);
    }
    g.FillCircle(fade(mMouseIsOver && !IsDisabled() ? theme::kPanelRaised
                                                    : theme::kPanel),
                 cx, cy, r - 4.f);
    const ig::IVec2 p0 = theme::Polar(cx, cy, (r - 4.f) * 0.3f, a);
    const ig::IVec2 p1 = theme::Polar(cx, cy, r - 5.f, a);
    g.DrawLine(fade(theme::kText), p0.x, p0.y, p1.x, p1.y, nullptr, 2.f);
  }

  template <typename Fade>
  void DrawSegments(iplug::igraphics::IGraphics& g, const Fade& fade) {
    namespace ig = iplug::igraphics;
    const iplug::IParam* p = GetParam();
    if (!p) return;
    if (label_width_ > 0.f) {
      g.DrawText(theme::Text(10.f, fade(theme::kTextDim)), label_.c_str(),
                 mRECT.GetFromLeft(label_width_));
    }
    const ig::IRECT box = mRECT.GetReducedFromLeft(label_width_);
    const int n = p->NDisplayTexts();
    const int current = static_cast<int>(std::lround(p->Value()));
    for (int i = 0; i < n; ++i) {
      const ig::IRECT seg = box.SubRectHorizontal(n, i).GetHPadded(-1.f);
      const bool on = i == current;
      g.FillRoundRect(fade(on ? color_ : theme::kPanel), seg, 3.f);
      if (!on) g.DrawRoundRect(fade(theme::kBorder), seg, 3.f);
      g.DrawText(
          theme::Text(10.f, fade(on ? theme::kBackground : theme::kTextDim),
                      ig::EAlign::Center),
          p->GetDisplayTextAtIdx(i), seg);
    }
  }

  // Moves the value by `delta` (normalized) from where the last nudge left
  // it, so that small moves add up across the parameter's detents (the
  // plugin snaps what it receives).
  void Nudge(double delta) {
    const iplug::IParam* p = GetParam();
    if (!p) return;
    const double from =
        p->GetNormalized() == nudged_to_ ? nudge_raw_ : p->GetNormalized();
    nudge_raw_ = std::clamp(from + delta, 0.0, 1.0);
    SetValue(nudge_raw_);
    SetDirty(true);
    nudged_to_ = p->GetNormalized();
  }

  bool HasMenu() const {
    const iplug::IParam* p = GetParam();
    return !menu_labels_.empty() || (p && p->NDisplayTexts() > 0);
  }

  bool IsContinuous() const {
    const iplug::IParam* p = GetParam();
    return p && (style_ == Style::kField || style_ == Style::kDial) &&
           p->NDisplayTexts() == 0 && p->Type() == iplug::IParam::kTypeDouble;
  }

  std::string label_;
  iplug::igraphics::IColor color_ = theme::kAccent;
  float label_width_;
  Style style_;
  std::vector<std::string> menu_labels_;
  iplug::igraphics::IPopupMenu menu_;
  static constexpr double kPixelsPerDetent = 4.0;

  int detents_ = 0;
  double nudge_raw_ = 0.0;   // Unsnapped value of the last nudge,
  double nudged_to_ = -1.0;  // and the parameter value it produced.
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_PARAM_FIELD_H_
