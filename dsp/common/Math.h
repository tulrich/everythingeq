#pragma once

#include <algorithm>
#include <cmath>

namespace eeq {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kLn2 = 0.69314718055994530942;
inline constexpr double kButterworthQ = 0.70710678118654752440;

inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }
inline double gainToDb(double g) { return 20.0 * std::log10(std::max(g, 1e-30)); }

// Clamp a frequency into the range the SVF designs handle well.
inline double clampFreq(double hz, double sampleRate) {
    return std::clamp(hz, 5.0, 0.49 * sampleRate);
}

// One-pole smoothing coefficient for a time constant in seconds.
inline double onePoleCoeff(double tauSeconds, double sampleRate) {
    if (tauSeconds <= 0.0) return 1.0;
    return 1.0 - std::exp(-1.0 / (tauSeconds * sampleRate));
}

}  // namespace eeq
