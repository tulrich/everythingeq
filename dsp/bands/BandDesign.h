#pragma once

#include <array>
#include <complex>

#include "engine/Params.h"
#include "svf/Svf.h"

namespace eeq {

enum class BandTopology {
    Passthrough,  // y = x
    Serial,       // cells in series, then postGain
    Crossover,    // LR4: cell0 Butterworth split, cell1 = LP on low, cell2 = HP on high
};

// Stateless description of one band (one channel's coefficients).
struct BandDesign {
    BandTopology topology = BandTopology::Passthrough;
    int numCells = 0;
    std::array<SvfCoeffs, 4> cells{};
    double postGain = 1.0;
    bool crossoverHigh = false;  // HP24_LR: y is the high output
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

BandDesign designBand(BandType type, double x, double y, double z, int option, double sampleRate);

// Response of the band's main output y.
std::complex<double> bandResponse(const BandDesign& d, double freqHz, double sampleRate);

// Response of the complementary crossover output (Crossover topology only).
std::complex<double> bandComplementResponse(const BandDesign& d, double freqHz, double sampleRate);

// Detector / solo filter for a band type (ABOUT.md section 3.B): unit-gain
// bandpass, lowpass, highpass, a parallel sum of bandpasses (Vowel), or none.
struct DetectorDesign {
    int numCells = 0;  // 0 = full band (passthrough)
    std::array<SvfCoeffs, 3> cells{};
};

DetectorDesign designDetector(BandType type, double x, double z, double sampleRate);

std::complex<double> detectorResponse(const DetectorDesign& d, double freqHz, double sampleRate);

}  // namespace eeq
