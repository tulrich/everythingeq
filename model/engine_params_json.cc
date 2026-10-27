#include "model/engine_params_json.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace eeq {
namespace {

using nlohmann::json;

const std::pair<const char*, BandType> kBandTypes[] = {
    {"Off", BandType::kOff},
    {"Peak", BandType::kPeak},
    {"LowShelf", BandType::kLowShelf},
    {"HighShelf", BandType::kHighShelf},
    {"BandShelf", BandType::kBandShelf},
    {"LP", BandType::kLp},
    {"HP", BandType::kHp},
    {"Notch", BandType::kNotch},
    {"BP", BandType::kBp},
    // Older names (before LP and HP took a Slope option).
    {"LP12", BandType::kLp},
    {"HP12", BandType::kHp},
    {"LP24_LR", BandType::kLp},
    {"HP24_LR", BandType::kHp},
    {"Tilt", BandType::kTilt},
    {"FletcherMunson", BandType::kFletcherMunson},
    {"Vowel", BandType::kVowel},
    {"KWeight", BandType::kKWeight}};
const std::pair<const char*, DynTarget> kDynTargets[] = {
    {"Gain", DynTarget::kGain},
    {"Freq", DynTarget::kFreq},
    {"Morph", DynTarget::kMorph}};
const std::pair<const char*, Tap> kTaps[] = {{"Main", Tap::kMain},
                                             {"Aux", Tap::kAux}};
const std::pair<const char*, Chan> kChans[] = {{"Stereo", Chan::kStereo},
                                               {"Left", Chan::kLeft},
                                               {"Right", Chan::kRight},
                                               {"Mid", Chan::kMid},
                                               {"Side", Chan::kSide}};
const std::pair<const char*, VowelWeighting> kWeightings[] = {
    {"Natural", VowelWeighting::kNatural}, {"Flat", VowelWeighting::kFlat}};
const std::pair<const char*, FilterSlope> kSlopes[] = {
    {"12", FilterSlope::k12}, {"24", FilterSlope::k24}};
const std::pair<const char*, DetectorInput> kInputs[] = {
    {"InternalDry", DetectorInput::kInternalDry},
    {"InternalBandSum", DetectorInput::kInternalBandSum},
    {"ExternalSC", DetectorInput::kExternalSc}};
const std::pair<const char*, ScFilter> kScFilters[] = {
    {"Off", ScFilter::kOff},   {"BP", ScFilter::kBp},
    {"LP", ScFilter::kLp},     {"HP", ScFilter::kHp},
    {"Tilt", ScFilter::kTilt}, {"KWeight", ScFilter::kKWeight}};
const std::pair<const char*, TriggerMode> kModes[] = {
    {"Above", TriggerMode::kAbove}, {"Below", TriggerMode::kBelow}};
const std::pair<const char*, OutputMode> kOutputModes[] = {
    {"Stereo", OutputMode::kStereo},
    {"SplitLR", OutputMode::kSplitLr},
    {"SplitMS", OutputMode::kSplitMs}};

template <typename E, size_t N>
void ReadEnum(const json& j, const char* key,
              const std::pair<const char*, E> (&names)[N], E& out) {
  if (!j.contains(key)) return;
  const std::string s = j.at(key).get<std::string>();
  for (const auto& [name, value] : names) {
    if (s == name) {
      out = value;
      return;
    }
  }
  throw std::runtime_error(std::string("bad value for ") + key + ": " + s);
}

template <typename E, size_t N>
void WriteEnum(json& j, const char* key,
               const std::pair<const char*, E> (&names)[N], E value,
               E default_value) {
  if (value == default_value) return;
  for (const auto& [name, v] : names) {
    if (v == value) j[key] = name;
  }
}

template <typename T>
void Read(const json& j, const char* key, T& out) {
  if (j.contains(key)) out = j.at(key).get<T>();
}

template <typename T>
void Write(json& j, const char* key, T value, T default_value) {
  if (value != default_value) j[key] = value;
}

}  // namespace

