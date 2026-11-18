// The Vocoder band type (ABOUT.md section 3.A).

#include "dsp/bands/vocoder.h"

#include <catch2/catch_test_macros.hpp>
#include <random>
#include <vector>

#include "dsp/bands/vowel.h"
#include "dsp/dynamics/spectral_envelope.h"
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

// G1 analyzes the sidechain's spectrum; band 1 is a Vocoder on it.
EngineParams VocoderParams(double amount = 100.0,
                           VocoderMode mode = VocoderMode::kShape) {
  EngineParams p;
  GroupParams& gp = p.groups[0];
  gp.detect = DetectMode::kSpectrum;
  gp.input = DetectorInput::kExternalSc;
  gp.attack_ms = 5.0;
  gp.release_ms = 50.0;
  BandParams& bp = p.bands[0];
  bp.type = BandType::kVocoder;
  bp.x = 1.0;   // No shift.
  bp.y = 0.0;   // Gain.
  bp.z = 24.0;  // Range.
  bp.option = static_cast<int>(mode);
  bp.dyn_group = 0;
  bp.dyn_target = DynTarget::kShape;
  bp.dyn_range = amount;
  return p;
}

int NearestBand(double hz) {
  int best = 0;
  for (int i = 1; i < kVocoderBands; ++i) {
    if (std::abs(std::log(SpectralBandHz(i) / hz)) <
        std::abs(std::log(SpectralBandHz(best) / hz))) {
      best = i;
    }
  }
  return best;
}

// Runs `e` on noise with `sc` on the sidechain.
void RunWithSidechain(Engine& e, const std::vector<float>& sc) {
  const Stereo synth = Noise(sc.size(), 0.2, 9);
  Stereo side(sc.size());
  side.l = side.r = sc;
  Run(e, synth, &side);
}

std::vector<float> Mono(const Stereo& s) { return s.l; }

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
    const BandDesign d = DesignVocoder(1.0, 0.0, shape, fs);
    for (int i = 0; i < kVocoderBands; ++i) {
      worst = std::max(
          worst,
          std::abs(MagDb(BandResponse(d, SpectralBandHz(i), fs)) - shape[i]));
    }
  }
  CAPTURE(worst);
  REQUIRE(worst < 1.0);
}

TEST_CASE("Vocoder takes on the sidechain's spectral envelope", "[vocoder]") {
  const double fs = 48000.0;
  const size_t n = static_cast<size_t>(0.5 * fs);
  const std::vector<float> vowel = SungVowel(2.0, 150.0, n, fs);  // AH
  const Formants f = VowelFormants(2.0);
  for (const double amount : {100.0, -100.0}) {
    Engine e;
    e.SetParams(VocoderParams(amount));
    e.Prepare(fs);
    RunWithSidechain(e, vowel);
    const VocoderShape& shape = e.vocoder_shape(0);
    // Boosts at the formants, cuts far above them; a negative Amount
    // inverts.
    const double sign = amount > 0.0 ? 1.0 : -1.0;
    CAPTURE(amount, shape[NearestBand(f.hz[0])], shape[NearestBand(f.hz[1])],
            shape[kVocoderBands - 1]);
    REQUIRE(sign * shape[NearestBand(f.hz[0])] > 3.0);
    REQUIRE(sign * shape[NearestBand(f.hz[1])] > 3.0);
    REQUIRE(sign * shape[kVocoderBands - 1] < -3.0);
  }
}

