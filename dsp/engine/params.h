#ifndef EVERYTHINGEQ_DSP_ENGINE_PARAMS_H_
#define EVERYTHINGEQ_DSP_ENGINE_PARAMS_H_

#include <array>

namespace eeq {

inline constexpr int kNumBands = 8;
inline constexpr int kNumGroups = 4;
inline constexpr int kNumChannels = 2;
inline constexpr int kControlBlock = 16;  // samples per control sub-block
inline constexpr double kMaxLookaheadMs = 10.0;

// A band's filter type.  Every band always has one of the types before
// kOff; kOff (no filter) is internal: what a bypassed band fades to, and a
// band a preset file leaves out.  The Type parameter's index is the enum
// value, so kOff stays last.
enum class BandType {
  kPeak,
  kLowShelf,
  kHighShelf,
  kBandShelf,
  kLp,
  kHp,
  kNotch,
  kBp,
  kTilt,
  kFletcherMunson,
  kVowel,
  kKWeight,
  kVintageLow,
  kVintageHigh,
  kOff,
};
inline constexpr int kNumBandTypes = static_cast<int>(BandType::kOff);

enum class DynTarget { kGain, kFreq, kMorph };
enum class Chan { kStereo, kLeft, kRight, kMid, kSide };
// The per-band Option, by band type.
enum class VowelWeighting { kNatural, kFlat };
enum class FilterSlope { k12, k24 };          // LP, HP
enum class VintageAtten { k5k, k10k, k20k };  // Vintage High
inline constexpr int kNumOptions = 3;

enum class DetectorInput { kInternalDry, kInternalBandSum, kExternalSc };
enum class ScFilter { kOff, kBp, kLp, kHp, kTilt, kKWeight };
enum class TriggerMode { kAbove, kBelow };
// What a group's D follows: input level (the gain computer), or the tracked
// vowel or pitch of the detector input (dsp/dynamics/tracker.h).
enum class DetectMode { kLevel, kVowel, kPitch };
// Output modes (ABOUT.md section 3.D).  In the split modes, bands
// 1..kSplitBands pick the split side and the rest shape the residual.
enum class OutputMode { kNormal, kSplitLr, kSplit, kResidual };
inline constexpr int kSplitBands = 4;

// Physical (unnormalized) band parameters.  Meaning of x/y/z by type:
//   Peak, Low/HighShelf: x = Hz, y = gain dB, z = Q
//   BandShelf:           x = center Hz (geometric mean of the edges),
//                        y = plateau gain dB, z = width in octaves
//   LP, HP, Notch:       x = Hz, z = Q (y unused); LP and HP at 24 dB/oct
//                        are Linkwitz-Riley crossovers (LR4 at Q 0.707)
//   BP:                  x = Hz, y = gain dB (<= kBpOffDb is silence), z = Q
//   Tilt:                x = pivot Hz, y = high-minus-low gain dB,
//                        z = width in octaves (the span of the ramp)
//   FletcherMunson:      y = phon delta (positive = boost lows/highs)
//   Vowel:               x = morph index 0..4, y = formant gain dB,
//                        z = tract scale
//   KWeight:             fixed (x, y, z unused)
//   VintageLow:           x = Hz, y = boost dB, z = attenuation dB
//   VintageHigh:          x = boost Hz, y = boost dB, z = attenuation dB
//                        (a shelf at the Option's VintageAtten frequency)
struct BandParams {
  BandType type = BandType::kOff;
  double x = 1000.0;
  double y = 0.0;
  double z = 0.70710678118654752440;
  int option = 0;  // Vowel: VowelWeighting; LP, HP: FilterSlope;
                   // Vintage High: VintageAtten
  // -1 = none, else 0..kNumGroups-1 (see SubscribesTo).  A band can follow
  // a group with dyn_range 0, e.g. a preset set up for the user to turn up.
  int dyn_group = -1;
  DynTarget dyn_target = DynTarget::kGain;
  double dyn_range = 0.0;  // dB, octaves or morph index (per dyn_target)
  // 0..1: broadband gain opposing the band's dynamic gain (Dyn Target Gain
  // on HasDynMakeup types), so e.g. ducking a band boosts everything else.
  double dyn_makeup = 0.0;
  // A second dynamics assignment ("Dyn B"), applied on top of the first,
  // e.g. a vowel-tracking group on Morph plus a level group on Gain.
  int dyn_b_group = -1;
  DynTarget dyn_b_target = DynTarget::kGain;
  double dyn_b_range = 0.0;
  Chan chan = Chan::kStereo;
  bool bypass = false;
  bool solo = false;
};

struct GroupParams {
  DetectMode detect = DetectMode::kLevel;
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
  OutputMode output_mode = OutputMode::kNormal;
  // Split L/R into a mono output: play the split side plus the residual (on
  // both channels) instead of one per channel.
  bool sum_split = false;
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

// Whether either of a band's dynamics assignments uses group g.
inline bool SubscribesTo(const BandParams& bp, int g) {
  return bp.dyn_group == g || bp.dyn_b_group == g;
}

// Bands whose dynamic gain can be offset by Dyn Makeup.
inline bool HasDynMakeup(BandType t) {
  return t == BandType::kPeak || t == BandType::kLowShelf ||
         t == BandType::kHighShelf || t == BandType::kBandShelf ||
         t == BandType::kVowel || t == BandType::kVintageLow ||
         t == BandType::kVintageHigh;
}

// LP and HP at 24 dB/oct (Linkwitz-Riley).  When one is the only band on
// the split side, the residual is its complementary crossover band.
inline bool IsCrossover(BandType t, int option) {
  return (t == BandType::kLp || t == BandType::kHp) &&
         option == static_cast<int>(FilterSlope::k24);
}

// The split side (bands 1..kSplitBands) in the split output modes.
struct SplitSide {
  int bands = 0;       // Bands that are on and not bypassed.  With none,
                       // nothing is split off.
  int crossover = -1;  // The only band, when it's a crossover; else -1.
};
inline SplitSide SplitSideOf(const EngineParams& p) {
  SplitSide s;
  int last = -1;
  for (int b = 0; b < kSplitBands; ++b) {
    const BandParams& bp = p.bands[b];
    if (bp.type != BandType::kOff && !bp.bypass) ++s.bands, last = b;
  }
  if (s.bands == 1 && IsCrossover(p.bands[last].type, p.bands[last].option)) {
    s.crossover = last;
  }
  return s;
}

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_ENGINE_PARAMS_H_
