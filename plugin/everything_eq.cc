#include "plugin/everything_eq.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "IPlug_include_in_plug_src.h"
#include "model/presets.h"
#include "model/state.h"
#include "plugin/ui/editor.h"

namespace eeq {

namespace ig = iplug::igraphics;

EverythingEq::EverythingEq(const iplug::InstanceInfo& info)
    : iplug::Plugin(
          info, iplug::MakeConfig(kNumParams,
                                  static_cast<int>(FactoryPresets().size()))) {
  InitParams();
  MakeFactoryPresets();

#if IPLUG_DSP
  SetChannelLabel(iplug::ERoute::kInput, 0, "Main L");
  SetChannelLabel(iplug::ERoute::kInput, 1, "Main R");
  SetChannelLabel(iplug::ERoute::kInput, 2, "Sidechain L");
  SetChannelLabel(iplug::ERoute::kInput, 3, "Sidechain R");
#endif

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return ig::MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS,
                            GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };

  mLayoutFunc = [&](ig::IGraphics* graphics) {
    graphics->AttachCornerResizer(ig::EUIResizerMode::Scale, false);
    graphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    editor_ = std::make_unique<Editor>(*graphics, *this);
    layout_changed_ = true;
  };
#endif
}

EverythingEq::~EverythingEq() = default;

GroupControls EverythingEq::LiveControls() const {
  GroupControls c{};
  for (int g = 0; g < kNumGroups; ++g) {
    c[g] = group_controls_[g].load(std::memory_order_relaxed);
  }
  return c;
}

void EverythingEq::SelectBand(int band) {
  selected_band_ = std::clamp(band, 0, kNumBands - 1);
#if IPLUG_EDITOR
  if (editor_) editor_->OnBandSelected(selected_band_);
#endif
}

void EverythingEq::BeginEdit(int param_id) {
  BeginInformHostOfParamChangeFromUI(param_id);
}

void EverythingEq::SetEdit(int param_id, double normalized) {
  SendParameterValueFromUI(param_id, normalized);
  // Keep any controls bound to this parameter in sync.
  SendParameterValueFromDelegate(param_id, normalized, true);
}

void EverythingEq::EndEdit(int param_id) {
  EndInformHostOfParamChangeFromUI(param_id);
}

void EverythingEq::InitParams() {
  const std::vector<ParamSpec>& table = ParamTable();
  for (int id = 0; id < kNumParams; ++id) {
    const ParamSpec& s = table[id];
    iplug::IParam* param = GetParam(id);
    const int flags = s.automatable ? 0 : iplug::IParam::kFlagCannotAutomate;
    switch (s.kind) {
      case ParamKind::kEnum:
        param->InitEnum(s.name.c_str(), static_cast<int>(s.default_value),
                        static_cast<int>(s.enum_labels.size()), "", flags,
                        s.group.c_str());
        for (size_t i = 0; i < s.enum_labels.size(); ++i) {
          param->SetDisplayText(static_cast<double>(i),
                                s.enum_labels[i].c_str());
        }
        break;
      case ParamKind::kBool:
        param->InitBool(s.name.c_str(), s.default_value >= 0.5, "", flags,
                        s.group.c_str(), "Off", "On");
        break;
      case ParamKind::kContinuous:
        if (s.log) {
          param->InitDouble(s.name.c_str(), s.default_value, s.min, s.max,
                            s.step > 0.0 ? s.step : 0.001, s.unit.c_str(),
                            flags, s.group.c_str(), iplug::IParam::ShapeExp());
        } else {
          param->InitDouble(s.name.c_str(), s.default_value, s.min, s.max,
                            s.step > 0.0 ? s.step : 0.001, s.unit.c_str(),
                            flags, s.group.c_str());
        }
        break;
      case ParamKind::kSlot:
        // Normalized; the display text follows the band type (D14).
        param->InitDouble(
            s.name.c_str(), s.default_value, 0.0, 1.0, 1e-6, "", flags,
            s.group.c_str(), iplug::IParam::ShapeLinear(),
            iplug::IParam::kUnitCustom,
            [this, id](double value, WDL_String& text) {
              text.Set(FormatParamValue(CurrentValues(), id, value).c_str());
            });
        param->SetParseFunc([this, id](const char* text, double& value) {
          const std::optional<double> v =
              ParseSlotText(CurrentValues(), id, text);
          if (v) value = *v;
          return v.has_value();
        });
        break;
    }
  }
}

