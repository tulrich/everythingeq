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
  if (version < 4) {
    // Version 4 folded LP24_LR and HP24_LR into LP and HP (formerly LP12 and
    // HP12) with a Slope option.  v3 indices: LP12 5, HP12 6, LP24_LR 9,
    // HP24_LR 10, then Tilt..K-Weight 11..14.
    for (int b = 0; b < kNumBands; ++b) {
      const std::string k = "band" + std::to_string(b + 1) + ".";
      const auto it = params.find(k + "type");
      if (it == params.end() || !it->is_number()) continue;
      const int type = static_cast<int>(std::lround(it->get<double>()));
      if (type == 5 || type == 6) {
        params[k + "option"] = static_cast<int>(FilterSlope::k12);
      } else if (type == 9 || type == 10) {
        *it = type == 9 ? static_cast<int>(BandType::kLp)
                        : static_cast<int>(BandType::kHp);
        params[k + "option"] = static_cast<int>(FilterSlope::k24);
        params[k + "z"] = kSlotDefault;  // Q 0.707: LR4.
      } else if (type > 10) {
        *it = type - 2;
      }
    }
  }
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
