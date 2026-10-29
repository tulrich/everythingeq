#include "plugin/everything_eq.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>

#include "IPlugPaths.h"
#include "IPlug_include_in_plug_src.h"
#include "dsp/common/denormals.h"
#include "model/presets.h"
#include "model/state.h"
#include "plugin/ui/editor.h"

namespace eeq {

namespace ig = iplug::igraphics;

namespace {

// ~/Library/Application Support/Everything EQ/Presets (macOS),
// %LOCALAPPDATA%\Everything EQ\Presets (Windows).
std::filesystem::path UserPresetDir() {
  WDL_String path;
  iplug::AppSupportPath(path);
  return std::filesystem::path(path.Get()) / "Everything EQ" / "Presets";
}

}  // namespace

EverythingEq::EverythingEq(const iplug::InstanceInfo& info)
    : iplug::Plugin(
          info, iplug::MakeConfig(kNumParams,
                                  static_cast<int>(FactoryPresets().size()))),
      presets_(UserPresetDir()) {
  InitParams();
  MakeFactoryPresets();
  // Host bypass (AU, VST3) crossfades in the engine instead of switching
  // abruptly to iPlug2's pass-through.
  SetProcessWhileBypassed(true);

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
    // Command-Z / Shift-Command-Z (Control on Windows), when the host passes
    // the keys on.
    graphics->SetKeyHandlerFunc(
        [this](const iplug::IKeyPress& key, bool is_up) {
          const bool z = key.VK == 'Z' || std::tolower(key.utf8[0]) == 'z';
          if (!key.C || key.A || !z) return false;
          if (!is_up) key.S ? Redo() : Undo();
          return true;
        });
    editor_ = std::make_unique<Editor>(*graphics, *this);
    editor_open_ = true;
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

std::array<EditorModel::Tracked, kNumGroups> EverythingEq::LiveTracking()
    const {
  std::array<Tracked, kNumGroups> t{};
  for (int g = 0; g < kNumGroups; ++g) {
    t[g] = {group_tracked_[g].load(std::memory_order_relaxed),
            group_voiced_[g].load(std::memory_order_relaxed)};
  }
  return t;
}

int EverythingEq::PopAnalyzerSamples(bool post, float* dest, int max_count) {
  auto& ring = post ? analyzer_post_ : analyzer_pre_;
  return static_cast<int>(ring.Pop(dest, static_cast<size_t>(max_count)));
}

int EverythingEq::PopDetectorFrames(DetectorFrame* dest, int max_count) {
  return static_cast<int>(
      detector_frames_.Pop(dest, static_cast<size_t>(max_count)));
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
  SendParameterValueFromDelegate(param_id, GetParam(param_id)->GetNormalized(),
                                 true);
}

void EverythingEq::EndEdit(int param_id) {
  EndInformHostOfParamChangeFromUI(param_id);
}

void EverythingEq::BeginInformHostOfParamChangeFromUI(int param_index) {
  if (!applying_undo_) undo_.BeginGesture();
  iplug::Plugin::BeginInformHostOfParamChangeFromUI(param_index);
}

void EverythingEq::SendParameterValueFromUI(int param_index,
                                            double normalized) {
  const iplug::IParam* p = GetParam(param_index);
  const double before = p->Value();
  if (!exact_values_) {
    // UI edits land on the parameter's detents (controls keep their own
    // unsnapped position while dragging).
    normalized = p->ToNormalized(SnapParamValue(CurrentValues(), param_index,
                                                p->FromNormalized(normalized)));
  }
  iplug::Plugin::SendParameterValueFromUI(param_index, normalized);
  if (!applying_undo_) {
    undo_.Record(param_index, before, GetParam(param_index)->Value());
  }
}

void EverythingEq::EndInformHostOfParamChangeFromUI(int param_index) {
  iplug::Plugin::EndInformHostOfParamChangeFromUI(param_index);
  if (!applying_undo_) undo_.EndGesture();
}

void EverythingEq::ApplyValues(const ParamValues& values) {
  undo_.EndAllGestures();
  undo_.BeginGesture();
  exact_values_ = true;
  for (int id = 0; id < kNumParams; ++id) {
    const iplug::IParam* p = GetParam(id);
    if (p->Value() == values[id]) continue;
    BeginEdit(id);
    SetEdit(id, p->ToNormalized(values[id]));
    EndEdit(id);
  }
  exact_values_ = false;
  undo_.EndGesture();
}

void EverythingEq::ApplyChanges(const std::vector<ParamChange>& changes) {
  applying_undo_ = true;
  exact_values_ = true;
  for (const ParamChange& c : changes) {
    BeginEdit(c.id);
    SetEdit(c.id, GetParam(c.id)->ToNormalized(c.value));
    EndEdit(c.id);
  }
  exact_values_ = false;
  applying_undo_ = false;
}

void EverythingEq::Undo() {
  SyncUiState();
  undo_.EndAllGestures();
  ApplyChanges(undo_.Undo());
}

void EverythingEq::Redo() {
  SyncUiState();
  undo_.EndAllGestures();
  ApplyChanges(undo_.Redo());
}

void EverythingEq::SyncUiState() {
  if (state_restored_.exchange(false)) {
    // The host replaced the settings (project load, its preset menu): the
    // history no longer applies.
    undo_.Clear();
    preset_values_.reset();
  }
}

void EverythingEq::RefreshPresets() {
  presets_.Refresh();
  preset_values_.reset();  // A user preset may have changed on disk.
}

PresetRef EverythingEq::CurrentPreset() const {
  std::lock_guard<std::mutex> lock(preset_mutex_);
  return current_preset_;
}

void EverythingEq::SetCurrentPreset(const PresetRef& ref) {
  std::lock_guard<std::mutex> lock(preset_mutex_);
  current_preset_ = ref;
}

bool EverythingEq::PresetModified() {
  SyncUiState();
  const PresetRef ref = CurrentPreset();
  if (ref.name.empty()) return false;
  if (!preset_values_ || !(preset_values_ref_ == ref)) {
    preset_values_ = presets_.Load(presets_.Find(ref));
    preset_values_ref_ = ref;
    modified_epoch_ = ~0u;
  }
  if (!preset_values_) return false;  // E.g. a deleted user preset.
  const unsigned epoch = param_epoch_.load(std::memory_order_relaxed);
  if (epoch != modified_epoch_) {
    modified_epoch_ = epoch;
    modified_ = !ValuesMatch(CurrentValues(), *preset_values_);
  }
  return modified_;
}

bool EverythingEq::LoadPreset(int index, std::string* error) {
  SyncUiState();
  const std::optional<ParamValues> values = presets_.Load(index, error);
  if (!values) return false;
  ApplyValues(*values);
  SetCurrentPreset(presets_.Ref(index));
  preset_values_ = values;
  preset_values_ref_ = presets_.Ref(index);
  modified_epoch_ = ~0u;
  return true;
}

bool EverythingEq::SavePreset(std::string_view name, std::string* error) {
  SyncUiState();
  const int index = presets_.Save(name, CurrentValues(), error);
  if (index < 0) return false;
  SetCurrentPreset(presets_.Ref(index));
  preset_values_.reset();
  return true;
}

bool EverythingEq::RenamePreset(std::string_view name, std::string* error) {
  SyncUiState();
  const int index = presets_.Find(CurrentPreset());
  if (index < presets_.num_factory()) {
    if (error) *error = "Only user presets can be renamed.";
    return false;
  }
  const int renamed = presets_.Rename(index, name, error);
  if (renamed < 0) return false;
  SetCurrentPreset(presets_.Ref(renamed));
  preset_values_.reset();
  return true;
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
      case ParamKind::kOption:
        // An integer whose display text follows the band type, so hosts show
        // e.g. "24 dB/oct" for LP and "Flat" for Vowel.
        param->InitInt(s.name.c_str(), static_cast<int>(s.default_value),
                       static_cast<int>(s.min), static_cast<int>(s.max), "",
                       flags, s.group.c_str());
        param->SetDisplayFunc([this, id](double value, WDL_String& text) {
          text.Set(FormatParamValue(CurrentValues(), id, value).c_str());
        });
        param->SetParseFunc([this, id](const char* text, double& value) {
          const std::optional<double> v =
              ParseSlotText(CurrentValues(), id, text);
          if (v) value = *v;
          return v.has_value();
        });
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
    chunk.PutStr(eeq::SerializeState(ValuesFromEngineParams(preset.params),
                                     {preset.name, false})
                     .c_str());
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
  param_epoch_.fetch_add(1, std::memory_order_relaxed);
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
  chunk.PutStr(eeq::SerializeState(CurrentValues(), CurrentPreset()).c_str());
  return true;
}

int EverythingEq::UnserializeState(const iplug::IByteChunk& chunk,
                                   int start_pos) {
  WDL_String text;
  const int end_pos = chunk.GetStr(text, start_pos);
  if (end_pos < 0) return -1;
  PresetRef preset;
  const std::optional<ParamValues> values =
      DeserializeState(text.Get(), &preset);
  if (!values) return -1;
  SetCurrentPreset(preset);
  state_restored_ = true;
  ENTER_PARAMS_MUTEX
  for (int id = 0; id < kNumParams; ++id) GetParam(id)->Set((*values)[id]);
  OnParamReset(iplug::kPresetRecall);
  LEAVE_PARAMS_MUTEX
  return end_pos;
}

void EverythingEq::OnIdle() {
#if IPLUG_EDITOR
  SyncUiState();
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
  editor_open_ = false;
  undo_.EndAllGestures();
  editor_.reset();  // Its controls are owned and deleted by IGraphics.
#endif
}

#if IPLUG_DSP
void EverythingEq::OnReset() {
  const int max_block = std::max(GetBlockSize(), 1);
  for (auto& buffer : scratch_) buffer.assign(max_block, 0.f);
  engine_.Prepare(GetSampleRate());
  engine_.FadeInFromDry();  // The filters restart from silence.
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
  ScopedNoDenormals no_denormals;  // Covers the metering as well as the engine.
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

  const int max_block = static_cast<int>(scratch_[0].size());
  if (num_frames > max_block && max_block > 0) {
    // Hosts should not exceed the prepared block size; if one does, process
    // in pieces rather than dropping out.
    std::array<iplug::sample*, 8> in_ptrs{}, out_ptrs{};
    const int n_in = std::min<int>(MaxNChannels(iplug::ERoute::kInput), 8);
    const int n_out = std::min<int>(MaxNChannels(iplug::ERoute::kOutput), 8);
    for (int offset = 0; offset < num_frames; offset += max_block) {
      for (int c = 0; c < n_in; ++c) in_ptrs[c] = inputs[c] + offset;
      for (int c = 0; c < n_out; ++c) out_ptrs[c] = outputs[c] + offset;
      ProcessBlock(in_ptrs.data(), out_ptrs.data(),
                   std::min(max_block, num_frames - offset));
    }
    return;
  }
  if (num_frames > max_block) {  // Not prepared yet.
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
  if (GetBypassed() != host_bypassed_) {
    host_bypassed_ = GetBypassed();
    params_dirty_ = true;
  }
  if (params_dirty_.exchange(false)) {
    EngineParams params = BuildEngineParams(CurrentValues(), mono_output_);
    params.global.lookahead_ms = lookahead;
    params.global.bypass = params.global.bypass || host_bypassed_;
    engine_.SetParams(params);
  }

  engine_.Process(in, (sc_l || sc_r) ? sc : nullptr, out, num_frames);

  // Metering for the UI.
  DetectorFrame frame;
  frame.samples = num_frames;
  for (int g = 0; g < kNumGroups; ++g) {
    DynamicsGroup& group = engine_.mutable_group(g);
    group_controls_[g].store(
        static_cast<float>(std::max(group.control(0), group.control(1))),
        std::memory_order_relaxed);
    const DynamicsGroup::Meter m = group.TakeMeter();
    frame.trigger_min[g] = static_cast<float>(m.trigger_min);
    frame.trigger_max[g] = static_cast<float>(m.trigger_max);
    frame.control_max[g] = static_cast<float>(m.control_max);
    group_tracked_[g].store(static_cast<float>(m.tracked),
                            std::memory_order_relaxed);
    group_voiced_[g].store(m.voiced, std::memory_order_relaxed);
  }
  if (editor_open_.load(std::memory_order_relaxed)) {
    detector_frames_.Push(frame);
    float* mono = scratch_[6].data();
    for (int i = 0; i < num_frames; ++i) mono[i] = 0.5f * (in[0][i] + in[1][i]);
    analyzer_pre_.Push(mono, num_frames);
    for (int i = 0; i < num_frames; ++i)
      mono[i] = 0.5f * (out[0][i] + out[1][i]);
    analyzer_post_.Push(mono, num_frames);
  }

  const int num_out = NOutChansConnected();
  for (int c = 0; c < num_out; ++c) {
    for (int i = 0; i < num_frames; ++i) outputs[c][i] = out[c][i];
    AccumulatePeak(4 + c, outputs[c], num_frames);
  }
}
#endif  // IPLUG_DSP

}  // namespace eeq
