// Phase 2: band types (ABOUT.md sections 3.A and 3.C).

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "dsp/bands/band_design.h"
#include "dsp/bands/band_processor.h"
#include "dsp/bands/iso226.h"
#include "dsp/bands/vowel.h"
#include "tests/test_util.h"

namespace eeq::test {

namespace {

std::complex<double> BiquadResponse(const double b[3], const double a[3],
                                    double f, double fs) {
  const std::complex<double> z1 = std::polar(1.0, -2.0 * kPi * f / fs);
  return (b[0] + b[1] * z1 + b[2] * z1 * z1) /
         (a[0] + a[1] * z1 + a[2] * z1 * z1);
}

double CellHz(const SvfCoeffs& c, double fs) {
  return std::atan(c.g) * fs / kPi;
}

}  // namespace

TEST_CASE("Band Shelf is a plateau between half-gain edges", "[bands]") {
  const double fs = 48000.0;
  const double gain = GENERATE(-12.0, 9.0);
  // Edges at 125 Hz and 8 kHz.
  const BandDesign d =
      DesignBand(BandType::kBandShelf, 1000.0, gain, 6.0, 0, fs);
  auto db = [&](double f) { return MagDb(BandResponse(d, f, fs)); };
  CAPTURE(gain);
  REQUIRE(std::abs(db(1000.0) - gain) < 0.05);
  REQUIRE(std::abs(db(125.0) - gain / 2) < 0.1);
  REQUIRE(std::abs(db(8000.0) - gain / 2) < 0.1);
  REQUIRE(std::abs(db(15.0)) < 0.1);
  REQUIRE(std::abs(db(20000.0)) < 0.5);
  // Monotonic transitions (Butterworth shelves don't overshoot).
  double prev = db(10.0);
  for (double f : LogFreqs(10.0, 1000.0, 40)) {
    REQUIRE((gain > 0 ? db(f) >= prev - 1e-9 : db(f) <= prev + 1e-9));
    prev = db(f);
  }
}

TEST_CASE("Band Shelf narrows into a bell that doesn't reach full gain",
          "[bands]") {
  const double fs = 48000.0;
  const double peak = MagDb(BandResponse(
      DesignBand(BandType::kBandShelf, 1000.0, 12.0, 0.5, 0, fs), 1000.0, fs));
  REQUIRE(peak > 0.5);
  REQUIRE(peak < 12.0);
}

TEST_CASE("Band Shelf detector passes the plateau", "[bands]") {
  const double fs = 48000.0;
  const DetectorDesign d =
      DesignDetector(BandType::kBandShelf, 1000.0, 2.0, fs);
  auto db = [&](double f) { return MagDb(DetectorResponse(d, f, fs)); };
  REQUIRE(std::abs(db(1000.0)) < 0.01);
  REQUIRE(std::abs(db(500.0) + 3.01) < 0.05);
  REQUIRE(std::abs(db(2000.0) + 3.01) < 0.05);
}

TEST_CASE("Morph target changes a Band Shelf's width", "[bands]") {
  BandParams bp;
  bp.type = BandType::kBandShelf;
  bp.z = 2.0;
  bp.dyn_target = DynTarget::kMorph;
  bp.dyn_range = 1.5;
  REQUIRE(ResolveBand(bp, 1.0, 1.0).z == 3.5);
  REQUIRE(ResolveBand(bp, 1.0, 1.0).x == bp.x);
}

TEST_CASE("LR24 crossover sums to a flat all-pass", "[bands]") {
  const double fs = GENERATE(44100.0, 48000.0, 96000.0);
  const double fc = GENERATE(80.0, 1000.0, 8000.0);
  const BandDesign lp = DesignBand(BandType::kLp24Lr, fc, 0, 0, 0, fs);
  const BandDesign hp = DesignBand(BandType::kHp24Lr, fc, 0, 0, 0, fs);
  SvfCoeffs ap = DesignSvf(SvfShape::kAllPass, fc, kButterworthQ, 0.0, fs);
  for (double f : LogFreqs(10.0, 0.49 * fs, 60)) {
    CAPTURE(fs, fc, f);
    const auto sum =
        BandResponse(lp, f, fs) + BandComplementResponse(lp, f, fs);
    REQUIRE(std::abs(MagDb(sum)) < 0.01);
    REQUIRE(std::abs(sum - SvfResponse(ap, f, fs)) < 1e-9);
    // The HP band's main output is the LP band's complement.
    REQUIRE(std::abs(BandResponse(hp, f, fs) -
                     BandComplementResponse(lp, f, fs)) < 1e-12);
  }
  // -6 dB at the crossover frequency.
  REQUIRE(std::abs(MagDb(BandResponse(lp, fc, fs)) + 6.0206) < 0.01);
}

TEST_CASE("LR24 processor output matches its design", "[bands]") {
  const double fs = 48000.0;
  const BandDesign d = DesignBand(BandType::kLp24Lr, 500.0, 0, 0, 0, fs);
  BandProcessor p;
  p.Apply(d, 1);
  std::vector<double> lo(24000), hi(24000);
  for (size_t i = 0; i < lo.size(); ++i) {
    const BandOutput o = p.Process(i == 0 ? 1.0 : 0.0);
    lo[i] = o.y, hi[i] = o.comp;
  }
  for (double f : LogFreqs(20.0, 20000.0, 30)) {
    REQUIRE(std::abs(DftAt(lo, f, fs) - BandResponse(d, f, fs)) < 1e-6);
    REQUIRE(std::abs(DftAt(hi, f, fs) - BandComplementResponse(d, f, fs)) <
            1e-6);
  }
}

TEST_CASE("Tilt pivots around f0", "[bands]") {
  const double fs = 48000.0;
  for (double tilt : {-12.0, 6.0}) {
    const BandDesign d = DesignBand(BandType::kTilt, 800.0, tilt, 0, 0, fs);
    REQUIRE(std::abs(MagDb(BandResponse(d, 800.0, fs))) < 0.01);
    REQUIRE(std::abs(MagDb(BandResponse(d, 10.0, fs)) + tilt / 2) < 0.1);
    REQUIRE(std::abs(MagDb(BandResponse(d, 23900.0, fs)) - tilt / 2) < 0.1);
  }
}

TEST_CASE("K-weighting matches ITU-R BS.1770 at 48 kHz", "[bands]") {
  const double fs = 48000.0;
  const double b1[3] = {1.53512485958697, -2.69169618940638, 1.19839281085285};
  const double a1[3] = {1.0, -1.69065929318241, 0.73248077421585};
  const double b2[3] = {1.0, -2.0, 1.0};
  const double a2[3] = {1.0, -1.99004745483398, 0.99007225036621};
  const BandDesign d = DesignBand(BandType::kKWeight, 0, 0, 0, 0, fs);
  for (double f : LogFreqs(20.0, 20000.0, 50)) {
    CAPTURE(f);
    const auto ref =
        BiquadResponse(b1, a1, f, fs) * BiquadResponse(b2, a2, f, fs);
    REQUIRE(std::abs(MagDb(BandResponse(d, f, fs)) - MagDb(ref)) < 0.1);
  }
}

TEST_CASE("K-weighting is consistent across sample rates", "[bands]") {
  const BandDesign ref = DesignBand(BandType::kKWeight, 0, 0, 0, 0, 192000.0);
  for (double fs : {44100.0, 48000.0, 96000.0}) {
    const BandDesign d = DesignBand(BandType::kKWeight, 0, 0, 0, 0, fs);
    for (double f : LogFreqs(20.0, 16000.0, 40)) {
      CAPTURE(fs, f);
      REQUIRE(std::abs(MagDb(BandResponse(d, f, fs)) -
                       MagDb(BandResponse(ref, f, 192000.0))) < 0.1);
    }
  }
}

TEST_CASE("Fletcher-Munson contour fits ISO 226 and anchors 1 kHz", "[bands]") {
  const double fs = 48000.0;
  for (double phon : {-20.0, -5.0, 5.0, 10.0, 20.0, 30.0}) {
    CAPTURE(phon);
    const BandDesign d =
        DesignBand(BandType::kFletcherMunson, 0, phon, 0, 0, fs);
    REQUIRE(std::abs(MagDb(BandResponse(d, 1000.0, fs))) < 0.1);
    double max_err = 0.0;
    for (int i = 0; i < iso226::kNumFreqs; ++i) {
      const double f = iso226::kFreqs[i];
      const double err =
          MagDb(BandResponse(d, f, fs)) - phon * iso226::CompensationPerPhon(i);
      max_err = std::max(max_err, std::abs(err));
    }
    CAPTURE(max_err);
    // Within 1 dB per 20 phon of delta (and never worse than 1.5 dB).
    REQUIRE(max_err < std::max(0.5, 1.5 * std::abs(phon) / 20.0));
  }
}

TEST_CASE("Vowel formants land on the table and morph continuously",
          "[bands]") {
  const double fs = 48000.0;
  const double anchors[5][3] = {{300, 870, 2250},
                                {450, 1050, 2600},
                                {730, 1090, 2440},
                                {530, 1840, 2480},
                                {270, 2290, 3010}};
  for (int v = 0; v < 5; ++v) {
    for (double scale : {0.7, 1.0, 1.4}) {
      const BandDesign d = DesignBand(BandType::kVowel, v, 6.0, scale, 0, fs);
      for (int i = 0; i < 3; ++i) {
        CAPTURE(v, scale, i);
        REQUIRE(std::abs(CellHz(d.cells[i], fs) / (anchors[v][i] * scale) -
                         1.0) < 0.01);
      }
    }
  }
  Formants prev = VowelFormants(0.0);
  for (double m = 0.001; m <= 4.0; m += 0.001) {
    const Formants f = VowelFormants(m);
    for (int i = 0; i < 3; ++i) {
      CAPTURE(m, i);
      REQUIRE(std::abs(f.hz[i] / prev.hz[i] - 1.0) < 0.005);
    }
    prev = f;
  }
}

TEST_CASE("Vowel weighting: natural vs flat", "[bands]") {
  const double fs = 48000.0;
  const BandDesign natural =
      DesignBand(BandType::kVowel, 2.0, -4.0, 1.0,
                 static_cast<int>(VowelWeighting::kNatural), fs);
  const BandDesign flat =
      DesignBand(BandType::kVowel, 2.0, -4.0, 1.0,
                 static_cast<int>(VowelWeighting::kFlat), fs);
  // /a/ F3 at 2440 Hz: natural weight -18 dB => ~ -0.5 dB; flat => ~ -4 dB.
  REQUIRE(MagDb(BandResponse(natural, 2440.0, fs)) > -1.0);
  REQUIRE(MagDb(BandResponse(flat, 2440.0, fs)) < -3.5);
}

TEST_CASE("BP band at the off floor is silent", "[bands]") {
  const BandDesign d =
      DesignBand(BandType::kBp, 1000.0, kBpOffDb, 2.0, 0, 48000.0);
  REQUIRE(std::abs(BandResponse(d, 1000.0, 48000.0)) == 0.0);
}

TEST_CASE("Changing band type crossfades without a jump", "[bands]") {
  const double fs = 48000.0;
  const Stereo x = Sine(static_cast<size_t>(fs / 10), 100.0, 0.5, fs);
  BandSlot slot;
  slot.Prepare(fs);
  std::array<ResolvedBand, kNumChannels> p{};
  p[0] = p[1] = ResolvedBand{1000.0, 12.0, 2.0, 0};
  slot.Update(BandType::kPeak, p, true);
  std::vector<double> y;
  for (size_t i = 0; i < x.size(); ++i) {
    if (i == x.size() / 2) {
      p[0] = p[1] = ResolvedBand{200.0, 12.0, 0.7071, 0};
      slot.Update(BandType::kLowShelf, p, false);
    }
    y.push_back(slot.Process(0, x.l[i]).y);
    slot.Advance();
  }
  // Largest sample-to-sample step around the switch vs. the steady state.
  double steady = 0.0, around = 0.0;
  for (size_t i = 1; i < y.size(); ++i) {
    const double step = std::abs(y[i] - y[i - 1]);
    if (i > y.size() / 2 - 200 && i < y.size() / 2 + 400)
      around = std::max(around, step);
    else if (i > 1000)
      steady = std::max(steady, step);
  }
  CAPTURE(steady, around);
  REQUIRE(around < 2.5 * steady);
}

}  // namespace eeq::test
