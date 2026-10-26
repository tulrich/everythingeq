#include "EverythingEQ.h"
#include "IPlug_include_in_plug_src.h"

#include "ui/LevelMeter.h"

#include <algorithm>
#include <cmath>
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
    IRECT b = pGraphics->GetBounds().GetPadded(-20.f);

    // Input (and sidechain) meters on the left, output meter on the right.
    constexpr float kMeterW = 64.f;
    mInputMeter = new LevelMeter(b.ReduceFromLeft(kMeterW), "In", {"L", "R"});
    b.ReduceFromLeft(8.f);
    mSidechainMeter = new LevelMeter(b.ReduceFromLeft(kMeterW), "SC", {"L", "R"});
    mOutputMeter = new LevelMeter(b.ReduceFromRight(kMeterW), "Out", {"L", "R"});
    pGraphics->AttachControl(mInputMeter);
    pGraphics->AttachControl(mSidechainMeter);
    pGraphics->AttachControl(mOutputMeter);

    b.Pad(-20.f);
    const IRECT header = b.ReduceFromTop(40.f);
    const IRECT footer = b.ReduceFromBottom(30.f);
    const IVStyle style = DEFAULT_STYLE.WithColor(kFG, COLOR_WHITE.WithOpacity(0.3f));
    pGraphics->AttachControl(new ITextControl(header, "Everything EQ " PLUG_VERSION_STR " (spike build)",
                                              IText(24.f, COLOR_WHITE, "Roboto-Regular", EAlign::Near)));
    pGraphics->AttachControl(new ITextControl(footer, "", IText(14.f, COLOR_LIGHT_GRAY, "Roboto-Regular", EAlign::Near)),
                             kCtrlTagLayout);
    pGraphics->AttachControl(new IVKnobControl(b.GetCentredInside(120.f, 160.f), kGain, "Output Gain", style));
    mLayoutChanged = true;
  };
#endif
}

void EverythingEQ::OnIdle()
{
#if IPLUG_EDITOR
  IGraphics* pGraphics = GetUI();
  if (!pGraphics || !mInputMeter)
    return;

  float peaks[kNumMeterChannels];
  for (int i = 0; i < kNumMeterChannels; ++i)
    peaks[i] = mPeaks[i].exchange(0.f, std::memory_order_relaxed);
  mInputMeter->AddPeaks(peaks);
  mSidechainMeter->AddPeaks(peaks + 2);
  mOutputMeter->AddPeaks(peaks + 4);

  if (mTransportStarted.exchange(false))
  {
    mInputMeter->ResetClip();
    mSidechainMeter->ResetClip();
    mOutputMeter->ResetClip();
  }

  if (mLayoutChanged.exchange(false))
  {
    mInputMeter->SetChannelActive(0, mInConnected[0]);
    mInputMeter->SetChannelActive(1, mInConnected[1]);
    mSidechainMeter->SetChannelActive(0, mInConnected[2]);
    mSidechainMeter->SetChannelActive(1, mInConnected[3]);
    mOutputMeter->SetChannelActive(0, mOutConnected[0]);
    mOutputMeter->SetChannelActive(1, mOutConnected[1]);

    const int nIn = (mInConnected[0] ? 1 : 0) + (mInConnected[1] ? 1 : 0);
    const int nSc = (mInConnected[2] ? 1 : 0) + (mInConnected[3] ? 1 : 0);
    const int nOut = (mOutConnected[0] ? 1 : 0) + (mOutConnected[1] ? 1 : 0);
    WDL_String host, str;
    GetHostStr(host);
    str.SetFormatted(256, "Layout: %d in, %d sidechain, %d out   |   host: %s   |   latency: %d samples",
                     nIn, nSc, nOut, host.Get(), GetLatency());
    if (auto* pText = pGraphics->GetControlWithTag(kCtrlTagLayout)->As<ITextControl>())
      pText->SetStr(str.Get());
  }
#endif
}

void EverythingEQ::OnUIClose()
{
#if IPLUG_EDITOR
  mInputMeter = mSidechainMeter = mOutputMeter = nullptr;  // owned and deleted by IGraphics
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

void EverythingEQ::AccumulatePeak(int meterCh, const sample* pData, int nFrames)
{
  float peak = 0.f;
  for (int s = 0; s < nFrames; ++s)
    peak = std::max(peak, static_cast<float>(std::fabs(pData[s])));
  float prev = mPeaks[meterCh].load(std::memory_order_relaxed);
  while (peak > prev && !mPeaks[meterCh].compare_exchange_weak(prev, peak, std::memory_order_relaxed)) {}
}

void EverythingEQ::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  UpdateConnections();

  const bool running = GetTransportIsRunning();
  if (running && !mTransportWasRunning)
    mTransportStarted = true;
  mTransportWasRunning = running;

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

  if (mInConnected[0]) AccumulatePeak(0, inputs[0], nFrames);
  if (mainR) AccumulatePeak(1, inputs[1], nFrames);
  if (scL) AccumulatePeak(2, inputs[2], nFrames);
  if (scR) AccumulatePeak(3, inputs[3], nFrames);

  mEngine.process(in, (scL || scR) ? sc : nullptr, out, nFrames);

  const double gain = GetParam(kGain)->DBToAmp();
  const int nOut = NOutChansConnected();
  for (int c = 0; c < nOut; ++c)
  {
    for (int s = 0; s < nFrames; ++s)
      outputs[c][s] = out[c][s] * gain;
    AccumulatePeak(4 + c, outputs[c], nFrames);
  }
}
#endif  // IPLUG_DSP
