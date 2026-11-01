#include "model/band_clipboard.h"

#include <catch2/catch_test_macros.hpp>

namespace eeq::test {

TEST_CASE("Band copy/paste carries the chosen sections", "[clipboard]") {
  ParamValues v{};
  for (int i = 0; i < kNumParams; ++i) v[i] = 0.001 * i;
  const ParamValues original = v;
  const BandClip clip = CopyBand(v, 2);

  // All: band 5 becomes band 2; nothing else changes.
  ParamValues all = v;
  PasteBand(clip, BandSection::kAll, 5, all);
  for (int f = 0; f < kNumBandFields; ++f) {
    const auto field = static_cast<BandField>(f);
    REQUIRE(all[BandParamId(5, field)] == v[BandParamId(2, field)]);
  }
  for (int i = 0; i < kNumParams; ++i) {
    if (i / kNumBandFields == 5 && i < kNumBands * kNumBandFields) continue;
    REQUIRE(all[i] == original[i]);
  }

  // Shape and Dynamics each move only their own fields.
  for (const BandSection section :
       {BandSection::kShape, BandSection::kDynamics}) {
    ParamValues w = v;
    PasteBand(clip, section, 6, w);
    for (int f = 0; f < kNumBandFields; ++f) {
      const auto field = static_cast<BandField>(f);
      const double expected = SectionHas(section, field)
                                  ? v[BandParamId(2, field)]
                                  : v[BandParamId(6, field)];
      REQUIRE(w[BandParamId(6, field)] == expected);
    }
  }
  // The sections don't overlap, and routing (Tap, Chan) and the switches
  // (Bypass, Solo) only travel with All.
  for (int f = 0; f < kNumBandFields; ++f) {
    const auto field = static_cast<BandField>(f);
    REQUIRE_FALSE((SectionHas(BandSection::kShape, field) &&
                   SectionHas(BandSection::kDynamics, field)));
  }
  REQUIRE_FALSE(SectionHas(BandSection::kShape, BandField::kTap));
  REQUIRE_FALSE(SectionHas(BandSection::kDynamics, BandField::kBypass));
}

}  // namespace eeq::test
