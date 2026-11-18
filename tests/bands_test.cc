// Phase 2: band types (ABOUT.md sections 3.A and 3.C).

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <random>
#include <vector>

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
  const BandDesign lp =
      DesignBand(BandType::kLp, fc, 0, kButterworthQ, kSlope24, fs);
  const BandDesign hp =
      DesignBand(BandType::kHp, fc, 0, kButterworthQ, kSlope24, fs);
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
  const BandDesign d =
      DesignBand(BandType::kLp, 500.0, 0, kButterworthQ, kSlope24, fs);
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

TEST_CASE("Tilt width spreads a straight ramp across the octaves", "[bands]") {
  const double fs = 48000.0;
  const double width = GENERATE(0.0, 2.0, 4.0, 6.0, 8.0);
  const double tilt = 12.0;
  const BandDesign d = DesignBand(BandType::kTilt, 1000.0, tilt, width, 0, fs);
  auto db = [&](double f) { return MagDb(BandResponse(d, f, fs)); };
  CAPTURE(width);
  REQUIRE(std::abs(db(1000.0)) < 0.05);
  // Monotonic: no ripple.
  double prev = db(10.0);
  for (double f : LogFreqs(10.0, 22000.0, 200)) {
    REQUIRE(db(f) >= prev - 1e-9);
    prev = db(f);
  }
  if (width >= 4.0) {
    // The middle half of the ramp is straight within 0.25 dB.
    for (double oct = -width / 4; oct <= width / 4; oct += 0.25) {
      CAPTURE(oct);
      REQUIRE(std::abs(db(1000.0 * std::exp2(oct)) - tilt * oct / width) <
              0.25);
    }
  }
}

TEST_CASE("Morph target changes a Tilt's width", "[bands]") {
  BandParams bp;
  bp.type = BandType::kTilt;
  bp.z = 4.0;
  bp.dyn_target = DynTarget::kMorph;
  bp.dyn_range = -2.0;
  REQUIRE(ResolveBand(bp, 0.5, 1.0).z == 3.0);
}

TEST_CASE("Dyn Makeup opposes the dynamic gain", "[bands]") {
  BandParams bp;
  bp.type = BandType::kPeak;
  bp.y = 2.0;
  bp.dyn_target = DynTarget::kGain;
  bp.dyn_range = -6.0;
  bp.dyn_makeup = 0.5;
  const ResolvedBand rb = ResolveBand(bp, 0.5, 1.0);
  REQUIRE(rb.y == -1.0);
  REQUIRE(rb.makeup_db == 1.5);
  // No makeup for other targets or for types without a dynamic gain.
  bp.dyn_target = DynTarget::kFreq;
  REQUIRE(ResolveBand(bp, 0.5, 1.0).makeup_db == 0.0);
  bp.dyn_target = DynTarget::kGain;
  bp.type = BandType::kLp;
  REQUIRE(ResolveBand(bp, 0.5, 1.0).makeup_db == 0.0);
}

TEST_CASE("Dyn A and Dyn B modulate together", "[bands]") {
  // A vowel carve: Dyn A follows the vowel (Morph), Dyn B the level (Gain).
  BandParams bp;
  bp.type = BandType::kVowel;
  bp.x = 0.0;
  bp.y = 0.0;
  bp.z = 1.0;
  bp.dyn_target = DynTarget::kMorph;
  bp.dyn_range = 4.0;
  bp.dyn_b_target = DynTarget::kGain;
  bp.dyn_b_range = -6.0;
  bp.dyn_makeup = 0.5;
  const ResolvedBand rb = ResolveBand(bp, 0.5, 0.25, 1.0);
  REQUIRE(rb.x == 2.0);
  REQUIRE(rb.y == -1.5);
  REQUIRE(rb.makeup_db == 0.75);
  // Both on Gain: the offsets add, and Makeup opposes the total.
  bp.dyn_target = DynTarget::kGain;
  bp.dyn_range = 2.0;
  const ResolvedBand both = ResolveBand(bp, 1.0, 1.0, 1.0);
  REQUIRE(both.y == -4.0);
  REQUIRE(both.makeup_db == 2.0);
}

