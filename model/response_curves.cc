#include "model/response_curves.h"

#include <algorithm>
#include <cmath>

#include "dsp/bands/band_design.h"
#include "dsp/bands/band_processor.h"

namespace eeq {
namespace {

float ToDb(std::complex<double> h) {
  return static_cast<float>(20.0 * std::log10(std::max(std::abs(h), 1e-9)));
}

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
                          const std::vector<double>& freqs,
                          ChainResponse& out) {
  const size_t n = freqs.size();
  out.total_db.assign(n, 0.0f);
  const bool split_mode = params.global.output_mode != OutputMode::kNormal;
  const SplitSide side = SplitSideOf(params);
  // As the engine's chain (ABOUT.md section 3.D).
  std::vector<std::complex<double>> main(n, 1.0), split(n, 0.0),
      xover_comp(n, 0.0);
  const double scale = params.global.global_scale;

  for (int b = 0; b < kNumBands; ++b) {
    if (split_mode && b == kSplitBands) {
      for (size_t i = 0; i < n; ++i) {
        split[i] = side.bands > 0 ? main[i] : 0.0;
        main[i] = side.crossover >= 0 ? xover_comp[i] : 1.0 - split[i];
      }
    }
    const BandParams& bp = params.bands[b];
    std::vector<float>& curve = out.band_db[b];
    if (bp.type == BandType::kOff) {
      curve.clear();
      continue;
    }
    curve.resize(n);
    const double control_a = ControlFor(bp.dyn_group, controls);
    const double control_b = ControlFor(bp.dyn_b_group, controls);
    for (size_t i = 0; i < n; ++i) {
      std::complex<double> comp;
      const std::complex<double> h = BandParamsResponse(
          bp, control_a, control_b, scale, freqs[i], sample_rate, &comp);
      curve[i] = ToDb(h);
      if (bp.bypass) continue;
      if (b == side.crossover) xover_comp[i] = main[i] * comp;
      main[i] *= h;
    }
  }
  out.split_db.clear();
  out.residual_db.clear();
  if (split_mode) {
    out.split_db.resize(n);
    out.residual_db.resize(n);
  }
  for (size_t i = 0; i < n; ++i) {
    out.total_db[i] = ToDb(main[i] + split[i]);
    if (split_mode) {
      out.split_db[i] = ToDb(split[i]);
      out.residual_db[i] = ToDb(main[i]);
    }
  }
}

}  // namespace eeq
