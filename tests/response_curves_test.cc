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

EngineParams RandomTappedChain(std::mt19937& rng) {
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
    bp.tap = unit(rng) < 0.3 ? Tap::kAux : Tap::kMain;
    bp.bypass = unit(rng) < 0.1;
  }
  return p;
}

}  // namespace

TEST_CASE("Chain response matches the engine's impulse response", "[curves]") {
  std::mt19937 rng(GENERATE(range(1u, 16u)));
  const EngineParams p = RandomTappedChain(rng);
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

}  // namespace eeq::test
