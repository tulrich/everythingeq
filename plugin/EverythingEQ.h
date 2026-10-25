#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "ISender.h"

#include "engine/Engine.h"

#include <array>
#include <vector>

const int kNumPresets = 1;

enum EParams
{
  kGain = 0,
  kNumParams
};

enum ECtrlTags
{
  kCtrlTagInputMeter = 0,
  kCtrlTagOutputMeter,
  kCtrlTagLayout,
  kNumCtrlTags
};

using namespace iplug;
using namespace igraphics;

// Phase 0 spike: a gain plugin with a sidechain input that runs audio through
// eeq::Engine at its default (pass-through) settings.  It exists to prove out
// the formats, channel layouts and host integration before the real
// parameter set and UI arrive.
class EverythingEQ final : public Plugin
{
public:
  EverythingEQ(const InstanceInfo& info);

  void OnIdle() override;
#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnActivate(bool enable) override;
  void GetBusName(ERoute direction, int busIdx, int nBuses, WDL_String& str) const override;
#endif

private:
  static constexpr int kMaxIn = 4;   // main L/R + sidechain L/R
  static constexpr int kMaxOut = 2;

  void UpdateConnections();

  eeq::Engine mEngine;
  std::array<std::vector<float>, 6> mScratch;  // in L/R, sc L/R, out L/R

  std::array<bool, kMaxIn> mInConnected{};
  std::array<bool, kMaxOut> mOutConnected{};
  bool mLayoutChanged = true;

  IPeakAvgSender<kMaxIn> mInputPeakSender;
  IPeakAvgSender<kMaxOut> mOutputPeakSender;
};
