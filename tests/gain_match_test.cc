// Gain Match: output level matching for A/B and bypass comparisons.

#include "dsp/engine/gain_match.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
#include <vector>

namespace eeq::test {
namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 256;

// Runs `seconds` of white noise through `match`, with the output at
// `out_gain` times the input; returns the last block's output gain (dB)
// measured from the samples.
double Run(GainMatch& match, double seconds, double out_gain, bool enabled,
           bool hold, std::mt19937& rng) {
  std::normal_distribution<float> noise(0.f, 0.1f);
  std::vector<float> il(kBlock), ir(kBlock), ol(kBlock), orr(kBlock);
  double ratio = 1.0;
  const int blocks = static_cast<int>(seconds * kFs / kBlock);
  for (int b = 0; b < blocks; ++b) {
    for (int i = 0; i < kBlock; ++i) {
      il[i] = noise(rng);
      ir[i] = noise(rng);
      ol[i] = static_cast<float>(il[i] * out_gain);
      orr[i] = static_cast<float>(ir[i] * out_gain);
    }
    const float* in[2] = {il.data(), ir.data()};
    float* out[2] = {ol.data(), orr.data()};
    match.Process(in, out, kBlock, enabled, hold);
    ratio = ol[kBlock - 1] / (il[kBlock - 1] * out_gain);
  }
  return 20.0 * std::log10(ratio);
}

}  // namespace

TEST_CASE("Gain Match brings the output to the input's loudness",
          "[gain_match]") {
  std::mt19937 rng(1);
  GainMatch m;
  m.Prepare(kFs);
  // 6 dB quieter output: matched within a fraction of a dB from the start.
  CHECK(std::abs(Run(m, 0.5, 0.5, true, false, rng) - 6.02) < 0.3);
  CHECK(std::abs(Run(m, 8.0, 0.5, true, false, rng) - 6.02) < 0.1);
  CHECK(std::abs(m.gain_db() - 6.02) < 0.1);
}

TEST_CASE("Gain Match is unity when held or off, and clamps", "[gain_match]") {
  std::mt19937 rng(2);
  GainMatch m;
  m.Prepare(kFs);
  Run(m, 4.0, 0.5, true, false, rng);
  CHECK(std::abs(Run(m, 0.5, 0.5, true, true, rng)) < 0.01);  // Held.
  // Held doesn't forget the measurement.
  CHECK(std::abs(Run(m, 0.5, 0.5, true, false, rng) - 6.02) < 0.1);
  CHECK(std::abs(Run(m, 0.5, 0.5, false, false, rng)) < 0.01);  // Off.
  CHECK(std::abs(Run(m, 4.0, 1e-3, true, false, rng) - GainMatch::kMaxDb) <
        0.01);
}

TEST_CASE("Gain Match keeps a measurement per A/B slot", "[gain_match]") {
  std::mt19937 rng(3);
  GainMatch m;
  m.Prepare(kFs);
  Run(m, 6.0, 0.5, true, false, rng);  // A: 6 dB quieter.
  m.SelectSlot(1);
  // A new slot starts from A's measurement.
  CHECK(std::abs(m.gain_db() - 6.02) < 0.1);
  Run(m, 15.0, 2.0, true, false, rng);  // B: 6 dB louder.
  CHECK(std::abs(m.gain_db() + 6.02) < 0.2);
  m.SelectSlot(0);
  // Back on A, matched again after the short glide, not the 3 s average.
  CHECK(std::abs(Run(m, 0.3, 0.5, true, false, rng) - 6.02) < 0.2);
}

}  // namespace eeq::test
