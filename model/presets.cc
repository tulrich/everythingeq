#include "model/presets.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <system_error>

#include "model/engine_params_json.h"

namespace eeq {

// Defined in the generated factory_presets_data.cc.
extern const char* const kFactoryPresetJson[];
extern const int kNumFactoryPresetJson;

namespace {

constexpr const char* kPresetFormat = "everythingeq-preset";

void SetError(std::string* error, std::string message) {
  if (error) *error = std::move(message);
}

}  // namespace

std::optional<Preset> ParsePreset(std::string_view json, std::string* error) {
  const nlohmann::json j =
      nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
  if (!j.is_object()) {
    SetError(error, "not valid JSON");
    return std::nullopt;
  }
  if (j.value("format", "") != kPresetFormat) {
    SetError(error, "not an Everything EQ preset");
    return std::nullopt;
  }
  if (j.value("version", 0) > kPresetVersion) {
    SetError(error, "preset is from a newer version");
    return std::nullopt;
  }
  try {
    Preset p;
    p.name = j.value("name", "");
    p.params = ParseEngineParams(j.value("params", nlohmann::json::object()));
    return p;
  } catch (const std::exception& e) {
    SetError(error, e.what());
    return std::nullopt;
  }
}

std::string PresetToJson(const Preset& preset) {
  nlohmann::json j;
  j["format"] = kPresetFormat;
  j["version"] = kPresetVersion;
  j["name"] = preset.name;
  j["params"] = EngineParamsToJson(preset.params);
  return j.dump(2) + "\n";
}

const std::vector<Preset>& FactoryPresets() {
  static const std::vector<Preset> presets = [] {
    std::vector<Preset> out;
    for (int i = 0; i < kNumFactoryPresetJson; ++i) {
      std::string error;
      std::optional<Preset> p = ParsePreset(kFactoryPresetJson[i], &error);
      if (!p) throw std::logic_error("bad factory preset: " + error);
      out.push_back(std::move(*p));
    }
    return out;
  }();
  return presets;
}

bool SavePresetFile(const std::filesystem::path& path, const Preset& preset) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f << PresetToJson(preset);
  return static_cast<bool>(f);
}

std::optional<Preset> LoadPresetFile(const std::filesystem::path& path,
                                     std::string* error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    SetError(error, "cannot read " + path.string());
    return std::nullopt;
  }
  const std::string text((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
  return ParsePreset(text, error);
}

std::vector<std::filesystem::path> ListPresetFiles(
    const std::filesystem::path& dir) {
  std::vector<std::filesystem::path> out;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.is_regular_file() &&
        entry.path().extension() == kPresetExtension) {
      out.push_back(entry.path());
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace eeq
