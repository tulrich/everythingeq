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
//    "preset": {"name": "...", "user": false}}   (optional)
// Parameters are keyed by their stable ParamSpec::key.  On load, unknown keys
// are ignored, missing keys get defaults, values are sanitized, and older
// versions are migrated.  The output is deterministic.

inline constexpr int kStateVersion = 4;

// `preset` names the preset the settings came from (omitted when empty).
std::string SerializeState(const ParamValues& values,
                           const PresetRef& preset = {});

// Returns nullopt if `data` is not a valid state (e.g. random bytes).  Sets
// *preset (if given) to the saved preset reference, or clears it.
std::optional<ParamValues> DeserializeState(std::string_view data,
                                            PresetRef* preset = nullptr);

}  // namespace eeq

#endif  // EVERYTHINGEQ_MODEL_STATE_H_
