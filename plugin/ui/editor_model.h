#ifndef EVERYTHINGEQ_PLUGIN_UI_EDITOR_MODEL_H_
#define EVERYTHINGEQ_PLUGIN_UI_EDITOR_MODEL_H_

#include <array>

#include "model/param_table.h"
#include "model/response_curves.h"

namespace eeq {

// Detector metering for one audio block (audio thread -> UI).
struct DetectorFrame {
  int samples = 0;
  std::array<float, kNumGroups> trigger_min{};
  std::array<float, kNumGroups> trigger_max{};
  std::array<float, kNumGroups> control_max{};
};

// What the custom UI controls need from the plugin (UI thread only).
class EditorModel {
 public:
  virtual ~EditorModel() = default;

  virtual ParamValues Values() const = 0;
  virtual double SampleRate() const = 0;
  // Latest dynamics control value D per group, from the audio thread.
  virtual GroupControls LiveControls() const = 0;
  // Vowel / Pitch groups: the tracker's latest estimate (as a control value
  // in [0, 1], held while unvoiced) and whether the input is voiced now.
  struct Tracked {
    float value = 0.f;
    bool voiced = false;
  };
  virtual std::array<Tracked, kNumGroups> LiveTracking() const = 0;
  // Audio published since the last call (mono): the plugin input ("pre") or
  // output ("post"), for the analyzer.  Returns the number of samples.
  virtual int PopAnalyzerSamples(bool post, float* dest, int max_count) = 0;
  virtual int PopDetectorFrames(DetectorFrame* dest, int max_count) = 0;

  virtual int SelectedBand() const = 0;
  virtual void SelectBand(int band) = 0;

  // A parameter edit gesture from the UI (normalized values).
  virtual void BeginEdit(int param_id) = 0;
  virtual void SetEdit(int param_id, double normalized) = 0;
  virtual void EndEdit(int param_id) = 0;

  // Sets a plain value as a single complete gesture.
  void SetValueGesture(int param_id, double normalized) {
    BeginEdit(param_id);
    SetEdit(param_id, normalized);
    EndEdit(param_id);
  }
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_EDITOR_MODEL_H_
