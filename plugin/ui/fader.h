#ifndef EVERYTHINGEQ_PLUGIN_UI_FADER_H_
#define EVERYTHINGEQ_PLUGIN_UI_FADER_H_

#include <algorithm>
#include <cstdio>

#include "IControl.h"
#include "plugin/ui/theme.h"

namespace eeq {

// A vertical fader for one parameter (Output Gain, beside the output meter,
// as in Logic's stock EQ): the value on top, the name underneath.  Drag the
// track (Shift = fine), double-click or Option-click for the default.
class Fader final : public iplug::igraphics::IControl {
 public:
  Fader(const iplug::igraphics::IRECT& bounds, int param_id, const char* label)
      : IControl(bounds, param_id), label_(label) {}

  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    const iplug::IParam* p = GetParam();
    if (!p) return;
    // Value readout, like the meters'.
    const ig::IRECT readout = ReadoutRect();
    g.FillRoundRect(ig::IColor(255, 30, 30, 34), readout, 2.f);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%+.1f", p->Value());
    g.DrawText(theme::Text(10.f, theme::kText, ig::EAlign::Center), buf,
               readout);
    g.DrawText(theme::Text(12.f, theme::kTextDim, ig::EAlign::Center), label_,
               mRECT.GetFromBottom(16.f));
    // Groove, default tick and handle.
    const ig::IRECT track = TrackRect();
    g.FillRoundRect(theme::kPanel, track.GetMidHPadded(2.f), 2.f);
    const float zero = YFor(p->ToNormalized(p->GetDefault()));
    g.DrawLine(theme::kGridStrong, track.L, zero, track.R, zero);
    const float y = YFor(p->GetNormalized());
    const ig::IRECT handle(track.L, y - 4.f, track.R, y + 4.f);
    g.FillRoundRect(mMouseIsOver || dragging_ ? theme::kText : theme::kTextDim,
                    handle, 2.f);
    g.DrawLine(theme::kBackground, handle.L + 2.f, y, handle.R - 2.f, y);
  }

  void OnMouseDown(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override {
    if (mod.A) {
      SetValueToDefault();
      return;
    }
    dragging_ = true;
    raw_ = GetParam() ? GetParam()->GetNormalized() : GetValue();
    SetDirty(false);
  }

  void OnMouseDrag(float x, float y, float dx, float dy,
                   const iplug::igraphics::IMouseMod& mod) override {
    // The plugin snaps to detents; accumulate unsnapped so small moves add
    // up.
    raw_ =
        std::clamp(raw_ - dy / TrackRect().H() * (mod.S ? 0.1 : 1.0), 0.0, 1.0);
    SetValue(raw_);
    SetDirty(true);
  }

  void OnMouseUp(float x, float y,
                 const iplug::igraphics::IMouseMod& mod) override {
    dragging_ = false;
    SetDirty(false);
  }

  void OnMouseDblClick(float x, float y,
                       const iplug::igraphics::IMouseMod& mod) override {
    // Outside a mouse-down gesture, so make it one.
    iplug::IEditorDelegate* delegate = GetDelegate();
    delegate->BeginInformHostOfParamChangeFromUI(GetParamIdx());
    SetValueToDefault();
    delegate->EndInformHostOfParamChangeFromUI(GetParamIdx());
  }

 private:
  iplug::igraphics::IRECT ReadoutRect() const {
    return mRECT.GetFromTop(18.f).GetPadded(-1.f);
  }
  iplug::igraphics::IRECT TrackRect() const {
    return mRECT.GetReducedFromTop(26.f).GetReducedFromBottom(20.f).GetHPadded(
        -2.f);
  }
  float YFor(double normalized) const {
    const iplug::igraphics::IRECT t = TrackRect();
    return t.B - static_cast<float>(normalized) * t.H();
  }

  const char* label_;
  double raw_ = 0.0;
  bool dragging_ = false;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_FADER_H_
