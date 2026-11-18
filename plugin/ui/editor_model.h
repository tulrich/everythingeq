#ifndef EVERYTHINGEQ_PLUGIN_UI_EDITOR_MODEL_H_
#define EVERYTHINGEQ_PLUGIN_UI_EDITOR_MODEL_H_

#include <array>
#include <string>
#include <string_view>

#include "model/param_table.h"
#include "model/preset_library.h"
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
  // What the engine last applied to each band (controls, Vocoder shapes).
  virtual LiveBands LiveBandState() const { return {}; }
  // Spectrum groups: the latest band levels (dB).
  virtual std::array<SpectralLevels, kNumGroups> LiveSpectra() const {
    return {};
  }
  // Vowel / Pitch groups: the tracker's latest estimate (as a control value
  // in [0, 1], held while unvoiced) and whether the input is voiced now.
  struct Tracked {
    float value = 0.f;
    bool voiced = false;
  };
  virtual std::array<Tracked, kNumGroups> LiveTracking() const = 0;
  // Audio published since the last call (mono) for the analyzer: the plugin
  // input, its output or the sidechain.  Returns the number of samples.
  enum class AnalyzerSource { kPre, kPost, kSidechain };
  virtual int PopAnalyzerSamples(AnalyzerSource source, float* dest,
                                 int max_count) = 0;
  virtual int PopDetectorFrames(DetectorFrame* dest, int max_count) = 0;

  virtual int SelectedBand() const = 0;
  virtual void SelectBand(int band) = 0;

  // A parameter edit gesture from the UI (normalized values).
  virtual void BeginEdit(int param_id) = 0;
  virtual void SetEdit(int param_id, double normalized) = 0;
  virtual void EndEdit(int param_id) = 0;

  // Presets (ABOUT.md section 3.E).  Loading a preset is one undo step.
  // Saving and renaming return false and set *error on failure.
  virtual const PresetLibrary& Presets() const = 0;
  virtual void RefreshPresets() = 0;
  virtual PresetRef CurrentPreset() const = 0;
  // The settings differ from the current preset's.
  virtual bool PresetModified() = 0;
  virtual bool LoadPreset(int index, std::string* error) = 0;
  virtual bool SavePreset(std::string_view name, std::string* error) = 0;
  // Renames the current (user) preset.
  virtual bool RenamePreset(std::string_view name, std::string* error) = 0;

  // Undo/redo of edits made in the plugin UI.
  virtual bool CanUndo() const = 0;
  virtual bool CanRedo() const = 0;
  virtual void Undo() = 0;
  virtual void Redo() = 0;

  // A/B comparison: two sets of settings (each with its own preset name
  // and undo history), 0 = A, 1 = B.  Switching is not an undo step.
  virtual int CompareSlot() const = 0;
  virtual void SwitchCompare() = 0;
  // Copies the active slot's settings over the other slot's.
  virtual void CopyCompare() = 0;

  // Gain Match, a listening aid (not saved): the output plays at the
  // input's loudness.  GainMatchDb() is the gain it applies now.
  virtual bool GainMatchOn() const = 0;
  virtual void SetGainMatchOn(bool on) = 0;
  virtual float GainMatchDb() const = 0;

  // Moves every parameter to `values` (host values) as one undo step, with
  // exact values (no detents).
  virtual void SetValuesGesture(const ParamValues& values) = 0;

  // Sets a plain value as a single complete gesture.
  void SetValueGesture(int param_id, double normalized) {
    BeginEdit(param_id);
    SetEdit(param_id, normalized);
    EndEdit(param_id);
  }
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_EDITOR_MODEL_H_
