// Phase 3: chain, routing and output modes (ABOUT.md section 3.D).

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "tests/test_util.h"

namespace eeq::test {

namespace {

constexpr double kFs = 48000.0;
constexpr size_t kLen = 24000;

Stereo Render(const EngineParams& p, const Stereo& x) {
  Engine e;
  e.SetParams(p);
  e.Prepare(kFs);
  return Run(e, x);
}

// Random static chain of gain-type and filter-type bands.
EngineParams RandomChain(std::mt19937& rng) {
  const BandType types[] = {BandType::kPeak,      BandType::kLowShelf,
                            BandType::kHighShelf, BandType::kLp12,
                            BandType::kHp12,      BandType::kNotch,
                            BandType::kBp,        BandType::kTilt,
                            BandType::kVowel,     BandType::kFletcherMunson,
                            BandType::kKWeight};
  std::uniform_int_distribution<int> type(0, std::size(types) - 1);
  std::uniform_real_distribution<double> oct(0.0, 10.0), gain(-12.0, 12.0),
      q(0.3, 6.0), morph(0.0, 4.0);
  EngineParams p;
  const int n = std::uniform_int_distribution<int>(3, kNumBands)(rng);
  for (int b = 0; b < n; ++b) {
    BandParams& bp = p.bands[b];
    bp.type = types[type(rng)];
    bp.x =
        bp.type == BandType::kVowel ? morph(rng) : 20.0 * std::exp2(oct(rng));
    bp.y = gain(rng);
    bp.z = bp.type == BandType::kVowel ? 1.0 : q(rng);
    bp.chan = static_cast<Chan>(std::uniform_int_distribution<int>(0, 4)(rng));
  }
  return p;
}

int LastActiveBand(const EngineParams& p) {
  int last = -1;
  for (int b = 0; b < kNumBands; ++b)
    if (p.bands[b].type != BandType::kOff) last = b;
  return last;
}

}  // namespace

TEST_CASE("All bands off or bypassed is bit-exact identity", "[routing]") {
  const Stereo x = Noise(kLen, 0.9, 3);
  EngineParams p;
  REQUIRE(MaxAbsDiff(Render(p, x), x) == 0.0);
  p.bands[2] = {BandType::kPeak, 1000.0, 9.0, 1.0};
  p.bands[2].bypass = true;
  p.bands[5] = {BandType::kLp24Lr, 300.0};
  p.bands[5].bypass = true;
  REQUIRE(MaxAbsDiff(Render(p, x), x) == 0.0);
}

TEST_CASE("Delta tap on the last band recombines to the untapped chain",
          "[routing]") {
  std::mt19937 rng(GENERATE(range(1u, 21u)));
  const Stereo x = Noise(kLen, 0.5, 4);
  EngineParams p = RandomChain(rng);
  const int last = LastActiveBand(p);
  if (IsFilterType(p.bands[last].type)) p.bands[last].type = BandType::kPeak;
  const Stereo untapped = Render(p, x);
  p.bands[last].tap = Tap::kAux;
  REQUIRE(MaxAbsDiff(Render(p, x), untapped) < 1e-5);
}

TEST_CASE("Split tap on the last band recombines to the chain without it",
          "[routing]") {
  std::mt19937 rng(GENERATE(range(1u, 21u)));
  const Stereo x = Noise(kLen, 0.5, 5);
  EngineParams p = RandomChain(rng);
  const int b = LastActiveBand(p);
  const BandType filters[] = {BandType::kLp12, BandType::kHp12, BandType::kBp,
                              BandType::kNotch};
  p.bands[b].type = filters[rng() % 4];
  p.bands[b].tap = Tap::kAux;
  const Stereo tapped = Render(p, x);
  p.bands[b].type = BandType::kOff;
  REQUIRE(MaxAbsDiff(tapped, Render(p, x)) < 1e-5);
}

TEST_CASE("Mid-chain taps bypass the downstream bands for Aux only",
          "[routing]") {
  // Split tap at band 0, gain band after it: out = D(x - y) + y, with D the
  // downstream band and y the tapped filter output.
  const Stereo x = Noise(kLen, 0.5, 15);
  EngineParams p;
  p.bands[0] = {BandType::kLp12, 400.0, 0.0, 0.7071};
  p.bands[0].tap = Tap::kAux;
  p.bands[1] = {BandType::kHighShelf, 2000.0, 6.0, 0.7071};
  const Stereo out = Render(p, x);
  SvfCell lp, shelf;
  lp.Snap(DesignSvf(SvfShape::kLowPass, 400.0, 0.7071, 0.0, kFs));
  shelf.Snap(DesignSvf(SvfShape::kHighShelf, 2000.0, 0.7071, 6.0, kFs));
  double err = 0.0;
  for (size_t i = 0; i < kLen; ++i) {
    const double y = lp.Process(x.l[i]);
    err = std::max(err, std::abs(shelf.Process(x.l[i] - y) + y - out.l[i]));
  }
  REQUIRE(err < 1e-5);
}

TEST_CASE("LR tap recombines to the LR all-pass", "[routing]") {
  const Stereo x = Noise(kLen, 0.5, 6);
  EngineParams p;
  p.bands[0] = {BandType::kHp24Lr, 150.0};
  p.bands[0].tap = Tap::kAux;
  const Stereo y = Render(p, x);
  SvfCell ap[2];
  for (SvfCell& c : ap)
    c.Snap(DesignSvf(SvfShape::kAllPass, 150.0, kButterworthQ, 0.0, kFs));
  double err = 0.0;
  for (size_t i = 0; i < kLen; ++i) {
    err = std::max(err, std::abs(ap[0].Process(x.l[i]) - y.l[i]));
    err = std::max(err, std::abs(ap[1].Process(x.r[i]) - y.r[i]));
  }
  REQUIRE(err < 1e-5);
}

TEST_CASE("Split modes: L/R carries Main/Aux and M/S encodes them",
          "[routing]") {
  const Stereo x = Noise(kLen, 0.5, 7);
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 2000.0, 6.0, 1.0};
  p.bands[1] = {BandType::kHp24Lr, 150.0};
  p.bands[1].tap = Tap::kAux;
  p.bands[2] = {BandType::kLowShelf, 80.0, 4.0, 0.7071};

