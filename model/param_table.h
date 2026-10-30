#ifndef EVERYTHINGEQ_MODEL_PARAM_TABLE_H_
#define EVERYTHINGEQ_MODEL_PARAM_TABLE_H_

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "dsp/engine/params.h"

namespace eeq {

// The fixed, versioned host parameter list (ABOUT.md section 3.E).  Parameter
// IDs are positions in this list and must never change once released; new
// parameters are appended.  Each band and group occupies a contiguous block.

// Field order is the host order: most-used first.  The X/Y/Z slots are named
// "Freq", "Gain" and "Q" for hosts (their meaning for other band types is in
// ABOUT.md section 3.E).
enum class BandField {
  kBypass,
  kType,
  kX,
  kY,
  kZ,
  kDynGroup,
  kDynTarget,
  kDynRange,
  kDynMakeup,
  kDynBGroup,
  kDynBTarget,
  kDynBRange,
  kOption,
  kTap,
  kChan,
  kSolo,
};
inline constexpr int kNumBandFields = 16;

enum class GroupField {
  kAttack,
  kHold,
  kRelease,
  kThreshold,
  kRise,
  kKnee,
  kMode,
  kAlpha,
  kRmsWindow,
  kInput,
  kDetect,
  kScFilter,
  kScHz,
  kScQ,
  kLink,
  kScListen,
};
inline constexpr int kNumGroupFields = 16;

enum class GlobalField {
  kOutputMode,
  kGlobalScale,
  kDelta,
  kBypass,
  kOutputGain,
  kLookahead,
};
inline constexpr int kNumGlobalFields = 6;

inline constexpr int kFirstGroupParam = kNumBands * kNumBandFields;
inline constexpr int kFirstGlobalParam =
    kFirstGroupParam + kNumGroups * kNumGroupFields;
inline constexpr int kNumParams = kFirstGlobalParam + kNumGlobalFields;

constexpr int BandParamId(int band, BandField field) {
  return band * kNumBandFields + static_cast<int>(field);
}
constexpr int GroupParamId(int group, GroupField field) {
  return kFirstGroupParam + group * kNumGroupFields + static_cast<int>(field);
}
constexpr int GlobalParamId(GlobalField field) {
  return kFirstGlobalParam + static_cast<int>(field);
}

// A band's default Dyn Group: bands 1-2 use Group 1, 3-4 Group 2, and so on.
constexpr int DefaultDynGroup(int band) {
  return band * kNumGroups / kNumBands;
}

enum class ParamKind {
  kContinuous,  // Plain value in [min, max].
  kEnum,        // Integer index into enum_labels.
  kBool,
  kSlot,    // Normalized [0, 1]; meaning depends on the band type.
  kOption,  // Integer 0..kNumOptions-1; labels depend on the band type.
};

struct ParamSpec {
  std::string key;    // Stable state key, e.g. "band3.x".
  std::string name;   // Host name, e.g. "B3 X".
  std::string group;  // Host group, e.g. "Band 3".
  ParamKind kind = ParamKind::kContinuous;
  double min = 0.0;
  double max = 1.0;
  double default_value = 0.0;
  double step = 0.0;  // 0 = continuous.
  bool log = false;   // Logarithmic skew (continuous only).
  std::string unit;
  std::vector<std::string> enum_labels;
  bool automatable = true;
};

// All parameters, indexed by parameter ID (size kNumParams).
const std::vector<ParamSpec>& ParamTable();

// Host-side parameter values: plain values for continuous, enum and bool
// parameters, normalized values for slots.
using ParamValues = std::array<double, kNumParams>;

ParamValues DefaultParamValues();

// Clamps (and rounds enums/bools) into each parameter's legal range.
double SanitizeParamValue(int id, double value);

// Host values -> engine parameters.  `mono_output` applies the mono -> mono
// rules (D10): output mode forced to Stereo and per-band Chan ignored.
EngineParams BuildEngineParams(const ParamValues& values, bool mono_output);

// Engine parameters -> host values (used to load presets).  Slots a band type
// doesn't use get their defaults.
ParamValues ValuesFromEngineParams(const EngineParams& params);

// The detent nearest a parameter value (host units), for edits made in the
// plugin UI: levels (dB, phon) in 0.5 dB steps, times in 0.1 ms steps below
// 10 ms and 1 ms steps above, and other continuous parameters in their
// ParamSpec step.  Slots snap in their physical unit for the current band
// type.  Values without detents are returned unchanged.
double SnapParamValue(const ParamValues& values, int id, double value);

// For levels (dB, phon), the number of 0.5 dB detents across the range (so
// UI controls can move one detent at a time); 0 for other parameters.
int LevelDetentCount(const ParamValues& values, int id);

// Display text for a parameter value, using the current band type for slots.
std::string FormatParamValue(const ParamValues& values, int id, double value);

// Inverse of FormatParamValue for slot and option parameters: returns the
// host value, or nullopt if the text can't be parsed (or the slot is unused).
std::optional<double> ParseSlotText(const ParamValues& values, int id,
                                    const std::string& text);

}  // namespace eeq

#endif  // EVERYTHINGEQ_MODEL_PARAM_TABLE_H_
