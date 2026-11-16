#include "model/response_curves.h"

#include <algorithm>
#include <cmath>

#include "dsp/bands/band_design.h"
#include "dsp/bands/band_processor.h"
#include "dsp/common/math.h"

namespace eeq {
namespace {

double ControlFor(int group, const GroupControls& controls) {
  if (group < 0 || group >= kNumGroups) return 0.0;
  return std::clamp(controls[group], 0.0, 1.0);
}

}  // namespace

std::vector<double> DisplayFrequencies(int count, double lo, double hi) {
  std::vector<double> f(count);
  for (int i = 0; i < count; ++i) {
    f[i] =
        lo * std::pow(hi / lo,
                      count > 1 ? static_cast<double>(i) / (count - 1) : 0.0);
  }
  return f;
}

std::complex<double> BandParamsResponse(const BandParams& bp, double control_a,
                                        double control_b, double global_scale,
                                        double freq_hz, double sample_rate,
                                        std::complex<double>* complement) {
  const ResolvedBand rb = ResolveBand(bp, control_a, control_b, global_scale);
  const BandDesign d = DesignResolvedBand(bp.type, rb, sample_rate);
  if (complement) *complement = BandComplementResponse(d, freq_hz, sample_rate);
  return BandResponse(d, freq_hz, sample_rate);
}

void ComputeChainResponse(const EngineParams& params,
                          const GroupControls& controls, double sample_rate,
                          const std::vector<double>& freqs, ChainResponse& out,
                          const VocoderShapes* vocoder_shapes) {
  const size_t n = freqs.size();
  const bool split_mode = params.global.output_mode != OutputMode::kNormal;
  const SplitSide side = SplitSideOf(params);
  const double scale = params.global.global_scale;

  // Each band's design, once.
  std::array<BandDesign, kNumBands> design{};
  int combs = 0;
  int comb = -1;             // The (last) Comb band
  double comb_period = 0.0;  // its Hz between teeth
  for (int b = 0; b < kNumBands; ++b) {
    const BandParams& bp = params.bands[b];
    if (bp.type == BandType::kOff) continue;
    const ResolvedBand rb =
        ResolveBand(bp, ControlFor(bp.dyn_group, controls),
                    ControlFor(bp.dyn_b_group, controls), scale);
    design[b] = bp.type == BandType::kVocoder && vocoder_shapes
                    ? DesignVocoder(rb.x, (*vocoder_shapes)[b], sample_rate)
                    : DesignResolvedBand(bp.type, rb, sample_rate);
    if (bp.type == BandType::kComb) {
      ++combs;
      comb = b;
      comb_period = sample_rate / design[b].comb_delay;
    }
  }

  // The chain at one frequency, as the engine runs it (ABOUT.md 3.D).
  struct Point {
    std::array<std::complex<double>, kNumBands> band;
    std::complex<double> total, split, residual;
  };
  // `comb_h`, if set, stands in for the Comb band's response.
  auto eval = [&](double f, Point& pt,
                  const std::complex<double>* comb_h = nullptr) {
    std::complex<double> main = 1.0, split = 0.0, xover_comp = 0.0;
    for (int b = 0; b < kNumBands; ++b) {
      if (split_mode && b == kSplitBands) {
        split = side.bands > 0 ? main : 0.0;
        main = side.crossover >= 0 ? xover_comp : 1.0 - split;
      }
      const BandParams& bp = params.bands[b];
      if (bp.type == BandType::kOff) continue;
      const std::complex<double> h =
          comb_h && b == comb ? *comb_h
                              : BandResponse(design[b], f, sample_rate);
      pt.band[b] = h;
      if (bp.bypass) continue;
      if (b == side.crossover) {
        xover_comp = main * BandComplementResponse(design[b], f, sample_rate);
      }
      main *= h;
    }
    pt.total = main + split;
    pt.split = split;
    pt.residual = main;
  };

  for (int b = 0; b < kNumBands; ++b) {
    if (params.bands[b].type == BandType::kOff) {
      out.band_db[b].clear();
    } else {
      out.band_db[b].resize(n);
    }
  }
  out.total_db.assign(n, 0.0f);
  out.split_db.assign(split_mode ? n : 0, 0.0f);
  out.residual_db.assign(split_mode ? n : 0, 0.0f);
  auto db = [](double power) {
    return static_cast<float>(10.0 * std::log10(std::max(power, 1e-18)));
  };
  Point pt;
  for (size_t i = 0; i < n; ++i) {
    // A Comb's teeth are evenly spaced in Hz, so up high they're denser
    // than the display points.  Each point shows the average power over its
    // share of the axis: sampled finely enough to follow the teeth or, once
    // the share spans a whole tooth spacing, exactly.  Every output is
    // o0 + (o1 - o0) H in the Comb's response H, which over a spacing has
    // mean dry + wet and mean power dry^2 + 2 dry wet + wet^2 / (1 - g^2).
    int samples = 1;
    double lo = freqs[i], width = 0.0;
    bool exact = false;
    if (combs > 0 && n > 1) {
      // Halfway (in log frequency) to each neighbor; at the ends, as far
      // as to the one neighbor.
      const double below =
          i > 0 ? freqs[i - 1] : freqs[i] * freqs[i] / freqs[1];
      const double above =
          i + 1 < n ? freqs[i + 1] : freqs[i] * freqs[i] / freqs[n - 2];
      lo = std::sqrt(freqs[i] * below);
      width = std::sqrt(freqs[i] * above) - lo;
      if (combs > 1) {
        samples = 48;
      } else if (width >= comb_period) {
        exact = true;
      } else {
        // About 8 samples across each tooth (about spacing / Q wide).
        const double q = kPi / (1.0 - design[comb].comb_g);
        samples = std::clamp(
            static_cast<int>(std::ceil(8.0 * q * width / comb_period)), 1, 64);
      }
    }
    std::array<double, kNumBands> band_power{};
    double total = 0.0, split = 0.0, residual = 0.0;
    if (exact) {
      const BandDesign& d = design[comb];
      const double mean = d.comb_dry + d.comb_wet;
      const double second =
          d.comb_dry * d.comb_dry + 2.0 * d.comb_dry * d.comb_wet +
          d.comb_wet * d.comb_wet / (1.0 - d.comb_g * d.comb_g);
      const std::complex<double> zero = 0.0, one = 1.0;
      Point p0, p1;
      eval(freqs[i], p0, &zero);
      eval(freqs[i], p1, &one);
      auto power = [&](std::complex<double> o0, std::complex<double> o1) {
        const std::complex<double> slope = o1 - o0;
        return std::norm(o0) + 2.0 * std::real(std::conj(o0) * slope) * mean +
               std::norm(slope) * second;
      };
      for (int b = 0; b < kNumBands; ++b) {
        band_power[b] = power(p0.band[b], p1.band[b]);
      }
      total = power(p0.total, p1.total);
      split = power(p0.split, p1.split);
      residual = power(p0.residual, p1.residual);
    } else {
      for (int k = 0; k < samples; ++k) {
        eval(samples == 1 ? freqs[i] : lo + width * (k + 0.5) / samples, pt);
        for (int b = 0; b < kNumBands; ++b) {
          band_power[b] += std::norm(pt.band[b]);
        }
        total += std::norm(pt.total);
        split += std::norm(pt.split);
        residual += std::norm(pt.residual);
      }
    }
    for (int b = 0; b < kNumBands; ++b) {
      if (!out.band_db[b].empty())
        out.band_db[b][i] = db(band_power[b] / samples);
    }
    out.total_db[i] = db(total / samples);
    if (split_mode) {
      out.split_db[i] = db(split / samples);
      out.residual_db[i] = db(residual / samples);
    }
  }
}

}  // namespace eeq
