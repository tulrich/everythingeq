#ifndef EVERYTHINGEQ_PLUGIN_UI_WIDGETS_H_
#define EVERYTHINGEQ_PLUGIN_UI_WIDGETS_H_

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>

#include "Controls/ICornerResizerControl.h"
#include "IControl.h"
#include "plugin/ui/editor_model.h"
#include "plugin/ui/theme.h"

namespace eeq {

// Small controls for the editor layout (ABOUT.md section 4).

// A panel background with an optional title.  A solid panel (the dynamics
// drawer) is opaque and swallows clicks so they don't reach what's under it.
class Panel final : public iplug::igraphics::IControl {
 public:
  explicit Panel(const iplug::igraphics::IRECT& bounds, const char* title = "")
      : IControl(bounds), title_(title) {
    mIgnoreMouse = true;
  }
  void SetSolid(bool solid) {
    solid_ = solid;
    mIgnoreMouse = !solid;
  }
  void Draw(iplug::igraphics::IGraphics& g) override {
    if (solid_) {
      g.FillRoundRect(iplug::igraphics::IColor(90, 0, 0, 0),
                      mRECT.GetTranslated(0.f, 4.f), 5.f);
      g.FillRoundRect(theme::kPanel, mRECT, 4.f);
      g.DrawRoundRect(theme::kBorder, mRECT, 4.f);
    } else {
      g.FillRoundRect(theme::kPanel.WithOpacity(0.6f), mRECT, 4.f);
    }
    if (*title_) {
      g.DrawText(theme::Text(10.f, theme::kTextDim), title_,
                 mRECT.GetFromTop(16.f).GetHPadded(-6.f));
    }
  }
  void OnMouseDown(float, float, const iplug::igraphics::IMouseMod&) override {}

 private:
  const char* title_;
  bool solid_ = false;
};

// A clickable text button.
class TextButton final : public iplug::igraphics::IControl {
 public:
  enum class Style { kBox, kOutline, kGhost };

  TextButton(const iplug::igraphics::IRECT& bounds, std::string label,
             std::function<void()> on_click, Style style = Style::kBox)
      : IControl(bounds),
        label_(std::move(label)),
        on_click_(std::move(on_click)),
        style_(style) {}

  void SetLabel(const std::string& label) {
    if (label != label_) {
      label_ = label;
      SetDirty(false);
    }
  }

  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    const bool hover = mMouseIsOver && !IsDisabled();
    switch (style_) {
      case Style::kBox:
        g.FillRoundRect(hover ? theme::kPanelRaised : theme::kPanel, mRECT,
                        3.f);
        g.DrawRoundRect(theme::kBorder, mRECT, 3.f);
        break;
      case Style::kOutline:
        if (hover) g.FillRoundRect(theme::kPanelRaised, mRECT, 3.f);
        g.DrawRoundRect(theme::kBorder, mRECT, 3.f);
        break;
      case Style::kGhost:
        if (hover) g.FillRoundRect(theme::kPanelRaised, mRECT, 3.f);
        break;
    }
    g.DrawText(
        theme::Text(style_ == Style::kGhost ? 12.f : 10.f,
                    hover ? theme::kText : theme::kTextDim, ig::EAlign::Center),
        label_.c_str(), mRECT);
  }
  void OnMouseDown(float, float, const iplug::igraphics::IMouseMod&) override {
    if (on_click_) on_click_();
  }
  void OnMouseOver(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override {
    IControl::OnMouseOver(x, y, mod);
    SetDirty(false);
  }
  void OnMouseOut() override {
    IControl::OnMouseOut();
    SetDirty(false);
  }

 private:
  std::string label_;
  std::function<void()> on_click_;
  Style style_;
};

// A foldable section title in the dynamics panel: click to fold or unfold.
// Folded, it shows a one-line summary instead of its fields.
class SectionHeader final : public iplug::igraphics::IControl {
 public:
  SectionHeader(const iplug::igraphics::IRECT& bounds, const char* title,
                std::function<void()> on_toggle)
      : IControl(bounds), title_(title), on_toggle_(std::move(on_toggle)) {}

