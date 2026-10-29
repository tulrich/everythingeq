#include "model/preset_library.h"

#include <algorithm>
#include <cmath>
#include <system_error>
#include <utility>

#include "model/presets.h"
#include "model/slot_mapping.h"

namespace eeq {
namespace {

constexpr size_t kMaxNameBytes = 64;

void SetError(std::string* error, std::string message) {
  if (error) *error = std::move(message);
}

}  // namespace

std::string CleanPresetName(std::string_view name) {
  std::string out;
  for (const char c : name) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7f || c == '/') continue;
    out.push_back(c);
  }
  const size_t first = out.find_first_not_of(' ');
  if (first == std::string::npos) return "";
  out = out.substr(first, out.find_last_not_of(' ') - first + 1);
  if (out.size() > kMaxNameBytes) {
    // Don't split a UTF-8 sequence.
    size_t n = kMaxNameBytes;
    while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xc0) == 0x80) --n;
    out.resize(n);
    out = out.substr(0, out.find_last_not_of(' ') + 1);
  }
  return out;
}

std::string PresetFileName(std::string_view name) {
  std::string out(name);
  for (char& c : out) {
    if (std::string_view("\\:*?\"<>|").find(c) != std::string_view::npos) {
      c = '_';
    }
  }
  if (out.empty() || out[0] == '.') out.insert(out.begin(), '_');
  return out + kPresetExtension;
}

bool ValuesMatch(const ParamValues& a, const ParamValues& b) {
  // Compare what a preset would store: slots and options a band type
  // doesn't use are not stored.
  ParamValues ca = ValuesFromEngineParams(BuildEngineParams(a, false));
  ParamValues cb = ValuesFromEngineParams(BuildEngineParams(b, false));
  for (int band = 0; band < kNumBands; ++band) {
    const int type =
        static_cast<int>(std::lround(ca[BandParamId(band, BandField::kType)]));
    if (OptionLabels(static_cast<BandType>(type)).empty()) {
      const int id = BandParamId(band, BandField::kOption);
      ca[id] = cb[id] = 0.0;
    }
  }
  const std::vector<ParamSpec>& table = ParamTable();
  for (int i = 0; i < kNumParams; ++i) {
    const ParamSpec& s = table[i];
    const double tol = std::max(0.5 * s.step, 1e-6 * (s.max - s.min));
    if (std::abs(ca[i] - cb[i]) > tol) return false;
  }
  return true;
}

PresetLibrary::PresetLibrary(std::filesystem::path user_dir)
    : user_dir_(std::move(user_dir)) {
  Refresh();
}

void PresetLibrary::Refresh() {
  entries_.clear();
  for (const Preset& p : FactoryPresets())
    entries_.push_back({p.name, false, {}});
  num_factory_ = static_cast<int>(entries_.size());
  std::vector<Entry> user;
  for (const std::filesystem::path& path : ListPresetFiles(user_dir_)) {
    const std::optional<Preset> p = LoadPresetFile(path);
    if (!p) continue;  // Not a preset; leave it alone.
    std::string name = CleanPresetName(p->name);
    if (name.empty()) name = path.stem().string();
    user.push_back({std::move(name), true, path});
  }
  std::stable_sort(
      user.begin(), user.end(),
      [](const Entry& a, const Entry& b) { return a.name < b.name; });
  entries_.insert(entries_.end(), user.begin(), user.end());
}

int PresetLibrary::Find(const PresetRef& ref) const {
  if (ref.name.empty()) return -1;
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].user == ref.user && entries_[i].name == ref.name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

PresetRef PresetLibrary::Ref(int index) const {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return {};
  return {entries_[index].name, entries_[index].user};
}

std::optional<ParamValues> PresetLibrary::Load(int index,
                                               std::string* error) const {
  if (index < 0 || index >= static_cast<int>(entries_.size())) {
    SetError(error, "no such preset");
    return std::nullopt;
  }
  if (index < num_factory_) {
    return ValuesFromEngineParams(FactoryPresets()[index].params);
  }
  const std::optional<Preset> p = LoadPresetFile(entries_[index].path, error);
  if (!p) return std::nullopt;
  return ValuesFromEngineParams(p->params);
}

std::filesystem::path PresetLibrary::PathFor(std::string_view name) const {
  return user_dir_ / PresetFileName(name);
}

int PresetLibrary::Save(std::string_view name, const ParamValues& values,
                        std::string* error) {
  const std::string clean = CleanPresetName(name);
  if (clean.empty()) {
    SetError(error, "empty preset name");
    return -1;
  }
  // Replace an existing user preset of that name even if its file name
  // differs (e.g. it was renamed by hand).
  std::filesystem::path path = PathFor(clean);
  const int existing = Find({clean, true});
  if (existing >= 0) path = entries_[existing].path;
  if (!SavePresetFile(
          path, {clean, BuildEngineParams(values, /*mono_output=*/false)})) {
    SetError(error, "cannot write " + path.string());
    return -1;
  }
  Refresh();
  return Find({clean, true});
}

int PresetLibrary::Rename(int index, std::string_view new_name,
                          std::string* error) {
  if (index < num_factory_ || index >= static_cast<int>(entries_.size())) {
    SetError(error, "only user presets can be renamed");
    return -1;
  }
  const std::string clean = CleanPresetName(new_name);
  if (clean.empty()) {
    SetError(error, "empty preset name");
    return -1;
  }
  const Entry old = entries_[index];
  if (clean == old.name) return index;
  if (Find({clean, true}) >= 0) {
    SetError(error, "a user preset named \"" + clean + "\" already exists");
    return -1;
  }
  std::optional<Preset> p = LoadPresetFile(old.path, error);
  if (!p) return -1;
  p->name = clean;
  const std::filesystem::path path = PathFor(clean);
  if (!SavePresetFile(path, *p)) {
    SetError(error, "cannot write " + path.string());
    return -1;
  }
  // A change of case only may name the same file.
  std::error_code ec;
  if (!std::filesystem::equivalent(path, old.path, ec)) {
    std::filesystem::remove(old.path, ec);
  }
  Refresh();
  return Find({clean, true});
}

}  // namespace eeq
