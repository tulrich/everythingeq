// Phase 4: dynamics engine (ABOUT.md section 3.B).

#include <catch2/catch_test_macros.hpp>

#include "TestUtil.h"
#include "dynamics/DynamicsGroup.h"

using namespace eeq;
using namespace eeq::test;

namespace {

constexpr double kFs = 48000.0;
constexpr double kProbeHz = 8000.0;  // high enough that x^2 ripple through the 1 ms tracker is small

DynamicsGroup makeGroup(const GroupParams& p) {
    DynamicsGroup g;
    g.setParams(p);
    g.prepare(kFs);
    return g;
}

double sineAt(size_t i, double amp) { return amp * std::sin(2.0 * kPi * kProbeHz * i / kFs); }

}  // namespace

TEST_CASE("Gain computer knee and modes", "[dynamics]") {
    using G = DynamicsGroup;
    REQUIRE(G::gainComputer(-10, -20, 0, TriggerMode::Above) == 1.0);
    REQUIRE(G::gainComputer(-30, -20, 0, TriggerMode::Above) == 0.0);
    REQUIRE(G::gainComputer(-30, -20, 0, TriggerMode::Below) == 1.0);
    REQUIRE(G::gainComputer(-20, -20, 10, TriggerMode::Above) == 0.5);
    REQUIRE(G::gainComputer(-15, -20, 10, TriggerMode::Above) == 1.0);
    REQUIRE(G::gainComputer(-25, -20, 10, TriggerMode::Above) == 0.0);
    REQUIRE(G::gainComputer(-22.5, -20, 10, TriggerMode::Below) == 0.75);
}

TEST_CASE("Alpha = 0 tracks level", "[dynamics]") {
    GroupParams p;
    p.alpha = 0.0;
    DynamicsGroup g = makeGroup(p);
    size_t i = 0;
    for (double ampDb : {-40.0, -12.0, -24.0, -3.0}) {
        const double amp = dbToGain(ampDb);
        double lo = 1e9, hi = -1e9;
        for (size_t n = 0; n < static_cast<size_t>(0.1 * kFs); ++n, ++i) {
            const double s = sineAt(i, amp);
            g.process(s, s);
            if (n > static_cast<size_t>(0.05 * kFs)) lo = std::min(lo, g.trigger(0)), hi = std::max(hi, g.trigger(0));
        }
        const double expected = ampDb - 3.0103;  // sine power
        CAPTURE(ampDb, lo, hi);
        REQUIRE(std::abs(0.5 * (lo + hi) - expected) < 0.1);
    }
}

TEST_CASE("Alpha = 1 is level independent", "[dynamics]") {
    GroupParams p;
    p.alpha = 1.0;
    p.rmsWindowMs = 50.0;

    // Steady sines read ~0 dB of rise at any level.
    for (double ampDb : {-40.0, -6.0}) {
        DynamicsGroup g = makeGroup(p);
        for (size_t i = 0; i < static_cast<size_t>(kFs); ++i) g.process(sineAt(i, dbToGain(ampDb)), 0.0);
        CAPTURE(ampDb, g.trigger(0));
        REQUIRE(std::abs(g.trigger(0)) < 0.5);
    }

    // Bursts 20 dB above a background give the same peak trigger at any level.
    auto peakRise = [&](double levelDb) {
        DynamicsGroup g = makeGroup(p);
        double peak = -1e9;
        for (size_t i = 0; i < static_cast<size_t>(kFs); ++i) {
            const bool burst = (i % 12000) < 480;  // 10 ms burst every 250 ms
            const double amp = dbToGain(burst ? levelDb : levelDb - 20.0);
            g.process(sineAt(i, amp), 0.0);
            if (i > 12000) peak = std::max(peak, g.trigger(0));
        }
        return peak;
    };
    const double quiet = peakRise(-40.0), loud = peakRise(-6.0);
    CAPTURE(quiet, loud);
    REQUIRE(quiet > 6.0);
    REQUIRE(std::abs(quiet - loud) < 1.0);
}

TEST_CASE("Threshold and Rise blend with alpha", "[dynamics]") {
    GroupParams p;
    p.thresholdDb = -30.0;
    p.riseDb = 10.0;
    p.alpha = 0.25;
    REQUIRE(std::abs(makeGroup(p).effectiveThreshold() - (0.75 * -30.0 + 0.25 * 10.0)) < 1e-12);
}

