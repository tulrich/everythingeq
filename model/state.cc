#include "model/state.h"

#include <nlohmann/json.hpp>

namespace eeq {
namespace {

constexpr const char* kStateFormat = "everythingeq-state";

// Upgrades `params` in place from `version` to kStateVersion.  Version 1 is
// the first format, so there is nothing to migrate yet.
void MigrateState(int version, nlohmann::json& params) {
  (void)version;
  (void)params;
}

}  // namespace

std::string SerializeState(const ParamValues& values) {
  nlohmann::json params = nlohmann::json::object();
  const std::vector<ParamSpec>& table = ParamTable();
  for (int i = 0; i < kNumParams; ++i) params[table[i].key] = values[i];
  nlohmann::json j;
  j["format"] = kStateFormat;
  j["version"] = kStateVersion;
  j["params"] = std::move(params);
  return j.dump();
}

std::optional<ParamValues> DeserializeState(std::string_view data) {
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
  return values;
}

}  // namespace eeq
