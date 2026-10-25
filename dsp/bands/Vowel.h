#pragma once

#include <array>

namespace eeq {

inline constexpr double kVowelMorphMax = 4.0;
inline constexpr double kTractScaleMin = 0.70;
inline constexpr double kTractScaleMax = 1.40;
inline constexpr std::array<double, 3> kFormantQ = {5.0, 8.0, 10.0};

struct Formants {
    std::array<double, 3> hz;
    std::array<double, 3> weightDb;  // natural relative weights
};

// Interpolate the vowel table (OO, OH, AH, EH, EE at 0..4).  Frequencies are
// interpolated in log space, weights in dB.
Formants vowelFormants(double morph);

}  // namespace eeq
