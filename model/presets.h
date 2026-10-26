#ifndef EVERYTHINGEQ_MODEL_PRESETS_H_
#define EVERYTHINGEQ_MODEL_PRESETS_H_

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dsp/engine/params.h"

namespace eeq {

// A preset file (factory or user):
//   {"format": "everythingeq-preset", "version": 1, "name": "...",
//    "params": <EngineParams JSON, see engine_params_json.h>}
struct Preset {
  std::string name;
  EngineParams params;
};

inline constexpr int kPresetVersion = 1;
inline constexpr const char* kPresetExtension = ".eeqpreset";

// Returns nullopt (and sets *error if given) for invalid preset JSON.
std::optional<Preset> ParsePreset(std::string_view json,
                                  std::string* error = nullptr);
std::string PresetToJson(const Preset& preset);

// Factory presets, embedded from presets/factory/*.json at build time, in
// file-name order.
const std::vector<Preset>& FactoryPresets();

// User preset files.
bool SavePresetFile(const std::filesystem::path& path, const Preset& preset);
std::optional<Preset> LoadPresetFile(const std::filesystem::path& path,
                                     std::string* error = nullptr);
// Preset files (by extension) in `dir`, sorted by file name.
std::vector<std::filesystem::path> ListPresetFiles(
    const std::filesystem::path& dir);

}  // namespace eeq

#endif  // EVERYTHINGEQ_MODEL_PRESETS_H_
