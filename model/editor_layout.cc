#include "model/editor_layout.h"

#include <algorithm>

namespace eeq {
namespace {

constexpr float kPad = 8.f;
constexpr float kGap = 4.f;
constexpr float kFooterHeight = 48.f;
constexpr float kDrawerTabWidth = 22.f;

// Panel rows.
constexpr float kTitleHeight = 12.f;
constexpr float kLegendHeight = 11.f;
constexpr float kHeaderHeight = 18.f;
constexpr float kWarningHeight = 12.f;
constexpr float kScopeHeight = 54.f;
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

}  // namespace

LayoutTier TierFor(float width, float height) {
  const int w = width >= 900.f   ? 3
                : width >= 640.f ? 2
                : width >= 460.f ? 1
                                 : 0;
  const int h = height >= 520.f   ? 3
                : height >= 380.f ? 2
                : height >= 280.f ? 1
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
    L.graph = {kGap, kGap, width - kGap, height - kGap};
    return L;
  }

  L.show_top = true;
  const float top_h = full ? 34.f : compact ? 30.f : 26.f;
  const float body_t = kPad + top_h + kPad;
  const float body_b = height - kPad;
  L.ribbon_power = tier >= 2;
  L.preset_undo = tier >= 2;

  // Left column: graph, strip and footer.  Right: the dynamics panel or the
  // drawer tab.
  Rect left;
  if (full) {
    const float panel_w = width >= 1100.f ? 330.f : 290.f;
    left = {kPad, body_t, width - kPad - panel_w - kPad, body_b};
    L.ribbon = {kPad, kPad, left.r, kPad + top_h};
    L.preset = {left.r + kPad, kPad, width - kPad, kPad + top_h};
    L.show_panel = true;
    L.panel = {L.preset.l, body_t, L.preset.r, body_b};
  } else if (compact) {
    left = {kPad, body_t, width - kPad - kDrawerTabWidth - 6.f, body_b};
    const float preset_w = 190.f;
    L.ribbon = {kPad, kPad, width - kPad - preset_w - kGap, kPad + top_h};
    L.preset = {L.ribbon.r + kGap, kPad, width - kPad, kPad + top_h};
    L.show_drawer_tab = true;
    L.drawer_tab = {left.r + 6.f, body_t, left.r + 6.f + kDrawerTabWidth,
                    body_b};
    L.show_panel = drawer_open;
    L.panel_overlay = true;
    const float panel_w = std::min(300.f, width - 80.f);
    L.panel = {L.drawer_tab.l - 6.f - panel_w, body_t, L.drawer_tab.l - 6.f,
               body_b};
  } else {
    left = {kPad, body_t, width - kPad, body_b};
    const float preset_w = 150.f;
    L.ribbon = {kPad, kPad, width - kPad - preset_w - kGap, kPad + top_h};
    L.preset = {L.ribbon.r + kGap, kPad, width - kPad, kPad + top_h};
  }

  float graph_b = left.b;
  if (full) {
    L.show_footer = true;
    L.footer = {left.l, left.b - kFooterHeight, left.r, left.b};
    graph_b = L.footer.t - kPad;
  }
  if (tier >= 2) {
    L.show_strip = true;
    const float strip_h = full ? (width >= 1100.f ? 132.f : 118.f) : 92.f;
    const Rect strip = {left.l, graph_b - strip_h, left.r, graph_b};
    L.strip = ComputeStrip(strip, full);
    graph_b = strip.t - kPad;
  }
  Rect graph_area = {left.l, left.t, left.r, graph_b};
  if (tier >= 2) {
    L.show_meters = true;
    const float meter_w = full ? 30.f : 22.f;
    const float fader_w = full ? 26.f : 20.f;
    L.in_meter = {graph_area.l, graph_area.t, graph_area.l + meter_w,
                  graph_area.b};
    L.fader = {graph_area.r - fader_w, graph_area.t, graph_area.r,
               graph_area.b};
    L.out_meter = {L.fader.l - kGap - meter_w, graph_area.t, L.fader.l - kGap,
                   graph_area.b};
    graph_area.l = L.in_meter.r + kGap;
    graph_area.r = L.out_meter.l - kGap;
  }
  L.graph = graph_area;
  return L;
}

float MapHeight(float block) { return kMapHeaderHeight + 6.f + 8.f * block; }

PanelLayout ComputePanelLayout(const Rect& panel,
                               const std::vector<PanelSection>& sections,
                               bool warning, bool sc_meter) {
  PanelLayout P;
  const Rect inner = {panel.l + kPad, panel.t + kPad, panel.r - kPad,
                      panel.b - kPad};
  float y = inner.t;
  P.title = Box(inner.l, y, inner.w(), kTitleHeight);
  y += kTitleHeight + 2.f;
  P.map_block = std::clamp((inner.h() * 0.5f - kMapHeaderHeight - 6.f) / 8.f,
                           kMapMinBlock, kMapMaxBlock);
  P.map = Box(inner.l, y, inner.w(), MapHeight(P.map_block));
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
  // Fold the lowest-priority open sections until everything fits, then
  // drop the legend.
  while (below(P.show_legend) > avail) {
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
  P.show_scope = spare >= kScopeHeight + 8.f;
  if (P.show_scope) {
    P.scope = Box(inner.l, y, inner.w(), kScopeHeight);
    y += kScopeHeight + 6.f;
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