TEST_CASE("A ducked midrange Band Shelf with full makeup boosts the ends",
          "[bands]") {
  const double fs = 48000.0;
  BandParams bp;
  bp.type = BandType::kBandShelf;
  bp.x = 1000.0;
  bp.z = 6.0;  // 125 Hz - 8 kHz.
  bp.dyn_target = DynTarget::kGain;
  bp.dyn_range = -1.5;
  bp.dyn_makeup = 1.0;
  const BandDesign d =
      DesignResolvedBand(bp.type, ResolveBand(bp, 1.0, 1.0), fs);
  auto db = [&](double f) { return MagDb(BandResponse(d, f, fs)); };
  REQUIRE(std::abs(db(1000.0)) < 0.01);
  REQUIRE(std::abs(db(20.0) - 1.5) < 0.05);
  REQUIRE(std::abs(db(20000.0) - 1.5) < 0.1);
  // At rest the band is flat.
  const BandDesign rest =
      DesignResolvedBand(bp.type, ResolveBand(bp, 0.0, 1.0), fs);
  REQUIRE(std::abs(MagDb(BandResponse(rest, 20.0, fs))) < 1e-9);
}

TEST_CASE("K-weighting matches ITU-R BS.1770 at 48 kHz", "[bands]") {
  const double fs = 48000.0;
  const double b1[3] = {1.53512485958697, -2.69169618940638, 1.19839281085285};
  const double a1[3] = {1.0, -1.69065929318241, 0.73248077421585};
  const double b2[3] = {1.0, -2.0, 1.0};
  const double a2[3] = {1.0, -1.99004745483398, 0.99007225036621};
  const BandDesign d = DesignKWeighting(fs);
  for (double f : LogFreqs(20.0, 20000.0, 50)) {
    CAPTURE(f);
    const auto ref =
        BiquadResponse(b1, a1, f, fs) * BiquadResponse(b2, a2, f, fs);
    REQUIRE(std::abs(MagDb(BandResponse(d, f, fs)) - MagDb(ref)) < 0.1);
  }
}

