// Phase 3: chain, routing and output modes (ABOUT.md section 3.D).

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "TestUtil.h"

using namespace eeq;
using namespace eeq::test;

namespace {

constexpr double kFs = 48000.0;
constexpr size_t kLen = 24000;

Stereo render(const EngineParams& p, const Stereo& x) {
    Engine e;
    e.setParams(p);
    e.prepare(kFs);
    return run(e, x);
}

// Random static chain of gain-type and filter-type bands.
EngineParams randomChain(std::mt19937& rng) {
    const BandType types[] = {BandType::Peak, BandType::LowShelf, BandType::HighShelf, BandType::LP12,
                              BandType::HP12, BandType::Notch, BandType::BP, BandType::Tilt,
                              BandType::Vowel, BandType::FletcherMunson, BandType::KWeight};
    std::uniform_int_distribution<int> type(0, std::size(types) - 1);
    std::uniform_real_distribution<double> oct(0.0, 10.0), gain(-12.0, 12.0), q(0.3, 6.0), morph(0.0, 4.0);
    EngineParams p;
    const int n = std::uniform_int_distribution<int>(3, kNumBands)(rng);
    for (int b = 0; b < n; ++b) {
        BandParams& bp = p.bands[b];
        bp.type = types[type(rng)];
        bp.x = bp.type == BandType::Vowel ? morph(rng) : 20.0 * std::exp2(oct(rng));
        bp.y = gain(rng);
        bp.z = bp.type == BandType::Vowel ? 1.0 : q(rng);
        bp.chan = static_cast<Chan>(std::uniform_int_distribution<int>(0, 4)(rng));
    }
    return p;
}

int lastActiveBand(const EngineParams& p) {
    int last = -1;
    for (int b = 0; b < kNumBands; ++b)
        if (p.bands[b].type != BandType::Off) last = b;
    return last;
}

}  // namespace

TEST_CASE("All bands off or bypassed is bit-exact identity", "[routing]") {
    const Stereo x = noise(kLen, 0.9, 3);
    EngineParams p;
    REQUIRE(maxAbsDiff(render(p, x), x) == 0.0);
    p.bands[2] = {BandType::Peak, 1000.0, 9.0, 1.0};
    p.bands[2].bypass = true;
    p.bands[5] = {BandType::LP24_LR, 300.0};
    p.bands[5].bypass = true;
    REQUIRE(maxAbsDiff(render(p, x), x) == 0.0);
}

TEST_CASE("Delta tap on the last band recombines to the untapped chain", "[routing]") {
    std::mt19937 rng(GENERATE(range(1u, 21u)));
    const Stereo x = noise(kLen, 0.5, 4);
    EngineParams p = randomChain(rng);
    const int last = lastActiveBand(p);
    if (isFilterType(p.bands[last].type)) p.bands[last].type = BandType::Peak;
    const Stereo untapped = render(p, x);
    p.bands[last].tap = Tap::Aux;
    REQUIRE(maxAbsDiff(render(p, x), untapped) < 1e-5);
}

TEST_CASE("Split tap on the last band recombines to the chain without it", "[routing]") {
    std::mt19937 rng(GENERATE(range(1u, 21u)));
    const Stereo x = noise(kLen, 0.5, 5);
    EngineParams p = randomChain(rng);
    const int b = lastActiveBand(p);
    const BandType filters[] = {BandType::LP12, BandType::HP12, BandType::BP, BandType::Notch};
    p.bands[b].type = filters[rng() % 4];
    p.bands[b].tap = Tap::Aux;
    const Stereo tapped = render(p, x);
    p.bands[b].type = BandType::Off;
    REQUIRE(maxAbsDiff(tapped, render(p, x)) < 1e-5);
}

TEST_CASE("Mid-chain taps bypass the downstream bands for Aux only", "[routing]") {
    // Split tap at band 0, gain band after it: out = D(x - y) + y, with D the
    // downstream band and y the tapped filter output.
    const Stereo x = noise(kLen, 0.5, 15);
    EngineParams p;
    p.bands[0] = {BandType::LP12, 400.0, 0.0, 0.7071};
    p.bands[0].tap = Tap::Aux;
    p.bands[1] = {BandType::HighShelf, 2000.0, 6.0, 0.7071};
    const Stereo out = render(p, x);
    SvfCell lp, shelf;
    lp.snap(designSvf(SvfShape::LowPass, 400.0, 0.7071, 0.0, kFs));
    shelf.snap(designSvf(SvfShape::HighShelf, 2000.0, 0.7071, 6.0, kFs));
    double err = 0.0;
    for (size_t i = 0; i < kLen; ++i) {
        const double y = lp.process(x.l[i]);
        err = std::max(err, std::abs(shelf.process(x.l[i] - y) + y - out.l[i]));
    }
    REQUIRE(err < 1e-5);
}

