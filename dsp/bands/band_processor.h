#ifndef EVERYTHINGEQ_DSP_BANDS_BAND_PROCESSOR_H_
#define EVERYTHINGEQ_DSP_BANDS_BAND_PROCESSOR_H_

#include <array>

#include "dsp/bands/band_design.h"
#include "dsp/common/math.h"
#include "dsp/engine/params.h"
#include "dsp/svf/svf.h"

namespace eeq {

// Linear ramp for a scalar gain.
class LinearRamp {
 public:
  void Snap(double v) { cur_ = target_ = v, remaining_ = 0; }
  void SetTarget(double v, int samples) {
    target_ = v;
    if (samples <= 1) return Snap(v);
    step_ = (v - cur_) / samples;
    remaining_ = samples;
  }
  double Next() {
    if (remaining_ > 0) cur_ = --remaining_ == 0 ? target_ : cur_ + step_;
    return cur_;
  }
  double value() const { return cur_; }

 private:
  double cur_ = 0.0, target_ = 0.0, step_ = 0.0;
  int remaining_ = 0;
};

struct BandOutput {
  double y;     // main band output
  double comp;  // complementary crossover output (Crossover topology only)
};

// One channel of a band: up to 4 SVF cells with coefficient ramps.
class BandProcessor {
 public:
  void Reset();
  // Apply a design.  If the topology or cell count changes, the processor
  // snaps (and should be crossfaded by the caller).
  void Apply(const BandDesign& d, int ramp_samples);
  BandOutput Process(double x);
  BandTopology topology() const { return topology_; }

 private:
  BandTopology topology_ = BandTopology::kPassthrough;
  int num_cells_ = 0;
  bool crossover_high_ = false;
  std::array<SvfCell, 4> cells_{};
  LinearRamp post_;
};

// Static band parameters resolved for one channel after dynamics.
struct ResolvedBand {
  double x = 1000.0, y = 0.0, z = kButterworthQ;
  int option = 0;
};

// A stereo band slot.  Handles dirty-checking, coefficient ramps, and
// crossfading when the band type changes.
class BandSlot {
 public:
  static constexpr int kFadeSamples = 64;

  void Prepare(double sample_rate);
  void Reset();
  // Called once per control sub-block.  `snap_now` jumps without ramping.
  void Update(BandType type, const std::array<ResolvedBand, kNumChannels>& p,
              bool snap_now);
  // Process one channel sample.  Call Advance() once per frame afterwards.
  BandOutput Process(int ch, double x);
  void Advance() {
    if (fade_ > 0) --fade_;
  }
  BandType type() const { return type_; }
  bool IsFading() const { return fade_ > 0; }
  const BandDesign& design(int ch) const { return design_[ch]; }

 private:
  static bool Changed(const ResolvedBand& a, const ResolvedBand& b);

  double fs_ = 48000.0;
  BandType type_ = BandType::kOff;
  bool initialized_ = false;
  std::array<ResolvedBand, kNumChannels> last_{};
  std::array<BandDesign, kNumChannels> design_{};
  std::array<BandProcessor, kNumChannels> cur_{};
  std::array<BandProcessor, kNumChannels> prev_{};
  int fade_ = 0;
};

// Stereo detector / solo filter (parallel sum of up to 3 cells).
class DetectorFilter {
 public:
  void Reset();
  void Apply(const DetectorDesign& d, int ramp_samples);
  double Process(int ch, double x);

 private:
  int num_cells_ = 0;
  std::array<std::array<SvfCell, 3>, kNumChannels> cells_{};
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_BANDS_BAND_PROCESSOR_H_
