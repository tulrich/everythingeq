// Phase 6: host automation of every continuous parameter is click-free.
//
// Automation arrives as a new host value per block.  Each automatable
// continuous (and slot) parameter is swept over its whole range in 0.5 s,
// updated every 64-sample block, through the same path the plugin uses
// (host values -> BuildEngineParams -> Engine::SetParams).  The input is a
// low sine, so any click or zipper noise shows up as energy far above it.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include <vector>

#include "dsp/svf/svf.h"
#include "model/param_table.h"
#include "model/slot_mapping.h"
#include "tests/test_util.h"

namespace eeq::test {
namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 64;
constexpr double kSweepSeconds = 0.5;
constexpr double kInputHz = 220.0;

void SetBand(ParamValues& v, int band, BandType type, double x, double y,
             double z) {
  v[BandParamId(band, BandField::kType)] = static_cast<int>(type);
  v[BandParamId(band, BandField::kX)] = x;
  v[BandParamId(band, BandField::kY)] = y;
  v[BandParamId(band, BandField::kZ)] = z;
  v[BandParamId(band, BandField::kBypass)] = 0;
}

// A busy chain that gives every band and group parameter something to do.
ParamValues BaseValues() {
  ParamValues v = DefaultParamValues();
  SetBand(v, 0, BandType::kPeak, 0.45, 0.65, 0.6);
  SetBand(v, 1, BandType::kVowel, 0.5, 0.6, 0.5);
  SetBand(v, 2, BandType::kTilt, 0.5, 0.55, 0.5);
  SetBand(v, 3, BandType::kFletcherMunson, 0.5, 0.6, 0.5);
  SetBand(v, 4, BandType::kLowShelf, 0.4, 0.6, 0.5);
  SetBand(v, 5, BandType::kHp, 0.2, 0.5, 0.5);
  SetBand(v, 6, BandType::kBp, 0.5, 0.5, 0.5);
  SetBand(v, 7, BandType::kLp, 0.9, 0.5, 0.5);
  v[BandParamId(7, BandField::kOption)] = kSlope24;
  // Dynamic bands on two groups.
  v[BandParamId(0, BandField::kDynGroup)] = 0;
  v[BandParamId(0, BandField::kDynRange)] = 0.55;
  v[BandParamId(1, BandField::kDynGroup)] = 1;
  v[BandParamId(1, BandField::kDynTarget)] =
      static_cast<int>(DynTarget::kMorph);
  v[BandParamId(1, BandField::kDynRange)] = 0.6;
  v[BandParamId(5, BandField::kDynGroup)] = 0;
  v[BandParamId(5, BandField::kDynTarget)] = static_cast<int>(DynTarget::kFreq);
  v[BandParamId(5, BandField::kDynRange)] = 0.55;
  for (int g = 0; g < 2; ++g) {
    v[GroupParamId(g, GroupField::kThreshold)] = -30.0;
    v[GroupParamId(g, GroupField::kKnee)] = 12.0;
    v[GroupParamId(g, GroupField::kScFilter)] = static_cast<int>(ScFilter::kBp);
  }
  v[GroupParamId(1, GroupField::kAlpha)] = 0.5;
  return v;
}

// Value for sweep position t in [0, 1] (up and back down), in the
// parameter's natural space (log for log parameters).
double SweepValue(const ParamSpec& s, double t) {
  const double u = t < 0.5 ? 2.0 * t : 2.0 - 2.0 * t;
  if (s.log) return s.min * std::pow(s.max / s.min, u);
  return s.min + u * (s.max - s.min);
}

struct SweepResult {
  double peak = 0.0;
  double rms = 0.0;
  double hf_rms = 0.0;  // Energy above ~12 kHz.
  bool finite = true;
};

SweepResult Sweep(int id) {
  const ParamSpec& s = ParamTable()[id];
  ParamValues v = BaseValues();
  Engine engine;
  engine.SetParams(BuildEngineParams(v, false));
  engine.Prepare(kFs);

  // Settle the dynamics on the static settings first.
  const int settle = static_cast<int>(0.3 * kFs);
  const int sweep = static_cast<int>(kSweepSeconds * kFs);
  const Stereo x = Sine(settle + sweep, kInputHz, 0.25, kFs);
  Stereo y(x.size());

  SvfCell hp[2];
  for (SvfCell& c : hp) {
    c.Snap(DesignSvf(SvfShape::kHighPass, 12000.0, 0.7071, 0.0, kFs));
  }
  SweepResult r;
  double energy = 0.0, hf_energy = 0.0;
  for (int pos = 0; pos < static_cast<int>(x.size()); pos += kBlock) {
    if (pos >= settle) {
      const double t = static_cast<double>(pos - settle) / sweep;
      v[id] = SanitizeParamValue(id, SweepValue(s, std::min(t, 1.0)));
      engine.SetParams(BuildEngineParams(v, false));
    }
    const float* in[2] = {x.l.data() + pos, x.r.data() + pos};
    float* out[2] = {y.l.data() + pos, y.r.data() + pos};
    engine.Process(in, nullptr, out, kBlock);
    for (int i = pos; i < pos + kBlock; ++i) {
      const double o = y.l[i];
      // The measurement filter runs through the settling period too, so its
      // own start-up transient doesn't count.
      const double h = hp[1].Process(hp[0].Process(o));
      if (pos < settle) continue;
      r.finite = r.finite && std::isfinite(o);
      r.peak = std::max(r.peak, std::abs(o));
      energy += o * o;
      hf_energy += h * h;
    }
  }
  r.rms = std::sqrt(energy / sweep);
  r.hf_rms = std::sqrt(hf_energy / sweep);
  return r;
}

}  // namespace

