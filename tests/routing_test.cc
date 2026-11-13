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
  const BandType types[] = {BandType::kPeak,
                            BandType::kLowShelf,
                            BandType::kBandShelf,
                            BandType::kHighShelf,
                            BandType::kLp,
                            BandType::kHp,
                            BandType::kNotch,
                            BandType::kBp,
                            BandType::kTilt,
                            BandType::kVowel,
                            BandType::kFletcherMunson,
                            BandType::kKWeight,
                            BandType::kVintageLow,
                            BandType::kVintageHigh};
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

}  // namespace

TEST_CASE("All bands off or bypassed is bit-exact identity", "[routing]") {
  const Stereo x = Noise(kLen, 0.9, 3);
  EngineParams p;
  REQUIRE(MaxAbsDiff(Render(p, x), x) == 0.0);
  p.bands[2] = {BandType::kPeak, 1000.0, 9.0, 1.0};
  p.bands[2].bypass = true;
  p.bands[5] = {BandType::kLp, 300.0, 0.0, kButterworthQ, kSlope24};
  p.bands[5].bypass = true;
  REQUIRE(MaxAbsDiff(Render(p, x), x) == 0.0);
}

Stereo Mono(const Stereo& x) {
  Stereo m = x;
  for (size_t i = 0; i < x.l.size(); ++i)
    m.l[i] = m.r[i] = 0.5f * (x.l[i] + x.r[i]);
  return m;
}

double MaxSumErr(const Stereo& a, const Stereo& b, const Stereo& sum) {
  double err = 0.0;
  for (size_t i = 0; i < sum.l.size(); ++i) {
    err = std::max(err, std::abs(double(a.l[i]) + b.l[i] - sum.l[i]));
    err = std::max(err, std::abs(double(a.r[i]) + b.r[i] - sum.r[i]));
  }
  return err;
}

TEST_CASE("Split side plus residual recombines to the input", "[routing]") {
  // With bands 5-8 off, the split side and the residual sum to the input,
  // whatever bands 1-4 do (including per-band Chan).
  std::mt19937 rng(GENERATE(range(1u, 21u)));
  const Stereo x = Noise(kLen, 0.5, 4);
  EngineParams p = RandomChain(rng);
  for (int b = kSplitBands; b < kNumBands; ++b)
    p.bands[b].type = BandType::kOff;
  p.global.output_mode = OutputMode::kSplit;
  const Stereo side = Render(p, x);
  p.global.output_mode = OutputMode::kResidual;
  const Stereo residual = Render(p, x);
  REQUIRE(MaxSumErr(side, residual, x) < 1e-5);
}

TEST_CASE("Bands after the split shape only the residual", "[routing]") {
  // Split side = LP(x); residual = shelf(x - LP(x)).
  const Stereo x = Noise(kLen, 0.5, 15);
  EngineParams p;
  p.bands[0] = {BandType::kLp, 400.0, 0.0, 0.7071};
  p.bands[kSplitBands] = {BandType::kHighShelf, 2000.0, 6.0, 0.7071};
  p.global.output_mode = OutputMode::kSplit;
  const Stereo side = Render(p, x);
  p.global.output_mode = OutputMode::kResidual;
  const Stereo residual = Render(p, x);
  SvfCell lp, shelf;
  lp.Snap(DesignSvf(SvfShape::kLowPass, 400.0, 0.7071, 0.0, kFs));
  shelf.Snap(DesignSvf(SvfShape::kHighShelf, 2000.0, 0.7071, 6.0, kFs));
  double err = 0.0;
  for (size_t i = 0; i < kLen; ++i) {
    const double y = lp.Process(x.l[i]);
    err = std::max(err, std::abs(y - side.l[i]));
    err = std::max(err, std::abs(shelf.Process(x.l[i] - y) - residual.l[i]));
  }
  REQUIRE(err < 1e-5);
  // Normal mode runs all the bands in series.
  p.global.output_mode = OutputMode::kNormal;
  const Stereo stereo = Render(p, x);
  lp.Reset();
  shelf.Reset();
  double chain_err = 0.0;
  for (size_t i = 0; i < kLen; ++i) {
    chain_err = std::max(
        chain_err, std::abs(shelf.Process(lp.Process(x.l[i])) - stereo.l[i]));
  }
  REQUIRE(chain_err < 1e-5);
}

TEST_CASE("A lone crossover band splits into LR bands", "[routing]") {
  // HP 24 dB/oct alone on the split side: the residual is its LR low band,
  // and the two sum to the LR all-pass.
  const Stereo x = Noise(kLen, 0.5, 6);
  EngineParams p;
  p.bands[1] = {BandType::kHp, 150.0, 0.0, kButterworthQ, kSlope24};
  p.global.output_mode = OutputMode::kSplit;
  const Stereo side = Render(p, x);
  p.global.output_mode = OutputMode::kResidual;
  const Stereo residual = Render(p, x);
  SvfCell ap[2];
  for (SvfCell& c : ap)
    c.Snap(DesignSvf(SvfShape::kAllPass, 150.0, kButterworthQ, 0.0, kFs));
  double err = 0.0;
  for (size_t i = 0; i < kLen; ++i) {
    err = std::max(err, std::abs(ap[0].Process(x.l[i]) -
                                 (double(side.l[i]) + residual.l[i])));
    err = std::max(err, std::abs(ap[1].Process(x.r[i]) -
                                 (double(side.r[i]) + residual.r[i])));
  }
  REQUIRE(err < 1e-5);
}

