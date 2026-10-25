#pragma once

#include <array>

namespace eeq::iso226 {

inline constexpr int kNumFreqs = 29;

// ISO 226:2003 one-third-octave frequencies, 20 Hz .. 12.5 kHz.
extern const std::array<double, kNumFreqs> kFreqs;

// Sound pressure level (dB SPL) of the equal-loudness contour for `phon` at
// kFreqs[index].  Valid for roughly 20..90 phon.
double contourSpl(double phon, int index);

// Gain (dB) that restores the 80-phon spectral balance when listening
// `phonDelta` phon quieter, normalized to 0 dB at 1 kHz and expressed per
// phon:  ((L(80-20) - 60) - (L(80) - 80)) / 20.  Multiply by a phon delta
// to get a compensation curve.
double compensationPerPhon(int index);

}  // namespace eeq::iso226
