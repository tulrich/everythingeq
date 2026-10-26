#ifndef EVERYTHINGEQ_PLUGIN_EVERYTHING_EQ_H_
#define EVERYTHINGEQ_PLUGIN_EVERYTHING_EQ_H_

#include <array>
#include <atomic>
#include <vector>

#include "IControls.h"
#include "IPlug_include_in_plug_hdr.h"
#include "dsp/engine/engine.h"
#include "model/param_table.h"

namespace eeq {

class LevelMeter;

enum ControlTag { kControlTagLayout = 0, kNumControlTags };

// The Everything EQ plugin: host parameters, state and presets from model/,
// audio through eeq::Engine.  The UI is still the Phase 0 placeholder.
//
// Overrides keep iPlug2's method names.
class EverythingEq final : public iplug::Plugin {
 public:
  explicit EverythingEq(const iplug::InstanceInfo& info);

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
  std::array<std::vector<float>, 6> scratch_;  // In L/R, SC L/R, out L/R.
  std::atomic<bool> params_dirty_{true};
  bool mono_output_ = false;

  std::array<bool, kMaxIn> in_connected_{};
  std::array<bool, kMaxOut> out_connected_{};
  std::atomic<bool> layout_changed_{true};

  // Audio thread -> UI: max |sample| per channel since the UI last read it.
  std::array<std::atomic<float>, kNumMeterChannels> peaks_{};
  std::atomic<bool> transport_started_{false};
  bool transport_was_running_ = false;

#if IPLUG_EDITOR
  LevelMeter* input_meter_ = nullptr;
  LevelMeter* sidechain_meter_ = nullptr;
  LevelMeter* output_meter_ = nullptr;
#endif
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_EVERYTHING_EQ_H_
