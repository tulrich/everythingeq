#ifndef EVERYTHINGEQ_MODEL_SLOT_MAPPING_H_
#define EVERYTHINGEQ_MODEL_SLOT_MAPPING_H_

#include <optional>
#include <string>
#include <vector>

#include "dsp/engine/params.h"

namespace eeq {

// The generic X/Y/Z band slots and DynRange are normalized [0, 1] host
// parameters whose physical meaning depends on the band type (and, for
// DynRange, on Dyn Target); see ABOUT.md section 3.E and D14.
//
// Each mapping is piecewise: [0, 0.5] spans min..mid and [0.5, 1] spans
// mid..max, logarithmically or linearly.  Putting every slot's default at 0.5
// keeps the defaults consistent across band types, because the normalized
// value is what persists when the type changes.
struct SlotMapping {
  double min = 0.0;
  double mid = 0.0;
  double max = 1.0;
  bool log = false;
  const char* name = "";        // e.g. "Freq"
  const char* unit = "";        // e.g. "Hz"
  bool min_is_silence = false;  // BP gain: the minimum means "off".
};

enum class Slot { kX, kY, kZ };

inline constexpr double kSlotDefault = 0.5;

// Mapping for a slot of a band type, or nullopt if the type doesn't use it.
std::optional<SlotMapping> SlotMappingFor(BandType type, Slot slot);

// The per-band Option's name and choices for a band type (kNumOptions or
// fewer), or "" and none if the type has no option.
const char* OptionName(BandType type);
std::vector<std::string> OptionLabels(BandType type);

// Mapping for DynRange under a Dyn Target.
SlotMapping DynRangeMapping(DynTarget target);

double SlotToPhysical(const SlotMapping& m, double normalized);
double SlotFromPhysical(const SlotMapping& m, double physical);

// Display text for a physical value, e.g. "1.20 kHz", "-3.0 dB", "AH 2.00".
std::string FormatSlotValue(const SlotMapping& m, double physical);

// Parses display text (with or without units, "k" suffixes or a vowel name)
// back to a physical value.  Returns nullopt if there is no number.
std::optional<double> ParseSlotValue(const SlotMapping& m,
                                     const std::string& text);

}  // namespace eeq

#endif  // EVERYTHINGEQ_MODEL_SLOT_MAPPING_H_
