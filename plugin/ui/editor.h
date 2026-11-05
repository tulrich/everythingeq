#ifndef EVERYTHINGEQ_PLUGIN_UI_EDITOR_H_
#define EVERYTHINGEQ_PLUGIN_UI_EDITOR_H_

#include <array>
#include <chrono>
#include <string>
#include <vector>

#include "IControls.h"
#include "model/editor_layout.h"
#include "model/spectrum_analyzer.h"
#include "plugin/ui/detector_scope.h"
#include "plugin/ui/editor_model.h"

namespace eeq {

class BandRibbon;
class CardPanel;
class DrawerTab;
class EqGraph;
class Fader;
class GroupHeader;
class LevelMeter;
class MiniLabel;
class Panel;
class ParamField;
class PresetBar;
class RoutingMap;
class SectionHeader;
class StripPanel;
class TextButton;

// Per-frame data from the plugin to the editor.
struct EditorIdleData {
  std::array<float, 6> peaks{};  // In L/R, SC L/R, out L/R (linear).
  bool transport_started = false;
  std::array<bool, 4> in_connected{};
  std::array<bool, 2> out_connected{};
  bool layout_changed = false;
  int latency_samples = 0;
};

// Builds the plugin UI (ABOUT.md section 4) and keeps it in sync.  Owned by
// the plugin while the editor window is open; UI thread only.  All controls
// are owned by IGraphics.  The layout is responsive: Layout() positions the
// controls for the window's size tier (model/editor_layout.h) and hides
// what the tier leaves out.
class Editor {
 public:
  Editor(iplug::igraphics::IGraphics& graphics, EditorModel& model);

  void Idle(const EditorIdleData& data);
  // Lays the controls out for the window's current size.
  void Layout();

  // Band copy/paste (also Command-C / Command-V): copies the edited band,
  // pastes onto the selected bands.  Return whether they did anything.
  bool CopySelectedBand();
  bool PasteToSelection();

  int selected_group() const { return selected_group_; }
  void SelectGroup(int group);
  // Called by the model when the selected band changes.
  void OnBandSelected(int band);

 private:
  // A Dyn A / Dyn B assignment card under the graph.
  struct Card {
    CardPanel* panel = nullptr;
    ParamField* group = nullptr;
    ParamField* target_menu = nullptr;
    ParamField* range_dial = nullptr;   // Full tier, when tall enough.
    ParamField* range_field = nullptr;  // Otherwise.
    ParamField* makeup = nullptr;       // Card A only, placed in either.
    ParamField* makeup_dial = nullptr;
    TextButton* disconnect = nullptr;
    TextButton* connect = nullptr;
  };
  // A foldable section of the selected group's fields.
  struct Section {
    SectionHeader* header = nullptr;
    std::vector<int> fields;  // Indices into group_fields_.
    bool want_open = true;
    int priority = 0;  // Higher stays open longer.
  };

  void BuildTop();
  void BuildGraphArea();
  void BuildStrip();
  void BuildFooter();
  void BuildPanel();
  ParamField* AddBandField(BandField f, const char* label, float label_width,
                           int style);

  void LayoutStrip();
  void LayoutCards(const ParamValues& v, bool force);
  void LayoutPanel(const ParamValues& v, bool force);
  void LayoutFooter();
  void ToggleSection(int section);
  void PasteBands(int section, const std::vector<int>& bands);
  void Connect(int slot);
  void Arm(int band, int slot);

  void UpdateBandEditor(const ParamValues& v);
  void UpdateDynamicsPanel(const ParamValues& v);
  std::string Display(int param_id) const;
  bool SidechainMissing(const ParamValues& v, int group) const;

  iplug::igraphics::IGraphics& graphics_;
  EditorModel& model_;
  int selected_group_ = 0;
  int bound_band_ = -1;
  int shown_band_group_ = -1;
  int bound_group_ = -1;
  bool sc_connected_ = false;

  EditorLayout layout_;
  bool drawer_open_ = false;
  // What the dynamics panel was last laid out for.
  std::string panel_key_;
  std::array<int, 2> card_state_ = {-1, -1};
  // Ports just connected with Range 0 (which alone means unassigned): their
  // cards stay open until a range is set or another band is selected.
  int armed_band_ = -1;
  std::array<bool, 2> armed_{};

  BandRibbon* ribbon_ = nullptr;
  PresetBar* preset_bar_ = nullptr;
  EqGraph* graph_ = nullptr;
  LevelMeter* input_meter_ = nullptr;
  LevelMeter* sidechain_meter_ = nullptr;
  LevelMeter* output_meter_ = nullptr;
  Fader* fader_ = nullptr;
  iplug::igraphics::ITextControl* now_readout_ = nullptr;
  MiniLabel* mini_label_ = nullptr;

  // Band strip.
  StripPanel* strip_panel_ = nullptr;
  ParamField* type_field_ = nullptr;
  ParamField* tap_field_ = nullptr;
  ParamField* chan_field_ = nullptr;
  ParamField* option_field_ = nullptr;
  ParamField* bypass_field_ = nullptr;
  ParamField* solo_field_ = nullptr;
  std::array<ParamField*, 3> dials_{};
  std::array<Card, 2> cards_;
  struct BandFieldControl {
    BandField field;
    ParamField* control;
  };
  std::vector<BandFieldControl> band_fields_;

  // Footer.
  Panel* footer_panel_ = nullptr;
  std::vector<iplug::igraphics::IControl*> footer_row1_;
  std::vector<iplug::igraphics::IControl*> footer_row2_;
  std::vector<float> footer_row2_widths_;
  iplug::igraphics::ITextControl* status_ = nullptr;
  std::string status_text_;

  // Dynamics panel.
  DrawerTab* drawer_tab_ = nullptr;
  Panel* panel_bg_ = nullptr;
  iplug::igraphics::ITextControl* routing_title_ = nullptr;
  RoutingMap* routing_ = nullptr;
  iplug::igraphics::ITextControl* legend_ = nullptr;
  GroupHeader* group_header_ = nullptr;
  iplug::igraphics::ITextControl* warning_ = nullptr;
  DetectorScope* scope_ = nullptr;
  std::array<Section, 3> sections_;
  int section_clock_ = 3;
  struct GroupFieldControl {
    GroupField field;
    ParamField* control;
  };
  std::vector<GroupFieldControl> group_fields_;

  DetectorHistory history_;
  SpectrumAnalyzer pre_analyzer_;
  SpectrumAnalyzer post_analyzer_;
  int analyzer_mode_ = 3;  // EqGraph::AnalyzerMode::kPreAndPost.
  double tilt_db_per_octave_ = 0.0;
  std::vector<float> samples_ = std::vector<float>(4096);
  std::vector<float> spectrum_pre_, spectrum_post_;
  std::chrono::steady_clock::time_point last_idle_ =
      std::chrono::steady_clock::now();
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_EDITOR_H_
