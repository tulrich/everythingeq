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

}  // namespace

std::string SerializeState(const ParamValues& values, const PresetRef& preset) {
  nlohmann::json params = nlohmann::json::object();
  const std::vector<ParamSpec>& table = ParamTable();
  for (int i = 0; i < kNumParams; ++i) params[table[i].key] = values[i];
  nlohmann::json j;
  j["format"] = kStateFormat;
  j["version"] = kStateVersion;
  j["params"] = std::move(params);
  if (!preset.name.empty()) {
    j["preset"] = {{"name", preset.name}, {"user", preset.user}};
  }
  return j.dump();
}

std::optional<ParamValues> DeserializeState(std::string_view data,
                                            PresetRef* preset) {
  nlohmann::json j = nlohmann::json::parse(data, nullptr,
                                           /*allow_exceptions=*/false);
  if (!j.is_object() || j.value("format", "") != kStateFormat ||
      !j.contains("version") || !j["version"].is_number_integer() ||
      !j.contains("params") || !j["params"].is_object()) {
    return std::nullopt;
  }
  nlohmann::json& params = j["params"];
  MigrateState(j["version"].get<int>(), params);

  ParamValues values = DefaultParamValues();
  const std::vector<ParamSpec>& table = ParamTable();
  for (int i = 0; i < kNumParams; ++i) {
    const auto it = params.find(table[i].key);
    if (it != params.end() && it->is_number()) {
      values[i] = SanitizeParamValue(i, it->get<double>());
    }
  }
  if (preset) {
    *preset = {};
    const auto it = j.find("preset");
    if (it != j.end() && it->is_object()) {
      const auto name = it->find("name");
      const auto user = it->find("user");
      if (name != it->end() && name->is_string()) {
        preset->name = CleanPresetName(name->get<std::string>());
      }
      preset->user =
          user != it->end() && user->is_boolean() && user->get<bool>();
    }
  }
  return values;
}

}  // namespace eeq
