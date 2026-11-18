#ifndef EVERYTHINGEQ_DSP_ENGINE_GAIN_MATCH_H_
#define EVERYTHINGEQ_DSP_ENGINE_GAIN_MATCH_H_

#include <array>

#include "dsp/bands/band_processor.h"

namespace eeq {

// Gain Match, a listening aid for fair comparisons: measures the K-weighted
// loudness of the input and of the output over a few seconds and scales the
// output by the difference, so the processed sound plays as loud as the
// input (and as loud as bypass).  Each A/B slot keeps its own output
// measurement, so switching slots plays matched at once.
class GainMatch {
 public:
  static constexpr double kMaxDb = 24.0;  // The largest correction.
  static constexpr double kTauSeconds = 3.0;
  static constexpr double kGlideSeconds = 0.05;
  // Blocks quieter than this (K-weighted mean square) aren't measured.
  static constexpr double kSilencePower = 1e-7;  // -70 dB.

  void Prepare(double sample_rate);
  // Forgets the measurements (the gain glides back to unity).
  void Reset();

  // The A/B slot (0 or 1) the output now comes from.  A slot not measured
  // yet starts from the current slot's measurement.
  void SelectSlot(int slot);

  // Measures in and out (stereo, n samples) and scales out in place.  While
  // `hold` (bypass or a listen mode) the measurement pauses and the gain
  // glides to unity; disabled, the gain glides to unity and turning it on
  // again starts a new measurement.
  void Process(const float* const in[2], float* const out[2], int n,
               bool enabled, bool hold);

  // The gain (dB) applied at the end of the last block.
  double gain_db() const;

 private:
  double MeanSquare(BandProcessor* k, const float* const x[2], int n);

  double fs_ = 48000.0;
  // K-weighting: input L/R, output L/R.
  std::array<BandProcessor, 2> k_in_;
  std::array<BandProcessor, 2> k_out_;
  // Exponential averages started from zero, and how far each has risen
  // (1 - exp(-t / tau)), which unbiases them: power / weight.
  double in_power_ = 0.0, in_weight_ = 0.0;
  std::array<double, 2> out_power_{}, out_weight_{};
  int slot_ = 0;
  bool was_enabled_ = false;
  double gain_ = 1.0;
  double glide_ = 1.0;  // Per-sample one-pole coefficient.
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_ENGINE_GAIN_MATCH_H_
