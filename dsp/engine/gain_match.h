#ifndef EVERYTHINGEQ_DSP_ENGINE_GAIN_MATCH_H_
#define EVERYTHINGEQ_DSP_ENGINE_GAIN_MATCH_H_

#include <array>

#include "dsp/bands/band_processor.h"

namespace eeq {

// Gain Match, a one-shot measurement for fair comparisons: the K-weighted
// loudness of the input against the output over a few seconds of playing
// audio.  The plugin then moves Output Gain by the difference, so the
// processed sound plays as loud as the input (and as loud as bypass).
class GainMatch {
 public:
  static constexpr double kSeconds = 3.0;  // Of audio that counts.
  // Blocks quieter than this (K-weighted mean square) don't count.
  static constexpr double kSilencePower = 1e-7;  // -70 dB.
  static constexpr double kMaxDb = 60.0;         // Limits correction_db().

  void Prepare(double sample_rate);

  void Start();
  void Cancel() { running_ = false; }
  bool running() const { return running_; }
  // How much of the measurement is done (0 to 1).
  double progress() const;

  // Measures one block (stereo, n samples) while running.  Silent blocks
  // and blocks with `hold` (bypass or a listen mode) don't count.  Returns
  // true when this block completes the measurement.
  bool Process(const float* const in[2], const float* const out[2], int n,
               bool hold);

  // The gain (dB) that brings the output to the input's loudness, from the
  // last completed measurement.
  double correction_db() const { return correction_db_; }

 private:
  double MeanSquare(BandProcessor* k, const float* const x[2], int n);

  double fs_ = 48000.0;
  // K-weighting: input L/R, output L/R.
  std::array<BandProcessor, 2> k_in_;
  std::array<BandProcessor, 2> k_out_;
  BandDesign k_design_;
  bool running_ = false;
  double in_sum_ = 0.0, out_sum_ = 0.0;  // Of mean squares times samples.
  double counted_ = 0.0;                 // Samples.
  double correction_db_ = 0.0;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_ENGINE_GAIN_MATCH_H_
