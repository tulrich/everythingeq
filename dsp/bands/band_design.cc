#include "dsp/bands/band_design.h"

#include <algorithm>
#include <cmath>

#include "dsp/bands/fletcher_munson.h"
#include "dsp/bands/vowel.h"
#include "dsp/common/math.h"

namespace eeq {

namespace {

double ClampF(double hz) { return std::clamp(hz, kMinFreq, kMaxFreq); }
double ClampQ(double q) { return std::clamp(q, kMinQ, kMaxQ); }
double ClampGain(double db) { return std::clamp(db, -kMaxGainDb, kMaxGainDb); }

BandDesign MakeSerial(std::initializer_list<SvfCoeffs> cells,
                      double post_gain = 1.0) {
  BandDesign d;
  d.topology = BandTopology::kSerial;
  for (const SvfCoeffs& c : cells) d.cells[d.num_cells++] = c;
  d.post_gain = post_gain;
  return d;
}

BandDesign DesignFletcherMunson(double phon_delta, double fs) {
  phon_delta = std::clamp(phon_delta, -kMaxPhonDelta, kMaxPhonDelta);
  BandDesign d = MakeSerial({});
  for (const FmCell& c : kFmCells)
    d.cells[d.num_cells++] =
        DesignSvf(c.shape, c.hz, c.q, c.db_per_phon * phon_delta, fs);
  // Anchor the contour to 0 dB at 1 kHz.
  d.post_gain = 1.0 / std::abs(BandResponse(d, 1000.0, fs));
  return d;
}

BandDesign DesignVowel(double morph, double gain_db, double scale, int option,
                       double fs) {
  const Formants f = VowelFormants(morph);
  scale = std::clamp(scale, kTractScaleMin, kTractScaleMax);
  gain_db = ClampGain(gain_db);
  BandDesign d = MakeSerial({});
  for (int i = 0; i < 3; ++i) {
    const double g = option == static_cast<int>(VowelWeighting::kFlat)
                         ? gain_db
                         : gain_db * DbToGain(f.weight_db[i]);
    d.cells[d.num_cells++] =
        DesignSvf(SvfShape::kBell, f.hz[i] * scale, kFormantQ[i], g, fs);
  }
  return d;
}

// Vintage Low: the low section of a classic passive program EQ.  The boost is
// the LC network: a resonant second-order shelf whose half-gain point is the
// selected frequency.  The attenuation is the RC network: a much broader shelf
// centered five times higher.  With both up, the boost wins at the bottom and
// the attenuation above it, giving the bump below the frequency and the dip
// above it. Shaped after published curves, not fitted to measurements.
BandDesign DesignVintageLow(double hz, double boost_db, double atten_db,
                            double fs) {
  const double f = ClampF(hz);
  return MakeSerial(
      {DesignSvf(SvfShape::kLowShelf, f, kVintageBoostQ, ClampGain(boost_db),
                 fs),
       DesignSvf(SvfShape::kLowShelf, ClampF(f * kVintageLowAttenRatio),
                 kVintageAttenQ, -ClampGain(atten_db), fs)});
}

// The high section: a broad boost bell and a gentle attenuation shelf at
// 5, 10 or 20 kHz (the "atten sel" switch), e.g. boost 10 kHz and cut
// 20 kHz for the classic air lift.
BandDesign DesignVintageHigh(double hz, double boost_db, double atten_db,
                             int option, double fs) {
  static const double kAttenHz[] = {5000.0, 10000.0, 20000.0};
  const double atten_hz = kAttenHz[std::clamp(option, 0, 2)];
  return MakeSerial({DesignSvf(SvfShape::kBell, ClampF(hz), kVintageHighBoostQ,
                               ClampGain(boost_db), fs),
                     DesignSvf(SvfShape::kHighShelf, ClampF(atten_hz),
                               kVintageAttenQ, -ClampGain(atten_db), fs)});
}

// Teeth on the harmonics of f0 (all, odd or even ones) with gain_db at
// each tooth and 0 dB midway between: y = x + (G - 1) R x, where the
// resonator R = (1 - g) / (1 - s g z^-D), rescaled to be 1 at the teeth
// and 0 midway (where it is r = (1 - g) / (1 + g)).
BandDesign DesignComb(double f0, double gain_db, double q, int option,
                      double fs) {
  const auto harmonics = static_cast<CombHarmonics>(std::clamp(option, 0, 2));
  f0 = std::clamp(f0, kCombMinHz, kCombMaxHz);
  BandDesign d;
  d.topology = BandTopology::kComb;
  // Odd: a negative comb at twice f0 (teeth at odd multiples of f0).  Even:
  // a comb at twice f0.
  const bool all = harmonics == CombHarmonics::kAll;
  d.comb_delay = std::clamp(fs / (all ? f0 : 2.0 * f0), 2.0, fs / kCombMinHz);
  d.comb_sign = harmonics == CombHarmonics::kOdd ? -1.0 : 1.0;
  const double g = CombFeedback(q);
  const double r = (1.0 - g) / (1.0 + g);
  const double k = (DbToGain(ClampGain(gain_db)) - 1.0) / (1.0 - r);
  d.comb_g = g;
  d.comb_dry = 1.0 - k * r;
  d.comb_wet = k * (1.0 - g);
  d.comb_slew = 1.0 - std::exp(-1000.0 / (kCombGlideMs * fs));
  return d;
}

}  // namespace

double CombFeedback(double q) {
  return 1.0 - kPi / std::clamp(q, kCombMinQ, kCombMaxQ);
}

// ITU-R BS.1770 K-weighting expressed exactly as two SVF cells.
BandDesign DesignKWeighting(double fs) {
  const double vh = std::pow(10.0, kKWeightShelfDb / 20.0);
  const double vb = std::pow(vh, 0.4996667741545416);
  const SvfCoeffs shelf = MakeSvf(kKWeightShelfHz, 1.0 / kKWeightShelfQ, vh,
                                  (vb - vh) / kKWeightShelfQ, 1.0 - vh, fs);
  const SvfCoeffs hp = MakeSvf(kKWeightHpHz, 1.0 / kKWeightHpQ, 1.0,
                               -1.0 / kKWeightHpQ, -1.0, fs);
  return MakeSerial({shelf, hp});
}

BandDesign DesignShelfTilt(double hz, double tilt_db, double fs) {
  const double half = std::clamp(tilt_db, -kMaxTiltDb, kMaxTiltDb) / 2.0;
  return MakeSerial(
      {DesignSvf(SvfShape::kLowShelf, ClampF(hz), 0.5, -half, fs),
       DesignSvf(SvfShape::kHighShelf, ClampF(hz), 0.5, half, fs)});
}

BandDesign DesignBand(BandType type, double x, double y, double z, int option,
                      double fs) {
  switch (type) {
    case BandType::kOff:
      return {};
    case BandType::kPeak:
      return MakeSerial(
          {DesignSvf(SvfShape::kBell, ClampF(x), ClampQ(z), ClampGain(y), fs)});
    case BandType::kLowShelf:
      return MakeSerial({DesignSvf(SvfShape::kLowShelf, ClampF(x), ClampQ(z),
                                   ClampGain(y), fs)});
    case BandType::kHighShelf:
      return MakeSerial({DesignSvf(SvfShape::kHighShelf, ClampF(x), ClampQ(z),
                                   ClampGain(y), fs)});
    case BandType::kBandShelf: {
      // A plateau between two Butterworth shelves whose half-gain points are
      // the edges.  Narrow widths don't reach the full gain (like a wide bell).
      const double half = 0.5 * std::clamp(z, kMinWidthOct, kMaxWidthOct);
      const double lo = ClampF(x * std::exp2(-half));
      const double hi = ClampF(x * std::exp2(half));
      const double g = ClampGain(y);
      return MakeSerial(
          {DesignSvf(SvfShape::kHighShelf, lo, kButterworthQ, g, fs),
           DesignSvf(SvfShape::kHighShelf, hi, kButterworthQ, -g, fs)});
    }
    case BandType::kLp:
    case BandType::kHp: {
      const bool high = type == BandType::kHp;
      if (option != static_cast<int>(FilterSlope::k24)) {
        return MakeSerial(
            {DesignSvf(high ? SvfShape::kHighPass : SvfShape::kLowPass,
                       ClampF(x), ClampQ(z), 0.0, fs)});
      }
      // Linkwitz-Riley (LR4 at Q 0.707): a split cell then a second LP or HP.
      BandDesign d;
      d.topology = BandTopology::kCrossover;
      d.num_cells = 3;
      const SvfCoeffs lp =
          DesignSvf(SvfShape::kLowPass, ClampF(x), ClampQ(z), 0.0, fs);
      const SvfCoeffs hp =
          DesignSvf(SvfShape::kHighPass, ClampF(x), ClampQ(z), 0.0, fs);
      d.cells = {lp, lp, hp, SvfCoeffs{}};
      d.crossover_high = high;
      return d;
    }
    case BandType::kNotch:
      return MakeSerial(
          {DesignSvf(SvfShape::kNotch, ClampF(x), ClampQ(z), 0.0, fs)});
    case BandType::kBp: {
      const double gain =
          y <= kBpOffDb ? 0.0 : DbToGain(std::min(y, kMaxGainDb));
      return MakeSerial(
          {DesignSvf(SvfShape::kBandPass, ClampF(x), ClampQ(z), 0.0, fs)},
          gain);
    }
    case BandType::kTilt: {
      // Four high shelves at the midpoints of the width, each with a quarter
      // of the tilt: a monotonic dB-per-octave ramp from -tilt/2 to +tilt/2
      // across the width, centered on the pivot.
      const double tilt = std::clamp(y, -kMaxTiltDb, kMaxTiltDb);
      const double width = std::clamp(z, 0.0, kMaxWidthOct);
      BandDesign d = MakeSerial({}, DbToGain(-tilt / 2.0));
      for (int i = 0; i < kTiltCells; ++i) {
        const double t = (i + 0.5) / kTiltCells - 0.5;
        d.cells[d.num_cells++] =
            DesignSvf(SvfShape::kHighShelf, ClampF(x * std::exp2(t * width)),
                      kTiltShelfQ, tilt / kTiltCells, fs);
      }
      return d;
    }
    case BandType::kFletcherMunson:
      return DesignFletcherMunson(y, fs);
    case BandType::kVowel:
      return DesignVowel(x, y, z, option, fs);
    case BandType::kVintageLow:
      return DesignVintageLow(x, y, z, fs);
    case BandType::kVintageHigh:
      return DesignVintageHigh(x, y, z, option, fs);
    case BandType::kComb:
      return DesignComb(x, y, z, option, fs);
  }
  return {};
}

namespace {

std::complex<double> CrossoverResponse(const BandDesign& d, bool high, double f,
                                       double fs) {
  if (high) {
    SvfCoeffs split_hp = d.cells[0];
    split_hp.m0 = 1.0, split_hp.m1 = -split_hp.k, split_hp.m2 = -1.0;
    return SvfResponse(split_hp, f, fs) * SvfResponse(d.cells[2], f, fs);
  }
  return SvfResponse(d.cells[0], f, fs) * SvfResponse(d.cells[1], f, fs);
}

}  // namespace

std::complex<double> BandResponse(const BandDesign& d, double f, double fs) {
  switch (d.topology) {
    case BandTopology::kPassthrough:
      return 1.0;
    case BandTopology::kCrossover:
      return CrossoverResponse(d, d.crossover_high, f, fs);
    case BandTopology::kSerial: {
      std::complex<double> h = d.post_gain;
      for (int i = 0; i < d.num_cells; ++i) h *= SvfResponse(d.cells[i], f, fs);
      return h;
    }
    case BandTopology::kComb: {
      const std::complex<double> zd =
          std::polar(1.0, -2.0 * kPi * f * d.comb_delay / fs);
      return d.comb_dry + d.comb_wet / (1.0 - d.comb_sign * d.comb_g * zd);
    }
  }
  return 1.0;
}

std::complex<double> BandComplementResponse(const BandDesign& d, double f,
                                            double fs) {
  if (d.topology != BandTopology::kCrossover) return 0.0;
  return CrossoverResponse(d, !d.crossover_high, f, fs);
}

DetectorDesign DesignDetector(BandType type, double x, double z, double fs) {
  DetectorDesign d;
  auto add = [&](SvfShape shape, double hz, double q) {
    d.cells[d.num_cells++] = DesignSvf(shape, hz, q, 0.0, fs);
  };
  switch (type) {
    case BandType::kPeak:
    case BandType::kNotch:
    case BandType::kBp:
      add(SvfShape::kBandPass, ClampF(x), ClampQ(z));
      break;
    case BandType::kBandShelf: {
      // Bandpass with its -3 dB points at the plateau edges.
      const double w = std::exp2(std::clamp(z, kMinWidthOct, kMaxWidthOct));
      add(SvfShape::kBandPass, ClampF(x), ClampQ(std::sqrt(w) / (w - 1.0)));
      break;
    }
    case BandType::kLowShelf:
    case BandType::kLp:
    case BandType::kVintageLow:
      add(SvfShape::kLowPass, ClampF(x), kButterworthQ);
      break;
    case BandType::kVintageHigh:
      add(SvfShape::kBandPass, ClampF(x), kVintageHighBoostQ);
      break;
    case BandType::kHighShelf:
    case BandType::kHp:
      add(SvfShape::kHighPass, ClampF(x), kButterworthQ);
      break;
    case BandType::kVowel: {
      const Formants f = VowelFormants(x);
      const double scale = std::clamp(z, kTractScaleMin, kTractScaleMax);
      for (int i = 0; i < 3; ++i)
        add(SvfShape::kBandPass, f.hz[i] * scale, kFormantQ[i]);
      break;
    }
    case BandType::kOff:
    case BandType::kTilt:
    case BandType::kFletcherMunson:
    case BandType::kComb:
      break;
  }
  return d;
}

std::complex<double> DetectorResponse(const DetectorDesign& d, double f,
                                      double fs) {
  if (d.num_cells == 0) return 1.0;
  std::complex<double> h = 0.0;
  for (int i = 0; i < d.num_cells; ++i) h += SvfResponse(d.cells[i], f, fs);
  return h;
}

}  // namespace eeq
