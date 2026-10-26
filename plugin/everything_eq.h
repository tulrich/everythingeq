#ifndef EVERYTHINGEQ_PLUGIN_EVERYTHING_EQ_H_
#define EVERYTHINGEQ_PLUGIN_EVERYTHING_EQ_H_

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "IControls.h"
#include "IPlug_include_in_plug_hdr.h"
#include "dsp/common/spsc_ring.h"
#include "dsp/engine/engine.h"
#include "model/lookahead_gate.h"
#include "model/param_table.h"
#include "plugin/ui/editor_model.h"

namespace eeq {

class Editor;

// The Everything EQ plugin: host parameters, state and presets from model/,
// audio through eeq::Engine, UI from plugin/ui/ (Editor).
//
// Overrides keep iPlug2's method names.
class EverythingEq final : public iplug::Plugin, public EditorModel {
 public:
  explicit EverythingEq(const iplug::InstanceInfo& info);
  ~EverythingEq() override;

  // EditorModel.
  ParamValues Values() const override { return CurrentValues(); }
  double SampleRate() const override { return GetSampleRate(); }
  GroupControls LiveControls() const override;
  int PopAnalyzerSamples(bool post, float* dest, int max_count) override;
  int PopDetectorFrames(DetectorFrame* dest, int max_count) override;
  int SelectedBand() const override { return selected_band_; }
  void SelectBand(int band) override;
  void BeginEdit(int param_id) override;
  void SetEdit(int param_id, double normalized) override;
  void EndEdit(int param_id) override;

  void OnIdle() override;
  void OnUIClose() override;
  void OnParamChange(int param_index) override;
  bool SerializeState(iplug::IByteChunk& chunk) const override;
  int UnserializeState(const iplug::IByteChunk& chunk, int start_pos) override;
#if IPLUG_DSP
  void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs,
                    int num_frames) override;
  void OnReset() override;
  void OnActivate(bool enable) override;
  void GetBusName(iplug::ERoute direction, int bus_index, int num_buses,
                  WDL_String& name) const override;
#endif

 private:
  static constexpr int kMaxIn = 4;  // Main L/R + sidechain L/R.
  static constexpr int kMaxOut = 2;
  // Meter channels: main in L/R, sidechain L/R, out L/R.
  static constexpr int kNumMeterChannels = kMaxIn + kMaxOut;

  void InitParams();
  void MakeFactoryPresets();
  ParamValues CurrentValues() const;
  void UpdateLatency();
  void UpdateConnections();
  void AccumulatePeak(int meter_channel, const iplug::sample* data,
                      int num_frames);

  Engine engine_;
  // In L/R, SC L/R, out L/R, mono analyzer buffer.
  std::array<std::vector<float>, 7> scratch_;
  std::atomic<bool> params_dirty_{true};
  LookaheadGate lookahead_gate_;  // Audio thread.
  bool mono_output_ = false;

  std::array<bool, kMaxIn> in_connected_{};
  std::array<bool, kMaxOut> out_connected_{};
  std::atomic<bool> layout_changed_{true};

  // Audio thread -> UI: max |sample| per channel since the UI last read it,
  // and the latest dynamics control value per group.
  std::array<std::atomic<float>, kNumMeterChannels> peaks_{};
  std::array<std::atomic<float>, kNumGroups> group_controls_{};
  std::atomic<bool> transport_started_{false};
  std::atomic<bool> editor_open_{false};
  SpscRing<float, 32768> analyzer_pre_;
  SpscRing<float, 32768> analyzer_post_;
  SpscRing<DetectorFrame, 1024> detector_frames_;
  bool transport_was_running_ = false;

  // UI thread.
  int selected_band_ = 0;
  std::unique_ptr<Editor> editor_;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_EVERYTHING_EQ_H_
