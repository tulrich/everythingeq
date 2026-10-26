// Phase 7: lock-free ring, FFT and spectrum analyzer for the UI.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <thread>
#include <vector>

#include "dsp/common/spsc_ring.h"
#include "model/response_curves.h"
#include "model/spectrum_analyzer.h"

namespace eeq::test {
namespace {
constexpr double kPi = 3.14159265358979323846;
}  // namespace

TEST_CASE("SPSC ring keeps order, wraps and drops when full", "[analyzer]") {
  SpscRing<int, 8> ring;
  const int a[5] = {1, 2, 3, 4, 5};
  REQUIRE(ring.Push(a, 5) == 5);
  int out[8];
  REQUIRE(ring.Pop(out, 3) == 3);
  REQUIRE(out[0] == 1);
  REQUIRE(out[2] == 3);
  REQUIRE(ring.Push(a, 5) == 5);  // Wraps around.
  REQUIRE(ring.Push(a, 5) == 1);  // Only one slot left.
  REQUIRE(ring.size() == 8);
  REQUIRE(ring.Pop(out, 8) == 8);
  REQUIRE(out[0] == 4);
  REQUIRE(out[2] == 1);
  REQUIRE(out[7] == 1);
  REQUIRE(ring.Pop(out, 8) == 0);
}

TEST_CASE("SPSC ring transfers across threads intact", "[analyzer]") {
  SpscRing<int, 256> ring;
  constexpr int kCount = 200000;
  std::thread producer([&] {
    for (int i = 0; i < kCount;) {
      if (ring.Push(i)) ++i;
    }
  });
  int expected = 0;
  bool in_order = true;
  int buf[64];
  while (expected < kCount) {
    const size_t n = ring.Pop(buf, 64);
    for (size_t i = 0; i < n; ++i) in_order = in_order && buf[i] == expected++;
  }
  producer.join();
  REQUIRE(in_order);
}

TEST_CASE("FFT matches a direct DFT", "[analyzer]") {
  constexpr int kN = 64;
  Fft fft(kN);
  std::vector<std::complex<float>> x(kN);
  for (int i = 0; i < kN; ++i) {
    x[i] = {static_cast<float>(std::sin(0.3 * i) + 0.2 * std::cos(1.7 * i)),
            0.f};
  }
  std::vector<std::complex<float>> y = x;
  fft.Forward(y);
  for (int k = 0; k < kN; ++k) {
    std::complex<double> ref = 0.0;
    for (int i = 0; i < kN; ++i) {
      ref +=
          std::complex<double>(x[i]) * std::polar(1.0, -2.0 * kPi * k * i / kN);
    }
    CAPTURE(k);
    REQUIRE(std::abs(std::complex<double>(y[k]) - ref) < 1e-4);
  }
}

TEST_CASE("Analyzer reads a sine at its level and frequency", "[analyzer]") {
  constexpr double kFs = 48000.0;
  SpectrumAnalyzer analyzer(4096);
  std::vector<float> x(8192);
  for (size_t i = 0; i < x.size(); ++i) {
    x[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 1000.0 * i / kFs));
  }
  analyzer.AddSamples(x.data(), static_cast<int>(x.size()));
  const std::vector<double> freqs = DisplayFrequencies(200);
  std::vector<float> db;
  analyzer.Compute(freqs, kFs, 0.0, 0.0, db);
  size_t peak = 0;
  for (size_t i = 0; i < db.size(); ++i) {
    if (db[i] > db[peak]) peak = i;
  }
  CAPTURE(freqs[peak], db[peak]);
  REQUIRE(std::abs(freqs[peak] / 1000.0 - 1.0) < 0.05);
  REQUIRE(std::abs(db[peak] - (-6.02f)) < 1.0f);
  REQUIRE(db.front() < -80.f);  // 20 Hz.
  REQUIRE(db.back() < -80.f);   // 20 kHz.

  // Tilt: +4.5 dB/oct around 1 kHz.
  std::vector<float> tilted;
  SpectrumAnalyzer analyzer2(4096);
  analyzer2.AddSamples(x.data(), static_cast<int>(x.size()));
  analyzer2.Compute(freqs, kFs, 4.5, 0.0, tilted);
  REQUIRE(std::abs(tilted.back() - db.back() - 4.5f * std::log2(20.f)) < 0.01f);

  // Release: silence falls at the release rate, not instantly.
  std::vector<float> silence(4096, 0.f);
  analyzer.AddSamples(silence.data(), 4096);
  analyzer.Compute(freqs, kFs, 0.0, 0.1, db);
  REQUIRE(std::abs(db[peak] - (-6.02f - 4.f)) < 1.0f);
}

}  // namespace eeq::test
