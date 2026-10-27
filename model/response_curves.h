#ifndef EVERYTHINGEQ_MODEL_RESPONSE_CURVES_H_
#define EVERYTHINGEQ_MODEL_RESPONSE_CURVES_H_

#include <array>
#include <complex>
#include <vector>

#include "dsp/engine/params.h"

namespace eeq {

// Magnitude responses for the EQ graph, computed analytically from the same
// coefficient designs the audio uses (PLAN.md, "One source of truth for
// curves").

// `count` log-spaced frequencies from lo to hi.
std::vector<double> DisplayFrequencies(int count, double lo = 20.0,
                                       double hi = 20000.0);

// Per-group dynamics control values D in [0, 1] (0 = static settings).
using GroupControls = std::array<double, kNumGroups>;

struct ChainResponse {
  // Magnitude in dB of each band's own output (empty for Off bands).
  // Bypassed bands still get a curve; they are left out of `total_db`.
  std::array<std::vector<float>, kNumBands> band_db;
  // The whole chain, Main + Aux (what Stereo output mode plays), excluding
  // Output Gain.  Per-band Chan routing is not modeled: every band is drawn
  // as if it processed both channels.
  std::vector<float> total_db;
};

// Response of one band's main output (and its crossover complement) with
// dynamics control values applied for its Dyn A and Dyn B assignments.
std::complex<double> BandParamsResponse(const BandParams& bp, double control_a,
                                        double control_b, double global_scale,
                                        double freq_hz, double sample_rate,
                                        std::complex<double>* complement);

void ComputeChainResponse(const EngineParams& params,
                          const GroupControls& controls, double sample_rate,
                          const std::vector<double>& freqs, ChainResponse& out);

}  // namespace eeq

#endif  // EVERYTHINGEQ_MODEL_RESPONSE_CURVES_H_
