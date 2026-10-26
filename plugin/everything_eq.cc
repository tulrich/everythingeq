#include "plugin/everything_eq.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "IPlug_include_in_plug_src.h"
#include "plugin/ui/level_meter.h"

namespace eeq {

namespace ig = iplug::igraphics;

EverythingEq::EverythingEq(const iplug::InstanceInfo& info)
    : iplug::Plugin(info, iplug::MakeConfig(kNumParams, kNumPresets)) {
  GetParam(kParamGain)->InitGain("Output Gain", 0., -24., 24.);

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
    graphics->AttachPanelBackground(ig::COLOR_DARK_GRAY);
    graphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    ig::IRECT bounds = graphics->GetBounds().GetPadded(-20.f);

    // Input (and sidechain) meters on the left, output meter on the right.
    constexpr float kMeterWidth = 32.f;
    input_meter_ = new LevelMeter(bounds.ReduceFromLeft(kMeterWidth), "In", 2);
    bounds.ReduceFromLeft(6.f);
    sidechain_meter_ =
        new LevelMeter(bounds.ReduceFromLeft(kMeterWidth), "SC", 2);
    output_meter_ =
        new LevelMeter(bounds.ReduceFromRight(kMeterWidth), "Out", 2);
    graphics->AttachControl(input_meter_);
    graphics->AttachControl(sidechain_meter_);
    graphics->AttachControl(output_meter_);

    bounds.Pad(-20.f);
    const ig::IRECT header = bounds.ReduceFromTop(40.f);
    const ig::IRECT footer = bounds.ReduceFromBottom(30.f);
    const ig::IVStyle style =
        ig::DEFAULT_STYLE.WithColor(ig::kFG, ig::COLOR_WHITE.WithOpacity(0.3f));
    graphics->AttachControl(new ig::ITextControl(
        header, "Everything EQ " PLUG_VERSION_STR " (spike build)",
        ig::IText(24.f, ig::COLOR_WHITE, "Roboto-Regular", ig::EAlign::Near)));
    graphics->AttachControl(
        new ig::ITextControl(footer, "",
                             ig::IText(14.f, ig::COLOR_LIGHT_GRAY,
                                       "Roboto-Regular", ig::EAlign::Near)),
        kControlTagLayout);
    graphics->AttachControl(
        new ig::IVKnobControl(bounds.GetCentredInside(120.f, 160.f), kParamGain,
                              "Output Gain", style));
    layout_changed_ = true;
  };
#endif
}

void EverythingEq::OnIdle() {
#if IPLUG_EDITOR
  ig::IGraphics* graphics = GetUI();
  if (!graphics || !input_meter_) return;

  float peaks[kNumMeterChannels];
  for (int i = 0; i < kNumMeterChannels; ++i) {
    peaks[i] = peaks_[i].exchange(0.f, std::memory_order_relaxed);
  }
  input_meter_->AddPeaks(peaks);
  sidechain_meter_->AddPeaks(peaks + 2);
  output_meter_->AddPeaks(peaks + 4);

  if (transport_started_.exchange(false)) {
    input_meter_->ResetClip();
    sidechain_meter_->ResetClip();
    output_meter_->ResetClip();
  }

  if (layout_changed_.exchange(false)) {
    input_meter_->SetChannelActive(0, in_connected_[0]);
    input_meter_->SetChannelActive(1, in_connected_[1]);
    sidechain_meter_->SetChannelActive(0, in_connected_[2]);
    sidechain_meter_->SetChannelActive(1, in_connected_[3]);
    output_meter_->SetChannelActive(0, out_connected_[0]);
    output_meter_->SetChannelActive(1, out_connected_[1]);

    const int num_in = in_connected_[0] + in_connected_[1];
    const int num_sc = in_connected_[2] + in_connected_[3];
    const int num_out = out_connected_[0] + out_connected_[1];
    WDL_String host, text;
    GetHostStr(host);
    text.SetFormatted(256,
                      "Layout: %d in, %d sidechain, %d out   |   host: %s   "
                      "|   latency: %d samples",
                      num_in, num_sc, num_out, host.Get(), GetLatency());
    if (auto* label = graphics->GetControlWithTag(kControlTagLayout)
                          ->As<ig::ITextControl>()) {
      label->SetStr(text.Get());
    }
  }
#endif
}

void EverythingEq::OnUIClose() {
#if IPLUG_EDITOR
  // The meters are owned and deleted by IGraphics.
  input_meter_ = sidechain_meter_ = output_meter_ = nullptr;
#endif
}

#if IPLUG_DSP
void EverythingEq::OnReset() {
  const int max_block = std::max(GetBlockSize(), 1);
  for (auto& buffer : scratch_) buffer.assign(max_block, 0.f);
  engine_.Prepare(GetSampleRate());
  if (engine_.latency_samples() != GetLatency()) {
    SetLatency(engine_.latency_samples());
  }
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
  if (running && !transport_was_running_) transport_started_ = true;
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

  engine_.Process(in, (sc_l || sc_r) ? sc : nullptr, out, num_frames);

  const double gain = GetParam(kParamGain)->DBToAmp();
  const int num_out = NOutChansConnected();
  for (int c = 0; c < num_out; ++c) {
    for (int i = 0; i < num_frames; ++i) outputs[c][i] = out[c][i] * gain;
    AccumulatePeak(4 + c, outputs[c], num_frames);
  }
}
#endif  // IPLUG_DSP

}  // namespace eeq
