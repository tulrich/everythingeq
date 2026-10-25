#pragma once

#include <array>

#include "svf/Svf.h"

namespace eeq {

// One cell of the Fletcher-Munson (ISO 226) compensation cascade.  The cell
// gain is dbPerPhon * phonDelta, so the whole contour scales proportionally.
struct FmCell {
    SvfShape shape;
    double hz;
    double q;
    double dbPerPhon;
};

// Fitted offline by tools/fmfit against iso226::compensationPerPhon at a
// 20-phon delta.  Regenerate with:  build/tools/fmfit/fmfit
extern const std::array<FmCell, 3> kFmCells;

}  // namespace eeq