TEST_CASE("Automating continuous parameters is click-free", "[automation]") {
  int swept = 0;
  for (int id = 0; id < kNumParams; ++id) {
    const ParamSpec& s = ParamTable()[id];
    if (!s.automatable) continue;
    if (s.kind != ParamKind::kContinuous && s.kind != ParamKind::kSlot) {
      continue;
    }
    const SweepResult r = Sweep(id);
    CAPTURE(s.name, r.peak, r.rms, r.hf_rms);
    REQUIRE(r.finite);
    REQUIRE(r.peak < 20.0);
    // Clicks are broadband: require the energy above 12 kHz to stay 70 dB
    // below the signal (or below -100 dBFS when the signal is tiny).  The
    // worst case measured is about -83 dB (Output Gain).
    REQUIRE(r.hf_rms < std::max(3e-4 * r.rms, 1e-5));
    ++swept;
  }
  // 4 slots per band, 11 continuous parameters per group, Scale and Output
  // Gain (Lookahead isn't automatable).
  REQUIRE(swept == kNumBands * 4 + kNumGroups * 11 + 2);
}

TEST_CASE("Switching enum parameters stays finite and bounded",
          "[automation]") {
  const Stereo x = Sine(static_cast<size_t>(0.25 * kFs), kInputHz, 0.25, kFs);
  for (int id = 0; id < kNumParams; ++id) {
    const ParamSpec& s = ParamTable()[id];
    if (s.kind != ParamKind::kEnum && s.kind != ParamKind::kBool) continue;
    ParamValues v = BaseValues();
    Engine engine;
    engine.SetParams(BuildEngineParams(v, false));
    engine.Prepare(kFs);
    Stereo y(x.size());
    int step = 0;
    for (int pos = 0; pos + kBlock <= static_cast<int>(x.size());
         pos += kBlock, ++step) {
      v[id] = step % static_cast<int>(s.max + 1);
      engine.SetParams(BuildEngineParams(v, false));
      const float* in[2] = {x.l.data() + pos, x.r.data() + pos};
      float* out[2] = {y.l.data() + pos, y.r.data() + pos};
      engine.Process(in, nullptr, out, kBlock);
    }
    CAPTURE(s.name);
    REQUIRE(AllFinite(y));
    REQUIRE(MaxAbs(y) < 20.0);
  }
}

}  // namespace eeq::test
