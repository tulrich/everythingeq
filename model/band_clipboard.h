#ifndef EVERYTHINGEQ_MODEL_BAND_CLIPBOARD_H_
#define EVERYTHINGEQ_MODEL_BAND_CLIPBOARD_H_

#include <array>

#include "model/param_table.h"

namespace eeq {

// Copying a band's settings onto other bands (ABOUT.md section 4, band
// copy/paste).

// What a paste carries over.
enum class BandSection {
  kAll,       // Every band setting.
  kShape,     // Type, X/Y/Z and Option.
  kDynamics,  // Dyn A and Dyn B (group, target, range) and Dyn Makeup.
};

// A copied band's host values (plain, with slots normalized).
struct BandClip {
  std::array<double, kNumBandFields> fields{};
};

BandClip CopyBand(const ParamValues& values, int band);

// Whether a paste of `section` carries `field`.
bool SectionHas(BandSection section, BandField field);

// Writes the clip's `section` fields into `band` of `values`.
void PasteBand(const BandClip& clip, BandSection section, int band,
               ParamValues& values);

}  // namespace eeq

#endif  // EVERYTHINGEQ_MODEL_BAND_CLIPBOARD_H_
