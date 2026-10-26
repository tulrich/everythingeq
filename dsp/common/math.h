#ifndef EVERYTHINGEQ_DSP_COMMON_MATH_H_
#define EVERYTHINGEQ_DSP_COMMON_MATH_H_

#include <algorithm>
#include <cmath>

namespace eeq {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kLn2 = 0.69314718055994530942;
inline constexpr double kButterworthQ = 0.70710678118654752440;

inline double DbToGain(double db) { return std::pow(10.0, db / 20.0); }
inline double GainToDb(double g) {
  return 20.0 * std::log10(std::max(g, 1e-30));
}

// Clamp a frequency into the range the SVF designs handle well.
inline double ClampFreq(double hz, double sample_rate) {
  return std::clamp(hz, 5.0, 0.49 * sample_rate);
}

// One-pole smoothing coefficient for a time constant in seconds.
inline double OnePoleCoeff(double tau_seconds, double sample_rate) {
  if (tau_seconds <= 0.0) return 1.0;
  return 1.0 - std::exp(-1.0 / (tau_seconds * sample_rate));
}

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_COMMON_MATH_H_
