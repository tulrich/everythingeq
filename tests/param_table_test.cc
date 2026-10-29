// Phase 5: parameter table, slot mappings and host <-> engine conversion
// (ABOUT.md section 3.E, D14).

#include "model/param_table.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <random>
#include <set>
#include <string>

#include "model/engine_params_json.h"
#include "model/presets.h"
#include "model/slot_mapping.h"
#include "tests/test_util.h"

namespace eeq::test {
namespace {

// Compares two EngineParams field by field (via their flattened JSON forms).
bool NearlyEqual(const EngineParams& a, const EngineParams& b,
                 double rel_tol = 1e-9) {
  nlohmann::json ja = EngineParamsToJson(a).flatten();
  nlohmann::json jb = EngineParamsToJson(b).flatten();
  if (ja.size() != jb.size()) return false;
  for (auto it = ja.begin(); it != ja.end(); ++it) {
    if (!jb.contains(it.key())) return false;
    const nlohmann::json& va = it.value();
    const nlohmann::json& vb = jb[it.key()];
    if (va.is_number() && vb.is_number()) {
      const double x = va.get<double>(), y = vb.get<double>();
      if (std::abs(x - y) > rel_tol * std::max(1.0, std::abs(x))) return false;
    } else if (va != vb) {
      return false;
    }
  }
  return true;
}

ParamValues RandomValues(std::mt19937& rng) {
  ParamValues v{};
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  const std::vector<ParamSpec>& table = ParamTable();
  for (int i = 0; i < kNumParams; ++i) {
    const ParamSpec& s = table[i];
    v[i] = SanitizeParamValue(i, s.min + unit(rng) * (s.max - s.min));
  }
  return v;
}

}  // namespace

TEST_CASE("Parameter IDs and keys are stable", "[params]") {
  const std::vector<ParamSpec>& table = ParamTable();
  REQUIRE(table.size() == static_cast<size_t>(kNumParams));
  REQUIRE(kNumParams == 198);
  // IDs are a host-visible contract: compare against the checked-in list.
  std::ifstream golden(EEQ_SOURCE_DIR "/tests/golden/param_keys.txt");
  REQUIRE(golden);
  std::string line;
  int id = 0;
  while (std::getline(golden, line)) {
    if (line.empty() || line[0] == '#') continue;
    REQUIRE(id < kNumParams);
    CAPTURE(id);
    REQUIRE(table[id].key == line);
    ++id;
  }
  REQUIRE(id == kNumParams);
}

TEST_CASE("Parameter specs are well formed", "[params]") {
  std::set<std::string> keys, names;
  for (int i = 0; i < kNumParams; ++i) {
    const ParamSpec& s = ParamTable()[i];
    CAPTURE(s.key);
    REQUIRE(keys.insert(s.key).second);
    REQUIRE(names.insert(s.name).second);
    REQUIRE(s.name.size() < 32);   // iPlug2 MAX_PARAM_NAME_LEN
    REQUIRE(s.group.size() < 32);  // MAX_PARAM_GROUP_LEN
    REQUIRE(s.min <= s.default_value);
    REQUIRE(s.default_value <= s.max);
    if (s.log) REQUIRE(s.min > 0.0);
  }
  REQUIRE_FALSE(
      ParamTable()[GlobalParamId(GlobalField::kLookahead)].automatable);
}

TEST_CASE("Slot mappings round-trip and center on their defaults", "[params]") {
  for (int t = 0; t <= static_cast<int>(BandType::kKWeight); ++t) {
    for (Slot slot : {Slot::kX, Slot::kY, Slot::kZ}) {
      const auto m = SlotMappingFor(static_cast<BandType>(t), slot);
      if (!m) continue;
      CAPTURE(t, static_cast<int>(slot));
      REQUIRE(SlotToPhysical(*m, 0.0) == m->min);
      REQUIRE(std::abs(SlotToPhysical(*m, 0.5) - m->mid) < 1e-12);
      REQUIRE(std::abs(SlotToPhysical(*m, 1.0) - m->max) < 1e-9 * m->max);
      for (double u = 0.0; u <= 1.0; u += 0.01) {
        REQUIRE(std::abs(SlotFromPhysical(*m, SlotToPhysical(*m, u)) - u) <
                1e-12);
      }
    }
  }
  const SlotMapping freq = *SlotMappingFor(BandType::kPeak, Slot::kX);
  REQUIRE(SlotToPhysical(freq, 0.5) == 1000.0);
  REQUIRE(SlotToPhysical(*SlotMappingFor(BandType::kVowel, Slot::kX), 0.5) ==
          2.0);
  REQUIRE(SlotToPhysical(*SlotMappingFor(BandType::kVowel, Slot::kZ), 0.5) ==
          1.0);
  REQUIRE(SlotToPhysical(DynRangeMapping(DynTarget::kFreq), 0.75) == 4.0);
}

TEST_CASE("Default host values give the default band layout", "[params]") {
  const EngineParams p = BuildEngineParams(DefaultParamValues(), false);
  const nlohmann::json j = EngineParamsToJson(p);
  // Only the bands differ from the engine's defaults.
  REQUIRE(j.size() == 1);
  REQUIRE(j["bands"].size() == kNumBands);
  REQUIRE(p.bands[0].type == BandType::kHp);
  REQUIRE(p.bands[0].bypass);
  REQUIRE(std::abs(p.bands[0].x - 30.0) < 1e-6);
  REQUIRE(p.bands[3].type == BandType::kPeak);
  REQUIRE(std::abs(p.bands[3].x - 500.0) < 1e-6);
  REQUIRE(p.bands[7].type == BandType::kLp);
  REQUIRE(p.bands[7].bypass);
  for (int b = 1; b < kNumBands - 1; ++b) REQUIRE_FALSE(p.bands[b].bypass);
}

TEST_CASE("Host values survive engine params and back", "[params]") {
  std::mt19937 rng(42);
  for (int trial = 0; trial < 200; ++trial) {
    const ParamValues v = RandomValues(rng);
    const EngineParams p = BuildEngineParams(v, false);
    const EngineParams q = BuildEngineParams(ValuesFromEngineParams(p), false);
    CAPTURE(trial);
    REQUIRE(NearlyEqual(p, q));
  }
}

TEST_CASE("Factory presets parse and are representable", "[params]") {
  const std::vector<Preset>& presets = FactoryPresets();
  REQUIRE(presets.size() == 12);
  REQUIRE(presets[0].name == "Init");
  REQUIRE(NearlyEqual(presets[0].params,
                      BuildEngineParams(DefaultParamValues(), false)));
  std::set<std::string> names;
  for (const Preset& preset : presets) {
    CAPTURE(preset.name);
    REQUIRE(names.insert(preset.name).second);
    // Hosts may treat preset names as file names (Logic drops everything up
    // to a '/').
    REQUIRE(preset.name.find_first_of("/\\:") == std::string::npos);
    const EngineParams round_trip =
        BuildEngineParams(ValuesFromEngineParams(preset.params), false);
    REQUIRE(NearlyEqual(preset.params, round_trip));
  }
}

TEST_CASE("Mono output forces Stereo mode and ignores Chan", "[params]") {
  ParamValues v = DefaultParamValues();
  v[GlobalParamId(GlobalField::kOutputMode)] =
      static_cast<int>(OutputMode::kSplitLr);
  v[BandParamId(0, BandField::kType)] = static_cast<int>(BandType::kPeak);
  v[BandParamId(0, BandField::kY)] = 0.75;  // +15 dB
  v[BandParamId(0, BandField::kChan)] = static_cast<int>(Chan::kSide);
  const EngineParams mono = BuildEngineParams(v, true);
  REQUIRE(mono.global.output_mode == OutputMode::kStereo);
  REQUIRE(mono.bands[0].chan == Chan::kStereo);

  // Mono -> mono output equals the left channel of the stereo engine fed a
  // dual-mono signal with the band on both channels.
  Stereo x = Noise(24000, 0.5, 30);
  x.r = x.l;
  Engine e;
  e.SetParams(mono);
  e.Prepare(48000.0);
  const Stereo y = Run(e, x);
  v[BandParamId(0, BandField::kChan)] = static_cast<int>(Chan::kStereo);
  v[GlobalParamId(GlobalField::kOutputMode)] =
      static_cast<int>(OutputMode::kStereo);
  Engine ref;
  ref.SetParams(BuildEngineParams(v, false));
  ref.Prepare(48000.0);
  const Stereo z = Run(ref, x);
  REQUIRE(MaxAbsDiff(y, z) == 0.0);
}

TEST_CASE("Display text follows the band type", "[params]") {
  ParamValues v = DefaultParamValues();
  const int type = BandParamId(0, BandField::kType);
  const int x = BandParamId(0, BandField::kX);
  const int y = BandParamId(0, BandField::kY);
  v[type] = static_cast<int>(BandType::kPeak);
  REQUIRE(FormatParamValue(v, x, 0.5) == "1.00 kHz");
  REQUIRE(FormatParamValue(v, y, 0.75) == "+15.0 dB");
  v[type] = static_cast<int>(BandType::kVowel);
  REQUIRE(FormatParamValue(v, x, 0.5) == "AH 2.00");
  v[type] = static_cast<int>(BandType::kFletcherMunson);
  REQUIRE(FormatParamValue(v, x, 0.5) == "-");
  v[type] = static_cast<int>(BandType::kBp);
  REQUIRE(FormatParamValue(v, y, 0.0) == "-inf dB");
  REQUIRE(FormatParamValue(v, type, static_cast<int>(BandType::kBp)) == "BP");
  v[BandParamId(0, BandField::kDynTarget)] = static_cast<int>(DynTarget::kFreq);
  REQUIRE(FormatParamValue(v, BandParamId(0, BandField::kDynRange), 0.75) ==
          "+4.0 oct");
}

TEST_CASE("Option text follows the band type", "[params]") {
  ParamValues v = DefaultParamValues();
  const int type = BandParamId(0, BandField::kType);
  const int option = BandParamId(0, BandField::kOption);
  v[type] = static_cast<int>(BandType::kLp);
  REQUIRE(FormatParamValue(v, option, 1.0) == "24 dB/oct");
  REQUIRE(ParseSlotText(v, option, "12 dB/oct") == 0.0);
  REQUIRE(ParseSlotText(v, option, "24") == 1.0);
  v[type] = static_cast<int>(BandType::kVowel);
  REQUIRE(FormatParamValue(v, option, 1.0) == "Flat");
  REQUIRE(ParseSlotText(v, option, "Natural") == 0.0);
  v[type] = static_cast<int>(BandType::kPeak);
  REQUIRE(FormatParamValue(v, option, 1.0) == "-");
  REQUIRE_FALSE(ParseSlotText(v, option, "1").has_value());
}

TEST_CASE("Slot display text parses back consistently", "[params]") {
  std::mt19937 rng(5);
  for (int trial = 0; trial < 500; ++trial) {
    ParamValues v = RandomValues(rng);
    for (BandField f :
         {BandField::kX, BandField::kY, BandField::kZ, BandField::kDynRange}) {
      const int id = BandParamId(trial % kNumBands, f);
      const std::string text = FormatParamValue(v, id, v[id]);
      const std::optional<double> parsed = ParseSlotText(v, id, text);
      CAPTURE(trial, id, text);
      if (text == "-") {
        REQUIRE_FALSE(parsed);
        continue;
      }
      REQUIRE(parsed.has_value());
      REQUIRE(FormatParamValue(v, id, *parsed) == text);
    }
  }
  ParamValues v = DefaultParamValues();
  v[BandParamId(0, BandField::kType)] = static_cast<int>(BandType::kPeak);
  const int x = BandParamId(0, BandField::kX);
  REQUIRE(ParseSlotText(v, x, "2k") == ParseSlotText(v, x, "2000 Hz"));
  REQUIRE(FormatParamValue(v, x, *ParseSlotText(v, x, "2.5 kHz")) ==
          "2.50 kHz");
  REQUIRE_FALSE(ParseSlotText(v, x, "loud"));
  // Only the BP floor reads as -inf; Dyn Range at its minimum is a number.
  REQUIRE(FormatParamValue(v, BandParamId(0, BandField::kDynRange), 0.0) ==
          "-60.0 dB");
}

TEST_CASE("The default band layout is transparent", "[params]") {
  const EngineParams p = BuildEngineParams(DefaultParamValues(), false);
  const Stereo x = Noise(48000, 0.5, 3);
  Stereo y(x.size());
  Engine e;
  e.SetParams(p);
  e.Prepare(48000.0);
  const float* in[2] = {x.l.data(), x.r.data()};
  float* out[2] = {y.l.data(), y.r.data()};
  e.Process(in, nullptr, out, static_cast<int>(x.size()));
  REQUIRE(MaxAbsDiff(y, x) < 1e-6);
}

}  // namespace eeq::test