void EverythingEq::MakeFactoryPresets() {
  for (const Preset& preset : FactoryPresets()) {
    iplug::IByteChunk chunk;
    chunk.PutStr(
        eeq::SerializeState(ValuesFromEngineParams(preset.params)).c_str());
    MakePresetFromChunk(preset.name.c_str(), chunk);
  }
}

ParamValues EverythingEq::CurrentValues() const {
  ParamValues values{};
  for (int id = 0; id < kNumParams; ++id) values[id] = GetParam(id)->Value();
  return values;
}

void EverythingEq::OnParamChange(int param_index) {
  params_dirty_ = true;
  if (param_index == GlobalParamId(GlobalField::kLookahead)) UpdateLatency();
}

void EverythingEq::UpdateLatency() {
  // Must match Engine::ApplyStaticDesigns().
  const double ms = GetParam(GlobalParamId(GlobalField::kLookahead))->Value();
  const int samples = static_cast<int>(std::lround(
      std::clamp(ms, 0.0, kMaxLookaheadMs) * GetSampleRate() / 1000.0));
  if (samples != GetLatency()) SetLatency(samples);
}

bool EverythingEq::SerializeState(iplug::IByteChunk& chunk) const {
  chunk.PutStr(eeq::SerializeState(CurrentValues()).c_str());
  return true;
}

int EverythingEq::UnserializeState(const iplug::IByteChunk& chunk,
                                   int start_pos) {
  WDL_String text;
  const int end_pos = chunk.GetStr(text, start_pos);
  if (end_pos < 0) return -1;
  const std::optional<ParamValues> values = DeserializeState(text.Get());
  if (!values) return -1;
  ENTER_PARAMS_MUTEX
  for (int id = 0; id < kNumParams; ++id) GetParam(id)->Set((*values)[id]);
  OnParamReset(iplug::kPresetRecall);
  LEAVE_PARAMS_MUTEX
  return end_pos;
}

void EverythingEq::OnIdle() {
#if IPLUG_EDITOR
  if (!editor_) return;
  EditorIdleData data;
  for (int i = 0; i < kNumMeterChannels; ++i) {
    data.peaks[i] = peaks_[i].exchange(0.f, std::memory_order_relaxed);
  }
  data.transport_started = transport_started_.exchange(false);
  for (int i = 0; i < kMaxIn; ++i) data.in_connected[i] = in_connected_[i];
  for (int i = 0; i < kMaxOut; ++i) data.out_connected[i] = out_connected_[i];
  data.layout_changed = layout_changed_.exchange(false);
  data.latency_samples = GetLatency();
  editor_->Idle(data);
#endif
}

void EverythingEq::OnUIClose() {
#if IPLUG_EDITOR
  editor_.reset();  // Its controls are owned and deleted by IGraphics.
#endif
}

#if IPLUG_DSP
void EverythingEq::OnReset() {
  const int max_block = std::max(GetBlockSize(), 1);
  for (auto& buffer : scratch_) buffer.assign(max_block, 0.f);
  engine_.Prepare(GetSampleRate());
  params_dirty_ = true;
  UpdateLatency();
  // Logic and GarageBand only re-read AU latency when the transport starts.
#if defined AU_API
  lookahead_gate_.set_host_defers_latency(GetHost() == iplug::kHostLogic ||
                                          GetHost() == iplug::kHostGarageBand);
#endif
  lookahead_gate_.Reset(
      GetParam(GlobalParamId(GlobalField::kLookahead))->Value());
  layout_changed_ = true;
}

void EverythingEq::OnActivate(bool enable) { layout_changed_ = true; }

void EverythingEq::GetBusName(iplug::ERoute direction, int bus_index,
                              int num_buses, WDL_String& name) const {
  if (direction == iplug::ERoute::kInput) {
    name.Set(bus_index == 0 ? "Main Input" : "Sidechain");
  } else {
    name.Set("Output");
  }
}

void EverythingEq::UpdateConnections() {
  for (int i = 0; i < kMaxIn; ++i) {
    const bool connected = IsChannelConnected(iplug::ERoute::kInput, i);
    if (connected != in_connected_[i]) {
      in_connected_[i] = connected;
      layout_changed_ = true;
    }
  }
  for (int i = 0; i < kMaxOut; ++i) {
    const bool connected = IsChannelConnected(iplug::ERoute::kOutput, i);
    if (connected != out_connected_[i]) {
      out_connected_[i] = connected;
      layout_changed_ = true;
    }
  }
}

