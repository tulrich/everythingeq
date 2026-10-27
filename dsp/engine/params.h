#ifndef EVERYTHINGEQ_DSP_ENGINE_PARAMS_H_
#define EVERYTHINGEQ_DSP_ENGINE_PARAMS_H_

#include <array>

namespace eeq {

inline constexpr int kNumBands = 8;
inline constexpr int kNumGroups = 4;
inline constexpr int kNumChannels = 2;
inline constexpr int kControlBlock = 16;  // samples per control sub-block
inline constexpr double kMaxLookaheadMs = 10.0;

enum class BandType {
  kOff,
  kPeak,
  kLowShelf,
  kHighShelf,
  kBandShelf,
  kLp12,
  kHp12,
  kNotch,
  kBp,
  kLp24Lr,
  kHp24Lr,
  kTilt,
  kFletcherMunson,
  kVowel,
  kKWeight,
};

enum class DynTarget { kGain, kFreq, kMorph };
enum class Tap { kMain, kAux };
enum class Chan { kStereo, kLeft, kRight, kMid, kSide };
enum class VowelWeighting { kNatural, kFlat };

enum class DetectorInput { kInternalDry, kInternalBandSum, kExternalSc };
enum class ScFilter { kOff, kBp, kLp, kHp, kTilt, kKWeight };
enum class TriggerMode { kAbove, kBelow };
enum class OutputMode { kStereo, kSplitLr, kSplitMs };

// Physical (unnormalized) band parameters.  Meaning of x/y/z by type:
//   Peak, Low/HighShelf: x = Hz, y = gain dB, z = Q
//   BandShelf:           x = center Hz (geometric mean of the edges),
//                        y = plateau gain dB, z = width in octaves
//   LP12, HP12, Notch:   x = Hz, z = Q (y unused)
//   BP:                  x = Hz, y = gain dB (<= kBpOffDb is silence), z = Q
//   LP24_LR, HP24_LR:    x = Hz (y, z unused)
//   Tilt:                x = pivot Hz, y = high-minus-low gain dB
//   FletcherMunson:      y = phon delta (positive = boost lows/highs)
//   Vowel:               x = morph index 0..4, y = formant gain dB,
//                        z = tract scale
//   KWeight:             fixed (x, y, z unused)
struct BandParams {
  BandType type = BandType::kOff;
  double x = 1000.0;
  double y = 0.0;
  double z = 0.70710678118654752440;
  int option = 0;      // Vowel: VowelWeighting
  int dyn_group = -1;  // -1 = static, else 0..kNumGroups-1
  DynTarget dyn_target = DynTarget::kGain;
  double dyn_range = 0.0;  // dB, octaves or morph index (per dyn_target)
  Tap tap = Tap::kMain;
  Chan chan = Chan::kStereo;
  bool bypass = false;
  bool solo = false;
};

struct GroupParams {
  DetectorInput input = DetectorInput::kInternalDry;
  ScFilter sc_filter = ScFilter::kOff;
  double sc_hz = 1000.0;
  double sc_q = 0.70710678118654752440;
  bool sc_listen = false;
  double rms_window_ms = 15.0;
  double alpha = 0.0;
  double link = 1.0;            // 0..1
  double threshold_db = -20.0;  // dBFS, used at alpha = 0
  double rise_db = 6.0;         // dB above average, used at alpha = 1
  double knee_db = 0.0;
  TriggerMode mode = TriggerMode::kAbove;
  double attack_ms = 5.0;
  double hold_ms = 0.0;
  double release_ms = 100.0;
};

struct GlobalParams {
  OutputMode output_mode = OutputMode::kStereo;
  double global_scale = 1.0;
  bool delta = false;
  bool bypass = false;
  double output_gain_db = 0.0;
  double lookahead_ms = 0.0;
};

struct EngineParams {
  std::array<BandParams, kNumBands> bands{};
  std::array<GroupParams, kNumGroups> groups{};
  GlobalParams global{};
};

inline constexpr double kBpOffDb = -60.0;

inline bool IsFilterType(BandType t) {
  return t == BandType::kLp12 || t == BandType::kHp12 || t == BandType::kBp ||
         t == BandType::kNotch;
}
inline bool IsCrossoverType(BandType t) {
  return t == BandType::kLp24Lr || t == BandType::kHp24Lr;
}

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_ENGINE_PARAMS_H_
