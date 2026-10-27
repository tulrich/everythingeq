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
};

// Stateless description of one band (one channel's coefficients).
struct BandDesign {
  BandTopology topology = BandTopology::kPassthrough;
  int num_cells = 0;
  std::array<SvfCoeffs, 4> cells{};
  double post_gain = 1.0;
  bool crossover_high = false;  // HP24_LR: y is the high output
};

// Legal parameter ranges (ABOUT.md section 3.E).
inline constexpr double kMinFreq = 10.0;
inline constexpr double kMaxFreq = 22000.0;
inline constexpr double kMinQ = 0.1;
inline constexpr double kMaxQ = 40.0;
inline constexpr double kMaxGainDb = 30.0;
inline constexpr double kMaxTiltDb = 24.0;
inline constexpr double kMaxPhonDelta = 40.0;
inline constexpr double kTiltShelfQ = 0.5;
inline constexpr double kMinWidthOct = 0.1;
inline constexpr double kMaxWidthOct = 10.0;

BandDesign DesignBand(BandType type, double x, double y, double z, int option,
                      double sample_rate);

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