  bool open() const { return open_; }
  void SetOpen(bool open) {
    if (open != open_) {
      open_ = open;
      SetDirty(false);
    }
  }
  void SetSummary(const std::string& summary) {
    if (summary != summary_) {
      summary_ = summary;
      SetDirty(false);
    }
  }

  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    const float x = mRECT.L + 3.f, y = mRECT.MH() - 1.f;
    if (open_) {
      g.FillTriangle(theme::kAccent, x - 3.f, y - 1.5f, x + 3.f, y - 1.5f, x,
                     y + 2.5f);
    } else {
      g.FillTriangle(theme::kAccent, x - 1.5f, y - 3.f, x - 1.5f, y + 3.f,
                     x + 2.5f, y);
    }
    ig::IRECT text = mRECT.GetReducedFromLeft(10.f);
    g.DrawText(theme::Text(9.f, theme::kAccent), title_, text);
    if (!open_ && !summary_.empty()) {
      g.DrawText(theme::Text(9.f, theme::kTextDim, ig::EAlign::Far),
                 summary_.c_str(), text);
    }
    g.DrawLine(theme::kBorder, mRECT.L, mRECT.B - 0.5f, mRECT.R,
               mRECT.B - 0.5f);
  }
  void OnMouseDown(float, float, const iplug::igraphics::IMouseMod&) override {
    if (on_toggle_) on_toggle_();
  }

 private:
  const char* title_;
  std::function<void()> on_toggle_;
  bool open_ = true;
  std::string summary_;
};

// The background of a Dyn A / Dyn B assignment card: its title, the
// current offset ("now -3.2 dB") and a highlight while its group is
// hovered.
class CardPanel final : public iplug::igraphics::IControl {
 public:
  CardPanel(const iplug::igraphics::IRECT& bounds, const char* title)
      : IControl(bounds), title_(title) {
    mIgnoreMouse = true;
  }
  void SetNow(const std::string& now) {
    if (now != now_) {
      now_ = now;
      SetDirty(false);
    }
  }
  void SetHighlight(bool on) {
    if (on != highlight_) {
      highlight_ = on;
      SetDirty(false);
    }
  }
  // Room kept at the right of the title row (for the disconnect button).
  void SetTitleInsetRight(float inset) { inset_ = inset; }

  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    g.FillRoundRect(theme::kPanel.WithOpacity(0.6f), mRECT, 4.f);
    g.DrawRoundRect(highlight_ ? theme::kText : theme::kBorder, mRECT, 4.f);
    const ig::IRECT row =
        mRECT.GetPadded(-7.f).GetFromTop(13.f).GetReducedFromRight(inset_);
    g.DrawText(theme::Text(9.f, theme::kTextDim), title_, row);
    g.DrawText(theme::Text(10.f, theme::kTextDim, ig::EAlign::Far),
               now_.c_str(), row);
  }

 private:
  const char* title_;
  std::string now_;
  bool highlight_ = false;
  float inset_ = 0.f;
};

// The band strip's left panel: background plus "● Band n" over the info
// column.
class StripPanel final : public iplug::igraphics::IControl {
 public:
  StripPanel(const iplug::igraphics::IRECT& bounds, EditorModel& model)
      : IControl(bounds), model_(model) {
    mIgnoreMouse = true;
  }
  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    g.FillRoundRect(theme::kPanel.WithOpacity(0.6f), mRECT, 4.f);
    const int band = std::max(0, model_.SelectedBand());
    const ig::IRECT row = mRECT.GetPadded(-8.f).GetFromTop(14.f);
    g.FillCircle(theme::BandColor(band), row.L + 5.f, row.MH(), 4.5f);
    const std::string label = "Band " + std::to_string(band + 1);
    g.DrawText(theme::Text(12.f), label.c_str(), row.GetReducedFromLeft(14.f));
  }
  bool IsDirty() override {
    const int band = model_.SelectedBand();
    if (band != shown_) {
      shown_ = band;
      return true;
    }
    return IControl::IsDirty();
  }

 private:
  EditorModel& model_;
  int shown_ = -2;
};

// The selected group's title row in the dynamics panel: shape, name,
// "Detect · Input" and its live D.
class GroupHeader final : public iplug::igraphics::IControl {
 public:
  GroupHeader(const iplug::igraphics::IRECT& bounds, EditorModel& model,
              const int& group)
      : IControl(bounds), model_(model), group_(group) {
    mIgnoreMouse = true;
  }
  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    const ParamValues v = model_.Values();
    const auto label = [&](GroupField f) {
      const int id = GroupParamId(group_, f);
      const ParamSpec& spec = ParamTable()[id];
      const int i = static_cast<int>(v[id]);
      return i >= 0 && i < static_cast<int>(spec.enum_labels.size())
                 ? spec.enum_labels[i]
                 : std::string();
    };
    ig::IRECT r = mRECT;
    theme::FillGroupGlyph(g, group_, r.L + 6.f, r.MH(), 5.f, theme::kText);
    r.ReduceFromLeft(16.f);
    const std::string name = "G" + std::to_string(group_ + 1);
    g.DrawText(theme::Text(12.f), name.c_str(), r.ReduceFromLeft(22.f));
    char d[24];
    std::snprintf(
        d, sizeof(d), "D %d%%",
        static_cast<int>(model_.LiveControls()[group_] * 100.0 + 0.5));
    g.DrawText(theme::Text(10.f, theme::kAccent, ig::EAlign::Far), d, r);
    const std::string summary =
        label(GroupField::kDetect) + " \xc2\xb7 " + label(GroupField::kInput);
    g.DrawText(theme::Text(10.f, theme::kTextDim), summary.c_str(),
               r.GetReducedFromRight(44.f));
  }
  bool IsDirty() override { return true; }  // Live D.

 private:
  EditorModel& model_;
  const int& group_;
};

