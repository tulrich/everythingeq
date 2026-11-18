// Gain Match: the one-shot input/output loudness measurement.

#include "dsp/engine/gain_match.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
#include <vector>

namespace eeq::test {
namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 256;

// Feeds `seconds` of white noise (at `level`), with the output at
// `out_gain` times the input; returns the number of blocks until the
// measurement completed, or -1.
int Run(GainMatch& match, double seconds, double out_gain, bool hold,
        std::mt19937& rng, float level = 0.1f) {
  std::normal_distribution<float> noise(0.f, level);
  std::vector<float> il(kBlock), ir(kBlock), ol(kBlock), orr(kBlock);
  const int blocks = static_cast<int>(seconds * kFs / kBlock);
  for (int b = 0; b < blocks; ++b) {
    for (int i = 0; i < kBlock; ++i) {
      il[i] = noise(rng);
      ir[i] = noise(rng);
      ol[i] = static_cast<float>(il[i] * out_gain);
      orr[i] = static_cast<float>(ir[i] * out_gain);
    }
    const float* in[2] = {il.data(), ir.data()};
    const float* out[2] = {ol.data(), orr.data()};
    if (match.Process(in, out, kBlock, hold)) return b + 1;
  }
  return -1;
}

}  // namespace

TEST_CASE("Gain Match measures the output against the input once",
          "[gain_match]") {
  std::mt19937 rng(1);
  GainMatch m;
  m.Prepare(kFs);
  CHECK(Run(m, 1.0, 0.5, false, rng) == -1);  // Not started.
  m.Start();
  const int blocks = Run(m, 5.0, 0.5, false, rng);
  CHECK(std::abs(blocks * kBlock / kFs - GainMatch::kSeconds) < 0.01);
  CHECK_FALSE(m.running());
  CHECK(std::abs(m.correction_db() - 6.02) < 0.05);
  m.Start();
  Run(m, 5.0, 2.0, false, rng);
  CHECK(std::abs(m.correction_db() + 6.02) < 0.05);
}

TEST_CASE("Gain Match skips held and silent audio", "[gain_match]") {
  std::mt19937 rng(2);
  GainMatch m;
  m.Prepare(kFs);
  m.Start();
  CHECK(Run(m, 2.0, 0.5, false, rng) == -1);
  const double progress = m.progress();
  CHECK(progress > 0.6);
  CHECK(Run(m, 2.0, 1.0, true, rng) == -1);          // Held.
  CHECK(Run(m, 2.0, 1.0, false, rng, 1e-5f) == -1);  // Silent.
  // Only the K-weighting's ring-down at the drop into silence counts.
  CHECK(m.progress() - progress < 0.01);
  CHECK(Run(m, 2.0, 0.5, false, rng) > 0);
  CHECK(std::abs(m.correction_db() - 6.02) < 0.05);
  m.Start();
  m.Cancel();
  CHECK(Run(m, 4.0, 0.5, false, rng) == -1);
}

}  // namespace eeq::test
