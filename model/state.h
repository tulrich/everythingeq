#ifndef EVERYTHINGEQ_MODEL_STATE_H_
#define EVERYTHINGEQ_MODEL_STATE_H_

#include <optional>
#include <string>
#include <string_view>

#include "model/param_table.h"
#include "model/preset_library.h"

namespace eeq {

// Plugin state as saved by the host (ABOUT.md section 3.E):
//   {"format": "everythingeq-state", "version": 1,
//    "params": {"band1.type": 1, "band1.x": 0.5, ...},
//    "preset": {"name": "...", "user": false},   (optional)
//    "compare": {"active": 1, "params": {...}, "preset": {...}}}   (optional)
// Parameters are keyed by their stable ParamSpec::key.  On load, unknown keys
// are ignored, missing keys get defaults, values are sanitized, and older
// versions go through MigrateState (no steps yet).  The output is
// deterministic.

inline constexpr int kStateVersion = 9;

// A/B comparison: the host parameters hold the active slot; the other
// slot's settings are kept here ("compare" in the state).
struct AbState {
  int active = 0;  // 0 = A, 1 = B.
  // The other slot, or nullopt until it's first used.
  std::optional<ParamValues> other;
  PresetRef other_preset;

  bool operator==(const AbState&) const = default;
};

// `preset` names the preset the settings came from (omitted when empty).
// The comparison is omitted while it's unused (A active, B empty).
std::string SerializeState(const ParamValues& values,
                           const PresetRef& preset = {},
                           const AbState& compare = {});

// Returns nullopt if `data` is not a valid state (e.g. random bytes).  Sets
// *preset (if given) to the saved preset reference, or clears it, and
// *compare (if given) to the saved comparison, or nullopt when there is none
// (e.g. a preset).
std::optional<ParamValues> DeserializeState(
    std::string_view data, PresetRef* preset = nullptr,
    std::optional<AbState>* compare = nullptr);

}  // namespace eeq

#endif  // EVERYTHINGEQ_MODEL_STATE_H_
