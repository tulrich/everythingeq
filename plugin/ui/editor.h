#ifndef EVERYTHINGEQ_PLUGIN_UI_EDITOR_H_
#define EVERYTHINGEQ_PLUGIN_UI_EDITOR_H_

#include <array>
#include <chrono>
#include <string>
#include <vector>

#include "IControls.h"
#include "model/spectrum_analyzer.h"
#include "plugin/ui/detector_scope.h"
#include "plugin/ui/editor_model.h"

namespace eeq {

class BandRibbon;
class EqGraph;
class LevelMeter;
class ParamField;

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
// are owned by IGraphics.
class Editor {
 public:
  Editor(iplug::igraphics::IGraphics& graphics, EditorModel& model);

  void Idle(const EditorIdleData& data);

  int selected_group() const { return selected_group_; }
  void SelectGroup(int group);
  // Called by the model when the selected band changes.
  void OnBandSelected(int band);

 private:
  void BuildBandEditor(const iplug::igraphics::IRECT& r);
  void BuildDynamicsPanel(const iplug::igraphics::IRECT& r);
  void BuildFooter(const iplug::igraphics::IRECT& r);
  void UpdateBandEditor(const ParamValues& v);
  void UpdateDynamicsPanel(const ParamValues& v);

  iplug::igraphics::IGraphics& graphics_;
  EditorModel& model_;
  int selected_group_ = 0;
  int bound_band_ = -1;
  int shown_band_group_ = -1;
  int bound_group_ = -1;

  BandRibbon* ribbon_ = nullptr;
  EqGraph* graph_ = nullptr;
  LevelMeter* input_meter_ = nullptr;
  LevelMeter* sidechain_meter_ = nullptr;
  LevelMeter* output_meter_ = nullptr;

  struct BandFieldControl {
    BandField field;
    ParamField* control;
  };
  std::vector<BandFieldControl> band_fields_;
  struct GroupFieldControl {
    GroupField field;
    ParamField* control;
  };
  std::vector<GroupFieldControl> group_fields_;
  std::vector<iplug::igraphics::IControl*> group_tabs_;

  iplug::igraphics::ITextControl* now_readout_ = nullptr;
  iplug::igraphics::ITextControl* status_ = nullptr;
  std::string status_text_;

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
