#include "model/param_table.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

#include "model/slot_mapping.h"

namespace eeq {
namespace {

ParamSpec Continuous(std::string key, std::string name, std::string group,
                     double min, double max, double def, std::string unit,
                     bool log = false, double step = 0.0) {
  ParamSpec s;
  s.key = std::move(key);
  s.name = std::move(name);
  s.group = std::move(group);
  s.kind = ParamKind::kContinuous;
  s.min = min;
  s.max = max;
  s.default_value = def;
  s.unit = std::move(unit);
  s.log = log;
  s.step = step;
  return s;
}

ParamSpec Enum(std::string key, std::string name, std::string group, int def,
               std::vector<std::string> labels) {
  ParamSpec s;
  s.key = std::move(key);
  s.name = std::move(name);
  s.group = std::move(group);
  s.kind = ParamKind::kEnum;
  s.min = 0.0;
  s.max = static_cast<double>(labels.size() - 1);
  s.default_value = def;
  s.step = 1.0;
  s.enum_labels = std::move(labels);
  return s;
}

ParamSpec Bool(std::string key, std::string name, std::string group, bool def) {
  ParamSpec s = Enum(std::move(key), std::move(name), std::move(group),
                     def ? 1 : 0, {"Off", "On"});
  s.kind = ParamKind::kBool;
  return s;
}

ParamSpec SlotParam(std::string key, std::string name, std::string group) {
  ParamSpec s;
  s.key = std::move(key);
  s.name = std::move(name);
  s.group = std::move(group);
  s.kind = ParamKind::kSlot;
  s.min = 0.0;
  s.max = 1.0;
  s.default_value = kSlotDefault;
  return s;
}

std::vector<ParamSpec> MakeTable() {
  std::vector<ParamSpec> t;
  t.reserve(kNumParams);
  for (int b = 0; b < kNumBands; ++b) {
    const std::string k = "band" + std::to_string(b + 1) + ".";
    const std::string n = "B" + std::to_string(b + 1) + " ";
    const std::string g = "Band " + std::to_string(b + 1);
    t.push_back(Enum(k + "type", n + "Type", g, 0,
                     {"Off", "Peak", "Low Shelf", "High Shelf", "LP12", "HP12",
                      "Notch", "BP", "LP24 LR", "HP24 LR", "Tilt",
                      "Fletcher-Munson", "Vowel", "K-Weight"}));
    t.push_back(SlotParam(k + "x", n + "X", g));
    t.push_back(SlotParam(k + "y", n + "Y", g));
    t.push_back(SlotParam(k + "z", n + "Z", g));
    t.push_back(Enum(k + "option", n + "Option", g, 0, {"Natural", "Flat"}));
    t.push_back(Enum(k + "dyn_group", n + "Dyn Group", g, 0,
                     {"Static", "Group 1", "Group 2", "Group 3", "Group 4"}));
    t.push_back(Enum(k + "dyn_target", n + "Dyn Target", g, 0,
                     {"Gain", "Freq", "Morph"}));
    t.push_back(SlotParam(k + "dyn_range", n + "Dyn Range", g));
    t.push_back(Enum(k + "tap", n + "Tap", g, 0, {"Main", "Aux"}));
    t.push_back(Enum(k + "chan", n + "Chan", g, 0,
                     {"Stereo", "L", "R", "Mid", "Side"}));
    t.push_back(Bool(k + "bypass", n + "Bypass", g, false));
    t.push_back(Bool(k + "solo", n + "Solo", g, false));
  }
  for (int i = 0; i < kNumGroups; ++i) {
    const std::string k = "group" + std::to_string(i + 1) + ".";
    const std::string n = "G" + std::to_string(i + 1) + " ";
    const std::string g = "Group " + std::to_string(i + 1);
    t.push_back(Enum(k + "input", n + "Input", g, 0,
                     {"Internal Dry", "Internal Band-Sum", "External SC"}));
    t.push_back(Enum(k + "sc_filter", n + "SC Filter", g, 0,
                     {"Off", "BP", "LP", "HP", "Tilt", "K-Weight"}));
    t.push_back(Continuous(k + "sc_hz", n + "SC Hz", g, 20.0, 20000.0, 1000.0,
                           "Hz", true));
    t.push_back(Continuous(k + "sc_q", n + "SC Q", g, 0.1, 40.0,
                           0.70710678118654752440, "", true));
    t.push_back(Bool(k + "sc_listen", n + "SC Listen", g, false));
    t.push_back(Continuous(k + "rms_window_ms", n + "RMS Window", g, 1.0, 400.0,
                           15.0, "ms", true));
    t.push_back(Continuous(k + "alpha", n + "Alpha", g, 0.0, 1.0, 0.0, ""));
    t.push_back(Continuous(k + "link", n + "Link", g, 0.0, 100.0, 100.0, "%"));
    t.push_back(Continuous(k + "threshold_db", n + "Threshold", g, -70.0, 0.0,
                           -20.0, "dB"));
    t.push_back(Continuous(k + "rise_db", n + "Rise", g, 0.0, 30.0, 6.0, "dB"));
    t.push_back(Continuous(k + "knee_db", n + "Knee", g, 0.0, 30.0, 0.0, "dB"));
    t.push_back(Enum(k + "mode", n + "Mode", g, 0, {"Above", "Below"}));
    t.push_back(Continuous(k + "attack_ms", n + "Attack", g, 0.1, 500.0, 5.0,
                           "ms", true));
    t.push_back(
        Continuous(k + "hold_ms", n + "Hold", g, 0.0, 500.0, 0.0, "ms"));
    t.push_back(Continuous(k + "release_ms", n + "Release", g, 1.0, 5000.0,
                           100.0, "ms", true));
  }
  const std::string g = "Global";
  t.push_back(Enum("global.output_mode", "Output Mode", g, 0,
                   {"Stereo", "Split L/R", "Split M/S"}));
  t.push_back(
      Continuous("global.global_scale", "Scale", g, -100.0, 200.0, 100.0, "%"));
  t.push_back(Bool("global.delta", "Delta", g, false));
  t.push_back(Bool("global.bypass", "Bypass", g, false));
  t.push_back(Continuous("global.output_gain_db", "Output Gain", g, -24.0, 24.0,
                         0.0, "dB"));
  ParamSpec lookahead = Continuous("global.lookahead_ms", "Lookahead", g, 0.0,
                                   kMaxLookaheadMs, 0.0, "ms", false, 0.1);
  lookahead.automatable = false;
  t.push_back(lookahead);
  return t;
}

int Int(double v) { return static_cast<int>(std::lround(v)); }

BandType BandTypeOf(const ParamValues& v, int band) {
  return static_cast<BandType>(Int(v[BandParamId(band, BandField::kType)]));
}

double PhysicalSlot(BandType type, Slot slot, double normalized,
                    double fallback) {
  const std::optional<SlotMapping> m = SlotMappingFor(type, slot);
  return m ? SlotToPhysical(*m, normalized) : fallback;
}

double NormalizedSlot(BandType type, Slot slot, double physical) {
  const std::optional<SlotMapping> m = SlotMappingFor(type, slot);
  return m ? SlotFromPhysical(*m, physical) : kSlotDefault;
}

}  // namespace

const std::vector<ParamSpec>& ParamTable() {
  static const std::vector<ParamSpec> table = MakeTable();
  return table;
}

ParamValues DefaultParamValues() {
  ParamValues v{};
  const std::vector<ParamSpec>& t = ParamTable();
  for (int i = 0; i < kNumParams; ++i) v[i] = t[i].default_value;
  return v;
}

double SanitizeParamValue(int id, double value) {
  const ParamSpec& s = ParamTable()[id];
  if (!std::isfinite(value)) return s.default_value;
  value = std::clamp(value, s.min, s.max);
  if (s.kind == ParamKind::kEnum || s.kind == ParamKind::kBool) {
    value = std::round(value);
  }
  return value;
}

EngineParams BuildEngineParams(const ParamValues& v, bool mono_output) {
  EngineParams p;
  const BandParams defaults{};
  for (int b = 0; b < kNumBands; ++b) {
    auto at = [&](BandField f) { return v[BandParamId(b, f)]; };
    BandParams& bp = p.bands[b];
    bp.type = BandTypeOf(v, b);
    bp.x = PhysicalSlot(bp.type, Slot::kX, at(BandField::kX), defaults.x);
    bp.y = PhysicalSlot(bp.type, Slot::kY, at(BandField::kY), defaults.y);
    bp.z = PhysicalSlot(bp.type, Slot::kZ, at(BandField::kZ), defaults.z);
    bp.option = Int(at(BandField::kOption));
    bp.dyn_group = Int(at(BandField::kDynGroup)) - 1;
    bp.dyn_target = static_cast<DynTarget>(Int(at(BandField::kDynTarget)));
    bp.dyn_range = SlotToPhysical(DynRangeMapping(bp.dyn_target),
                                  at(BandField::kDynRange));
    bp.tap = static_cast<Tap>(Int(at(BandField::kTap)));
    bp.chan = mono_output ? Chan::kStereo
                          : static_cast<Chan>(Int(at(BandField::kChan)));
    bp.bypass = at(BandField::kBypass) >= 0.5;
    bp.solo = at(BandField::kSolo) >= 0.5;
  }
  for (int i = 0; i < kNumGroups; ++i) {
    auto at = [&](GroupField f) { return v[GroupParamId(i, f)]; };
    GroupParams& gp = p.groups[i];
    gp.input = static_cast<DetectorInput>(Int(at(GroupField::kInput)));
    gp.sc_filter = static_cast<ScFilter>(Int(at(GroupField::kScFilter)));
    gp.sc_hz = at(GroupField::kScHz);
    gp.sc_q = at(GroupField::kScQ);
    gp.sc_listen = at(GroupField::kScListen) >= 0.5;
    gp.rms_window_ms = at(GroupField::kRmsWindow);
    gp.alpha = at(GroupField::kAlpha);
    gp.link = at(GroupField::kLink) / 100.0;
    gp.threshold_db = at(GroupField::kThreshold);
    gp.rise_db = at(GroupField::kRise);
    gp.knee_db = at(GroupField::kKnee);
    gp.mode = static_cast<TriggerMode>(Int(at(GroupField::kMode)));
    gp.attack_ms = at(GroupField::kAttack);
    gp.hold_ms = at(GroupField::kHold);
    gp.release_ms = at(GroupField::kRelease);
  }
  auto at = [&](GlobalField f) { return v[GlobalParamId(f)]; };
  GlobalParams& gl = p.global;
  gl.output_mode =
      mono_output ? OutputMode::kStereo
                  : static_cast<OutputMode>(Int(at(GlobalField::kOutputMode)));
  gl.global_scale = at(GlobalField::kGlobalScale) / 100.0;
  gl.delta = at(GlobalField::kDelta) >= 0.5;
  gl.bypass = at(GlobalField::kBypass) >= 0.5;
  gl.output_gain_db = at(GlobalField::kOutputGain);
  gl.lookahead_ms = at(GlobalField::kLookahead);
  return p;
}

ParamValues ValuesFromEngineParams(const EngineParams& p) {
  ParamValues v = DefaultParamValues();
  for (int b = 0; b < kNumBands; ++b) {
    auto set = [&](BandField f, double x) { v[BandParamId(b, f)] = x; };
    const BandParams& bp = p.bands[b];
    set(BandField::kType, static_cast<int>(bp.type));
    set(BandField::kX, NormalizedSlot(bp.type, Slot::kX, bp.x));
    set(BandField::kY, NormalizedSlot(bp.type, Slot::kY, bp.y));
    set(BandField::kZ, NormalizedSlot(bp.type, Slot::kZ, bp.z));
    set(BandField::kOption, bp.option);
    set(BandField::kDynGroup, bp.dyn_group + 1);
    set(BandField::kDynTarget, static_cast<int>(bp.dyn_target));
    set(BandField::kDynRange,
        SlotFromPhysical(DynRangeMapping(bp.dyn_target), bp.dyn_range));
    set(BandField::kTap, static_cast<int>(bp.tap));
    set(BandField::kChan, static_cast<int>(bp.chan));
    set(BandField::kBypass, bp.bypass);
    set(BandField::kSolo, bp.solo);
  }
  for (int i = 0; i < kNumGroups; ++i) {
    auto set = [&](GroupField f, double x) { v[GroupParamId(i, f)] = x; };
    const GroupParams& gp = p.groups[i];
    set(GroupField::kInput, static_cast<int>(gp.input));
    set(GroupField::kScFilter, static_cast<int>(gp.sc_filter));
    set(GroupField::kScHz, gp.sc_hz);
    set(GroupField::kScQ, gp.sc_q);
    set(GroupField::kScListen, gp.sc_listen);
    set(GroupField::kRmsWindow, gp.rms_window_ms);
    set(GroupField::kAlpha, gp.alpha);
    set(GroupField::kLink, gp.link * 100.0);
    set(GroupField::kThreshold, gp.threshold_db);
    set(GroupField::kRise, gp.rise_db);
    set(GroupField::kKnee, gp.knee_db);
    set(GroupField::kMode, static_cast<int>(gp.mode));
    set(GroupField::kAttack, gp.attack_ms);
    set(GroupField::kHold, gp.hold_ms);
    set(GroupField::kRelease, gp.release_ms);
  }
  auto set = [&](GlobalField f, double x) { v[GlobalParamId(f)] = x; };
  const GlobalParams& gl = p.global;
  set(GlobalField::kOutputMode, static_cast<int>(gl.output_mode));
  set(GlobalField::kGlobalScale, gl.global_scale * 100.0);
  set(GlobalField::kDelta, gl.delta);
  set(GlobalField::kBypass, gl.bypass);
  set(GlobalField::kOutputGain, gl.output_gain_db);
  set(GlobalField::kLookahead, gl.lookahead_ms);
  for (int i = 0; i < kNumParams; ++i) v[i] = SanitizeParamValue(i, v[i]);
  return v;
}

namespace {

// The mapping behind a slot parameter of the current band, if any.
std::optional<SlotMapping> MappingForSlotParam(const ParamValues& values,
                                               int id) {
  if (id < 0 || id >= kFirstGroupParam) return std::nullopt;
  const int band = id / kNumBandFields;
  const auto field = static_cast<BandField>(id % kNumBandFields);
  switch (field) {
    case BandField::kDynRange:
      return DynRangeMapping(static_cast<DynTarget>(
          Int(values[BandParamId(band, BandField::kDynTarget)])));
    case BandField::kX:
      return SlotMappingFor(BandTypeOf(values, band), Slot::kX);
    case BandField::kY:
      return SlotMappingFor(BandTypeOf(values, band), Slot::kY);
    case BandField::kZ:
      return SlotMappingFor(BandTypeOf(values, band), Slot::kZ);
    default:
      return std::nullopt;
  }
}

}  // namespace

std::string FormatParamValue(const ParamValues& values, int id, double value) {
  const ParamSpec& s = ParamTable()[id];
  char buf[48];
  switch (s.kind) {
    case ParamKind::kEnum:
    case ParamKind::kBool:
      return s.enum_labels[std::clamp(Int(value), 0, static_cast<int>(s.max))];
    case ParamKind::kContinuous:
      std::snprintf(buf, sizeof(buf), s.unit.empty() ? "%.2f" : "%.1f %s",
                    value, s.unit.c_str());
      return buf;
    case ParamKind::kSlot:
      break;
  }
  const std::optional<SlotMapping> m = MappingForSlotParam(values, id);
  if (!m) return "-";
  return FormatSlotValue(*m, SlotToPhysical(*m, value));
}

std::optional<double> ParseSlotText(const ParamValues& values, int id,
                                    const std::string& text) {
  const std::optional<SlotMapping> m = MappingForSlotParam(values, id);
  if (!m) return std::nullopt;
  const std::optional<double> physical = ParseSlotValue(*m, text);
  if (!physical) return std::nullopt;
  return SlotFromPhysical(*m, *physical);
}

}  // namespace eeq
