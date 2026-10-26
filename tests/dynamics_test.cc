// Phase 4: dynamics engine (ABOUT.md section 3.B).

#include <catch2/catch_test_macros.hpp>

#include "dsp/dynamics/dynamics_group.h"
#include "tests/test_util.h"

namespace eeq::test {

namespace {

constexpr double kFs = 48000.0;
constexpr double kProbeHz =
    8000.0;  // high enough that x^2 ripple through the 1 ms tracker is small

DynamicsGroup MakeGroup(const GroupParams& p) {
  DynamicsGroup g;
  g.SetParams(p);
  g.Prepare(kFs);
  return g;
}

double SineAt(size_t i, double amp) {
  return amp * std::sin(2.0 * kPi * kProbeHz * i / kFs);
}

}  // namespace

TEST_CASE("Gain computer knee and modes", "[dynamics]") {
  using G = DynamicsGroup;
  REQUIRE(G::GainComputer(-10, -20, 0, TriggerMode::kAbove) == 1.0);
  REQUIRE(G::GainComputer(-30, -20, 0, TriggerMode::kAbove) == 0.0);
  REQUIRE(G::GainComputer(-30, -20, 0, TriggerMode::kBelow) == 1.0);
  REQUIRE(G::GainComputer(-20, -20, 10, TriggerMode::kAbove) == 0.5);
  REQUIRE(G::GainComputer(-15, -20, 10, TriggerMode::kAbove) == 1.0);
  REQUIRE(G::GainComputer(-25, -20, 10, TriggerMode::kAbove) == 0.0);
  REQUIRE(G::GainComputer(-22.5, -20, 10, TriggerMode::kBelow) == 0.75);
}

TEST_CASE("Alpha = 0 tracks level", "[dynamics]") {
  GroupParams p;
  p.alpha = 0.0;
  DynamicsGroup g = MakeGroup(p);
  size_t i = 0;
  for (double amp_db : {-40.0, -12.0, -24.0, -3.0}) {
    const double amp = DbToGain(amp_db);
    double lo = 1e9, hi = -1e9;
    for (size_t n = 0; n < static_cast<size_t>(0.1 * kFs); ++n, ++i) {
      const double s = SineAt(i, amp);
      g.Process(s, s);
      if (n > static_cast<size_t>(0.05 * kFs))
        lo = std::min(lo, g.trigger(0)), hi = std::max(hi, g.trigger(0));
    }
    const double expected = amp_db - 3.0103;  // sine power
    CAPTURE(amp_db, lo, hi);
    REQUIRE(std::abs(0.5 * (lo + hi) - expected) < 0.1);
  }
}

TEST_CASE("Alpha = 1 is level independent", "[dynamics]") {
  GroupParams p;
  p.alpha = 1.0;
  p.rms_window_ms = 50.0;

  // Steady sines read ~0 dB of rise at any level.
  for (double amp_db : {-40.0, -6.0}) {
    DynamicsGroup g = MakeGroup(p);
    for (size_t i = 0; i < static_cast<size_t>(kFs); ++i)
      g.Process(SineAt(i, DbToGain(amp_db)), 0.0);
    CAPTURE(amp_db, g.trigger(0));
    REQUIRE(std::abs(g.trigger(0)) < 0.5);
  }

  // Bursts 20 dB above a background give the same peak trigger at any level.
  auto peak_rise = [&](double level_db) {
    DynamicsGroup g = MakeGroup(p);
    double peak = -1e9;
    for (size_t i = 0; i < static_cast<size_t>(kFs); ++i) {
      const bool burst = (i % 12000) < 480;  // 10 ms burst every 250 ms
      const double amp = DbToGain(burst ? level_db : level_db - 20.0);
      g.Process(SineAt(i, amp), 0.0);
      if (i > 12000) peak = std::max(peak, g.trigger(0));
    }
    return peak;
  };
  const double quiet = peak_rise(-40.0), loud = peak_rise(-6.0);
  CAPTURE(quiet, loud);
  REQUIRE(quiet > 6.0);
  REQUIRE(std::abs(quiet - loud) < 1.0);
}

TEST_CASE("Threshold and Rise blend with alpha", "[dynamics]") {
  GroupParams p;
  p.threshold_db = -30.0;
  p.rise_db = 10.0;
  p.alpha = 0.25;
  REQUIRE(std::abs(MakeGroup(p).effective_threshold() -
                   (0.75 * -30.0 + 0.25 * 10.0)) < 1e-12);
}

TEST_CASE("Attack, hold and release timing", "[dynamics]") {
  GroupParams p;
  p.threshold_db = -20.0;
  p.knee_db = 0.0;
  p.attack_ms = 10.0;
  p.hold_ms = 20.0;
  p.release_ms = 100.0;
  DynamicsGroup g = MakeGroup(p);

  const size_t on = static_cast<size_t>(0.2 * kFs),
               off = static_cast<size_t>(0.6 * kFs);
  long cross_up = -1, reach63 = -1, cross_down = -1, leave_hold = -1,
       fall63 = -1;
  double held = 0.0;
  for (size_t i = 0; i < static_cast<size_t>(1.2 * kFs); ++i) {
    const double s = (i >= on && i < off) ? SineAt(i, 0.5) : 0.0;
    g.Process(s, s);
    const long n = static_cast<long>(i);
    if (cross_up < 0 && g.trigger(0) > p.threshold_db) cross_up = n;
    if (cross_up >= 0 && reach63 < 0 && g.control(0) >= 1.0 - std::exp(-1.0))
      reach63 = n;
    if (i >= off && cross_down < 0 && g.trigger(0) < p.threshold_db)
      cross_down = n, held = g.control(0);
    if (cross_down >= 0 && leave_hold < 0 && g.control(0) < held)
      leave_hold = n;
    if (leave_hold >= 0 && fall63 < 0 && g.control(0) <= held * std::exp(-1.0))
      fall63 = n;
  }
  const double attack_ms = (reach63 - cross_up) * 1000.0 / kFs;
  const double hold_ms = (leave_hold - cross_down) * 1000.0 / kFs;
  const double release_ms = (fall63 - leave_hold) * 1000.0 / kFs;
  CAPTURE(attack_ms, hold_ms, release_ms);
  REQUIRE(std::abs(attack_ms / p.attack_ms - 1.0) < 0.05);
  REQUIRE(std::abs(hold_ms / p.hold_ms - 1.0) < 0.05);
  REQUIRE(std::abs(release_ms / p.release_ms - 1.0) < 0.05);
}

TEST_CASE("Stereo link", "[dynamics]") {
  GroupParams p;
  p.threshold_db = -30.0;
  p.attack_ms = 1.0;
  for (double link : {0.0, 1.0}) {
    p.link = link;
    DynamicsGroup g = MakeGroup(p);
    for (size_t i = 0; i < static_cast<size_t>(0.1 * kFs); ++i)
      g.Process(SineAt(i, 0.5), 0.0);
    CAPTURE(link);
    REQUIRE(g.control(0) > 0.99);
    REQUIRE(g.control(1) == (link == 1.0 ? g.control(0) : 0.0));
  }
}

TEST_CASE("Feed-forward band-sum detection cannot latch", "[dynamics]") {
  Engine e;
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 1000.0, 12.0, 2.0};
  p.bands[0].dyn_group = 0;
  p.bands[0].dyn_range = 12.0;
  p.groups[0].input = DetectorInput::kInternalBandSum;
  p.groups[0].threshold_db = -30.0;
  p.groups[0].knee_db = 6.0;
  p.groups[0].release_ms = 50.0;
  e.SetParams(p);
  e.Prepare(kFs);

