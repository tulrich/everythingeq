// Phase 5: state serialization, preset files and undo history.

#include "model/state.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <random>

#include "model/param_table.h"
#include "model/presets.h"
#include "model/slot_mapping.h"
#include "model/undo_history.h"

namespace eeq::test {
namespace {

ParamValues RandomValues(uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  ParamValues v{};
  for (int i = 0; i < kNumParams; ++i) {
    const ParamSpec& s = ParamTable()[i];
    v[i] = SanitizeParamValue(i, s.min + unit(rng) * (s.max - s.min));
  }
  return v;
}

std::filesystem::path TempDir(const char* name) {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "eeq_tests" / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

}  // namespace

TEST_CASE("State round-trips exactly and deterministically", "[state]") {
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    const ParamValues v = RandomValues(seed);
    const std::string s = SerializeState(v);
    const std::optional<ParamValues> back = DeserializeState(s);
    REQUIRE(back.has_value());
    REQUIRE(*back == v);
    REQUIRE(SerializeState(*back) == s);
  }
}

TEST_CASE("Invalid state is rejected", "[state]") {
  REQUIRE_FALSE(DeserializeState(""));
  REQUIRE_FALSE(DeserializeState("{}"));
  REQUIRE_FALSE(DeserializeState("[1, 2, 3]"));
  REQUIRE_FALSE(DeserializeState(
      R"({"format": "something-else", "version": 1, "params": {}})"));
  std::mt19937 rng(7);
  std::string junk(4096, '\0');
  for (char& c : junk) c = static_cast<char>(rng());
  REQUIRE_FALSE(DeserializeState(junk));
}

TEST_CASE("State tolerates missing, unknown and out-of-range keys", "[state]") {
  const std::optional<ParamValues> v = DeserializeState(R"({
      "format": "everythingeq-state", "version": 1,
      "params": {"band2.type": 7, "band2.x": 3.5, "global.output_gain_db": -99,
                 "band1.dyn_group": 2.4, "future.param": 1}})");
  REQUIRE(v.has_value());
  ParamValues expected = DefaultParamValues();
  // Type 7 was BP before v3 inserted Band Shelf.
  expected[BandParamId(1, BandField::kType)] = static_cast<int>(BandType::kBp);
  expected[BandParamId(1, BandField::kX)] = 1.0;
  expected[GlobalParamId(GlobalField::kOutputGain)] = -24.0;
  expected[BandParamId(0, BandField::kDynGroup)] = 1;  // Migrated from v1.
  REQUIRE(*v == expected);
}

TEST_CASE("Version 2 band types shift past Band Shelf", "[state]") {
  const std::optional<ParamValues> v = DeserializeState(R"({
      "format": "everythingeq-state", "version": 2,
      "params": {"band1.type": 3, "band2.type": 4, "band3.type": 13}})");
  REQUIRE(v.has_value());
  REQUIRE((*v)[BandParamId(0, BandField::kType)] ==
          static_cast<int>(BandType::kHighShelf));
  REQUIRE((*v)[BandParamId(1, BandField::kType)] ==
          static_cast<int>(BandType::kLp));
  REQUIRE((*v)[BandParamId(2, BandField::kType)] ==
          static_cast<int>(BandType::kKWeight));
}

TEST_CASE("Version 3 LR crossovers become 24 dB/oct LP and HP", "[state]") {
  // v3: LP12 5, HP12 6, LP24_LR 9, HP24_LR 10, Tilt 11, K-Weight 14.
  const std::optional<ParamValues> v = DeserializeState(R"({
      "format": "everythingeq-state", "version": 3,
      "params": {"band1.type": 5, "band1.option": 1,
                 "band2.type": 9, "band2.z": 0.9,
                 "band3.type": 10, "band4.type": 11, "band5.type": 14}})");
  REQUIRE(v.has_value());
  auto at = [&](int b, BandField f) { return (*v)[BandParamId(b, f)]; };
  REQUIRE(at(0, BandField::kType) == static_cast<int>(BandType::kLp));
  REQUIRE(at(0, BandField::kOption) == 0);  // A stray Vowel option cleared.
  REQUIRE(at(1, BandField::kType) == static_cast<int>(BandType::kLp));
  REQUIRE(at(1, BandField::kOption) == static_cast<int>(FilterSlope::k24));
  REQUIRE(at(1, BandField::kZ) == kSlotDefault);  // Q 0.707: still LR4.
  REQUIRE(at(2, BandField::kType) == static_cast<int>(BandType::kHp));
  REQUIRE(at(2, BandField::kOption) == static_cast<int>(FilterSlope::k24));
  REQUIRE(at(3, BandField::kType) == static_cast<int>(BandType::kTilt));
  REQUIRE(at(4, BandField::kType) == static_cast<int>(BandType::kKWeight));
}