  p.global.output_mode = OutputMode::kSplitLr;
  const Stereo lr = Render(p, x);
  p.global.output_mode = OutputMode::kSplitMs;
  const Stereo ms = Render(p, x);
  p.global.output_mode = OutputMode::kStereo;

  double err = 0.0;
  for (size_t i = 0; i < kLen; ++i) {
    err = std::max(err, std::abs(double(ms.l[i]) + ms.r[i] - lr.l[i]));
    err = std::max(err, std::abs(double(ms.l[i]) - ms.r[i] - lr.r[i]));
  }
  REQUIRE(err < 1e-6);

  // Main + Aux in split L/R equals the stereo-mode output for a mono input.
  Stereo mono = x;
  for (size_t i = 0; i < kLen; ++i)
    mono.l[i] = mono.r[i] = 0.5f * (x.l[i] + x.r[i]);
  const Stereo stereo = Render(p, mono);
  double sum_err = 0.0;
  for (size_t i = 0; i < kLen; ++i)
    sum_err =
        std::max(sum_err, std::abs(double(lr.l[i]) + lr.r[i] - stereo.l[i]));
  REQUIRE(sum_err < 1e-5);
}

TEST_CASE("Chan routing leaves the other component untouched", "[routing]") {
  const Stereo x = Noise(kLen, 0.5, 8);
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 1000.0, 12.0, 1.0};

  p.bands[0].chan = Chan::kLeft;
  Stereo y = Render(p, x);
  for (size_t i = 0; i < kLen; ++i) REQUIRE(y.r[i] == x.r[i]);

  p.bands[0].chan = Chan::kMid;
  y = Render(p, x);
  double side_err = 0.0;
  for (size_t i = 0; i < kLen; ++i)
    side_err = std::max(side_err, std::abs((double(y.l[i]) - y.r[i]) -
                                           (double(x.l[i]) - x.r[i])));
  REQUIRE(side_err < 1e-6);
}

TEST_CASE("Delta listen outputs processed minus dry", "[routing]") {
  const Stereo x = Noise(kLen, 0.5, 9);
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 500.0, 6.0, 2.0};
  p.bands[1] = {BandType::kHighShelf, 8000.0, -4.0, 0.7071};
  const Stereo wet = Render(p, x);
  p.global.delta = true;
  const Stereo delta = Render(p, x);
  double err = 0.0;
  for (size_t i = 0; i < kLen; ++i)
    err = std::max(err, std::abs(double(delta.l[i]) + x.l[i] - wet.l[i]));
  REQUIRE(err < 1e-6);
}

TEST_CASE("Global bypass passes the input through", "[routing]") {
  const Stereo x = Noise(kLen, 0.5, 10);
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 500.0, 6.0, 2.0};
  p.global.bypass = true;
  REQUIRE(MaxAbsDiff(Render(p, x), x) == 0.0);
}

TEST_CASE("FadeInFromDry starts dry and crossfades to wet", "[routing]") {
  const Stereo x = Noise(kLen, 0.5, 12);
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 500.0, 12.0, 2.0};
  const Stereo wet = Render(p, x);
  Engine e;
  e.SetParams(p);
  e.Prepare(kFs);
  e.FadeInFromDry();
  Stereo y(x.size());
  const float* in[2] = {x.l.data(), x.r.data()};
  float* out[2] = {y.l.data(), y.r.data()};
  e.Process(in, nullptr, out, static_cast<int>(kLen));
  // Starts (one ramp step from) dry, is fully wet after the fade, and has no
  // step on the way.
  const size_t fade = static_cast<size_t>(Engine::kBypassFadeMs * kFs / 1000);
  REQUIRE(std::abs(double(y.l[0]) - x.l[0]) <=
          std::abs(double(wet.l[0]) - x.l[0]) * 2.0 / fade + 1e-7);
  for (size_t i = fade; i < kLen; ++i) REQUIRE(y.l[i] == wet.l[i]);
  double max_step = 0.0, max_input_step = 0.0;
  for (size_t i = 1; i < fade; ++i) {
    max_step = std::max(max_step, std::abs(double(y.l[i]) - y.l[i - 1]));
    max_input_step =
        std::max(max_input_step, std::abs(double(wet.l[i]) - wet.l[i - 1]));
  }
  REQUIRE(max_step <= max_input_step * 1.01);
}

TEST_CASE("Solo auditions the band's detector filter", "[routing]") {
  const Stereo x = Noise(kLen, 0.5, 11);
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 1000.0, 6.0, 4.0};
  p.bands[0].solo = true;
  const Stereo y = Render(p, x);
  DetectorFilter f;
  f.Apply(DesignDetector(BandType::kPeak, 1000.0, 4.0, kFs), 1);
  double err = 0.0;
  for (size_t i = 0; i < kLen; ++i)
    err = std::max(err, std::abs(f.Process(0, x.l[i]) - y.l[i]));
  REQUIRE(err < 1e-6);
}

}  // namespace eeq::test
