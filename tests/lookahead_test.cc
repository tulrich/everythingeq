// Lookahead (ABOUT.md section 3.B, D9).

#include <catch2/catch_test_macros.hpp>

#include "tests/test_util.h"

namespace eeq::test {

namespace {

constexpr double kFs = 48000.0;
constexpr size_t kLen = 48000;

EngineParams DynamicParams(double lookahead_ms) {
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 3000.0, 0.0, 2.0};
  p.bands[0].dyn_group = 0;
  p.bands[0].dyn_range = 9.0;
  p.bands[1] = {BandType::kLowShelf, 120.0, 3.0, 0.7071};
  p.groups[0].input = DetectorInput::kExternalSc;
  p.groups[0].alpha = 1.0;
  p.groups[0].rise_db = 6.0;
  p.groups[0].knee_db = 6.0;
  p.groups[0].attack_ms = 1.0;
  p.global.lookahead_ms = lookahead_ms;
  return p;
}

Stereo Render(const EngineParams& p, const Stereo& x,
              const Stereo* sc = nullptr) {
  Engine e;
  e.SetParams(p);
  e.Prepare(kFs);
  return Run(e, x, sc, 100);
}

}  // namespace

TEST_CASE("Latency matches the lookahead", "[lookahead]") {
  for (double ms : {0.0, 2.5, 10.0}) {
    Engine e;
    EngineParams p;
    p.global.lookahead_ms = ms;
    e.SetParams(p);
    e.Prepare(44100.0);
    REQUIRE(e.latency_samples() == static_cast<int>(std::lround(ms * 44.1)));
  }
}

TEST_CASE("Static chain with lookahead is a pure delay", "[lookahead]") {
  const Stereo x = Noise(kLen, 0.5, 20);
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 1000.0, 6.0, 1.0};
  p.bands[1] = {BandType::kHp24Lr, 200.0};
  p.bands[1].tap = Tap::kAux;
  const Stereo zero = Render(p, x);
  p.global.lookahead_ms = 5.0;
  REQUIRE(MaxAbsDiff(Render(p, x), Delayed(zero, 240)) == 0.0);
}

TEST_CASE("Lookahead lets the detector run ahead of the audio", "[lookahead]") {
  const size_t d = 480;  // 10 ms
  const Stereo x = Noise(kLen, 0.3, 21);
  Stereo sc(kLen);
  for (size_t i = 0; i < kLen; ++i)
    sc.l[i] = sc.r[i] = (i / 4800) % 2 ? x.l[i] : 0.01f * x.l[i];
  // Engine with lookahead L == engine without it, fed audio delayed by L.
  const Stereo with_lookahead = Render(DynamicParams(10.0), x, &sc);
  const Stereo reference = Render(DynamicParams(0.0), Delayed(x, d), &sc);
  REQUIRE(MaxAbsDiff(with_lookahead, reference) == 0.0);
}

TEST_CASE("Bypass and delta stay aligned with lookahead", "[lookahead]") {
  const Stereo x = Noise(kLen, 0.5, 22);
  EngineParams p = DynamicParams(5.0);
  p.global.bypass = true;
  REQUIRE(MaxAbsDiff(Render(p, x), Delayed(x, 240)) == 0.0);

  p.global.bypass = false;
  p.bands[0].dyn_group = -1;
  const Stereo wet = Render(p, x);
  p.global.delta = true;
  const Stereo delta = Render(p, x);
  const Stereo dry = Delayed(x, 240);
  double err = 0.0;
  for (size_t i = 0; i < kLen; ++i)
    err = std::max(err, std::abs(double(delta.l[i]) + dry.l[i] - wet.l[i]));
  REQUIRE(err < 1e-6);
}

}  // namespace eeq::test
