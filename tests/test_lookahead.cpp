// Lookahead (ABOUT.md section 3.B, D9).

#include <catch2/catch_test_macros.hpp>

#include "TestUtil.h"

using namespace eeq;
using namespace eeq::test;

namespace {

constexpr double kFs = 48000.0;
constexpr size_t kLen = 48000;

EngineParams dynamicParams(double lookaheadMs) {
    EngineParams p;
    p.bands[0] = {BandType::Peak, 3000.0, 0.0, 2.0};
    p.bands[0].dynGroup = 0;
    p.bands[0].dynRange = 9.0;
    p.bands[1] = {BandType::LowShelf, 120.0, 3.0, 0.7071};
    p.groups[0].input = DetectorInput::ExternalSC;
    p.groups[0].alpha = 1.0;
    p.groups[0].riseDb = 6.0;
    p.groups[0].kneeDb = 6.0;
    p.groups[0].attackMs = 1.0;
    p.global.lookaheadMs = lookaheadMs;
    return p;
}

Stereo render(const EngineParams& p, const Stereo& x, const Stereo* sc = nullptr) {
    Engine e;
    e.setParams(p);
    e.prepare(kFs);
    return run(e, x, sc, 100);
}

}  // namespace

TEST_CASE("Latency matches the lookahead", "[lookahead]") {
    for (double ms : {0.0, 2.5, 10.0}) {
        Engine e;
        EngineParams p;
        p.global.lookaheadMs = ms;
        e.setParams(p);
        e.prepare(44100.0);
        REQUIRE(e.latencySamples() == static_cast<int>(std::lround(ms * 44.1)));
    }
}

TEST_CASE("Static chain with lookahead is a pure delay", "[lookahead]") {
    const Stereo x = noise(kLen, 0.5, 20);
    EngineParams p;
    p.bands[0] = {BandType::Peak, 1000.0, 6.0, 1.0};
    p.bands[1] = {BandType::HP24_LR, 200.0};
    p.bands[1].tap = Tap::Aux;
    const Stereo zero = render(p, x);
    p.global.lookaheadMs = 5.0;
    REQUIRE(maxAbsDiff(render(p, x), delayed(zero, 240)) == 0.0);
}

TEST_CASE("Lookahead lets the detector run ahead of the audio", "[lookahead]") {
    const size_t d = 480;  // 10 ms
    const Stereo x = noise(kLen, 0.3, 21);
    Stereo sc(kLen);
    for (size_t i = 0; i < kLen; ++i) sc.l[i] = sc.r[i] = (i / 4800) % 2 ? x.l[i] : 0.01f * x.l[i];
    // Engine with lookahead L == engine without it, fed audio delayed by L.
    const Stereo withLookahead = render(dynamicParams(10.0), x, &sc);
    const Stereo reference = render(dynamicParams(0.0), delayed(x, d), &sc);
    REQUIRE(maxAbsDiff(withLookahead, reference) == 0.0);
}

TEST_CASE("Bypass and delta stay aligned with lookahead", "[lookahead]") {
    const Stereo x = noise(kLen, 0.5, 22);
    EngineParams p = dynamicParams(5.0);
    p.global.bypass = true;
    REQUIRE(maxAbsDiff(render(p, x), delayed(x, 240)) == 0.0);

    p.global.bypass = false;
    p.bands[0].dynGroup = -1;
    const Stereo wet = render(p, x);
    p.global.delta = true;
    const Stereo delta = render(p, x);
    const Stereo dry = delayed(x, 240);
    double err = 0.0;
    for (size_t i = 0; i < kLen; ++i) err = std::max(err, std::abs(double(delta.l[i]) + dry.l[i] - wet.l[i]));
    REQUIRE(err < 1e-6);
}
