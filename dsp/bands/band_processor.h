#ifndef EVERYTHINGEQ_DSP_BANDS_BAND_PROCESSOR_H_
#define EVERYTHINGEQ_DSP_BANDS_BAND_PROCESSOR_H_

#include <array>
#include <vector>

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
  bool settled() const { return remaining_ == 0; }

 private:
  double cur_ = 0.0, target_ = 0.0, step_ = 0.0;
  int remaining_ = 0;
};

struct BandOutput {
  double y;     // main band output
  double comp;  // complementary crossover output (Crossover topology only)
};

// One channel of a band: up to kMaxBandCells SVF cells with coefficient
// ramps, or a
// comb on a delay line the owner provides (copies share it).
class BandProcessor {
 public:
  // The Comb topology's delay line: `size` (a power of two) samples, at
  // least kCombMinHz's period plus 4.
  void SetCombBuffer(double* buffer, int size) {
    comb_buf_ = buffer;
    comb_mask_ = size - 1;
  }
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
  std::array<SvfCell, kMaxBandCells> cells_{};
  LinearRamp post_;
  // Comb.
  double ReadComb(double delay) const;
  double* comb_buf_ = nullptr;
  int comb_mask_ = 0;
  int comb_w_ = 0;
  double comb_d_ = 2.0, comb_d_target_ = 2.0;
  double comb_slew_ = 1.0, comb_sign_ = 1.0;
  LinearRamp comb_g_, comb_dry_, comb_wet_;
};

// Static band parameters resolved for one channel after dynamics.
struct ResolvedBand {
  double x = 1000.0, y = 0.0, z = kButterworthQ;
  int option = 0;
  double makeup_db = 0.0;  // Broadband gain from Dyn Makeup.
};

// What Global Scale multiplies a band's static gain (Y) by: global_scale
// for types with a gain, 1 for the rest.
double GainScaleFor(BandType type, double global_scale);

// Whether Global Scale scales a Dyn Target's range.
inline bool IsGainTarget(DynTarget t) {
  return t == DynTarget::kGain || t == DynTarget::kShape;
}

// The frequency a band is "at": where its graph node sits and where it
// reads a Spectrum group's envelope.
double BandCenterHz(const BandParams& bp);

// The band's design for resolved parameters, including Dyn Makeup.
BandDesign DesignResolvedBand(BandType type, const ResolvedBand& rb,
                              double sample_rate);

// Applies dynamics control values (D in [0, 1]) for the band's two
// assignments (Dyn A: dyn_group/target/range; Dyn B: dyn_b_*) to its static
// parameters (ABOUT.md section 3.B, step 5): x/y/z + D * DynRange per
// assignment.  global_scale multiplies the static gain and Gain-target ranges
// only.  Shared by the engine and the UI's response curves.
ResolvedBand ResolveBand(const BandParams& bp, double control_a,
                         double control_b, double global_scale);
// Dyn A only.
inline ResolvedBand ResolveBand(const BandParams& bp, double control,
                                double global_scale) {
  return ResolveBand(bp, control, 0.0, global_scale);
}

// A stereo band slot.  Handles dirty-checking, coefficient ramps, and
// crossfading when the band type changes.
class BandSlot {
 public:
  static constexpr int kFadeSamples = 64;

  BandSlot() = default;
  // Its processors point into its comb buffers.
  BandSlot(const BandSlot&) = delete;
  BandSlot& operator=(const BandSlot&) = delete;

  void Prepare(double sample_rate);
  void Reset();
  // Called once per control sub-block.  `snap_now` jumps without ramping.
  void Update(BandType type, const std::array<ResolvedBand, kNumChannels>& p,
              bool snap_now);
  // As Update, with a design computed elsewhere (a Vocoder's), applied to
  // both channels every call.
  void UpdateDesign(BandType type, const BandDesign& d, bool snap_now);
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
  // A fresh processor for channel ch (after a type change).
  BandProcessor FreshProcessor(int ch);

  double fs_ = 48000.0;
  BandType type_ = BandType::kOff;
  bool initialized_ = false;
  std::array<ResolvedBand, kNumChannels> last_{};
  std::array<BandDesign, kNumChannels> design_{};
  std::array<BandProcessor, kNumChannels> cur_{};
  std::array<BandProcessor, kNumChannels> prev_{};
  // One delay line per channel: at most one of cur_ and prev_ is a Comb
  // (a crossfade only runs between different types).
  std::array<std::vector<double>, kNumChannels> comb_buf_;
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
