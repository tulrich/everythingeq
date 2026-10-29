#ifndef EVERYTHINGEQ_PLUGIN_UI_PRESET_BAR_H_
#define EVERYTHINGEQ_PLUGIN_UI_PRESET_BAR_H_

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "IControl.h"
#include "plugin/ui/editor_model.h"
#include "plugin/ui/theme.h"

namespace eeq {

// The preset bar (ABOUT.md section 4): the current preset's name (with "*" once
// the settings differ from it) opening the preset menu, and Undo/Redo.  The
// menu lists the factory and user presets and has Save, Save As, Rename and
// Show Presets Folder; names are typed in place.
class PresetBar final : public iplug::igraphics::IControl {
 public:
  PresetBar(const iplug::igraphics::IRECT& bounds, EditorModel& model)
      : IControl(bounds), model_(model), menu_("") {
    SetTextEntryLength(64);
    SetTooltip(
        "Presets: factory presets and your own (saved as files, so they work "
        "in any host). * means the settings have changed since the preset was "
        "loaded or saved.");
  }

  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    // Name and menu affordance.
    const ig::IRECT box = NameRect();
    DrawBox(g, box, hover_ == kName);
    const float x = box.R - 9.f, y = box.MH();
    g.FillTriangle(theme::kTextDim, x - 3.f, y - 1.5f, x + 3.f, y - 1.5f, x,
                   y + 2.f);
    const PresetRef ref = model_.CurrentPreset();
    const std::string name = ref.name.empty() ? "No Preset" : ref.name;
    g.DrawText(
        theme::Text(11.f, ref.name.empty() ? theme::kTextDim : theme::kText),
        (name + (modified_ ? " *" : "")).c_str(),
        box.GetPadded(-6.f).GetReducedFromRight(8.f));
    // Undo / Redo.
    for (const bool redo : {false, true}) {
      const ig::IRECT r = redo ? RedoRect() : UndoRect();
      const bool enabled = redo ? can_redo_ : can_undo_;
      DrawBox(g, r, enabled && hover_ == (redo ? kRedo : kUndo));
      g.DrawText(
          theme::Text(
              10.f, enabled ? theme::kText : theme::kTextDim.WithOpacity(0.5f),
              ig::EAlign::Center),
          redo ? "REDO" : "UNDO", r);
    }
  }

  bool IsDirty() override {
    // Redraw only when something shown changes.
    const PresetRef ref = model_.CurrentPreset();
    const bool modified = model_.PresetModified();
    const bool can_undo = model_.CanUndo(), can_redo = model_.CanRedo();
    if (!(ref == shown_) || modified != modified_ || can_undo != can_undo_ ||
        can_redo != can_redo_) {
      shown_ = ref;
      modified_ = modified;
      can_undo_ = can_undo;
      can_redo_ = can_redo;
      SetDirty(false);
    }
    return IControl::IsDirty();
  }