TEST_CASE("Vocoder Level mode with a negative Amount ducks", "[vocoder]") {
  const double fs = 48000.0;
  EngineParams p = VocoderParams(-50.0, VocoderMode::kLevel);
  p.groups[0].threshold_db = -35.0;
  Engine e;
  e.SetParams(p);
  e.Prepare(fs);
  RunWithSidechain(e, Mono(Sine(24000, 1000.0, 0.3, fs)));
  const VocoderShape& shape = e.vocoder_shape(0);
  // Cut where the source is loud, and nothing is boosted.
  CAPTURE(shape[NearestBand(1000.0)]);
  REQUIRE(shape[NearestBand(1000.0)] < -6.0f);
  for (float g : shape) REQUIRE(g <= 0.0f);
  REQUIRE(shape[0] == 0.0f);
  REQUIRE(shape[kVocoderBands - 1] == 0.0f);
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

TEST_CASE("Vocoder follows only Shape assignments from Spectrum groups",
          "[vocoder]") {
  const double fs = 48000.0;
  const std::vector<float> vowel = SungVowel(2.0, 150.0, 24000, fs);
  for (int variant = 0; variant < 2; ++variant) {
    EngineParams p = VocoderParams();
    if (variant == 0) p.groups[0].detect = DetectMode::kLevel;
    if (variant == 1) p.bands[0].dyn_target = DynTarget::kGain;
    Engine e;
    e.SetParams(p);
    e.Prepare(fs);
    RunWithSidechain(e, vowel);
    for (float g : e.vocoder_shape(0)) REQUIRE(g == 0.0f);
  }
}

TEST_CASE("Spectral envelope finds a sine's frequency", "[vocoder]") {
  const double fs = 48000.0;
  for (const double hz : {300.0, 1500.0, 4000.0}) {
    SpectralEnvelope env;
    env.Prepare(fs);
    env.SetBallistics(5.0, 50.0);
    const Stereo s = Sine(12000, hz, 0.3, fs);
    for (float x : s.l) env.Process(x);
    const double peak = env.PeakHz(hz / 2.0, hz * 2.0);
    CAPTURE(hz, peak, env.DbAt(hz));
    REQUIRE(std::abs(std::log2(peak / hz)) < 0.15);
    // A sine reads about as the level trigger would (power: -3 dB).
    REQUIRE(std::abs(env.DbAt(hz) - (20.0 * std::log10(0.3) - 3.0)) < 2.0);
  }
}

TEST_CASE("Spectrum group gives each band its own reading", "[vocoder]") {
  const double fs = 48000.0;
  EngineParams p;
  p.groups[0].detect = DetectMode::kSpectrum;
  p.groups[0].input = DetectorInput::kExternalSc;
  p.groups[0].threshold_db = -40.0;
  for (int b = 0; b < 2; ++b) {
    BandParams& bp = p.bands[b];
    bp.type = BandType::kPeak;
    bp.x = b == 0 ? 300.0 : 3000.0;
    bp.dyn_group = 0;
    bp.dyn_target = DynTarget::kGain;
    bp.dyn_range = -12.0;
  }
  // Band 3 follows the loudest point within an octave of 1 kHz.
  BandParams& track = p.bands[2];
  track.type = BandType::kPeak;
  track.x = 1000.0;
  track.dyn_group = 0;
  track.dyn_target = DynTarget::kFreq;
  track.dyn_range = 1.0;
  Engine e;
  e.SetParams(p);
  e.Prepare(fs);
  Stereo sc = Sine(24000, 3000.0, 0.3, fs);
  const Stereo tone = Sine(24000, 1500.0, 0.3, fs);
  for (size_t i = 0; i < sc.size(); ++i)
    sc.l[i] = sc.r[i] = sc.l[i] + tone.l[i];
  Run(e, Noise(sc.size(), 0.2, 4), &sc);
  // The 3 kHz band ducks; the 300 Hz one doesn't.
  CAPTURE(e.band_control(0, 0), e.band_control(1, 0), e.band_control(2, 0));
  REQUIRE(e.band_control(0, 0) < 0.05);
  REQUIRE(e.band_control(1, 0) > 0.95);
  // 1.5 kHz is log2(1.5) = 0.58 oct above the band.
  REQUIRE(std::abs(e.band_control(2, 0) * 1.0 - std::log2(1.5)) < 0.15);

  // In silence the tracking band returns to its own frequency.
  Run(e, Noise(3 * 48000, 0.2, 5));
  REQUIRE(std::abs(e.band_control(2, 0)) < 0.02);
}

}  // namespace eeq::test
