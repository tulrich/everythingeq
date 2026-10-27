#ifndef EVERYTHINGEQ_DSP_ENGINE_ENGINE_H_
#define EVERYTHINGEQ_DSP_ENGINE_ENGINE_H_

#include <array>

#include "dsp/bands/band_processor.h"
#include "dsp/dynamics/dynamics_group.h"
#include "dsp/engine/delay_line.h"
#include "dsp/engine/params.h"

namespace eeq {

// The complete Everything EQ signal path for one plugin instance.
//
// Thread model: Prepare(), SetParams() and Process() are called from the
// audio thread (or with the audio thread stopped).  Process() does not
// allocate, lock or block.
class Engine {
 public:
  // Crossfade time for global bypass and FadeInFromDry().
  static constexpr double kBypassFadeMs = 4.0;

  // Allocates.  Must be called before Process() and on sample-rate change.
  void Prepare(double sample_rate);
  void Reset();
  // Starts from the (delayed) dry signal and crossfades to the current
  // output, e.g. after a host reset, so filters warming up from silence
  // don't click.
  void FadeInFromDry();

  // Takes effect at the next control sub-block.
  void SetParams(const EngineParams& p);
  const EngineParams& params() const { return params_; }

  // Stereo in/out.  sidechain may be null (treated as silence).
  // in and out may alias.
  void Process(const float* const* in, const float* const* sidechain,
               float* const* out, int num_samples);

  // Reported plugin latency (lookahead), in samples.
  int latency_samples() const { return latency_; }

  double sample_rate() const { return fs_; }
  const DynamicsGroup& group(int g) const { return groups_[g]; }
  DynamicsGroup& mutable_group(int g) { return groups_[g]; }
  const BandSlot& band(int b) const { return slots_[b]; }

 private:
  using Frame = std::array<double, kNumChannels>;

  int BypassFadeSamples() const;
  // Clears the band chain's filter state and snaps its designs to the
  // current detector state.
  void ResetBands();
  // Runs every group's detector on one frame of undelayed input.
  void ProcessDetectors(const Frame& dry, const Frame& sc);
  void ControlUpdate(bool snap_now);
  void ApplyStaticDesigns(bool snap_now);
  Frame DetectorInputFrame(int g, const Frame& dry, const Frame& sc);
  void ProcessChain(const Frame& chain_in, Frame& main, Frame& aux, Frame& solo,
                    bool& any_solo);
  Frame Encode(const Frame& main, const Frame& aux) const;

  double fs_ = 48000.0;
  bool prepared_ = false;
  EngineParams params_{};
  bool params_dirty_ = true;
  int latency_ = 0;
  int control_phase_ = 0;

  std::array<BandSlot, kNumBands> slots_{};
  std::array<DetectorFilter, kNumBands> band_detectors_{};
  std::array<DetectorFilter, kNumBands> solo_filters_{};
  std::array<DynamicsGroup, kNumGroups> groups_{};
  std::array<DelayLine, kNumChannels> delay_{};

  LinearRamp bypass_mix_;
  LinearRamp output_gain_;
  bool bypass_state_ = false;
  // Fully bypassed: only the detectors and the delay line run; the band
  // chain restarts from reset state when bypass is released.
  bool idle_bypass_ = false;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_ENGINE_ENGINE_H_