TEST_CASE("K-weighting is consistent across sample rates", "[bands]") {
  const BandDesign ref = DesignKWeighting(192000.0);
  for (double fs : {44100.0, 48000.0, 96000.0}) {
    const BandDesign d = DesignKWeighting(fs);
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

TEST_CASE("Vintage Low boosts below, attenuates above", "[bands]") {
  const double fs = 48000.0;
  auto db = [&](double boost, double atten, double f) {
    return MagDb(BandResponse(
        DesignBand(BandType::kVintageLow, 60.0, boost, atten, 0, fs), f, fs));
  };
  // Boost alone: a shelf with its half-gain point at the frequency.
  REQUIRE(std::abs(db(10.0, 0.0, 6.0) - 10.0) < 0.6);
  REQUIRE(std::abs(db(10.0, 0.0, 60.0) - 5.0) < 1.0);
  REQUIRE(std::abs(db(10.0, 0.0, 2000.0)) < 0.2);
  // Attenuation alone: a broad shelf centered five times higher.
  REQUIRE(std::abs(db(0.0, 10.0, 6.0) + 10.0) < 0.3);
  REQUIRE(std::abs(db(0.0, 10.0, 300.0) + 5.0) < 1.0);
  // Both: the bump below the frequency and the dip above it.
  REQUIRE(db(8.0, 4.0, 25.0) > 3.5);
  double dip = 0.0;
  for (double f = 80.0; f <= 250.0; f *= 1.05)
    dip = std::min(dip, db(8.0, 4.0, f));
  REQUIRE(dip < -2.5);
  REQUIRE(std::abs(db(8.0, 4.0, 5000.0)) < 0.5);
}

TEST_CASE("Vintage High boosts and attenuates at its own frequencies",
          "[bands]") {
  const double fs = 48000.0;
  auto db = [&](double boost, double atten, int sel, double f) {
    return MagDb(BandResponse(
        DesignBand(BandType::kVintageHigh, 10000.0, boost, atten, sel, fs), f,
        fs));
  };
  REQUIRE(std::abs(db(6.0, 0.0, 0, 10000.0) - 6.0) < 0.3);
  REQUIRE(std::abs(db(6.0, 0.0, 0, 300.0)) < 0.5);
  // The attenuation shelf sits at the Atten Sel frequency: half its depth
  // there, the full depth an octave or two above.
  const int k20k = static_cast<int>(VintageAtten::k20k);
  const int k5k = static_cast<int>(VintageAtten::k5k);
  REQUIRE(db(0.0, 8.0, k5k, 5000.0) < db(0.0, 8.0, k20k, 5000.0) - 2.0);
  REQUIRE(std::abs(db(0.0, 8.0, k5k, 5000.0) + 4.0) < 1.0);
  REQUIRE(std::abs(db(0.0, 8.0, k20k, 200.0)) < 0.3);
}

TEST_CASE("Global Scale scales the Vintage attenuation too", "[bands]") {
  BandParams bp;
  bp.type = BandType::kVintageLow;
  bp.x = 60.0, bp.y = 8.0, bp.z = 4.0;
  const ResolvedBand rb = ResolveBand(bp, 0.0, 0.0, 0.5);
  REQUIRE(rb.y == 4.0);
  REQUIRE(rb.z == 2.0);
  REQUIRE(rb.x == 60.0);
}

TEST_CASE("Comb teeth sit on the chosen harmonics, 0 dB midway", "[bands]") {
  const double fs = 48000.0, f0 = 200.0;
  const double gain = GENERATE(12.0, -12.0, -30.0);
  const double q = GENERATE(8.0, 20.0, 80.0);
  CAPTURE(gain, q);
  for (int option = 0; option < 3; ++option) {
    CAPTURE(option);
    const BandDesign d = DesignBand(BandType::kComb, f0, gain, q, option, fs);
    for (int k = 1; k <= 20; ++k) {
      // Which harmonics are teeth; the others sit midway between teeth.
      const bool tooth = option == 0 || (option == 1) == (k % 2 == 1);
      CAPTURE(k);
      REQUIRE(std::abs(MagDb(BandResponse(d, k * f0, fs)) -
                       (tooth ? gain : 0.0)) < 0.01);
    }
  }
}

TEST_CASE("Comb processor matches its design", "[bands]") {
  const double fs = 48000.0;
  // f0 = 240 Hz: an integer delay, so interpolation is exact.
  const BandDesign d = DesignBand(BandType::kComb, 240.0, 9.0, 20.0, 0, fs);
  for (double f : {240.0, 480.0, 600.0, 1000.0, 3000.0}) {
    CAPTURE(f);
    BandProcessor p;
    std::vector<double> buf(4096);
    p.SetCombBuffer(buf.data(), static_cast<int>(buf.size()));
    p.Apply(d, 1);
    double peak = 0.0;
    for (int n = 0; n < static_cast<int>(fs); ++n) {
      const double y = p.Process(std::sin(2.0 * kPi * f * n / fs)).y;
      if (n > fs / 2) peak = std::max(peak, std::abs(y));
    }
    REQUIRE(std::abs(20.0 * std::log10(peak) - MagDb(BandResponse(d, f, fs))) <
            0.05);
  }
}

TEST_CASE("Comb glides to a new f0 without clicking", "[bands]") {
  const double fs = 48000.0;
  BandProcessor p;
  std::vector<double> buf(4096);
  p.SetCombBuffer(buf.data(), static_cast<int>(buf.size()));
  p.Apply(DesignBand(BandType::kComb, 110.0, 12.0, 40.0, 0, fs), 1);
  // A note change: f0 jumps from A2 to E3.
  const BandDesign next = DesignBand(BandType::kComb, 164.8, 12.0, 40.0, 0, fs);
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> noise(-0.1, 0.1);
  double prev = 0.0, step_before = 0.0, step_after = 0.0;
  const int change = static_cast<int>(fs);
  for (int n = 0; n < 2 * change; ++n) {
    if (n == change) p.Apply(next, kControlBlock);
    const double y = p.Process(noise(rng)).y;
    const double step = std::abs(y - prev);
    prev = y;
    if (n > change / 2 && n < change) step_before = std::max(step_before, step);
    if (n >= change && n < change + 4800) {
      step_after = std::max(step_after, step);
    }
  }
  CAPTURE(step_before, step_after);
  REQUIRE(step_after < 1.5 * step_before);
}

TEST_CASE("LP and HP slopes", "[bands]") {
  const double fs = 192000.0, fc = 100.0;
  const double kDbPerOct[] = {6.0, 12.0, 18.0, 24.0, 36.0, 48.0};
  for (int slope = 0; slope < 6; ++slope) {
    for (const BandType type : {BandType::kLp, BandType::kHp}) {
      CAPTURE(slope, static_cast<int>(type));
      const BandDesign d = DesignBand(type, fc, 0, kButterworthQ, slope, fs);
      const bool lp = type == BandType::kLp;
      // Far into the stopband, each octave drops by the slope.
      const double f = lp ? fc * 32.0 : fc / 32.0;
      const double step = MagDb(BandResponse(d, f, fs)) -
                          MagDb(BandResponse(d, lp ? 2.0 * f : f / 2.0, fs));
      REQUIRE(std::abs(step - kDbPerOct[slope]) < 0.5);
      // Flat passband; -3 dB at fc (-6 dB for the LR24 crossover).
      REQUIRE(std::abs(MagDb(BandResponse(d, lp ? fc / 20.0 : fc * 20.0, fs))) <
              0.05);
      const double at_fc = MagDb(BandResponse(d, fc, fs));
      REQUIRE(std::abs(at_fc - (slope == kSlope24 ? -6.02 : -3.01)) < 0.05);
    }
  }
}

TEST_CASE("Butterworth slopes resonate with Q", "[bands]") {
  const double fs = 48000.0;
  for (const int slope : {2, 5}) {  // 18 and 48 dB/oct.
    const BandDesign flat =
        DesignBand(BandType::kLp, 1000.0, 0, kButterworthQ, slope, fs);
    const BandDesign peaky =
        DesignBand(BandType::kLp, 1000.0, 0, 4.0, slope, fs);
    double flat_max = -100.0, peak_max = -100.0;
    for (double f : LogFreqs(100.0, 2000.0, 200)) {
      flat_max = std::max(flat_max, MagDb(BandResponse(flat, f, fs)));
      peak_max = std::max(peak_max, MagDb(BandResponse(peaky, f, fs)));
    }
    CAPTURE(slope, flat_max, peak_max);
    REQUIRE(flat_max < 0.01);  // Butterworth: no overshoot.
    REQUIRE(peak_max > 9.0);
  }
}

TEST_CASE("Proportional Q narrows the bell as the gain grows", "[bands]") {
  const double fs = 48000.0;
  const int prop = static_cast<int>(PeakQ::kProportional);
  // Bandwidth between the half-gain points, in octaves.
  auto width = [&](double db, int option) {
    const BandDesign d =
        DesignBand(BandType::kPeak, 1000.0, db, 1.0, option, fs);
    double lo = 1e9, hi = 0.0;
    for (double f : LogFreqs(20.0, 20000.0, 2000)) {
      if (std::abs(MagDb(BandResponse(d, f, fs))) > std::abs(db) / 2.0) {
        lo = std::min(lo, f), hi = std::max(hi, f);
      }
    }
    return std::log2(hi / lo);
  };
  // The set Q at 6 dB; twice as narrow 12 dB higher, either way.
  REQUIRE(std::abs(width(6.0, prop) - width(6.0, 0)) < 0.02);
  REQUIRE(width(18.0, prop) < 0.6 * width(18.0, 0));
  REQUIRE(width(-18.0, prop) < 0.6 * width(-18.0, 0));
  REQUIRE(width(2.0, prop) > width(2.0, 0));
  REQUIRE(std::abs(ProportionalQ(1.0, 18.0) - 2.0) < 1e-12);
  // Gain still lands on target.
  REQUIRE(std::abs(MagDb(BandResponse(DesignBand(BandType::kPeak, 1000.0, -15.0,
                                                 1.0, prop, fs),
                                      1000.0, fs)) +
                   15.0) < 0.01);
}

}  // namespace eeq::test
