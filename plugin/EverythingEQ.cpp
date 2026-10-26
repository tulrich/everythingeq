#include "EverythingEQ.h"
#include "IPlug_include_in_plug_src.h"

#include <algorithm>
#include <cstring>

EverythingEQ::EverythingEQ(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kGain)->InitGain("Output Gain", 0., -24., 24.);

#if IPLUG_DSP
  SetChannelLabel(ERoute::kInput, 0, "Main L");
  SetChannelLabel(ERoute::kInput, 1, "Main R");
  SetChannelLabel(ERoute::kInput, 2, "Sidechain L");
  SetChannelLabel(ERoute::kInput, 3, "Sidechain R");
#endif

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    pGraphics->AttachPanelBackground(COLOR_DARK_GRAY);
    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    const IRECT b = pGraphics->GetBounds().GetPadded(-20.f);
    IRECT header = b.GetFromTop(40.f);
    IRECT body = b.GetReducedFromTop(50.f);
    IRECT knob = body.ReduceFromRight(140.f);

    const IVStyle style = DEFAULT_STYLE.WithColor(kFG, COLOR_WHITE.WithOpacity(0.3f));
    pGraphics->AttachControl(new ITextControl(header, "Everything EQ " PLUG_VERSION_STR " (spike build)",
                                              IText(24.f, COLOR_WHITE, "Roboto-Regular", EAlign::Near)));
    pGraphics->AttachControl(new ITextControl(body.GetFromBottom(30.f), "",
                                              IText(16.f, COLOR_LIGHT_GRAY, "Roboto-Regular", EAlign::Near)),
                             kCtrlTagLayout);
    body.ReduceFromBottom(40.f);
    IRECT meters = body;
    pGraphics->AttachControl(new IVPeakAvgMeterControl<kMaxIn>(meters.FracRectVertical(0.5f, true).GetPadded(-5.f), "Inputs", style,
                                                               EDirection::Horizontal, {"Main L", "Main R", "Sidechain L", "Sidechain R"}),
                             kCtrlTagInputMeter);
    pGraphics->AttachControl(new IVPeakAvgMeterControl<kMaxOut>(meters.FracRectVertical(0.5f, false).GetPadded(-5.f), "Outputs", style,
                                                                EDirection::Horizontal, {"Out L", "Out R"}),
                             kCtrlTagOutputMeter);
    pGraphics->AttachControl(new IVKnobControl(knob.GetCentredInside(120.f, 160.f), kGain, "Output Gain", style));
  };
#endif
}

void EverythingEQ::OnIdle()
{
  mInputPeakSender.TransmitData(*this);
  mOutputPeakSender.TransmitData(*this);

#if IPLUG_EDITOR
  if (mLayoutChanged && GetUI())
  {
    const int nIn = (mInConnected[0] ? 1 : 0) + (mInConnected[1] ? 1 : 0);
    const int nSc = (mInConnected[2] ? 1 : 0) + (mInConnected[3] ? 1 : 0);
    const int nOut = (mOutConnected[0] ? 1 : 0) + (mOutConnected[1] ? 1 : 0);
    WDL_String host, str;
    GetHostStr(host);
    str.SetFormatted(256, "Layout: %d in, %d sidechain, %d out   |   host: %s   |   latency: %d samples",
                     nIn, nSc, nOut, host.Get(), GetLatency());
    if (auto* pText = GetUI()->GetControlWithTag(kCtrlTagLayout)->As<ITextControl>())
      pText->SetStr(str.Get());
    GetUI()->SetAllControlsDirty();
    mLayoutChanged = false;
  }
#endif
}

#if IPLUG_DSP
void EverythingEQ::OnReset()
{
  const int maxBlock = std::max(GetBlockSize(), 1);
  for (auto& v : mScratch)
    v.assign(maxBlock, 0.f);
  mEngine.prepare(GetSampleRate());
  if (mEngine.latencySamples() != GetLatency())
    SetLatency(mEngine.latencySamples());
  mInputPeakSender.Reset(GetSampleRate());
  mOutputPeakSender.Reset(GetSampleRate());
  mLayoutChanged = true;
}

void EverythingEQ::OnActivate(bool enable)
{
  mLayoutChanged = true;
}

void EverythingEQ::GetBusName(ERoute direction, int busIdx, int nBuses, WDL_String& str) const
{
  if (direction == ERoute::kInput)
    str.Set(busIdx == 0 ? "Main Input" : "Sidechain");
  else
    str.Set("Output");
}

void EverythingEQ::UpdateConnections()
{
  for (int i = 0; i < kMaxIn; ++i)
  {
    const bool c = IsChannelConnected(ERoute::kInput, i);
    if (c != mInConnected[i]) { mInConnected[i] = c; mLayoutChanged = true; }
  }
  for (int i = 0; i < kMaxOut; ++i)
  {
    const bool c = IsChannelConnected(ERoute::kOutput, i);
    if (c != mOutConnected[i]) { mOutConnected[i] = c; mLayoutChanged = true; }
  }
}

void EverythingEQ::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  UpdateConnections();

  // Logic and GarageBand pass the main input on the sidechain bus when no
  // sidechain is selected; treat an identical sidechain as silence.
  bool scSameAsMain[2] = {false, false};
#if defined OS_MAC && defined AU_API
  if (GetHost() == kHostLogic || GetHost() == kHostGarageBand)
  {
    for (int c = 0; c < 2; ++c)
      scSameAsMain[c] = mInConnected[c] && mInConnected[2 + c] &&
                        !std::memcmp(inputs[c], inputs[2 + c], nFrames * sizeof(sample));
  }
#endif

  if (nFrames > static_cast<int>(mScratch[0].size()))
  {
    // Hosts should not exceed the prepared block size, but don't crash if one does.
    for (int c = 0; c < NOutChansConnected(); ++c)
      std::fill(outputs[c], outputs[c] + nFrames, 0.);
    return;
  }

  // Map the host layout onto the engine's stereo main + stereo sidechain.
  float* in[2] = {mScratch[0].data(), mScratch[1].data()};
  float* sc[2] = {mScratch[2].data(), mScratch[3].data()};
  float* out[2] = {mScratch[4].data(), mScratch[5].data()};
  const bool mainR = mInConnected[1];
  const bool scL = mInConnected[2] && !scSameAsMain[0];
  const bool scR = mInConnected[3] && !scSameAsMain[1];
  for (int s = 0; s < nFrames; ++s)
  {
    in[0][s] = static_cast<float>(inputs[0][s]);
    in[1][s] = static_cast<float>(mainR ? inputs[1][s] : inputs[0][s]);
    sc[0][s] = scL ? static_cast<float>(inputs[2][s]) : 0.f;
    sc[1][s] = scR ? static_cast<float>(inputs[3][s]) : sc[0][s];
  }

  mEngine.process(in, (scL || scR) ? sc : nullptr, out, nFrames);

  const double gain = GetParam(kGain)->DBToAmp();
  const int nOut = NOutChansConnected();
  for (int c = 0; c < nOut; ++c)
    for (int s = 0; s < nFrames; ++s)
      outputs[c][s] = out[c][s] * gain;

  mInputPeakSender.ProcessBlock(inputs, nFrames, kCtrlTagInputMeter, kMaxIn, 0);
  mOutputPeakSender.ProcessBlock(outputs, nFrames, kCtrlTagOutputMeter, kMaxOut, 0);
}
#endif  // IPLUG_DSP
