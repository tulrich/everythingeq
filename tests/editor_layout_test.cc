#include "model/editor_layout.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

namespace eeq::test {
namespace {

std::vector<Rect> Shown(const EditorLayout& L) {
  std::vector<Rect> r = {L.graph};
  if (L.show_top) r.insert(r.end(), {L.ribbon, L.preset});
  if (L.show_meters) r.insert(r.end(), {L.in_meter, L.out_meter, L.fader});
  if (L.show_strip) {
    r.insert(r.end(), {L.strip.shape, L.strip.cards[0], L.strip.cards[1]});
  }
  if (L.show_footer) r.push_back(L.footer);
  if (L.show_drawer_tab) r.push_back(L.drawer_tab);
  if (L.show_panel && !L.panel_overlay) r.push_back(L.panel);
  return r;
}

}  // namespace

TEST_CASE("Layout tiers follow the window size", "[layout]") {
  REQUIRE(TierFor(1200, 720) == LayoutTier::kFull);
  REQUIRE(TierFor(960, 560) == LayoutTier::kFull);
  REQUIRE(TierFor(720, 440) == LayoutTier::kCompact);
  REQUIRE(TierFor(1200, 440) == LayoutTier::kCompact);  // Height limits.
  REQUIRE(TierFor(520, 320) == LayoutTier::kSmall);
  REQUIRE(TierFor(360, 220) == LayoutTier::kMinimum);
  REQUIRE(TierFor(1600, 250) == LayoutTier::kMinimum);
}

TEST_CASE("Layout regions fit the window without overlapping", "[layout]") {
  for (float w = kMinEditorWidth; w <= kMaxEditorWidth; w += 37.f) {
    for (float h = kMinEditorHeight; h <= kMaxEditorHeight; h += 29.f) {
      for (const bool drawer : {false, true}) {
        const EditorLayout L = ComputeEditorLayout(w, h, drawer);
        const Rect window = {0, 0, w, h};
        const std::vector<Rect> shown = Shown(L);
        INFO("size " << w << " x " << h);
        REQUIRE(L.graph.w() >= 100.f);
        REQUIRE(L.graph.h() >= 100.f);
        for (size_t i = 0; i < shown.size(); ++i) {
          REQUIRE(!shown[i].empty());
          REQUIRE(window.Contains(shown[i]));
          for (size_t j = i + 1; j < shown.size(); ++j) {
            INFO("regions " << i << " and " << j);
            REQUIRE(!shown[i].Overlaps(shown[j]));
          }
        }
        if (L.show_strip) {
          for (const Rect& d : L.strip.dials) {
            REQUIRE(d.w() >= 30.f);
            REQUIRE(L.strip.shape.Contains(d));
          }
          REQUIRE(L.strip.shape.Contains(L.strip.info));
        }
        if (L.show_panel && L.panel_overlay) {
          REQUIRE(window.Contains(L.panel));
          REQUIRE(!L.panel.Overlaps(L.drawer_tab));
        }
      }
    }
  }
}

TEST_CASE("Tiers drop features as the window shrinks", "[layout]") {
  const EditorLayout full = ComputeEditorLayout(1200, 720, false);
  REQUIRE(full.show_panel);
  REQUIRE(!full.panel_overlay);
  REQUIRE(full.show_footer);
  const EditorLayout compact = ComputeEditorLayout(720, 440, false);
  REQUIRE(!compact.show_panel);
  REQUIRE(compact.show_drawer_tab);
  REQUIRE(compact.show_strip);
  REQUIRE(!compact.show_footer);
  REQUIRE(ComputeEditorLayout(720, 440, true).show_panel);
  const EditorLayout small_tier = ComputeEditorLayout(520, 320, false);
  REQUIRE(small_tier.show_top);
  REQUIRE(!small_tier.show_strip);
  REQUIRE(!small_tier.show_meters);
  const EditorLayout minimum = ComputeEditorLayout(360, 220, false);
  REQUIRE(!minimum.show_top);
  REQUIRE(minimum.graph.w() > 340.f);
}

TEST_CASE("The dynamics panel folds sections to fit", "[layout]") {
  const std::vector<PanelSection> sections = {
      {2, true, 2}, {3, true, 1}, {3, true, 0}};
  for (float h = 300.f; h <= 1200.f; h += 13.f) {
    const Rect panel = {0, 0, 330, h};
    const PanelLayout P = ComputePanelLayout(panel, sections, true, true);
    INFO("height " << h);
    REQUIRE(P.map_block >= kMapMinBlock);
    REQUIRE(P.map_block <= kMapMaxBlock);
    REQUIRE(P.sections.size() == 3);
    float bottom = P.map.b;
    for (const PanelLayout::Section& s : P.sections) {
      REQUIRE(s.header.t >= bottom);
      bottom = s.header.b;
      REQUIRE(s.cells.size() ==
              (s.open ? 2u * sections[&s - &P.sections[0]].rows : 0u));
      for (const Rect& c : s.cells) {
        REQUIRE(c.t >= s.header.b);
        bottom = std::max(bottom, c.b);
      }
    }
    // Whatever is open fits; with everything folded, at least the headers.
    const bool any_open =
        P.sections[0].open || P.sections[1].open || P.sections[2].open;
    if (any_open) REQUIRE(bottom <= h);
    // Lower priority folds first.
    if (P.sections[2].open) REQUIRE(P.sections[1].open);
    if (P.sections[1].open) REQUIRE(P.sections[0].open);
  }
  // A tall panel has room for everything, including the scope.
  const PanelLayout tall =
      ComputePanelLayout({0, 0, 330, 900}, sections, false, true);
  REQUIRE(tall.show_scope);
  REQUIRE(tall.show_sc_meter);
  for (const PanelLayout::Section& s : tall.sections) REQUIRE(s.open);
}

}  // namespace eeq::test