  void OnMouseOver(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override {
    const Part part = PartAt(x, y);
    if (part != hover_) {
      hover_ = part;
      SetDirty(false);
    }
  }

  void OnMouseOut() override {
    hover_ = kNone;
    SetDirty(false);
  }

  void OnMouseDown(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override {
    switch (PartAt(x, y)) {
      case kName:
        OpenMenu();
        break;
      case kUndo:
        model_.Undo();
        break;
      case kRedo:
        model_.Redo();
        break;
      case kNone:
        break;
    }
    SetDirty(false);
  }

  void OnPopupMenuSelection(iplug::igraphics::IPopupMenu* menu,
                            int val_idx) override {
    if (!menu) return;
    const int chosen = menu->GetChosenItemIdx();
    if (chosen < 0 || chosen >= static_cast<int>(actions_.size())) return;
    const int action = actions_[chosen];
    if (action >= 0) {
      Load(action);
    } else if (action == kSave) {
      Save(model_.CurrentPreset().name);
    } else if (action == kSaveAs) {
      const PresetRef ref = model_.CurrentPreset();
      PromptName(kSaveAs, ref.name.empty() ? "My Preset" : ref.name);
    } else if (action == kRename) {
      PromptName(kRename, model_.CurrentPreset().name);
    } else if (action == kShowFolder) {
      ShowFolder();
    }
    SetDirty(false);
  }

  void OnTextEntryCompletion(const char* str, int val_idx) override {
    const std::string name = CleanPresetName(str);
    if (name.empty()) return;
    if (entry_ == kRename) {
      std::string error;
      if (!model_.RenamePreset(name, &error)) ShowError(error);
    } else if (model_.Presets().Find({name, true}) >= 0 &&
               !(model_.CurrentPreset() == PresetRef{name, true})) {
      GetUI()->ShowMessageBox(
          ("Replace the user preset \"" + name + "\"?").c_str(),
          "Everything EQ", iplug::igraphics::kMB_YESNO,
          [this, name](iplug::igraphics::EMsgBoxResult result) {
            if (result == iplug::igraphics::kYES) Save(name);
          });
    } else {
      Save(name);
    }
    SetDirty(false);
  }

 private:
  enum Part { kNone, kName, kUndo, kRedo };
  // Menu actions other than loading preset i (>= 0).
  static constexpr int kNoAction = -1;
  static constexpr int kSave = -2;
  static constexpr int kSaveAs = -3;
  static constexpr int kRename = -4;
  static constexpr int kShowFolder = -5;

  static constexpr float kButtonWidth = 38.f;
  static constexpr float kGap = 3.f;

  iplug::igraphics::IRECT RedoRect() const {
    return mRECT.GetFromRight(kButtonWidth);
  }
  iplug::igraphics::IRECT UndoRect() const {
    return RedoRect().GetTranslated(-(kButtonWidth + kGap), 0.f);
  }
  iplug::igraphics::IRECT NameRect() const {
    return iplug::igraphics::IRECT(mRECT.L, mRECT.T, UndoRect().L - 2.f * kGap,
                                   mRECT.B);
  }

  Part PartAt(float x, float y) const {
    if (NameRect().Contains(x, y)) return kName;
    if (UndoRect().Contains(x, y)) return kUndo;
    if (RedoRect().Contains(x, y)) return kRedo;
    return kNone;
  }

  static void DrawBox(iplug::igraphics::IGraphics& g,
                      const iplug::igraphics::IRECT& r, bool hover) {
    g.FillRoundRect(hover ? theme::kPanelRaised : theme::kPanel, r, 3.f);
    g.DrawRoundRect(theme::kBorder, r, 3.f);
  }

  void Load(int index) {
    std::string error;
    if (!model_.LoadPreset(index, &error)) ShowError(error);
  }

  void Save(const std::string& name) {
    std::string error;
    if (!model_.SavePreset(name, &error)) ShowError(error);
  }

  void OpenMenu() {
    namespace ig = iplug::igraphics;
    model_.RefreshPresets();
    const PresetLibrary& lib = model_.Presets();
    const PresetRef current = model_.CurrentPreset();
    const bool is_user = current.user && lib.Find(current) >= 0;
    menu_.Clear();
    actions_.clear();
    auto add = [&](const std::string& text, int action, int flags) {
      menu_.AddItem(text.c_str(), -1, flags);
      actions_.push_back(action);
    };
    auto separator = [&] {
      menu_.AddSeparator();
      actions_.push_back(kNoAction);
    };
    auto preset_flags = [&](int i) {
      return lib.Ref(i) == current ? ig::IPopupMenu::Item::kChecked
                                   : ig::IPopupMenu::Item::kNoFlags;
    };
    add("Factory", kNoAction, ig::IPopupMenu::Item::kTitle);
    for (int i = 0; i < lib.num_factory(); ++i) {
      add(lib.entries()[i].name, i, preset_flags(i));
    }
    separator();
    add("User", kNoAction, ig::IPopupMenu::Item::kTitle);
    const int n = static_cast<int>(lib.entries().size());
    if (n == lib.num_factory()) {
      add("(none yet: use Save As)", kNoAction,
          ig::IPopupMenu::Item::kDisabled);
    }
    for (int i = lib.num_factory(); i < n; ++i) {
      add(lib.entries()[i].name, i, preset_flags(i));
    }
    separator();
    add(is_user ? "Save \"" + current.name + "\"" : "Save", kSave,
        is_user ? ig::IPopupMenu::Item::kNoFlags
                : ig::IPopupMenu::Item::kDisabled);
    add("Save As...", kSaveAs, ig::IPopupMenu::Item::kNoFlags);
    add("Rename...", kRename,
        is_user ? ig::IPopupMenu::Item::kNoFlags
                : ig::IPopupMenu::Item::kDisabled);
    add("Show Presets Folder", kShowFolder, ig::IPopupMenu::Item::kNoFlags);
    GetUI()->CreatePopupMenu(*this, menu_, NameRect());
  }

  void PromptName(int mode, const std::string& initial) {
    entry_ = mode;
    GetUI()->CreateTextEntry(*this, theme::Text(11.f), NameRect(),
                             initial.c_str());
  }

  void ShowFolder() {
    const std::filesystem::path& dir = model_.Presets().user_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    WDL_String path(dir.string().c_str());
    GetUI()->RevealPathInExplorerOrFinder(path, false);
  }

  void ShowError(const std::string& error) {
    GetUI()->ShowMessageBox(error.c_str(), "Everything EQ",
                            iplug::igraphics::kMB_OK);
  }

  EditorModel& model_;
  iplug::igraphics::IPopupMenu menu_;
  std::vector<int> actions_;  // Per menu item.
  int entry_ = kSaveAs;       // What the text entry is for.
  Part hover_ = kNone;
  PresetRef shown_;
  bool modified_ = false;
  bool can_undo_ = false;
  bool can_redo_ = false;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_PRESET_BAR_H_
