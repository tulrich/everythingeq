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

double ControlFor(const BandParams& bp, const GroupControls& controls) {
  if (bp.dyn_group < 0 || bp.dyn_group >= kNumGroups) return 0.0;
  return std::clamp(controls[bp.dyn_group], 0.0, 1.0);
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

std::complex<double> BandParamsResponse(const BandParams& bp, double control,
                                        double global_scale, double freq_hz,
                                        double sample_rate,
                                        std::complex<double>* complement) {
  const ResolvedBand rb = ResolveBand(bp, control, global_scale);
  const BandDesign d =
      DesignBand(bp.type, rb.x, rb.y, rb.z, rb.option, sample_rate);
  if (complement) *complement = BandComplementResponse(d, freq_hz, sample_rate);
  return BandResponse(d, freq_hz, sample_rate);
}

void ComputeChainResponse(const EngineParams& params,
                          const GroupControls& controls, double sample_rate,
                          const std::vector<double>& freqs,
                          ChainResponse& out) {
  const size_t n = freqs.size();
  out.total_db.assign(n, 0.0f);
  std::vector<std::complex<double>> main(n, 1.0), aux(n, 0.0);
  const double scale = params.global.global_scale;

  for (int b = 0; b < kNumBands; ++b) {
    const BandParams& bp = params.bands[b];
    std::vector<float>& curve = out.band_db[b];
    if (bp.type == BandType::kOff) {
      curve.clear();
      continue;
    }
    curve.resize(n);
    const double control = ControlFor(bp, controls);
    for (size_t i = 0; i < n; ++i) {
      std::complex<double> comp;
      const std::complex<double> h =
          BandParamsResponse(bp, control, scale, freqs[i], sample_rate, &comp);
      curve[i] = ToDb(h);
      if (bp.bypass) continue;
      // Tap semantics (ABOUT.md section 3.D).
      if (bp.tap == Tap::kMain) {
        main[i] *= h;
      } else if (IsCrossoverType(bp.type)) {
        aux[i] += main[i] * h;
        main[i] *= comp;
      } else if (IsFilterType(bp.type)) {
        aux[i] += main[i] * h;
        main[i] *= 1.0 - h;
      } else {
        aux[i] += main[i] * (h - 1.0);
      }
    }
  }
  for (size_t i = 0; i < n; ++i) out.total_db[i] = ToDb(main[i] + aux[i]);
}

}  // namespace eeq
