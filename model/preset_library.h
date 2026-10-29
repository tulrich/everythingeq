#ifndef EVERYTHINGEQ_MODEL_PRESET_LIBRARY_H_
#define EVERYTHINGEQ_MODEL_PRESET_LIBRARY_H_

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/param_table.h"

namespace eeq {

// Which preset the current settings came from (saved with the plugin state).
struct PresetRef {
  std::string name;  // Empty: none.
  bool user = false;

  bool operator==(const PresetRef& o) const {
    return name == o.name && user == o.user;
  }
};

// The presets offered by the plugin's preset bar: the factory presets, then
// the user preset files in one folder (ABOUT.md section 3.E).  UI thread
// only.
class PresetLibrary {
 public:
  struct Entry {
    std::string name;
    bool user = false;
    std::filesystem::path path;  // User presets only.
  };

  explicit PresetLibrary(std::filesystem::path user_dir);

  const std::filesystem::path& user_dir() const { return user_dir_; }
  // Factory presets first (in their order), then user presets by name.
  const std::vector<Entry>& entries() const { return entries_; }
  int num_factory() const { return num_factory_; }

  // Rescans the user folder.
  void Refresh();

  // Index of the entry `ref` names, or -1.
  int Find(const PresetRef& ref) const;
  PresetRef Ref(int index) const;

  // The host parameter values of an entry (nullopt if a user file can't be
  // read).
  std::optional<ParamValues> Load(int index,
                                  std::string* error = nullptr) const;

  // Saves `values` as a user preset (replacing a user preset of the same
  // name) and refreshes.  Returns the new entry's index, or -1.
  int Save(std::string_view name, const ParamValues& values,
           std::string* error = nullptr);
  // Renames a user preset (its file and the name inside it).  Returns the
  // entry's new index, or -1.
  int Rename(int index, std::string_view new_name,
             std::string* error = nullptr);

 private:
  std::filesystem::path PathFor(std::string_view name) const;

  std::filesystem::path user_dir_;
  std::vector<Entry> entries_;
  int num_factory_ = 0;
};

// A preset name as typed, cleaned up: control characters and "/" removed,
// whitespace trimmed, at most 64 bytes.  May be empty.
std::string CleanPresetName(std::string_view name);

// File name for a preset name (characters that are invalid in file names
// replaced), with kPresetExtension.
std::string PresetFileName(std::string_view name);

// True if the settings a preset would store match within their resolution
// (e.g. to tell whether the settings still match the loaded preset).
bool ValuesMatch(const ParamValues& a, const ParamValues& b);

}  // namespace eeq

#endif  // EVERYTHINGEQ_MODEL_PRESET_LIBRARY_H_
