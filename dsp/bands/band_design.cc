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

// ITU-R BS.1770 K-weighting expressed exactly as two SVF cells.
BandDesign DesignKWeighting(double fs) {
  const double shelf_hz = 1681.974450955533;
  const double shelf_q = 0.7071752369554196;
  const double vh = std::pow(10.0, 3.999843853973347 / 20.0);
  const double vb = std::pow(vh, 0.4996667741545416);
  const SvfCoeffs shelf =
      MakeSvf(shelf_hz, 1.0 / shelf_q, vh, (vb - vh) / shelf_q, 1.0 - vh, fs);
  const double hp_q = 0.5003270373238773;
  const SvfCoeffs hp =
      MakeSvf(38.13547087602444, 1.0 / hp_q, 1.0, -1.0 / hp_q, -1.0, fs);
  return MakeSerial({shelf, hp});
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

}  // namespace

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
    case BandType::kLp12:
      return MakeSerial(
          {DesignSvf(SvfShape::kLowPass, ClampF(x), ClampQ(z), 0.0, fs)});
    case BandType::kHp12:
      return MakeSerial(
          {DesignSvf(SvfShape::kHighPass, ClampF(x), ClampQ(z), 0.0, fs)});
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
    case BandType::kLp24Lr:
    case BandType::kHp24Lr: {
      BandDesign d;
      d.topology = BandTopology::kCrossover;
      d.num_cells = 3;
      const SvfCoeffs lp =
          DesignSvf(SvfShape::kLowPass, ClampF(x), kButterworthQ, 0.0, fs);
      const SvfCoeffs hp =
          DesignSvf(SvfShape::kHighPass, ClampF(x), kButterworthQ, 0.0, fs);
      d.cells = {lp, lp, hp, SvfCoeffs{}};
      d.crossover_high = type == BandType::kHp24Lr;
      return d;
    }
    case BandType::kTilt: {
      const double half = std::clamp(y, -kMaxTiltDb, kMaxTiltDb) / 2.0;
      return MakeSerial(
          {DesignSvf(SvfShape::kLowShelf, ClampF(x), kTiltShelfQ, -half, fs),
           DesignSvf(SvfShape::kHighShelf, ClampF(x), kTiltShelfQ, half, fs)});
    }
    case BandType::kFletcherMunson:
      return DesignFletcherMunson(y, fs);
    case BandType::kVowel:
      return DesignVowel(x, y, z, option, fs);
    case BandType::kKWeight:
      return DesignKWeighting(fs);
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
    case BandType::kLp12:
    case BandType::kLp24Lr:
      add(SvfShape::kLowPass, ClampF(x), kButterworthQ);
      break;
    case BandType::kHighShelf:
    case BandType::kHp12:
    case BandType::kHp24Lr:
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
    case BandType::kKWeight:
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