void EverythingEq::AccumulatePeak(int meter_channel, const iplug::sample* data,
                                  int num_frames) {
  float peak = 0.f;
  for (int i = 0; i < num_frames; ++i) {
    peak = std::max(peak, static_cast<float>(std::fabs(data[i])));
  }
  std::atomic<float>& slot = peaks_[meter_channel];
  float prev = slot.load(std::memory_order_relaxed);
  while (peak > prev &&
         !slot.compare_exchange_weak(prev, peak, std::memory_order_relaxed)) {
  }
}

void EverythingEq::ProcessBlock(iplug::sample** inputs, iplug::sample** outputs,
                                int num_frames) {
  UpdateConnections();

  const bool running = GetTransportIsRunning();
  const bool started = running && !transport_was_running_;
  if (started) transport_started_ = true;
  transport_was_running_ = running;

  // Logic and GarageBand pass the main input on the sidechain bus when no
  // sidechain is selected; treat an identical sidechain as silence.
  bool sc_same_as_main[2] = {false, false};
#if defined OS_MAC && defined AU_API
  if (GetHost() == iplug::kHostLogic || GetHost() == iplug::kHostGarageBand) {
    for (int c = 0; c < 2; ++c) {
      sc_same_as_main[c] = in_connected_[c] && in_connected_[2 + c] &&
                           !std::memcmp(inputs[c], inputs[2 + c],
                                        num_frames * sizeof(iplug::sample));
    }
  }
#endif

  if (num_frames > static_cast<int>(scratch_[0].size())) {
    // Hosts should not exceed the prepared block size, but don't crash if
    // one does.
    for (int c = 0; c < NOutChansConnected(); ++c) {
      std::fill(outputs[c], outputs[c] + num_frames, 0.);
    }
    return;
  }

  // Map the host layout onto the engine's stereo main + stereo sidechain.
  float* in[2] = {scratch_[0].data(), scratch_[1].data()};
  float* sc[2] = {scratch_[2].data(), scratch_[3].data()};
  float* out[2] = {scratch_[4].data(), scratch_[5].data()};
  const bool main_r = in_connected_[1];
  const bool sc_l = in_connected_[2] && !sc_same_as_main[0];
  const bool sc_r = in_connected_[3] && !sc_same_as_main[1];
  for (int i = 0; i < num_frames; ++i) {
    in[0][i] = static_cast<float>(inputs[0][i]);
    in[1][i] = static_cast<float>(main_r ? inputs[1][i] : inputs[0][i]);
    sc[0][i] = sc_l ? static_cast<float>(inputs[2][i]) : 0.f;
    sc[1][i] = sc_r ? static_cast<float>(inputs[3][i]) : sc[0][i];
  }

  if (in_connected_[0]) AccumulatePeak(0, inputs[0], num_frames);
  if (main_r) AccumulatePeak(1, inputs[1], num_frames);
  if (sc_l) AccumulatePeak(2, inputs[2], num_frames);
  if (sc_r) AccumulatePeak(3, inputs[3], num_frames);

  // Rebuild the engine parameters only when something changed.
  const bool mono_output = !out_connected_[1];
  if (mono_output != mono_output_) {
    mono_output_ = mono_output;
    params_dirty_ = true;
  }
  const double applied_lookahead = lookahead_gate_.applied_ms();
  const double lookahead = lookahead_gate_.Update(
      GetParam(GlobalParamId(GlobalField::kLookahead))->Value(), running,
      started);
  if (lookahead != applied_lookahead) params_dirty_ = true;
  if (params_dirty_.exchange(false)) {
    EngineParams params = BuildEngineParams(CurrentValues(), mono_output_);
    params.global.lookahead_ms = lookahead;
    engine_.SetParams(params);
  }

  engine_.Process(in, (sc_l || sc_r) ? sc : nullptr, out, num_frames);
  for (int g = 0; g < kNumGroups; ++g) {
    const DynamicsGroup& group = engine_.group(g);
    group_controls_[g].store(
        static_cast<float>(std::max(group.control(0), group.control(1))),
        std::memory_order_relaxed);
  }

  const int num_out = NOutChansConnected();
  for (int c = 0; c < num_out; ++c) {
    for (int i = 0; i < num_frames; ++i) outputs[c][i] = out[c][i];
    AccumulatePeak(4 + c, outputs[c], num_frames);
  }
}
#endif  // IPLUG_DSP

}  // namespace eeq
