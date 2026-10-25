#pragma once

#include <array>

namespace eeq {

inline constexpr int kNumBands = 10;
inline constexpr int kNumGroups = 4;
inline constexpr int kNumChannels = 2;
inline constexpr int kControlBlock = 16;      // samples per control sub-block
inline constexpr double kMaxLookaheadMs = 10.0;

enum class BandType {
    Off,
    Peak,
    LowShelf,
    HighShelf,
    LP12,
    HP12,
    Notch,
    BP,
    LP24_LR,
    HP24_LR,
    Tilt,
    FletcherMunson,
    Vowel,
    KWeight,
};

enum class DynTarget { Gain, Freq, Morph };
enum class Tap { Main, Aux };
enum class Chan { Stereo, Left, Right, Mid, Side };
enum class VowelWeighting { Natural, Flat };

enum class DetectorInput { InternalDry, InternalBandSum, ExternalSC };
enum class ScFilter { Off, BP, LP, HP, Tilt, KWeight };
enum class TriggerMode { Above, Below };
enum class OutputMode { Stereo, SplitLR, SplitMS };

// Physical (unnormalized) band parameters.  Meaning of x/y/z by type:
//   Peak, LowShelf, HighShelf : x = Hz, y = gain dB, z = Q
//   LP12, HP12, Notch          : x = Hz, z = Q (y unused)
//   BP                         : x = Hz, y = gain dB (<= kBpOffDb is silence), z = Q
//   LP24_LR, HP24_LR           : x = Hz (y, z unused)
//   Tilt                       : x = pivot Hz, y = high-minus-low gain dB
//   FletcherMunson             : y = phon delta (positive = boost lows/highs)
//   Vowel                      : x = morph index 0..4, y = formant gain dB, z = tract scale
//   KWeight                    : fixed (x, y, z unused)
struct BandParams {
    BandType type = BandType::Off;
    double x = 1000.0;
    double y = 0.0;
    double z = 0.70710678118654752440;
    int option = 0;  // Vowel: VowelWeighting
    int dynGroup = -1;  // -1 = static, else 0..kNumGroups-1
    DynTarget dynTarget = DynTarget::Gain;
    double dynRange = 0.0;  // dB, octaves or morph index (per dynTarget)
    Tap tap = Tap::Main;
    Chan chan = Chan::Stereo;
    bool bypass = false;
    bool solo = false;
};

struct GroupParams {
    DetectorInput input = DetectorInput::InternalDry;
    ScFilter scFilter = ScFilter::Off;
    double scHz = 1000.0;
    double scQ = 0.70710678118654752440;
    bool scListen = false;
    double rmsWindowMs = 15.0;
    double alpha = 0.0;
    double link = 1.0;          // 0..1
    double thresholdDb = -20.0; // dBFS, used at alpha = 0
    double riseDb = 6.0;        // dB above average, used at alpha = 1
    double kneeDb = 0.0;
    TriggerMode mode = TriggerMode::Above;
    double attackMs = 5.0;
    double holdMs = 0.0;
    double releaseMs = 100.0;
};

struct GlobalParams {
    OutputMode outputMode = OutputMode::Stereo;
    double globalScale = 1.0;
    bool delta = false;
    bool bypass = false;
    double outputGainDb = 0.0;
    double lookaheadMs = 0.0;
};

struct EngineParams {
    std::array<BandParams, kNumBands> bands{};
    std::array<GroupParams, kNumGroups> groups{};
    GlobalParams global{};
};

inline constexpr double kBpOffDb = -60.0;

inline bool isFilterType(BandType t) {
    return t == BandType::LP12 || t == BandType::HP12 || t == BandType::BP || t == BandType::Notch;
}
inline bool isCrossoverType(BandType t) {
    return t == BandType::LP24_LR || t == BandType::HP24_LR;
}

}  // namespace eeq