TEST_CASE("Attack, hold and release timing", "[dynamics]") {
    GroupParams p;
    p.thresholdDb = -20.0;
    p.kneeDb = 0.0;
    p.attackMs = 10.0;
    p.holdMs = 20.0;
    p.releaseMs = 100.0;
    DynamicsGroup g = makeGroup(p);

    const size_t on = static_cast<size_t>(0.2 * kFs), off = static_cast<size_t>(0.6 * kFs);
    long crossUp = -1, reach63 = -1, crossDown = -1, leaveHold = -1, fall63 = -1;
    double held = 0.0;
    for (size_t i = 0; i < static_cast<size_t>(1.2 * kFs); ++i) {
        const double s = (i >= on && i < off) ? sineAt(i, 0.5) : 0.0;
        g.process(s, s);
        const long n = static_cast<long>(i);
        if (crossUp < 0 && g.trigger(0) > p.thresholdDb) crossUp = n;
        if (crossUp >= 0 && reach63 < 0 && g.control(0) >= 1.0 - std::exp(-1.0)) reach63 = n;
        if (i >= off && crossDown < 0 && g.trigger(0) < p.thresholdDb) crossDown = n, held = g.control(0);
        if (crossDown >= 0 && leaveHold < 0 && g.control(0) < held) leaveHold = n;
        if (leaveHold >= 0 && fall63 < 0 && g.control(0) <= held * std::exp(-1.0)) fall63 = n;
    }
    const double attackMs = (reach63 - crossUp) * 1000.0 / kFs;
    const double holdMs = (leaveHold - crossDown) * 1000.0 / kFs;
    const double releaseMs = (fall63 - leaveHold) * 1000.0 / kFs;
    CAPTURE(attackMs, holdMs, releaseMs);
    REQUIRE(std::abs(attackMs / p.attackMs - 1.0) < 0.05);
    REQUIRE(std::abs(holdMs / p.holdMs - 1.0) < 0.05);
    REQUIRE(std::abs(releaseMs / p.releaseMs - 1.0) < 0.05);
}

TEST_CASE("Stereo link", "[dynamics]") {
    GroupParams p;
    p.thresholdDb = -30.0;
    p.attackMs = 1.0;
    for (double link : {0.0, 1.0}) {
        p.link = link;
        DynamicsGroup g = makeGroup(p);
        for (size_t i = 0; i < static_cast<size_t>(0.1 * kFs); ++i) g.process(sineAt(i, 0.5), 0.0);
        CAPTURE(link);
        REQUIRE(g.control(0) > 0.99);
        REQUIRE(g.control(1) == (link == 1.0 ? g.control(0) : 0.0));
    }
}

TEST_CASE("Feed-forward band-sum detection cannot latch", "[dynamics]") {
    Engine e;
    EngineParams p;
    p.bands[0] = {BandType::Peak, 1000.0, 12.0, 2.0};
    p.bands[0].dynGroup = 0;
    p.bands[0].dynRange = 12.0;
    p.groups[0].input = DetectorInput::InternalBandSum;
    p.groups[0].thresholdDb = -30.0;
    p.groups[0].kneeDb = 6.0;
    p.groups[0].releaseMs = 50.0;
    e.setParams(p);
    e.prepare(kFs);

    const size_t len = static_cast<size_t>(60 * kFs);
    Stereo x = noise(len, 0.25, 12);
    const size_t silenceFrom = len - static_cast<size_t>(kFs);
    for (size_t i = silenceFrom; i < len; ++i) x.l[i] = x.r[i] = 0.0f;
    const Stereo y = run(e, x);
    REQUIRE(allFinite(y));
    REQUIRE(maxAbs(y) < 0.25 * dbToGain(24.0));
    REQUIRE(e.group(0).control(0) < 1e-3);  // released after the input stops
}

TEST_CASE("Resonant touch-wah sweep stress", "[dynamics]") {
    // Peak +12 dB, Q 6, frequency driven 20 Hz -> 0.45 fs by a 0/1 square-wave D.
    Engine e;
    EngineParams p;
    p.bands[0] = {BandType::Peak, 20.0, 12.0, 6.0};
    p.bands[0].dynGroup = 0;
    p.bands[0].dynTarget = DynTarget::Freq;
    p.bands[0].dynRange = std::log2(0.45 * kFs / 20.0);
    GroupParams& g = p.groups[0];
    g.input = DetectorInput::ExternalSC;
    g.thresholdDb = -20.0;
    g.attackMs = 0.5;
    g.releaseMs = 0.5;
    e.setParams(p);
    e.prepare(kFs);

    const size_t len = static_cast<size_t>(5 * kFs);
    const Stereo x = noise(len, 0.5, 13);
    Stereo sc(len);
    for (size_t i = 0; i < len; ++i) sc.l[i] = sc.r[i] = (i / 2400) % 2 ? static_cast<float>(sineAt(i, 0.5)) : 0.0f;
    const Stereo y = run(e, x, &sc);
    REQUIRE(allFinite(y));
    CAPTURE(maxAbs(y));
    REQUIRE(maxAbs(y) < 0.5 * dbToGain(12.0) * 2.0);
}

TEST_CASE("Vowel morph target sweeps the formants", "[dynamics]") {
    Engine e;
    EngineParams p;
    p.bands[0] = {BandType::Vowel, 0.0, 12.0, 1.0};
    p.bands[0].dynGroup = 0;
    p.bands[0].dynTarget = DynTarget::Morph;
    p.bands[0].dynRange = 4.0;
    p.groups[0].input = DetectorInput::ExternalSC;
    p.groups[0].thresholdDb = -20.0;
    p.groups[0].attackMs = 1.0;
    e.setParams(p);
    e.prepare(kFs);
    const size_t len = static_cast<size_t>(0.2 * kFs);
    Stereo sc(len);
    for (size_t i = 0; i < len; ++i) sc.l[i] = sc.r[i] = static_cast<float>(sineAt(i, 0.5));
    run(e, Stereo(len), &sc);
    // Fully triggered: morph 0 + 4 = /i/, so F2 sits at ~2290 Hz.
    const double f2 = std::atan(e.band(0).design(0).cells[1].g) * kFs / kPi;
    CAPTURE(f2);
    REQUIRE(std::abs(f2 / 2290.0 - 1.0) < 0.01);
}
