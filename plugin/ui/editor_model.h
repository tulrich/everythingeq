#ifndef EVERYTHINGEQ_PLUGIN_UI_EDITOR_MODEL_H_
#define EVERYTHINGEQ_PLUGIN_UI_EDITOR_MODEL_H_

#include "model/param_table.h"
#include "model/response_curves.h"

namespace eeq {

// What the custom UI controls need from the plugin (UI thread only).
class EditorModel {
 public:
  virtual ~EditorModel() = default;

  virtual ParamValues Values() const = 0;
  virtual double SampleRate() const = 0;
  // Latest dynamics control value D per group, from the audio thread.
  virtual GroupControls LiveControls() const = 0;

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
