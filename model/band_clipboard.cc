#include "model/band_clipboard.h"

namespace eeq {

BandClip CopyBand(const ParamValues& values, int band) {
  BandClip clip;
  for (int f = 0; f < kNumBandFields; ++f) {
    clip.fields[f] = values[BandParamId(band, static_cast<BandField>(f))];
  }
  return clip;
}

bool SectionHas(BandSection section, BandField field) {
  switch (section) {
    case BandSection::kAll:
      return true;
    case BandSection::kShape:
      return field == BandField::kType || field == BandField::kX ||
             field == BandField::kY || field == BandField::kZ ||
             field == BandField::kOption;
    case BandSection::kDynamics:
      return field == BandField::kDynOn || field == BandField::kDynGroup ||
             field == BandField::kDynTarget || field == BandField::kDynRange ||
             field == BandField::kDynMakeup || field == BandField::kDynBOn ||
             field == BandField::kDynBGroup ||
             field == BandField::kDynBTarget || field == BandField::kDynBRange;
  }
  return false;
}

void PasteBand(const BandClip& clip, BandSection section, int band,
               ParamValues& values) {
  for (int f = 0; f < kNumBandFields; ++f) {
    const auto field = static_cast<BandField>(f);
    if (SectionHas(section, field)) {
      values[BandParamId(band, field)] = clip.fields[f];
    }
  }
}

}  // namespace eeq