  const size_t len = static_cast<size_t>(60 * kFs);
  Stereo x = Noise(len, 0.25, 12);
  const size_t silence_from = len - static_cast<size_t>(kFs);
  for (size_t i = silence_from; i < len; ++i) x.l[i] = x.r[i] = 0.0f;
  const Stereo y = Run(e, x);
  REQUIRE(AllFinite(y));
  REQUIRE(MaxAbs(y) < 0.25 * DbToGain(24.0));
  REQUIRE(e.group(0).control(0) < 1e-3);  // released after the input stops
}

TEST_CASE("Resonant touch-wah sweep stress", "[dynamics]") {
  // Peak +12 dB, Q 6, frequency driven 20 Hz -> 0.45 fs by a 0/1 square-wave D.
  Engine e;
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 20.0, 12.0, 6.0};
  p.bands[0].dyn_group = 0;
  p.bands[0].dyn_target = DynTarget::kFreq;
  p.bands[0].dyn_range = std::log2(0.45 * kFs / 20.0);
  GroupParams& g = p.groups[0];
  g.input = DetectorInput::kExternalSc;
  g.threshold_db = -20.0;
  g.attack_ms = 0.5;
  g.release_ms = 0.5;
  e.SetParams(p);
  e.Prepare(kFs);

  const size_t len = static_cast<size_t>(5 * kFs);
  const Stereo x = Noise(len, 0.5, 13);
  Stereo sc(len);
  for (size_t i = 0; i < len; ++i)
    sc.l[i] = sc.r[i] =
        (i / 2400) % 2 ? static_cast<float>(SineAt(i, 0.5)) : 0.0f;
  const Stereo y = Run(e, x, &sc);
  REQUIRE(AllFinite(y));
  CAPTURE(MaxAbs(y));
  REQUIRE(MaxAbs(y) < 0.5 * DbToGain(12.0) * 2.0);
}

TEST_CASE("Vowel morph target sweeps the formants", "[dynamics]") {
  Engine e;
  EngineParams p;
  p.bands[0] = {BandType::kVowel, 0.0, 12.0, 1.0};
  p.bands[0].dyn_group = 0;
  p.bands[0].dyn_target = DynTarget::kMorph;
  p.bands[0].dyn_range = 4.0;
  p.groups[0].input = DetectorInput::kExternalSc;
  p.groups[0].threshold_db = -20.0;
  p.groups[0].attack_ms = 1.0;
  e.SetParams(p);
  e.Prepare(kFs);
  const size_t len = static_cast<size_t>(0.2 * kFs);
  Stereo sc(len);
  for (size_t i = 0; i < len; ++i)
    sc.l[i] = sc.r[i] = static_cast<float>(SineAt(i, 0.5));
  Run(e, Stereo(len), &sc);
  // Fully triggered: morph 0 + 4 = /i/, so F2 sits at ~2290 Hz.
  const double f2 = std::atan(e.band(0).design(0).cells[1].g) * kFs / kPi;
  CAPTURE(f2);
  REQUIRE(std::abs(f2 / 2290.0 - 1.0) < 0.01);
}

}  // namespace eeq::test
