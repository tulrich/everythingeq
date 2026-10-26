// Phase 1: filter core (ABOUT.md section 3.A).

#include "dsp/svf/svf.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cfloat>

#include "tests/test_util.h"

namespace eeq::test {

namespace {

std::vector<double> ImpulseResponse(const SvfCoeffs& c, size_t n) {
  SvfCell cell;
  cell.Snap(c);
  std::vector<double> h(n);
  for (size_t i = 0; i < n; ++i) h[i] = cell.Process(i == 0 ? 1.0 : 0.0);
  return h;
}

struct ShapeCase {
  const char* name;
  SvfShape shape;
  double hz, q, gain_db;
};

const ShapeCase kShapes[] = {
    {"lowpass", SvfShape::kLowPass, 1000.0, 0.7071, 0.0},
    {"highpass", SvfShape::kHighPass, 200.0, 2.0, 0.0},
    {"bandpass", SvfShape::kBandPass, 3000.0, 4.0, 0.0},
    {"notch", SvfShape::kNotch, 500.0, 1.0, 0.0},
    {"allpass", SvfShape::kAllPass, 2000.0, 0.7071, 0.0},
    {"bell+", SvfShape::kBell, 100.0, 2.0, 9.0},
    {"bell-", SvfShape::kBell, 8000.0, 1.0, -12.0},
    {"lowshelf", SvfShape::kLowShelf, 150.0, 0.7071, 6.0},
    {"highshelf", SvfShape::kHighShelf, 6000.0, 0.7071, -8.0},
};

}  // namespace

TEST_CASE("SVF impulse response matches the analytic response", "[svf]") {
  const double fs = GENERATE(44100.0, 48000.0, 96000.0, 192000.0);
  for (const ShapeCase& sc : kShapes) {
    CAPTURE(fs, sc.name);
    const SvfCoeffs c = DesignSvf(sc.shape, sc.hz, sc.q, sc.gain_db, fs);
    const std::vector<double> h =
        ImpulseResponse(c, static_cast<size_t>(fs / 2));
    for (double f : LogFreqs(20.0, 0.45 * fs, 40)) {
      CAPTURE(f);
      const auto measured = DftAt(h, f, fs);
      const auto analytic = SvfResponse(c, f, fs);
      // Compare in dB where the response is not vanishingly small.
      if (std::abs(analytic) > 1e-3) {
        REQUIRE(std::abs(MagDb(measured) - MagDb(analytic)) < 0.01);
      } else {
        REQUIRE(std::abs(measured - analytic) < 1e-4);
      }
    }
  }
}

TEST_CASE("Bell and shelf gains land on target", "[svf]") {
  const double fs = 48000.0;
  for (double db : {-18.0, -6.0, 3.0, 12.0}) {
    CAPTURE(db);
    REQUIRE(std::abs(MagDb(SvfResponse(
                         DesignSvf(SvfShape::kBell, 1000.0, 1.5, db, fs),
                         1000.0, fs)) -
                     db) < 0.05);
    REQUIRE(std::abs(MagDb(SvfResponse(
                         DesignSvf(SvfShape::kLowShelf, 500.0, 0.7071, db, fs),
                         10.0, fs)) -
                     db) < 0.05);
    REQUIRE(std::abs(MagDb(SvfResponse(DesignSvf(SvfShape::kHighShelf, 1000.0,
                                                 0.7071, db, fs),
                                       23000.0, fs)) -
                     db) < 0.05);
    // Shelf midpoint is half the gain.
    REQUIRE(std::abs(MagDb(SvfResponse(
                         DesignSvf(SvfShape::kLowShelf, 500.0, 0.7071, db, fs),
                         500.0, fs)) -
                     db / 2) < 0.05);
  }
}

namespace {

// Bandwidth in octaves between the half-gain (in dB) points of a bell.
double BellBandwidthOctaves(double hz, double q, double fs) {
  const double db = 12.0;
  const SvfCoeffs c = DesignSvf(SvfShape::kBell, hz, q, db, fs);
  auto edge = [&](double dir) {
    double lo = 0.0, hi = 6.0;  // octaves away from center
    for (int i = 0; i < 60; ++i) {
      const double mid = 0.5 * (lo + hi);
      const double f = std::min(hz * std::exp2(dir * mid), 0.4999 * fs);
      (MagDb(SvfResponse(c, f, fs)) > db / 2 ? lo : hi) = mid;
    }
    return lo;
  };
  return edge(-1.0) + edge(1.0);
}

}  // namespace

TEST_CASE("Q prewarp keeps bell bandwidth near Nyquist", "[svf]") {
  const double fs = 48000.0;
  const double low = BellBandwidthOctaves(1000.0, 2.0, fs);
  // The upper edge of a 12 kHz bell is still below Nyquist at Q = 2.
  const double high = BellBandwidthOctaves(12000.0, 2.0, fs);
  CAPTURE(low, high);
  REQUIRE(std::abs(high / low - 1.0) < 0.10);
}

TEST_CASE("Fast audio-rate modulation stays bounded", "[svf]") {
  // Bell +12 dB, Q 6, sweeping 20 Hz <-> 20 kHz every 50 ms (per-sample ramps).
  const double fs = 48000.0;
  const Stereo x = Noise(static_cast<size_t>(fs * 2), 0.5, 1);
  SvfCell cell;
  cell.Snap(DesignSvf(SvfShape::kBell, 20.0, 6.0, 12.0, fs));
  double peak = 0.0;
  const int period = static_cast<int>(0.05 * fs);
  for (size_t i = 0; i < x.size(); ++i) {
    if (i % kControlBlock == 0) {
      const double phase =
          static_cast<double>(i % (2 * period)) / period;  // 0..2
      const double tri = phase < 1.0 ? phase : 2.0 - phase;
      cell.SetTarget(DesignSvf(SvfShape::kBell, 20.0 * std::pow(1000.0, tri),
                               6.0, 12.0, fs),
                     kControlBlock);
    }
    const double y = cell.Process(x.l[i]);
    REQUIRE(std::isfinite(y));
    peak = std::max(peak, std::abs(y));
  }
  CAPTURE(peak);
  REQUIRE(peak < 0.5 * 4.0 * 2.0);  // input peak * 12 dB * 2x headroom
}

TEST_CASE("Silence after noise decays without denormals", "[svf]") {
  const double fs = 48000.0;
  Engine e;
  EngineParams p;
  p.bands[0] = {BandType::kPeak, 40.0, 12.0, 8.0};
  p.bands[1] = {BandType::kLp24Lr, 100.0};
  p.bands[2] = {BandType::kVowel, 2.0, 9.0, 1.0};
  e.SetParams(p);
  e.Prepare(fs);
  Stereo x(static_cast<size_t>(fs * 11));
  const Stereo n = Noise(static_cast<size_t>(fs), 1.0, 2);
  std::copy(n.l.begin(), n.l.end(), x.l.begin());
  std::copy(n.r.begin(), n.r.end(), x.r.begin());
  const Stereo y = Run(e, x);
  REQUIRE(AllFinite(y));
  for (size_t i = static_cast<size_t>(fs * 10); i < y.size(); ++i) {
    REQUIRE((y.l[i] == 0.0f || std::abs(y.l[i]) >= FLT_MIN));
    REQUIRE((y.r[i] == 0.0f || std::abs(y.r[i]) >= FLT_MIN));
  }
}

}  // namespace eeq::test
