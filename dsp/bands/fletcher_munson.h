#ifndef EVERYTHINGEQ_DSP_BANDS_FLETCHER_MUNSON_H_
#define EVERYTHINGEQ_DSP_BANDS_FLETCHER_MUNSON_H_

#include <array>

#include "dsp/svf/svf.h"

namespace eeq {

// One cell of the Fletcher-Munson (ISO 226) compensation cascade.  The cell
// gain is db_per_phon * phon_delta, so the whole contour scales proportionally.
struct FmCell {
  SvfShape shape;
  double hz;
  double q;
  double db_per_phon;
};

// Fitted offline by tools/fmfit against iso226::CompensationPerPhon at a
// 20-phon delta.  Regenerate with:  build/tools/fmfit
extern const std::array<FmCell, 3> kFmCells;

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_BANDS_FLETCHER_MUNSON_H_
