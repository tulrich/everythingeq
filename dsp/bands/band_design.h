#ifndef EVERYTHINGEQ_DSP_BANDS_BAND_DESIGN_H_
#define EVERYTHINGEQ_DSP_BANDS_BAND_DESIGN_H_

#include <array>
#include <complex>

#include "dsp/engine/params.h"
#include "dsp/svf/svf.h"

namespace eeq {

enum class BandTopology {
  kPassthrough,  // y = x
  kSerial,       // cells in series, then post_gain
  // LR4: cell 0 is the Butterworth split, cell 1 the LP on the low output,
  // cell 2 the HP on the high output.
  kCrossover,
  // y = comb_dry * x + comb_wet * c, c[n] = x[n] + comb_sign * comb_g *
  // c[n - D]: teeth where comb_sign * z^-D = 1.
  kComb,
};

// The most SVF cells a band runs (a Vocoder's bank).
inline constexpr int kMaxBandCells = 16;

// Stateless description of one band (one channel's coefficients).
struct BandDesign {
  BandTopology topology = BandTopology::kPassthrough;
  int num_cells = 0;
  std::array<SvfCoeffs, kMaxBandCells> cells{};
  double post_gain = 1.0;
  bool crossover_high = false;  // HP at 24 dB/oct: y is the high output
  // Comb topology.
  double comb_delay = 0.0;  // D, samples
  double comb_g = 0.0;      // feedback, 0 <= g < 1
  double comb_sign = 1.0;   // -1: teeth on odd multiples of fs / (2 D)
  double comb_dry = 1.0;
  double comb_wet = 0.0;
  double comb_slew = 1.0;  // one-pole coefficient smoothing D per sample
};

// Legal parameter ranges (ABOUT.md section 3.E).
inline constexpr double kMinFreq = 10.0;
inline constexpr double kMaxFreq = 22000.0;
inline constexpr double kMinQ = 0.1;
inline constexpr double kMaxQ = 40.0;
inline constexpr double kMaxGainDb = 30.0;
inline constexpr double kMaxTiltDb = 24.0;
inline constexpr double kMaxPhonDelta = 40.0;
inline constexpr double kTiltShelfQ = 0.6;  // Monotonic at every width.
// Vintage shapes (DesignBand).
inline constexpr double kVintageBoostQ = 0.9;
inline constexpr double kVintageAttenQ = 0.45;
// Comb: f0 range (the delay line holds 1 / kCombMinHz), tooth Q range, and
// how fast the delay follows f0 (no clicks when a tracked pitch jumps).
inline constexpr double kCombMinHz = 20.0;
inline constexpr double kCombMaxHz = 8000.0;
inline constexpr double kCombMinQ = 4.0;
inline constexpr double kCombMaxQ = 200.0;
inline constexpr double kCombGlideMs = 15.0;
// Comb feedback for a tooth Q: Q = pi / (1 - g).
double CombFeedback(double q);
inline constexpr double kVintageLowAttenRatio = 5.0;
inline constexpr double kVintageHighBoostQ = 0.7;
inline constexpr int kTiltCells = 4;
inline constexpr double kMinWidthOct = 0.1;
inline constexpr double kMaxWidthOct = 10.0;

BandDesign DesignBand(BandType type, double x, double y, double z, int option,
                      double sample_rate);

// ITU-R BS.1770 K-weighting (a +4 dB high shelf and a 38 Hz high-pass): the
// group SC filter's K-Weight.
inline constexpr double kKWeightShelfHz = 1681.974450955533;
inline constexpr double kKWeightShelfDb = 3.999843853973347;
inline constexpr double kKWeightShelfQ = 0.7071752369554196;
inline constexpr double kKWeightHpHz = 38.13547087602444;
inline constexpr double kKWeightHpQ = 0.5003270373238773;
BandDesign DesignKWeighting(double sample_rate);
// The K-weighting shelf as this EQ's High Shelf band (within 0.01 dB; the
// High Shelf's frequency sits at a different point on the slope).
inline constexpr double kKWeightHighShelfHz = 1500.0;
inline constexpr double kKWeightHighShelfQ = 0.71;

// A fixed two-shelf tilt (Q 0.5 shelves at hz): the group SC filter's Tilt.
BandDesign DesignShelfTilt(double hz, double tilt_db, double sample_rate);

// Response of the band's main output y.
std::complex<double> BandResponse(const BandDesign& d, double freq_hz,
                                  double sample_rate);

// Response of the complementary crossover output (Crossover topology only).
std::complex<double> BandComplementResponse(const BandDesign& d, double freq_hz,
                                            double sample_rate);

// Detector / solo filter for a band type (ABOUT.md section 3.B): unit-gain
// bandpass, lowpass, highpass, a parallel sum of bandpasses (Vowel), or none.
struct DetectorDesign {
  int num_cells = 0;  // 0 = full band (passthrough)
  std::array<SvfCoeffs, 3> cells{};
};

DetectorDesign DesignDetector(BandType type, double x, double z,
                              double sample_rate);

std::complex<double> DetectorResponse(const DetectorDesign& d, double freq_hz,
                                      double sample_rate);

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_BANDS_BAND_DESIGN_H_
