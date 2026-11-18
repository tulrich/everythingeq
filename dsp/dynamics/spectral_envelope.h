#ifndef EVERYTHINGEQ_DSP_DYNAMICS_SPECTRAL_ENVELOPE_H_
#define EVERYTHINGEQ_DSP_DYNAMICS_SPECTRAL_ENVELOPE_H_

#include <array>

#include "dsp/svf/svf.h"

namespace eeq {

// A group's Detect = Spectrum analysis (ABOUT.md section 3.B): a band-pass
// bank (two cascaded cells per band: -3 dB where neighbors cross, about
// -37 dB two octaves out) with a power envelope follower per band, giving
// the detector input's spectral envelope.  Vocoder bands take on its shape;
// other bands read it at their own frequency.

inline constexpr int kSpectralBands = 16;
inline constexpr double kSpectralLoHz = 100.0;
inline constexpr double kSpectralHiHz = 8000.0;
// The input fades in between these levels (sum of the bands, dBFS), so a
// Vocoder leaves the instrument flat in silence.
inline constexpr double kSpectralGateLoDb = -60.0;
inline constexpr double kSpectralGateHiDb = -45.0;

// Center of band i (log-spaced from kSpectralLoHz to kSpectralHiHz), and
// the spacing in octaves.
double SpectralBandHz(int i);
double SpectralSpacingOctaves();

// Band levels in dB, for display.
using SpectralLevels = std::array<float, kSpectralBands>;

// Mono.  Prepare() allocates nothing; Process() runs per sample.
class SpectralEnvelope {
 public:
  static constexpr double kFloorDb = -120.0;
  // Levels more than this far below the loudest band count as this far, in
  // MeanDb().
  static constexpr double kMeanFloorDb = 60.0;

  void Prepare(double sample_rate);
  void Reset();
  void SetBallistics(double attack_ms, double release_ms);
  void Process(double x);

  // Band i's level, dB (a sine at its center reads as the level-mode
  // trigger would).
  double BandDb(int i) const;
  // The bands' total, dB.
  double LevelDb() const;
  // 0 (silent) .. 1 (above kSpectralGateHiDb).
  double Presence() const;
  // The bands' mean level, dB, each floored at kMeanFloorDb below the
  // loudest.
  double MeanDb() const;
  // The level at hz, interpolated smoothly between the band centers (held
  // beyond the ends).
  double DbAt(double hz) const;
  // The loudest point of the interpolated envelope between lo_hz and hi_hz
  // (within the bank's span; the window's center if they don't overlap).
  double PeakHz(double lo_hz, double hi_hz) const;
  SpectralLevels Levels() const;

 private:
  double DbAtPosition(double p) const;  // p in band indices.

  double fs_ = 48000.0;
  std::array<std::array<SvfCell, 2>, kSpectralBands> cells_{};
  std::array<double, kSpectralBands> env_{};
  double attack_ = 1.0, release_ = 1.0;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_DYNAMICS_SPECTRAL_ENVELOPE_H_
