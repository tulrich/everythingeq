#ifndef EVERYTHINGEQ_DSP_BANDS_VOCODER_H_
#define EVERYTHINGEQ_DSP_BANDS_VOCODER_H_

#include <array>

#include "dsp/bands/band_design.h"
#include "dsp/svf/svf.h"

namespace eeq {

// The Vocoder band type (ABOUT.md section 3.A): a graphic EQ whose bands
// follow the spectral envelope of a group's (conditioned) detector input,
// e.g. a vocal on the sidechain.  Each band is boosted or cut by how far
// the source's level in that band sits above or below the source's mean
// across the bands, so the instrument takes on the source's spectral shape
// (vowels and consonants alike) while keeping its own level.

inline constexpr int kVocoderBands = 16;
inline constexpr double kVocoderLoHz = 100.0;
inline constexpr double kVocoderHiHz = 8000.0;
// Levels more than this far below the loudest band count as this far.
inline constexpr double kVocoderFloorDb = 60.0;
// The source fades the shape in between these levels (sum of the bands,
// dBFS), so silence leaves the instrument flat.
inline constexpr double kVocoderGateLoDb = -60.0;
inline constexpr double kVocoderGateHiDb = -45.0;

// Center of analysis band i (log-spaced from kVocoderLoHz to kVocoderHiHz).
double VocoderBandHz(int i);

// Per-band gains in dB (0 = flat).
using VocoderShape = std::array<float, kVocoderBands>;

// Analysis of the source: a band-pass bank with an envelope follower per
// band.  Mono.  Prepare() allocates nothing; Process() runs per sample.
class VocoderAnalysis {
 public:
  void Prepare(double sample_rate);
  void Reset();
  void SetBallistics(double attack_ms, double release_ms);
  void Process(double x);
  // The shape to impose: amount scales the deviations from the mean (1 =
  // as measured), each clamped to +-range_db.
  VocoderShape Shape(double amount, double range_db) const;

 private:
  double fs_ = 48000.0;
  std::array<SvfCell, kVocoderBands> cells_{};
  std::array<double, kVocoderBands> env_{};
  double attack_ = 1.0, release_ = 1.0;
};

// The synthesis bank: kVocoderBands bells at the analysis centers times
// `shift` (formant shift), with the shape's gains.
BandDesign DesignVocoder(double shift, const VocoderShape& shape,
                         double sample_rate);

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_BANDS_VOCODER_H_