// The compact tier's drawer handle: each group's shape and live D, and
// "DYNAMICS"; click to open or close the dynamics drawer.
class DrawerTab final : public iplug::igraphics::IControl {
 public:
  DrawerTab(const iplug::igraphics::IRECT& bounds, EditorModel& model,
            std::function<bool(int)> group_active,
            std::function<void()> on_click)
      : IControl(bounds),
        model_(model),
        group_active_(std::move(group_active)),
        on_click_(std::move(on_click)) {
    SetTooltip("Dynamics: click to open or close the routing and groups.");
  }
  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    g.FillRoundRect(mMouseIsOver ? theme::kPanelRaised : theme::kPanel, mRECT,
                    4.f);
    g.DrawRoundRect(theme::kBorder, mRECT, 4.f);
    const GroupControls d = model_.LiveControls();
    float y = mRECT.MH() - 70.f;
    for (int i = 0; i < kNumGroups; ++i) {
      const float a = group_active_ && group_active_(i) ? 1.f : 0.4f;
      theme::FillGroupGlyph(g, i, mRECT.MW(), y + 4.f, 4.f,
                            theme::kText.WithOpacity(a));
      const ig::IRECT bar(mRECT.MW() - 1.5f, y + 11.f, mRECT.MW() + 1.5f,
                          y + 29.f);
      g.FillRoundRect(theme::kPanelRaised, bar, 1.f);
      g.FillRect(theme::kAccent.WithOpacity(a),
                 bar.GetFromBottom(
                     static_cast<float>(std::clamp(d[i], 0.0, 1.0) * bar.H())));
      y += 36.f;
    }
    ig::IText text = theme::Text(9.f, theme::kTextDim, ig::EAlign::Center);
    text.mAngle = -90.f;
    g.DrawText(text, "DYNAMICS",
               ig::IRECT(mRECT.L, y + 6.f, mRECT.R, y + 66.f));
  }
  void OnMouseDown(float, float, const iplug::igraphics::IMouseMod&) override {
    if (on_click_) on_click_();
  }
  bool IsDirty() override { return true; }  // Live D.

 private:
  EditorModel& model_;
  std::function<bool(int)> group_active_;
  std::function<void()> on_click_;
};

// The window's resize grip: diagonal ridges in the bottom-right corner.
class GripResizer final : public iplug::igraphics::ICornerResizerControl {
 public:
  GripResizer(const iplug::igraphics::IRECT& graphics_bounds, float size)
      : ICornerResizerControl(graphics_bounds, size) {
    SetTooltip("Drag to resize; double-click for the default size.");
  }
  void Draw(iplug::igraphics::IGraphics& g) override {
    const iplug::igraphics::IColor c =
        GetMouseIsOver() ? theme::kText : theme::kTextDim;
    const float r = mRECT.R - 3.f, b = mRECT.B - 3.f;
    for (const float k : {4.f, 8.f, 12.f}) {
      g.DrawLine(c, r - k, b, r, b - k, nullptr, 1.5f);
    }
  }
};

// The selected band's name and settings over the graph's corner, for the
// small tiers that hide the band strip.
class MiniLabel final : public iplug::igraphics::IControl {
 public:
  explicit MiniLabel(const iplug::igraphics::IRECT& bounds) : IControl(bounds) {
    mIgnoreMouse = true;
  }
  void Set(const iplug::igraphics::IColor& color, const std::string& text) {
    if (text != text_ || color_ != color) {
      text_ = text;
      color_ = color;
      SetDirty(false);
    }
  }
  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    if (text_.empty()) return;
    float w = 0.f;
    {
      ig::IRECT measured;
      g.MeasureText(theme::Text(10.f), text_.c_str(), measured);
      w = measured.W();
    }
    const ig::IRECT pill(mRECT.L, mRECT.T,
                         std::min(mRECT.R, mRECT.L + w + 26.f), mRECT.B);
    g.FillRoundRect(theme::kPanel.WithOpacity(0.85f), pill, 3.f);
    g.FillCircle(color_, pill.L + 9.f, pill.MH(), 4.f);
    g.DrawText(theme::Text(10.f), text_.c_str(), pill.GetReducedFromLeft(18.f));
  }

 private:
  std::string text_;
  iplug::igraphics::IColor color_;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_WIDGETS_H_
