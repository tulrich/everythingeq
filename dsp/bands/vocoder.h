#ifndef EVERYTHINGEQ_DSP_BANDS_VOCODER_H_
#define EVERYTHINGEQ_DSP_BANDS_VOCODER_H_

#include <array>

#include "dsp/bands/band_design.h"
#include "dsp/dynamics/spectral_envelope.h"
#include "dsp/engine/params.h"

namespace eeq {

// The Vocoder band type (ABOUT.md section 3.A): a graphic EQ with a bell
// at each of a Spectrum group's analysis bands, whose gains follow that
// group's spectral envelope (e.g. a vocal on the sidechain).  A Dyn
// assignment with Target Shape connects the group; its Dyn Range is the
// Amount.

inline constexpr int kVocoderBands = kSpectralBands;

// Per-band gains in dB (0 = flat).
using VocoderShape = std::array<float, kVocoderBands>;

// Builds the synthesis bank's static tables; call before the audio thread
// designs a Vocoder.
void PrepareVocoderTables();

// Adds one Shape assignment's gains to `shape` (unclamped; the band clamps
// the sum to its Range).  Amount is a ratio (1 = 100%); negative inverts.
//   Shape: each band moves by Amount x how far the source sits above or
//          below its own mean there, faded in with the source's level, so
//          the instrument takes on the source's spectral shape (vowels and
//          consonants alike) while keeping its own level.
//   Level: each band moves by Amount x how far the source's level there is
//          over the group's threshold (as its trigger computes it), so a
//          negative Amount ducks the instrument where the source is loud.
void AddVocoderShape(const SpectralEnvelope& env, const GroupParams& gp,
                     VocoderMode mode, double amount, VocoderShape& shape);

// The synthesis bank: kVocoderBands bells at the analysis centers times
// `shift` (formant shift), with the shape's gains, plus a broadband gain.
BandDesign DesignVocoder(double shift, double gain_db,
                         const VocoderShape& shape, double sample_rate);

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_BANDS_VOCODER_H_
