#ifndef EVERYTHINGEQ_PLUGIN_UI_CHOICE_FIELD_H_
#define EVERYTHINGEQ_PLUGIN_UI_CHOICE_FIELD_H_

#include <functional>
#include <string>
#include <vector>

#include "IControl.h"
#include "plugin/ui/theme.h"

namespace eeq {

// A "LABEL [ value v ]" menu for UI-only settings that are not host
// parameters (e.g. analyzer mode).  Looks like ParamField.
class ChoiceField final : public iplug::igraphics::IControl {
 public:
  ChoiceField(const iplug::igraphics::IRECT& bounds, std::string label,
              float label_width, std::vector<std::string> options, int initial,
              std::function<void(int)> on_change)
      : IControl(bounds),
        label_(std::move(label)),
        label_width_(label_width),
        options_(std::move(options)),
        index_(initial),
        on_change_(std::move(on_change)),
        menu_("") {}

  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    const ig::IRECT box = mRECT.GetReducedFromLeft(label_width_);
    if (label_width_ > 0.f) {
      g.DrawText(theme::Text(10.f, theme::kTextDim), label_.c_str(),
                 mRECT.GetFromLeft(label_width_));
    }
    g.FillRoundRect(mMouseIsOver ? theme::kPanelRaised : theme::kPanel, box,
                    3.f);
    g.DrawRoundRect(theme::kBorder, box, 3.f);
    const float x = box.R - 9.f, y = box.MH();
    g.FillTriangle(theme::kTextDim, x - 3.f, y - 1.5f, x + 3.f, y - 1.5f, x,
                   y + 2.f);
    g.DrawText(theme::Text(11.f), options_[index_].c_str(),
               box.GetPadded(-6.f).GetReducedFromRight(8.f));
  }

  void OnMouseDown(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override {
    menu_.Clear();
    for (size_t i = 0; i < options_.size(); ++i) {
      menu_.AddItem(options_[i].c_str(), -1,
                    static_cast<int>(i) == index_
                        ? iplug::igraphics::IPopupMenu::Item::kChecked
                        : iplug::igraphics::IPopupMenu::Item::kNoFlags);
    }
    GetUI()->CreatePopupMenu(*this, menu_,
                             mRECT.GetReducedFromLeft(label_width_));
  }

  void OnPopupMenuSelection(iplug::igraphics::IPopupMenu* menu,
                            int val_idx) override {
    if (!menu) return;
    const int chosen = menu->GetChosenItemIdx();
    if (chosen < 0 || chosen >= static_cast<int>(options_.size())) return;
    index_ = chosen;
    on_change_(index_);
    SetDirty(false);
  }

 private:
  std::string label_;
  float label_width_;
  std::vector<std::string> options_;
  int index_;
  std::function<void(int)> on_change_;
  iplug::igraphics::IPopupMenu menu_;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_CHOICE_FIELD_H_
