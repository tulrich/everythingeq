#include "model/editor_layout.h"

#include <algorithm>

namespace eeq {
namespace {

constexpr float kPad = 8.f;
constexpr float kGap = 4.f;
constexpr float kFooterHeight = 48.f;
constexpr float kDrawerTabWidth = 22.f;
constexpr float kGripClearance = 14.f;  // The resize grip in the corner.

// Panel rows.
constexpr float kTitleHeight = 12.f;
constexpr float kLegendHeight = 11.f;
constexpr float kHeaderHeight = 18.f;
constexpr float kWarningHeight = 12.f;
constexpr float kScopeHeight = 54.f;
constexpr float kMaxScopeHeight = 140.f;
constexpr float kSectionHeader = 13.f;
constexpr float kFieldHeight = 18.f;
constexpr float kFieldPitch = 21.f;
constexpr float kSectionGap = 5.f;
constexpr float kScMeterWidth = 22.f;

Rect Box(float l, float t, float w, float h) { return {l, t, l + w, t + h}; }

float SectionHeight(const PanelSection& s, bool open) {
  return kSectionHeader + 3.f + (open ? s.rows * kFieldPitch : 0.f);
}

StripLayout ComputeStrip(const Rect& r, bool full) {
  StripLayout s;
  s.compact = !full;
  const float info_w = full ? 128.f : 112.f;
  const float card_w =
      full ? std::clamp((r.w() - 330.f) / 2.f, 140.f, 196.f) : 132.f;
  s.cards[1] = {r.r - card_w, r.t, r.r, r.b};
  s.cards[0] = {s.cards[1].l - kPad - card_w, r.t, s.cards[1].l - kPad, r.b};
  s.shape = {r.l, r.t, s.cards[0].l - kPad, r.b};
  const Rect inner = {s.shape.l + kPad, s.shape.t + kPad, s.shape.r - kPad,
                      s.shape.b - kPad};
  s.info = {inner.l, inner.t, inner.l + info_w, inner.b};
  const float dials_l = s.info.r + 10.f;
  const float dial_w = std::max(0.f, (inner.r - dials_l) / 3.f);
  for (int i = 0; i < 3; ++i) {
    s.dials[i] = {dials_l + i * dial_w, inner.t, dials_l + (i + 1) * dial_w,
                  inner.b};
  }
  return s;
}

// The input meter left of the graph, the output meter and fader right of it,
// in every tier.  Returns what's left for the graph.
Rect PlaceMeters(Rect area, bool full, EditorLayout& L) {
  L.show_meters = true;
  const float meter_w = full ? 28.f : 22.f;
  const float fader_w = full ? 24.f : 20.f;
  L.in_meter = {area.l, area.t, area.l + meter_w, area.b};
  L.fader = {area.r - fader_w, area.t, area.r, area.b};
  L.out_meter = {L.fader.l - kGap - meter_w, area.t, L.fader.l - kGap, area.b};
  area.l = L.in_meter.r + kGap;
  area.r = L.out_meter.l - kGap;
  return area;
}

}  // namespace

LayoutTier TierFor(float width, float height) {
  const int w = width >= 880.f   ? 3
                : width >= 600.f ? 2
                : width >= 420.f ? 1
                                 : 0;
  const int h = height >= 480.f   ? 3
                : height >= 380.f ? 2
                : height >= 260.f ? 1
                                  : 0;
  return static_cast<LayoutTier>(std::min(w, h));
}

EditorLayout ComputeEditorLayout(float width, float height, bool drawer_open) {
  EditorLayout L;
  L.width = width;
  L.height = height;
  L.tier = TierFor(width, height);
  const int tier = static_cast<int>(L.tier);
  const bool full = L.tier == LayoutTier::kFull;
  const bool compact = L.tier == LayoutTier::kCompact;

  if (L.tier == LayoutTier::kMinimum) {
    L.graph = PlaceMeters({kGap, kGap, width - kGap, height - kGap}, false, L);
    L.fader.b -= kGripClearance;
    return L;
  }

  L.show_top = true;
  const float top_h = full ? 30.f : compact ? 28.f : 26.f;
  const float body_t = kPad + top_h + kPad;
  const float body_b = height - kPad;
  L.ribbon_power = tier >= 2;
  L.preset_undo = tier >= 2;

  // Signal flow runs left to right: the dynamics panel (docked, or a drawer
  // behind a tab) on the left, the EQ column on the right.
  Rect eq;
  if (full) {
    const float panel_w = std::clamp(width * 0.3f, 290.f, 360.f);
    L.show_panel = true;
    L.panel = {kPad, body_t, kPad + panel_w, body_b};
    L.preset = {kPad, kPad, L.panel.r, kPad + top_h};
    eq = {L.panel.r + kPad, body_t, width - kPad, body_b};
  } else if (compact) {
    L.show_drawer_tab = true;
    L.drawer_tab = {kPad, body_t, kPad + kDrawerTabWidth, body_b};
    L.show_panel = drawer_open;
    L.panel_overlay = true;
    const float panel_w = std::min(320.f, width - 80.f);
    L.panel = {L.drawer_tab.r + 6.f, body_t, L.drawer_tab.r + 6.f + panel_w,
               body_b};
    L.preset = {kPad, kPad, kPad + 190.f, kPad + top_h};
    eq = {L.drawer_tab.r + 6.f, body_t, width - kPad, body_b};
  } else {
    L.preset = {kPad, kPad, kPad + 150.f, kPad + top_h};
    eq = {kPad, body_t, width - kPad, body_b};
  }

  float graph_b = eq.b;
  if (full) {
    L.show_footer = true;
    L.footer = {eq.l, eq.b - kFooterHeight, eq.r, eq.b};
    graph_b = L.footer.t - kPad;
  }
  if (tier >= 2) {
    L.show_strip = true;
    const float strip_h = full ? (height >= 700.f ? 128.f : 116.f) : 92.f;
    const Rect strip = {eq.l, graph_b - strip_h, eq.r, graph_b};
    L.strip = ComputeStrip(strip, full);
    graph_b = strip.t - kPad;
  }
  L.graph = PlaceMeters({eq.l, eq.t, eq.r, graph_b}, full, L);
  if (tier < 2) L.fader.b -= kGripClearance;  // It reaches the window corner.
  // The ribbon spans the graph, so each band's cell sits over its curve's
  // part of the window (beside the preset name when they'd collide).
  L.ribbon = {std::max(L.graph.l, L.preset.r + kPad), kPad, L.graph.r,
              kPad + top_h};
  return L;
}

float MapHeight(float row) {
  return kMapTopHeight + kNumMapGroups * row + kMapBottomHeight;
}

PanelLayout ComputePanelLayout(const Rect& panel,
                               const std::vector<PanelSection>& sections,
                               bool warning, bool sc_meter) {
  PanelLayout P;
  const Rect inner = {panel.l + kPad, panel.t + kPad, panel.r - kPad,
                      panel.b - kPad};
  float y = inner.t;
  P.title = Box(inner.l, y, inner.w(), kTitleHeight);
  y += kTitleHeight + 2.f;
  P.map_row = std::clamp(
      (inner.h() * 0.4f - kMapTopHeight - kMapBottomHeight) / kNumMapGroups,
      kMapMinRow, kMapMaxRow);
  P.map = Box(inner.l, y, inner.w(), MapHeight(P.map_row));
  y = P.map.b + kGap;

  // Everything below the map, given whether the legend is shown and which
  // sections are open.
  std::vector<bool> open(sections.size());
  for (size_t i = 0; i < sections.size(); ++i) open[i] = sections[i].want_open;
  auto below = [&](bool legend) {
    float h = (legend ? kLegendHeight + 2.f : 0.f) + 6.f + kHeaderHeight + 4.f +
              (warning ? kWarningHeight + 2.f : 0.f);
    for (size_t i = 0; i < sections.size(); ++i) {
      h += SectionHeight(sections[i], open[i]) + kSectionGap;
    }
    return h;
  };
  const float avail = inner.b - y;
  P.show_legend = true;
  P.show_scope = true;
  // The scope needs at least this; it's the group's main readout, so it
  // stays while sections can fold to make room.
  auto scope_min = [&] { return P.show_scope ? kScopeHeight + 8.f : 0.f; };
  // Fold the lowest-priority open sections until everything fits, then
  // drop the legend, then the scope.
  while (below(P.show_legend) + scope_min() > avail) {
    int fold = -1;
    for (size_t i = 0; i < sections.size(); ++i) {
      if (open[i] && (fold < 0 || sections[i].fold_priority <
                                      sections[fold].fold_priority)) {
        fold = static_cast<int>(i);
      }
    }
    if (fold >= 0) {
      open[fold] = false;
    } else if (P.show_legend) {
      P.show_legend = false;
    } else if (P.show_scope) {
      P.show_scope = false;
    } else {
      break;
    }
  }
  const float spare = avail - below(P.show_legend);

  if (P.show_legend) {
    P.legend = Box(inner.l, y, inner.w(), kLegendHeight);
    y += kLegendHeight + 2.f;
  }
  P.divider = Box(inner.l, y + 2.f, inner.w(), 1.f);
  y += 6.f;
  P.header = Box(inner.l, y, inner.w(), kHeaderHeight);
  y += kHeaderHeight + 4.f;
  P.show_warning = warning;
  if (warning) {
    P.warning = Box(inner.l, y, inner.w(), kWarningHeight);
    y += kWarningHeight + 2.f;
  }
  if (P.show_scope) {
    // The scope takes what room is spare, within reason.
    const float scope_h = std::min(spare - 8.f, kMaxScopeHeight);
    P.scope = Box(inner.l, y, inner.w(), scope_h);
    y += scope_h + 6.f;
  }

  const float sections_t = y;
  float sections_b = y;
  for (size_t i = 0; i < sections.size(); ++i) {
    sections_b += SectionHeight(sections[i], open[i]) + kSectionGap;
  }
  P.show_sc_meter = sc_meter && sections_b - sections_t >= 60.f;
  const float fields_r =
      P.show_sc_meter ? inner.r - kScMeterWidth - kGap : inner.r;
  if (P.show_sc_meter) {
    P.sc_meter = {inner.r - kScMeterWidth, sections_t, inner.r,
                  sections_b - kSectionGap};
  }
  const float col_w = (fields_r - inner.l - kPad) / 2.f;
  for (size_t i = 0; i < sections.size(); ++i) {
    PanelLayout::Section s;
    s.open = open[i];
    s.header = Box(inner.l, y, fields_r - inner.l, kSectionHeader);
    y += kSectionHeader + 3.f;
    if (s.open) {
      for (int row = 0; row < sections[i].rows; ++row) {
        for (int col = 0; col < 2; ++col) {
          s.cells.push_back(
              Box(inner.l + col * (col_w + kPad), y, col_w, kFieldHeight));
        }
        y += kFieldPitch;
      }
    }
    y += kSectionGap;
    P.sections.push_back(std::move(s));
  }
  return P;
}

}  // namespace eeq
