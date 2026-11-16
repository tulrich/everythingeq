// The Vocoder band type (ABOUT.md section 3.A).

#include "dsp/bands/vocoder.h"

#include <catch2/catch_test_macros.hpp>
#include <random>
#include <vector>

#include "dsp/bands/vowel.h"
#include "dsp/engine/engine.h"
#include "tests/test_util.h"

namespace eeq::test {
namespace {

// A sung vowel: a glottal pulse train through cascaded formant resonators.
std::vector<float> SungVowel(double morph, double f0, size_t n, double fs) {
  const Formants f = VowelFormants(morph);
  std::array<SvfCell, 3> cells;
  for (int i = 0; i < 3; ++i) {
    cells[i].Snap(DesignSvf(SvfShape::kLowPass, f.hz[i], 8.0, 0.0, fs));
  }
  std::vector<float> x(n);
  double phase = 0.0, peak = 0.0;
  std::vector<double> v(n);
  for (size_t i = 0; i < n; ++i) {
    phase += f0 / fs;
    double s = 0.0;
    if (phase >= 1.0) phase -= 1.0, s = 1.0;
    for (SvfCell& c : cells) s = c.Process(s);
    v[i] = s;
    peak = std::max(peak, std::abs(s));
  }
  for (size_t i = 0; i < n; ++i) x[i] = static_cast<float>(0.3 * v[i] / peak);
  return x;
}

EngineParams VocoderParams() {
  EngineParams p;
  p.groups[0].input = DetectorInput::kExternalSc;
  p.groups[0].attack_ms = 5.0;
  p.groups[0].release_ms = 50.0;
  BandParams& bp = p.bands[0];
  bp.type = BandType::kVocoder;
  bp.x = 1.0;     // No shift.
  bp.y = 100.0;   // Amount.
  bp.z = 24.0;    // Range.
  bp.option = 0;  // Source: G1.
  return p;
}

int NearestBand(double hz) {
  int best = 0;
  for (int i = 1; i < kVocoderBands; ++i) {
    if (std::abs(std::log(VocoderBandHz(i) / hz)) <
        std::abs(std::log(VocoderBandHz(best) / hz))) {
      best = i;
    }
  }
  return best;
}

}  // namespace

TEST_CASE("Vocoder bank hits its shape at the band centers", "[vocoder]") {
  const double fs = 48000.0;
  // Spectral envelopes are smooth: a random walk of up to 6 dB per band.
  std::mt19937 rng(3);
  std::uniform_real_distribution<float> step(-6.0f, 6.0f);
  double worst = 0.0;
  for (int trial = 0; trial < 20; ++trial) {
    VocoderShape shape;
    float g = 0.0f;
    for (float& s : shape) s = g = std::clamp(g + step(rng), -18.0f, 18.0f);
    const BandDesign d = DesignVocoder(1.0, shape, fs);
    for (int i = 0; i < kVocoderBands; ++i) {
      worst = std::max(
          worst,
          std::abs(MagDb(BandResponse(d, VocoderBandHz(i), fs)) - shape[i]));
    }
  }
  CAPTURE(worst);
  REQUIRE(worst < 1.0);
}

TEST_CASE("Vocoder takes on the sidechain's spectral envelope", "[vocoder]") {
  const double fs = 48000.0;
  Engine e;
  e.SetParams(VocoderParams());
  e.Prepare(fs);
  const size_t n = static_cast<size_t>(0.5 * fs);
  const std::vector<float> vowel = SungVowel(2.0, 150.0, n, fs);  // AH
  const Stereo synth = Noise(n, 0.2, 9);
  Stereo out(n);
  const float* in[2] = {synth.l.data(), synth.r.data()};
  const float* sc[2] = {vowel.data(), vowel.data()};
  float* o[2] = {out.l.data(), out.r.data()};
  e.Process(in, sc, o, static_cast<int>(n));
  const VocoderShape& shape = e.vocoder_shape(0);
  const Formants f = VowelFormants(2.0);
  // Boosts at the formants, cuts far above them.
  CAPTURE(shape[NearestBand(f.hz[0])], shape[NearestBand(f.hz[1])],
          shape[kVocoderBands - 1]);
  REQUIRE(shape[NearestBand(f.hz[0])] > 3.0f);
  REQUIRE(shape[NearestBand(f.hz[1])] > 3.0f);
  REQUIRE(shape[kVocoderBands - 1] < -3.0f);
}

TEST_CASE("Vocoder with a silent source leaves the signal alone", "[vocoder]") {
  const double fs = 48000.0;
  Engine e;
  e.SetParams(VocoderParams());
  e.Prepare(fs);
  const Stereo synth = Noise(24000, 0.2, 10);
  const Stereo y = Run(e, synth);  // No sidechain.
  for (float g : e.vocoder_shape(0)) REQUIRE(g == 0.0f);
  REQUIRE(MaxAbsDiff(y, synth) < 1e-5);
}

}  // namespace eeq::test
