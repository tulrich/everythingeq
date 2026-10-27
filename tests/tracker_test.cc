// Vowel and pitch tracking detectors (ABOUT.md section 3.B, Detect).

#include "dsp/dynamics/tracker.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "dsp/bands/vowel.h"
#include "dsp/dynamics/dynamics_group.h"
#include "model/presets.h"
#include "tests/test_util.h"

namespace eeq::test {

namespace {

// Band-limited harmonic tone (1/h amplitudes up to 5 kHz).
std::vector<double> Harmonic(double f0, double seconds, double fs,
                             double amp = 0.3) {
  std::vector<double> x(static_cast<size_t>(seconds * fs));
  for (int h = 1; h * f0 < 5000.0; ++h) {
    const double w = 2.0 * kPi * h * f0 / fs;
    for (size_t n = 0; n < x.size(); ++n) x[n] += amp / h * std::sin(w * n);
  }
  return x;
}

// Cascade formant synthesis: a glottal pulse train through three resonant
// low-pass cells (unity DC gain all-pole resonators) at the vowel's formants.
std::vector<double> SungVowel(double morph, double f0, double seconds,
                              double fs) {
  const Formants f = VowelFormants(morph);
  std::array<SvfCell, 3> cells;
  for (int i = 0; i < 3; ++i) {
    cells[i].Snap(DesignSvf(SvfShape::kLowPass, f.hz[i], 8.0, 0.0, fs));
  }
  std::vector<double> x(static_cast<size_t>(seconds * fs));
  double phase = 0.0;
  for (size_t n = 0; n < x.size(); ++n) {
    phase += f0 / fs;
    double v = 0.0;
    if (phase >= 1.0) phase -= 1.0, v = 1.0;
    for (SvfCell& c : cells) v = c.Process(v);
    x[n] = v;
  }
  double peak = 0.0;
  for (double v : x) peak = std::max(peak, std::abs(v));
  for (double& v : x) v *= 0.3 / peak;
  return x;
}

struct Result {
  bool voiced = false;
  double value = 0.0;
};

Result Track(DetectMode mode, const std::vector<double>& x, double fs) {
  Tracker t;
  t.Prepare(fs);
  t.set_mode(mode);
  for (double v : x) t.Push(v);
  return {t.voiced(), t.value()};
}

}  // namespace

TEST_CASE("Pitch tracking finds f0 of harmonic tones", "[tracker]") {
  const double fs = GENERATE(44100.0, 48000.0, 96000.0);
  const double f0 = GENERATE(55.0, 110.0, 196.0, 440.0, 880.0);
  CAPTURE(fs, f0);
  const Result r = Track(DetectMode::kPitch, Harmonic(f0, 0.2, fs), fs);
  REQUIRE(r.voiced);
  // Within 10 cents.
  REQUIRE(std::abs(1200.0 * std::log2(Tracker::ValueToPitch(r.value) / f0)) <
          10.0);
}

TEST_CASE("Pitch tracking holds through noise and silence", "[tracker]") {
  const double fs = 48000.0;
  Tracker t;
  t.Prepare(fs);
  t.set_mode(DetectMode::kPitch);
  for (double v : Harmonic(220.0, 0.2, fs)) t.Push(v);
  REQUIRE(t.voiced());
  // White noise, once it fills the analysis window: unvoiced, and the
  // estimate holds.
  const std::vector<float> noise = Noise(48000, 0.3, 3).l;
  size_t i = 0;
  for (; i < 4800; ++i) t.Push(noise[i]);
  const double held = t.value();
  int voiced_frames = 0;
  for (; i < noise.size(); ++i) {
    if (t.Push(noise[i]) && t.voiced()) ++voiced_frames;
  }
  REQUIRE(voiced_frames == 0);
  REQUIRE(t.value() == held);
  for (int n = 0; n < 9600; ++n) t.Push(0.0);
  REQUIRE_FALSE(t.voiced());
  REQUIRE(t.value() == held);
}

TEST_CASE("Vowel tracking finds the sung vowel's morph", "[tracker]") {
  const double fs = GENERATE(44100.0, 48000.0);
  const double morph = GENERATE(0.0, 1.0, 2.0, 3.0, 4.0, 1.5, 2.5);
  const double f0 = GENERATE(110.0, 160.0, 220.0);
  CAPTURE(fs, morph, f0);
  Tracker t;
  t.Prepare(fs);
  t.set_mode(DetectMode::kVowel);
  for (double v : SungVowel(morph, f0, 0.25, fs)) t.Push(v);
  CAPTURE(t.f1(), t.f2(), t.morph());
  REQUIRE(t.voiced());
  REQUIRE(std::abs(t.morph() - morph) < 0.3);
  REQUIRE(t.value() == t.morph() / kVowelMorphMax);
}

TEST_CASE("A Vowel group's D glides to the tracked vowel", "[tracker]") {
  const double fs = 48000.0;
  DynamicsGroup g;
  GroupParams p;
  p.detect = DetectMode::kVowel;
  p.attack_ms = 10.0;
  p.release_ms = 10.0;
  g.SetParams(p);
  g.Prepare(fs);
  for (double v : SungVowel(3.0, 140.0, 0.3, fs)) g.Process(v, v);
  REQUIRE(std::abs(g.control(0) - 0.75) < 0.08);
  REQUIRE(g.control(1) == g.control(0));
  const DynamicsGroup::Meter m = g.TakeMeter();
  REQUIRE(m.voiced);
  REQUIRE(std::abs(m.tracked - 0.75) < 0.08);
}

TEST_CASE("Vocal Vowel Carve preset follows a sung vowel on the sidechain",
          "[tracker]") {
  const double fs = 48000.0;
  const Preset* preset = nullptr;
  for (const Preset& p : FactoryPresets()) {
    if (p.name == "Vocal Vowel Carve (SC)") preset = &p;
  }
  REQUIRE(preset != nullptr);
  Engine e;
  e.SetParams(preset->params);
  e.Prepare(fs);
  const std::vector<double> vocal = SungVowel(3.0, 150.0, 0.4, fs);  // EH
  const Stereo guitar = Noise(vocal.size(), 0.2, 4);
  std::vector<float> sc(vocal.begin(), vocal.end());
  Stereo out(vocal.size());
  const float* in[2] = {guitar.l.data(), guitar.r.data()};
  const float* side[2] = {sc.data(), sc.data()};
  float* o[2] = {out.l.data(), out.r.data()};
  e.Process(in, side, o, static_cast<int>(vocal.size()));
  // Group 1 tracks the vowel (D = morph / 4), group 2 the vocal level.
  REQUIRE(std::abs(e.group(0).control(0) * kVowelMorphMax - 3.0) < 0.35);
  REQUIRE(e.group(1).control(0) > 0.9);
}

}  // namespace eeq::test
