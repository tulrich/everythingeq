#include "model/slot_mapping.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace eeq {
namespace {

constexpr SlotMapping kFreq{20.0, 1000.0, 20000.0, true, "Freq", "Hz"};
constexpr SlotMapping kGain{-30.0, 0.0, 30.0, false, "Gain", "dB"};
constexpr SlotMapping kBpGain{-60.0, 0.0, 30.0, false, "Gain", "dB", true};
constexpr SlotMapping kQ{0.1, 0.70710678118654752440, 40.0, true, "Q", ""};
constexpr SlotMapping kWidth{0.1, 2.0, 10.0, true, "Width", "oct"};
constexpr SlotMapping kPivot{20.0, 1000.0, 20000.0, true, "Pivot", "Hz"};
constexpr SlotMapping kTilt{-24.0, 0.0, 24.0, false, "Tilt", "dB"};
constexpr SlotMapping kPhon{-40.0, 0.0, 40.0, false, "Phon", "phon"};
constexpr SlotMapping kMorph{0.0, 2.0, 4.0, false, "Morph", ""};
constexpr SlotMapping kFormantGain{-30.0,          0.0, 30.0, false,
                                   "Formant Gain", "dB"};
constexpr SlotMapping kTract{0.70, 1.0, 1.40, true, "Tract", "x"};

double Lerp(double a, double b, double t, bool log) {
  return log ? a * std::pow(b / a, t) : a + (b - a) * t;
}

double InvLerp(double a, double b, double v, bool log) {
  return log ? std::log(v / a) / std::log(b / a) : (v - a) / (b - a);
}

}  // namespace

std::optional<SlotMapping> SlotMappingFor(BandType type, Slot slot) {
  switch (type) {
    case BandType::kPeak:
    case BandType::kLowShelf:
    case BandType::kHighShelf:
      if (slot == Slot::kX) return kFreq;
      if (slot == Slot::kY) return kGain;
      return kQ;
    case BandType::kBandShelf:
      if (slot == Slot::kX) return kFreq;
      if (slot == Slot::kY) return kGain;
      return kWidth;
    case BandType::kLp:
    case BandType::kHp:
    case BandType::kNotch:
      if (slot == Slot::kX) return kFreq;
      if (slot == Slot::kZ) return kQ;
      return std::nullopt;
    case BandType::kBp:
      if (slot == Slot::kX) return kFreq;
      if (slot == Slot::kY) return kBpGain;
      return kQ;
    case BandType::kTilt:
      if (slot == Slot::kX) return kPivot;
      if (slot == Slot::kY) return kTilt;
      return std::nullopt;
    case BandType::kFletcherMunson:
      if (slot == Slot::kY) return kPhon;
      return std::nullopt;
    case BandType::kVowel:
      if (slot == Slot::kX) return kMorph;
      if (slot == Slot::kY) return kFormantGain;
      return kTract;
    case BandType::kOff:
    case BandType::kKWeight:
      return std::nullopt;
  }
  return std::nullopt;
}

const char* OptionName(BandType type) {
  switch (type) {
    case BandType::kVowel:
      return "Weighting";
    case BandType::kLp:
    case BandType::kHp:
      return "Slope";
    default:
      return "";
  }
}

std::vector<std::string> OptionLabels(BandType type) {
  switch (type) {
    case BandType::kVowel:
      return {"Natural", "Flat"};  // VowelWeighting
    case BandType::kLp:
    case BandType::kHp:
      return {"12 dB/oct", "24 dB/oct"};  // FilterSlope
    default:
      return {};
  }
}

SlotMapping DynRangeMapping(DynTarget target) {
  switch (target) {
    case DynTarget::kGain:
      return {-60.0, 0.0, 60.0, false, "Dyn Range", "dB"};
    case DynTarget::kFreq:
      return {-8.0, 0.0, 8.0, false, "Dyn Range", "oct"};
    case DynTarget::kMorph:
      return {-4.0, 0.0, 4.0, false, "Dyn Range", ""};
  }
  return {};
}

double SlotToPhysical(const SlotMapping& m, double normalized) {
  const double u = std::clamp(normalized, 0.0, 1.0);
  if (u <= 0.5) return Lerp(m.min, m.mid, u / 0.5, m.log);
  return Lerp(m.mid, m.max, (u - 0.5) / 0.5, m.log);
}

double SlotFromPhysical(const SlotMapping& m, double physical) {
  const double v = std::clamp(physical, m.min, m.max);
  if (v <= m.mid) return 0.5 * InvLerp(m.min, m.mid, v, m.log);
  return 0.5 + 0.5 * InvLerp(m.mid, m.max, v, m.log);
}

namespace {

// Rounds to `decimals` places, without a negative zero ("-0.0").
double RoundForDisplay(double v, int decimals) {
  const double scale = std::pow(10.0, decimals);
  return std::round(v * scale) / scale + 0.0;
}

}  // namespace

std::string FormatSlotValue(const SlotMapping& m, double physical) {
  char buf[48];
  const std::string unit = m.unit;
  if (unit == "Hz") {
    if (physical >= 1000.0) {
      std::snprintf(buf, sizeof(buf), "%.2f kHz", physical / 1000.0);
    } else {
      std::snprintf(buf, sizeof(buf), "%.1f Hz", physical);
    }
  } else if (std::string(m.name) == "Morph") {
    static const char* const kVowels[] = {"OO", "OH", "AH", "EH", "EE"};
    // Name the vowel from the value as displayed (two decimals), so the text
    // parses back to the same label.
    const double shown = std::round(physical * 100.0) / 100.0;
    const int nearest = static_cast<int>(std::lround(shown));
    std::snprintf(buf, sizeof(buf), "%s %.2f",
                  kVowels[std::clamp(nearest, 0, 4)], physical);
  } else if (std::string(m.name) == "Width") {
    std::snprintf(buf, sizeof(buf), "%.2f oct", physical);
  } else if (unit == "x") {
    std::snprintf(buf, sizeof(buf), "%.2fx", physical);
  } else if (unit.empty()) {
    std::snprintf(buf, sizeof(buf), "%.2f", RoundForDisplay(physical, 2));
  } else if (m.min_is_silence && physical <= m.min) {
    std::snprintf(buf, sizeof(buf), "-inf dB");  // BP silence floor.
  } else {
    std::snprintf(buf, sizeof(buf), "%+.1f %s", RoundForDisplay(physical, 1),
                  m.unit);
  }
  return buf;
}

std::optional<double> ParseSlotValue(const SlotMapping& m,
                                     const std::string& text) {
  if (m.min_is_silence && text.find("inf") != std::string::npos) return m.min;
  const size_t start = text.find_first_of("+-.0123456789");
  if (start == std::string::npos) return std::nullopt;
  const char* begin = text.c_str() + start;
  char* end = nullptr;
  double v = std::strtod(begin, &end);
  if (end == begin) return std::nullopt;
  // "1.2 kHz" or "1.2k" means thousands.
  const size_t k = text.find_first_not_of(' ', end - text.c_str());
  if (k != std::string::npos && (text[k] == 'k' || text[k] == 'K')) v *= 1000.0;
  return std::clamp(v, m.min, m.max);
}

}  // namespace eeq