TEST_CASE("Split L/R carries the split side and residual of a mono input",
          "[routing]") {
  const Stereo x = Noise(kLen, 0.5, 7);
  EngineParams p;
  p.bands[0] = {BandType::kBp, 2000.0, 0.0, 1.0};
  p.bands[2] = {BandType::kPeak, 3000.0, 4.0, 2.0};
  p.bands[5] = {BandType::kLowShelf, 80.0, 4.0, 0.7071};
  p.global.output_mode = OutputMode::kSplitLr;
  const Stereo lr = Render(p, x);
  p.global.output_mode = OutputMode::kSplit;
  const Stereo side = Render(p, Mono(x));
  p.global.output_mode = OutputMode::kResidual;
  const Stereo residual = Render(p, Mono(x));
  double err = 0.0;
  for (size_t i = 0; i < kLen; ++i) {
    err = std::max(err, std::abs(double(lr.l[i]) - side.l[i]));
    err = std::max(err, std::abs(double(lr.r[i]) - residual.l[i]));
  }
  REQUIRE(err < 1e-6);
}

TEST_CASE("With no band on the split side, nothing is split off", "[routing]") {
  const Stereo x = Noise(kLen, 0.5, 5);
  EngineParams p;
  p.bands[1] = {BandType::kLp, 300.0, 0.0, kButterworthQ};
  p.bands[1].bypass = true;
  p.bands[6] = {BandType::kPeak, 1000.0, 6.0, 1.0};
  const Stereo stereo = Render(p, x);
  p.global.output_mode = OutputMode::kSplit;
  REQUIRE(MaxAbsDiff(Render(p, x), Stereo(kLen)) == 0.0);
  p.global.output_mode = OutputMode::kResidual;
  REQUIRE(MaxAbsDiff(Render(p, x), stereo) < 1e-6);
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

TEST_CASE("Bypass keeps the detectors running and crossfades back",
          "[routing]") {
  const Stereo x = Noise(kLen, 0.5, 13);
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 500.0, 12.0, 2.0};
  p.bands[0].dyn_group = 0;
  p.bands[0].dyn_range = -12.0;
  p.groups[0].input = DetectorInput::kInternalBandSum;
  p.groups[0].threshold_db = -30.0;
  p.groups[0].release_ms = 1000.0;
  const Stereo wet = Render(p, x);
  Engine e, ref;
  for (Engine* engine : {&e, &ref}) {
    engine->SetParams(p);
    engine->Prepare(kFs);
  }
  Stereo y(x.size()), y_ref(x.size());
  const size_t on = 4000, off = 8000;
  auto run = [&](size_t from, size_t to) {
    const float* in[2] = {x.l.data() + from, x.r.data() + from};
    float* out[2] = {y.l.data() + from, y.r.data() + from};
    float* out_ref[2] = {y_ref.l.data() + from, y_ref.r.data() + from};
    e.Process(in, nullptr, out, static_cast<int>(to - from));
    ref.Process(in, nullptr, out_ref, static_cast<int>(to - from));
  };
  run(0, on);
  p.global.bypass = true;
  e.SetParams(p);
  run(on, off);
  // The detector ran through the bypass exactly as if it hadn't happened.
  REQUIRE(e.group(0).control(0) > 0.1);
  REQUIRE(std::abs(e.group(0).control(0) - ref.group(0).control(0)) < 1e-12);
  p.global.bypass = false;
  e.SetParams(p);
  run(off, kLen);
  // Dry once the fade in is done, with no step at the release, and back to
  // the never-bypassed output once the filters have settled.
  const size_t fade = static_cast<size_t>(Engine::kBypassFadeMs * kFs / 1000);
  for (size_t i = on + fade; i < off; ++i) REQUIRE(y.l[i] == x.l[i]);
  double max_step = 0.0, max_input_step = 0.0;
  for (size_t i = off - 10; i < off + fade; ++i) {
    max_step = std::max(max_step, std::abs(double(y.l[i]) - y.l[i - 1]));
    max_input_step =
        std::max(max_input_step, std::abs(double(y_ref.l[i]) - y_ref.l[i - 1]));
  }
  REQUIRE(max_step <= max_input_step * 1.5);
  double err = 0.0;
  for (size_t i = off + 4800; i < kLen; ++i)
    err = std::max(err, std::abs(double(y.l[i]) - y_ref.l[i]));
  REQUIRE(err < 1e-4);
  REQUIRE(MaxAbsDiff(y_ref, wet) == 0.0);
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

TEST_CASE("Dyn B subscribes a band to its group's Band-Sum detector",
          "[routing]") {
  // A band subscribed only through Dyn B still feeds Internal Band-Sum.
  const Stereo x = Sine(kLen, 1000.0, 0.5, kFs);
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 1000.0, 0.0, 2.0};
  p.bands[0].dyn_b_group = 2;
  p.bands[0].dyn_b_range = -6.0;
  p.groups[2].input = DetectorInput::kInternalBandSum;
  p.groups[2].threshold_db = -40.0;
  Engine e;
  e.SetParams(p);
  e.Prepare(kFs);
  Run(e, x);
  REQUIRE(e.group(2).control(0) > 0.9);
  // The band ducked by Dyn B: the 1 kHz tone is about 6 dB down.
  const Stereo y = Run(e, x);
  double peak = 0.0;
  for (size_t i = kLen / 2; i < kLen; ++i)
    peak = std::max(peak, std::abs(double(y.l[i])));
  REQUIRE(std::abs(20.0 * std::log10(peak / 0.5) + 6.0) < 0.3);
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
