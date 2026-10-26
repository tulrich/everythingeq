#ifndef EVERYTHINGEQ_MODEL_ENGINE_PARAMS_JSON_H_
#define EVERYTHINGEQ_MODEL_ENGINE_PARAMS_JSON_H_

#include <nlohmann/json.hpp>

#include "dsp/engine/params.h"

namespace eeq {

// JSON form of EngineParams, shared by presets and the offline renderer:
//   {"global": {...}, "groups": [{...}, ...], "bands": [{...}, ...]}
// Keys are the camelCase forms of the BandParams / GroupParams / GlobalParams
// fields (e.g. "dynRange" for dyn_range); enums are given by name (e.g.
// "type": "Peak", "tap": "Aux"); Vowel weighting is "weighting".  Missing keys
// keep their defaults.

// Applies `j` on top of `params`.  Throws std::runtime_error on bad values.
void ApplyEngineParamsJson(const nlohmann::json& j, EngineParams& params);

EngineParams ParseEngineParams(const nlohmann::json& j);

// Writes every non-default field; Off bands and unused trailing groups are
// omitted.
nlohmann::json EngineParamsToJson(const EngineParams& params);

}  // namespace eeq

#endif  // EVERYTHINGEQ_MODEL_ENGINE_PARAMS_JSON_H_
