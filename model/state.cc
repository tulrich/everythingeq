#include "model/state.h"

#include <cmath>
#include <nlohmann/json.hpp>
#include <string>

#include "model/slot_mapping.h"

namespace eeq {
namespace {

constexpr const char* kStateFormat = "everythingeq-state";

// Upgrades `params` in place from `version` to kStateVersion.
void MigrateState(int version, nlohmann::json& params) {
  if (version < 2) {
    // Version 1 had a "Static" Dyn Group at index 0.  Static bands become
    // their default group with no Dyn Range (D19).
    for (int b = 0; b < kNumBands; ++b) {
      const std::string k = "band" + std::to_string(b + 1) + ".";
      const auto it = params.find(k + "dyn_group");
      if (it == params.end() || !it->is_number()) continue;
      const int old_group = static_cast<int>(std::lround(it->get<double>()));
      if (old_group <= 0) {
        *it = DefaultDynGroup(b);
        params[k + "dyn_range"] = kSlotDefault;
      } else {
        *it = old_group - 1;
      }
    }
  }
  if (version < 3) {
    // Version 3 inserted Band Shelf after High Shelf in the Type enum.
    constexpr int kBandShelfIndex = static_cast<int>(BandType::kBandShelf);
    for (int b = 0; b < kNumBands; ++b) {
      const auto it = params.find("band" + std::to_string(b + 1) + ".type");
      if (it == params.end() || !it->is_number()) continue;
      const int type = static_cast<int>(std::lround(it->get<double>()));
      if (type >= kBandShelfIndex) *it = type + 1;
    }
  }
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
