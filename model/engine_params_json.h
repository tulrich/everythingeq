#ifndef EVERYTHINGEQ_MODEL_ENGINE_PARAMS_JSON_H_
#define EVERYTHINGEQ_MODEL_ENGINE_PARAMS_JSON_H_

#include <nlohmann/json.hpp>

#include "dsp/engine/params.h"

namespace eeq {

// JSON form of EngineParams, shared by presets and the offline renderer:
//   {"global": {...}, "groups": [{...}, ...], "bands": [{...}, ...]}
// Keys are the camelCase forms of the BandParams / GroupParams / GlobalParams
// fields (e.g. "dynRange" for dyn_range); enums are given by name (e.g.
// "type": "Peak", "chan": "Mid"); Vowel weighting is "weighting".  Missing keys
// keep their defaults.  A dynamics assignment is on when it has a group
// ("dynGroup": 0..3).

// Applies `j` on top of `params`.  Throws std::runtime_error on bad values.
void ApplyEngineParamsJson(const nlohmann::json& j, EngineParams& params);

// As ApplyEngineParamsJson on defaults.  A band left out (or "Off") is the
// default layout's band, bypassed.  An assignment with a nonzero range and no
// group (from older presets) follows the band's default group.
EngineParams ParseEngineParams(const nlohmann::json& j);

// Writes every non-default field; unused trailing groups are omitted.
nlohmann::json EngineParamsToJson(const EngineParams& params);

}  // namespace eeq

#endif  // EVERYTHINGEQ_MODEL_ENGINE_PARAMS_JSON_H_