TEST_CASE("LR tap recombines to the LR all-pass", "[routing]") {
    const Stereo x = noise(kLen, 0.5, 6);
    EngineParams p;
    p.bands[0] = {BandType::HP24_LR, 150.0};
    p.bands[0].tap = Tap::Aux;
    const Stereo y = render(p, x);
    SvfCell ap[2];
    for (SvfCell& c : ap) c.snap(designSvf(SvfShape::AllPass, 150.0, kButterworthQ, 0.0, kFs));
    double err = 0.0;
    for (size_t i = 0; i < kLen; ++i) {
        err = std::max(err, std::abs(ap[0].process(x.l[i]) - y.l[i]));
        err = std::max(err, std::abs(ap[1].process(x.r[i]) - y.r[i]));
    }
    REQUIRE(err < 1e-5);
}

TEST_CASE("Split modes: L/R carries Main/Aux and M/S encodes them", "[routing]") {
    const Stereo x = noise(kLen, 0.5, 7);
    EngineParams p;
    p.bands[0] = {BandType::Peak, 2000.0, 6.0, 1.0};
    p.bands[1] = {BandType::HP24_LR, 150.0};
    p.bands[1].tap = Tap::Aux;
    p.bands[2] = {BandType::LowShelf, 80.0, 4.0, 0.7071};

    p.global.outputMode = OutputMode::SplitLR;
    const Stereo lr = render(p, x);
    p.global.outputMode = OutputMode::SplitMS;
    const Stereo ms = render(p, x);
    p.global.outputMode = OutputMode::Stereo;

    double err = 0.0;
    for (size_t i = 0; i < kLen; ++i) {
        err = std::max(err, std::abs(double(ms.l[i]) + ms.r[i] - lr.l[i]));
        err = std::max(err, std::abs(double(ms.l[i]) - ms.r[i] - lr.r[i]));
    }
    REQUIRE(err < 1e-6);

    // Main + Aux in split L/R equals the stereo-mode output for a mono input.
    Stereo mono = x;
    for (size_t i = 0; i < kLen; ++i) mono.l[i] = mono.r[i] = 0.5f * (x.l[i] + x.r[i]);
    const Stereo stereo = render(p, mono);
    double sumErr = 0.0;
    for (size_t i = 0; i < kLen; ++i) sumErr = std::max(sumErr, std::abs(double(lr.l[i]) + lr.r[i] - stereo.l[i]));
    REQUIRE(sumErr < 1e-5);
}

TEST_CASE("Chan routing leaves the other component untouched", "[routing]") {
    const Stereo x = noise(kLen, 0.5, 8);
    EngineParams p;
    p.bands[0] = {BandType::Peak, 1000.0, 12.0, 1.0};

    p.bands[0].chan = Chan::Left;
    Stereo y = render(p, x);
    for (size_t i = 0; i < kLen; ++i) REQUIRE(y.r[i] == x.r[i]);

    p.bands[0].chan = Chan::Mid;
    y = render(p, x);
    double sideErr = 0.0;
    for (size_t i = 0; i < kLen; ++i)
        sideErr = std::max(sideErr, std::abs((double(y.l[i]) - y.r[i]) - (double(x.l[i]) - x.r[i])));
    REQUIRE(sideErr < 1e-6);
}

TEST_CASE("Delta listen outputs processed minus dry", "[routing]") {
    const Stereo x = noise(kLen, 0.5, 9);
    EngineParams p;
    p.bands[0] = {BandType::Peak, 500.0, 6.0, 2.0};
    p.bands[1] = {BandType::HighShelf, 8000.0, -4.0, 0.7071};
    const Stereo wet = render(p, x);
    p.global.delta = true;
    const Stereo delta = render(p, x);
    double err = 0.0;
    for (size_t i = 0; i < kLen; ++i) err = std::max(err, std::abs(double(delta.l[i]) + x.l[i] - wet.l[i]));
    REQUIRE(err < 1e-6);
}

TEST_CASE("Global bypass passes the input through", "[routing]") {
    const Stereo x = noise(kLen, 0.5, 10);
    EngineParams p;
    p.bands[0] = {BandType::Peak, 500.0, 6.0, 2.0};
    p.global.bypass = true;
    REQUIRE(maxAbsDiff(render(p, x), x) == 0.0);
}

TEST_CASE("Solo auditions the band's detector filter", "[routing]") {
    const Stereo x = noise(kLen, 0.5, 11);
    EngineParams p;
    p.bands[0] = {BandType::Peak, 1000.0, 6.0, 4.0};
    p.bands[0].solo = true;
    const Stereo y = render(p, x);
    DetectorFilter f;
    f.apply(designDetector(BandType::Peak, 1000.0, 4.0, kFs), 1);
    double err = 0.0;
    for (size_t i = 0; i < kLen; ++i) err = std::max(err, std::abs(f.process(0, x.l[i]) - y.l[i]));
    REQUIRE(err < 1e-6);
}
