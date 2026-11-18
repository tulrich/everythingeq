#include "model/state.h"

#include <nlohmann/json.hpp>
#include <string>

namespace eeq {
namespace {

constexpr const char* kStateFormat = "everythingeq-state";

// Upgrades `params` in place from `version` to kStateVersion.  Each step
// is an `if (version < N)` block converting version N-1 to N, in order.
// There are none yet: older states load by key with defaults and
// sanitizing (DeserializeState), which is the fallback for anything a
// step doesn't cover.
void MigrateState(int version, nlohmann::json& params) {
  (void)version;
  (void)params;
}

nlohmann::json ParamsJson(const ParamValues& values) {
  nlohmann::json params = nlohmann::json::object();
  const std::vector<ParamSpec>& table = ParamTable();
  for (int i = 0; i < kNumParams; ++i) params[table[i].key] = values[i];
  return params;
}

void PutPreset(const PresetRef& preset, nlohmann::json& j) {
  if (!preset.name.empty()) {
    j["preset"] = {{"name", preset.name}, {"user", preset.user}};
  }
}

// Missing keys get defaults; values are sanitized.
ParamValues ReadParams(const nlohmann::json& params) {
  ParamValues values = DefaultParamValues();
  const std::vector<ParamSpec>& table = ParamTable();
  for (int i = 0; i < kNumParams; ++i) {
    const auto it = params.find(table[i].key);
    if (it != params.end() && it->is_number()) {
      values[i] = SanitizeParamValue(i, it->get<double>());
    }
  }
  return values;
}

PresetRef ReadPreset(const nlohmann::json& j) {
  PresetRef preset;
  const auto it = j.find("preset");
  if (it != j.end() && it->is_object()) {
    const auto name = it->find("name");
    const auto user = it->find("user");
    if (name != it->end() && name->is_string()) {
      preset.name = CleanPresetName(name->get<std::string>());
    }
    preset.user = user != it->end() && user->is_boolean() && user->get<bool>();
  }
  return preset;
}

}  // namespace

std::string SerializeState(const ParamValues& values, const PresetRef& preset,
                           const AbState& compare) {
  nlohmann::json j;
  j["format"] = kStateFormat;
  j["version"] = kStateVersion;
  j["params"] = ParamsJson(values);
  PutPreset(preset, j);
  if (compare.active != 0 || compare.other) {
    nlohmann::json c;
    c["active"] = compare.active;
    if (compare.other) {
      c["params"] = ParamsJson(*compare.other);
      PutPreset(compare.other_preset, c);
    }
    j["compare"] = std::move(c);
  }
  return j.dump();
}

std::optional<ParamValues> DeserializeState(std::string_view data,
                                            PresetRef* preset,
                                            std::optional<AbState>* compare) {
  nlohmann::json j = nlohmann::json::parse(data, nullptr,
                                           /*allow_exceptions=*/false);
  if (!j.is_object() || j.value("format", "") != kStateFormat ||
      !j.contains("version") || !j["version"].is_number_integer() ||
      !j.contains("params") || !j["params"].is_object()) {
    return std::nullopt;
  }
  const int version = j["version"].get<int>();
  nlohmann::json& params = j["params"];
  MigrateState(version, params);
  const ParamValues values = ReadParams(params);
  if (preset) *preset = ReadPreset(j);
  if (compare) {
    compare->reset();
    const auto it = j.find("compare");
    if (it != j.end() && it->is_object()) {
      AbState c;
      const auto active = it->find("active");
      c.active = active != it->end() && active->is_number_integer() &&
                         active->get<int>() == 1
                     ? 1
                     : 0;
      const auto other = it->find("params");
      if (other != it->end() && other->is_object()) {
        MigrateState(version, *other);
        c.other = ReadParams(*other);
        c.other_preset = ReadPreset(*it);
      }
      *compare = std::move(c);
    }
  }
  return values;
}

}  // namespace eeq