void ApplyEngineParamsJson(const json& j, EngineParams& p) {
  if (j.contains("global")) {
    const json& g = j["global"];
    GlobalParams& gp = p.global;
    ReadEnum(g, "outputMode", kOutputModes, gp.output_mode);
    Read(g, "globalScale", gp.global_scale);
    Read(g, "delta", gp.delta);
    Read(g, "bypass", gp.bypass);
    Read(g, "outputGainDb", gp.output_gain_db);
    Read(g, "lookaheadMs", gp.lookahead_ms);
  }
  if (j.contains("groups")) {
    for (size_t i = 0; i < j["groups"].size() && i < kNumGroups; ++i) {
      const json& g = j["groups"][i];
      GroupParams& gp = p.groups[i];
      ReadEnum(g, "input", kInputs, gp.input);
      ReadEnum(g, "scFilter", kScFilters, gp.sc_filter);
      ReadEnum(g, "mode", kModes, gp.mode);
      Read(g, "scHz", gp.sc_hz);
      Read(g, "scQ", gp.sc_q);
      Read(g, "scListen", gp.sc_listen);
      Read(g, "rmsWindowMs", gp.rms_window_ms);
      Read(g, "alpha", gp.alpha);
      Read(g, "link", gp.link);
      Read(g, "thresholdDb", gp.threshold_db);
      Read(g, "riseDb", gp.rise_db);
      Read(g, "kneeDb", gp.knee_db);
      Read(g, "attackMs", gp.attack_ms);
      Read(g, "holdMs", gp.hold_ms);
      Read(g, "releaseMs", gp.release_ms);
    }
  }
  if (j.contains("bands")) {
    for (size_t i = 0; i < j["bands"].size() && i < kNumBands; ++i) {
      const json& b = j["bands"][i];
      BandParams& bp = p.bands[i];
      ReadEnum(b, "type", kBandTypes, bp.type);
      const std::string type_name = b.value("type", "");
      if (type_name == "LP24_LR" || type_name == "HP24_LR") {
        bp.option = static_cast<int>(FilterSlope::k24);
      }
      ReadEnum(b, "dynTarget", kDynTargets, bp.dyn_target);
      ReadEnum(b, "tap", kTaps, bp.tap);
      ReadEnum(b, "chan", kChans, bp.chan);
      if (bp.type == BandType::kVowel) {
        auto weighting = static_cast<VowelWeighting>(bp.option);
        ReadEnum(b, "weighting", kWeightings, weighting);
        bp.option = static_cast<int>(weighting);
      } else if (bp.type == BandType::kLp || bp.type == BandType::kHp) {
        auto slope = static_cast<FilterSlope>(bp.option);
        ReadEnum(b, "slope", kSlopes, slope);
        bp.option = static_cast<int>(slope);
      }
      Read(b, "x", bp.x);
      Read(b, "y", bp.y);
      Read(b, "z", bp.z);
      Read(b, "dynGroup", bp.dyn_group);
      Read(b, "dynRange", bp.dyn_range);
      Read(b, "dynMakeup", bp.dyn_makeup);
      Read(b, "bypass", bp.bypass);
      Read(b, "solo", bp.solo);
    }
  }
}

EngineParams ParseEngineParams(const json& j) {
  EngineParams p;
  ApplyEngineParamsJson(j, p);
  return p;
}

json EngineParamsToJson(const EngineParams& p) {
  json out = json::object();

  json g = json::object();
  const GlobalParams gd{};
  WriteEnum(g, "outputMode", kOutputModes, p.global.output_mode,
            gd.output_mode);
  Write(g, "globalScale", p.global.global_scale, gd.global_scale);
  Write(g, "delta", p.global.delta, gd.delta);
  Write(g, "bypass", p.global.bypass, gd.bypass);
  Write(g, "outputGainDb", p.global.output_gain_db, gd.output_gain_db);
  Write(g, "lookaheadMs", p.global.lookahead_ms, gd.lookahead_ms);
  if (!g.empty()) out["global"] = g;

  json groups = json::array();
  const GroupParams d{};
  for (const GroupParams& gp : p.groups) {
    json j = json::object();
    WriteEnum(j, "input", kInputs, gp.input, d.input);
    WriteEnum(j, "scFilter", kScFilters, gp.sc_filter, d.sc_filter);
    WriteEnum(j, "mode", kModes, gp.mode, d.mode);
    Write(j, "scHz", gp.sc_hz, d.sc_hz);
    Write(j, "scQ", gp.sc_q, d.sc_q);
    Write(j, "scListen", gp.sc_listen, d.sc_listen);
    Write(j, "rmsWindowMs", gp.rms_window_ms, d.rms_window_ms);
    Write(j, "alpha", gp.alpha, d.alpha);
    Write(j, "link", gp.link, d.link);
    Write(j, "thresholdDb", gp.threshold_db, d.threshold_db);
    Write(j, "riseDb", gp.rise_db, d.rise_db);
    Write(j, "kneeDb", gp.knee_db, d.knee_db);
    Write(j, "attackMs", gp.attack_ms, d.attack_ms);
    Write(j, "holdMs", gp.hold_ms, d.hold_ms);
    Write(j, "releaseMs", gp.release_ms, d.release_ms);
    groups.push_back(j);
  }
  while (!groups.empty() && groups.back().empty())
    groups.erase(groups.size() - 1);
  if (!groups.empty()) out["groups"] = groups;

  json bands = json::array();
  const BandParams bd{};
  for (const BandParams& bp : p.bands) {
    json j = json::object();
    if (bp.type != BandType::kOff) {
      WriteEnum(j, "type", kBandTypes, bp.type, bd.type);
      j["x"] = bp.x;
      j["y"] = bp.y;
      j["z"] = bp.z;
      if (bp.type == BandType::kVowel) {
        WriteEnum(j, "weighting", kWeightings,
                  static_cast<VowelWeighting>(bp.option),
                  static_cast<VowelWeighting>(bd.option));
      } else if (bp.type == BandType::kLp || bp.type == BandType::kHp) {
        WriteEnum(j, "slope", kSlopes, static_cast<FilterSlope>(bp.option),
                  static_cast<FilterSlope>(bd.option));
      }
      Write(j, "dynGroup", bp.dyn_group, bd.dyn_group);
      WriteEnum(j, "dynTarget", kDynTargets, bp.dyn_target, bd.dyn_target);
      Write(j, "dynRange", bp.dyn_range, bd.dyn_range);
      Write(j, "dynMakeup", bp.dyn_makeup, bd.dyn_makeup);
      WriteEnum(j, "tap", kTaps, bp.tap, bd.tap);
      WriteEnum(j, "chan", kChans, bp.chan, bd.chan);
      Write(j, "bypass", bp.bypass, bd.bypass);
      Write(j, "solo", bp.solo, bd.solo);
    }
    bands.push_back(j);
  }
  while (!bands.empty() && bands.back().empty()) bands.erase(bands.size() - 1);
  if (!bands.empty()) out["bands"] = bands;
  return out;
}

}  // namespace eeq
