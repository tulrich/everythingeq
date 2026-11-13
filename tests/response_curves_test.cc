// Phase 7: the EQ graph's curves match what the engine actually does.

#include "model/response_curves.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <random>

#include "tests/test_util.h"

namespace eeq::test {
namespace {

constexpr double kFs = 48000.0;

EngineParams RandomChain(std::mt19937& rng) {
  const BandType types[] = {BandType::kPeak,      BandType::kLowShelf,
                            BandType::kHighShelf, BandType::kLp,
                            BandType::kHp,        BandType::kNotch,
                            BandType::kBp,        BandType::kTilt,
                            BandType::kVowel,     BandType::kFletcherMunson};
  std::uniform_int_distribution<int> type(0, std::size(types) - 1);
  std::uniform_real_distribution<double> oct(1.0, 9.5), gain(-12.0, 12.0),
      q(0.4, 4.0), morph(0.0, 4.0), unit(0.0, 1.0);
  EngineParams p;
  for (int b = 0; b < 6; ++b) {
    BandParams& bp = p.bands[b];
    bp.type = types[type(rng)];
    bp.x =
        bp.type == BandType::kVowel ? morph(rng) : 20.0 * std::exp2(oct(rng));
    bp.y = gain(rng);
    bp.z = bp.type == BandType::kVowel ? 1.0 : q(rng);
    if (bp.type == BandType::kLp || bp.type == BandType::kHp)
      bp.option = unit(rng) < 0.5 ? 0 : kSlope24;
    bp.bypass = unit(rng) < 0.1;
  }
  return p;
}

}  // namespace

TEST_CASE("Chain response matches the engine's impulse response", "[curves]") {
  std::mt19937 rng(GENERATE(range(1u, 16u)));
  const EngineParams p = RandomChain(rng);
  Engine e;
  e.SetParams(p);
  e.Prepare(kFs);
  Stereo impulse(static_cast<size_t>(kFs));
  impulse.l[0] = impulse.r[0] = 1.0f;
  const Stereo y = Run(e, impulse);
  const std::vector<double> h(y.l.begin(), y.l.end());

  const std::vector<double> freqs = DisplayFrequencies(64, 20.0, 20000.0);
  ChainResponse curves;
  ComputeChainResponse(p, GroupControls{}, kFs, freqs, curves);
  REQUIRE(curves.total_db.size() == freqs.size());
  for (size_t i = 0; i < freqs.size(); ++i) {
    const double measured = MagDb(DftAt(h, freqs[i], kFs));
    CAPTURE(freqs[i], measured, curves.total_db[i]);
    if (measured > -60.0) {
      REQUIRE(std::abs(measured - curves.total_db[i]) < 0.05);
    }
  }
  for (int b = 0; b < kNumBands; ++b) {
    REQUIRE(curves.band_db[b].empty() == (p.bands[b].type == BandType::kOff));
  }
  REQUIRE(curves.split_db.empty());

  // The split modes' two outputs.
  for (const OutputMode mode :
       {OutputMode::kSplit, OutputMode::kResidual}) {
    EngineParams q = p;
    q.global.output_mode = mode;
    Engine es;
    es.SetParams(q);
    es.Prepare(kFs);
    const Stereo ys = Run(es, impulse);
    const std::vector<double> hs(ys.l.begin(), ys.l.end());
    ComputeChainResponse(q, GroupControls{}, kFs, freqs, curves);
    const std::vector<float>& c =
        mode == OutputMode::kSplit ? curves.split_db : curves.residual_db;
    REQUIRE(c.size() == freqs.size());
    for (size_t i = 0; i < freqs.size(); ++i) {
      const double measured = MagDb(DftAt(hs, freqs[i], kFs));
      CAPTURE(mode, freqs[i], measured, c[i]);
      if (measured > -60.0) REQUIRE(std::abs(measured - c[i]) < 0.05);
    }
  }
}

TEST_CASE("Group controls move dynamic bands", "[curves]") {
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 1000.0, 0.0, 1.0};
  p.bands[0].dyn_group = 2;
  p.bands[0].dyn_range = 9.0;
  const std::vector<double> freqs = {1000.0};
  ChainResponse curves;
  ComputeChainResponse(p, GroupControls{}, kFs, freqs, curves);
  REQUIRE(std::abs(curves.total_db[0]) < 1e-3);
  GroupControls controls{};
  controls[2] = 1.0;
  ComputeChainResponse(p, controls, kFs, freqs, curves);
  REQUIRE(std::abs(curves.total_db[0] - 9.0) < 0.05);
  controls[2] = 0.5;
  p.global.global_scale = 2.0;  // Scale doubles the range.
  ComputeChainResponse(p, controls, kFs, freqs, curves);
  REQUIRE(std::abs(curves.band_db[0][0] - 9.0) < 0.05);
}

TEST_CASE("Scale is a gain scale", "[curves]") {
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 1000.0, 6.0, 1.0};
  p.bands[0].dyn_group = 0;
  p.bands[0].dyn_range = 4.0;
  p.bands[1] = {BandType::kPeak, 4000.0, 0.0, 1.0};
  p.bands[1].dyn_group = 0;
  p.bands[1].dyn_target = DynTarget::kFreq;
  p.bands[1].dyn_range = 1.0;
  p.global.global_scale = 0.5;
  GroupControls controls{};
  controls[0] = 1.0;
  // Static gain and the Gain range both halve: (6 + 4) / 2.
  const ResolvedBand gain = ResolveBand(p.bands[0], 1.0, 0.0, 0.5);
  REQUIRE(std::abs(gain.y - 5.0) < 1e-9);
  // Freq ranges are not scaled, so tracking stays in tune.
  const ResolvedBand freq = ResolveBand(p.bands[1], 1.0, 0.0, 0.5);
  REQUIRE(std::abs(freq.x - 8000.0) < 1e-6);
  // A filter has no gain to scale.
  BandParams hp = {BandType::kHp, 100.0, 0.0, 0.707};
  REQUIRE(ResolveBand(hp, 0.0, 0.0, 0.0).y == 0.0);
  // Zero Scale flattens the chain.
  p.global.global_scale = 0.0;
  const std::vector<double> freqs = {1000.0};
  ChainResponse curves;
  ComputeChainResponse(p, controls, kFs, freqs, curves);
  REQUIRE(std::abs(curves.total_db[0]) < 1e-3);
}

}  // namespace eeq::test