TEST_CASE("Version 1 Static bands migrate to no Dyn Range", "[state]") {
  const std::optional<ParamValues> v = DeserializeState(R"({
      "format": "everythingeq-state", "version": 1,
      "params": {"band4.dyn_group": 0, "band4.dyn_range": 0.8,
                 "band5.dyn_group": 4, "band5.dyn_range": 0.8}})");
  REQUIRE(v.has_value());
  ParamValues expected = DefaultParamValues();
  expected[BandParamId(3, BandField::kDynGroup)] = 1;  // Bands 3-4: Group 2.
  expected[BandParamId(4, BandField::kDynGroup)] = 3;
  expected[BandParamId(4, BandField::kDynRange)] = 0.8;
  REQUIRE(*v == expected);
}

TEST_CASE("Dyn Range 0 means a band is static", "[state]") {
  ParamValues v = DefaultParamValues();
  for (int b = 0; b < kNumBands; ++b) {
    REQUIRE(v[BandParamId(b, BandField::kDynGroup)] == b / 2);
  }
  REQUIRE(BuildEngineParams(v, false).bands[2].dyn_group == -1);
  v[BandParamId(2, BandField::kDynRange)] = 0.6;
  REQUIRE(BuildEngineParams(v, false).bands[2].dyn_group == 1);
  // And back: a static engine band gets its default group and no range.
  EngineParams p;
  p.bands[6].dyn_range = 12.0;
  const ParamValues back = ValuesFromEngineParams(p);
  REQUIRE(back[BandParamId(6, BandField::kDynGroup)] == 3);
  REQUIRE(back[BandParamId(6, BandField::kDynRange)] == 0.5);
}

TEST_CASE("Preset files save, load and list", "[presets]") {
  const std::filesystem::path dir = TempDir("presets");
  const Preset& factory = FactoryPresets()[2];
  REQUIRE(SavePresetFile(dir / ("b" + std::string(kPresetExtension)), factory));
  REQUIRE(SavePresetFile(dir / ("a" + std::string(kPresetExtension)),
                         FactoryPresets()[1]));
  std::ofstream(dir / "notes.txt") << "not a preset";

  const auto files = ListPresetFiles(dir);
  REQUIRE(files.size() == 2);
  REQUIRE(files[0].filename() == "a" + std::string(kPresetExtension));

  const std::optional<Preset> loaded = LoadPresetFile(files[1]);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->name == factory.name);
  REQUIRE(PresetToJson(*loaded) == PresetToJson(factory));

  std::string error;
  REQUIRE_FALSE(LoadPresetFile(dir / "notes.txt", &error));
  REQUIRE_FALSE(error.empty());
  REQUIRE_FALSE(ParsePreset(
      R"({"format": "everythingeq-preset", "version": 1,
          "params": {"bands": [{"type": "Bogus"}]}})",
      &error));
}

TEST_CASE("Undo groups gestures and clears redo on new edits", "[undo]") {
  UndoHistory h;
  REQUIRE_FALSE(h.CanUndo());

  h.Record(1, 0.0, 1.0);  // Single step.
  h.BeginGesture();       // A drag: one step, first before / last after.
  h.Record(2, 0.5, 0.6);
  h.Record(2, 0.6, 0.7);
  h.Record(3, 0.0, 1.0);
  h.EndGesture();

  std::vector<ParamChange> c = h.Undo();
  REQUIRE(c.size() == 2);
  REQUIRE(c[0].id == 3);
  REQUIRE(c[0].value == 0.0);
  REQUIRE(c[1].id == 2);
  REQUIRE(c[1].value == 0.5);
  REQUIRE(h.CanRedo());

  c = h.Redo();
  REQUIRE(c.size() == 2);
  REQUIRE(c[0].value == 0.7);

  h.Undo();
  h.Record(4, 0.0, 2.0);  // A new edit clears the redo history.
  REQUIRE_FALSE(h.CanRedo());
  c = h.Undo();
  REQUIRE(c.size() == 1);
  REQUIRE(c[0].id == 4);
  c = h.Undo();
  REQUIRE(c[0].id == 1);
  REQUIRE_FALSE(h.CanUndo());
}

TEST_CASE("Undo ignores no-op edits and is bounded", "[undo]") {
  UndoHistory h(3);
  h.Record(1, 0.5, 0.5);
  h.BeginGesture();
  h.Record(2, 0.1, 0.2);
  h.Record(2, 0.2, 0.1);  // Back where it started.
  h.EndGesture();
  REQUIRE_FALSE(h.CanUndo());
  for (int i = 0; i < 5; ++i) h.Record(i, 0.0, 1.0);
  int steps = 0;
  while (h.CanUndo()) {
    h.Undo();
    ++steps;
  }
  REQUIRE(steps == 3);
}

}  // namespace eeq::test
