#ifndef EVERYTHINGEQ_PLUGIN_UI_ROUTING_MAP_H_
#define EVERYTHINGEQ_PLUGIN_UI_ROUTING_MAP_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>

#include "IControl.h"
#include "model/editor_layout.h"
#include "model/param_table.h"
#include "model/slot_mapping.h"
#include "plugin/ui/editor_model.h"
#include "plugin/ui/theme.h"

namespace eeq {

// The dynamics routing, drawn like a subway map (ABOUT.md section 4).
// Three source lines on top (Input, Band Sum, Sidechain) feed one vertical
// track per group.  Bands are rows down the left, each with a Dyn A port
// above its Dyn B port; an assignment is a line in the band's color leaving
// its group's track with a rounded corner and ending in an arrowhead at the
// port (filled = A, hollow = B), labeled with its target and range.
//
// Click an empty crossing to connect that port to the group (moving it if
// it was connected elsewhere), a joined one to select the band and group,
// Option-click a joined one to disconnect it.  Click a source crossing to
// change the group's input, a group head to select it, a band to select it.
class RoutingMap final : public iplug::igraphics::IControl {
 public:
  struct Callbacks {
    std::function<void(int)> select_group;
    std::function<int()> selected_group;
    std::function<bool(int)> group_active;
    std::function<float(int)> group_gain_db;
  };

  RoutingMap(const iplug::igraphics::IRECT& bounds, EditorModel& model,
             Callbacks callbacks)
      : IControl(bounds), model_(model), cb_(std::move(callbacks)) {
    SetTooltip(
        "Routing. Click an empty crossing to connect a band's Dyn A (upper) "
        "or Dyn B (lower) port to a group; Option-click a connection to "
        "remove it. Click a source line crossing to choose what the group "
        "listens to.");
  }

  // The panel color behind the map, used to break lines where they cross.
  void SetBackground(const iplug::igraphics::IColor& c) { background_ = c; }
  void SetSidechainConnected(bool connected) { sc_connected_ = connected; }

  int hover_band() const { return hover_.band; }
  int hover_group() const { return hover_.group; }

  void Draw(iplug::igraphics::IGraphics& g) override;
  bool IsDirty() override { return true; }  // Live D and gain.

  void OnMouseDown(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override;
  void OnMouseOver(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override {
    IControl::OnMouseOver(x, y, mod);
    hover_ = HitTest(x, y);
  }
  void OnMouseOut() override {
    IControl::OnMouseOut();
    hover_ = {};
  }

 private:
  enum class Part { kNone, kBand, kHead, kCrossing, kSource };
  struct Hit {
    Part part = Part::kNone;
    int band = -1, slot = -1, group = -1, source = -1;
  };
  struct Port {
    bool on = false;  // Subscribed (nonzero range) and the band isn't Off.
    int group = 0;
    DynTarget target = DynTarget::kGain;
    double range = 0.0;
  };

  // Geometry (see kMapHeaderHeight).
  static constexpr float kBusY[3] = {6.f, 19.f, 32.f};
  static constexpr float kHeadTop = 43.f;
  static constexpr float kHeadBottom = 81.f;
  static constexpr float kRowsTop = 87.f;
  static constexpr float kLabelW = 74.f;
  static constexpr float kPortDx = kLabelW + 16.f;
  static constexpr float kCorner = 6.f;

  float Block() const {
    return std::max(kMapMinBlock, (mRECT.H() - kRowsTop) / kNumBands);
  }
  float PortX() const { return mRECT.L + kPortDx; }
  float TrackX(int group) const {
    const float x0 = mRECT.L + kPortDx + 30.f;
    const float spacing = (mRECT.R - 26.f - x0) / (kNumGroups - 1);
    return x0 + group * spacing;
  }
  float RowY(int band) const { return mRECT.T + kRowsTop + band * Block(); }
  float PortY(int band, int slot) const {
    return RowY(band) + Block() * (slot == 0 ? 0.27f : 0.69f);
  }
  float BusY(int source) const { return mRECT.T + kBusY[source]; }
  float HeadWidth() const {
    return std::min(TrackX(1) - TrackX(0) - 4.f, 56.f);
  }

  Port PortOf(const ParamValues& v, int band, int slot) const {
    Port p;
    const bool a = slot == 0;
    const auto type = static_cast<BandType>(
        std::lround(v[BandParamId(band, BandField::kType)]));
    p.group = static_cast<int>(std::lround(v[BandParamId(
        band, a ? BandField::kDynGroup : BandField::kDynBGroup)]));
    p.target = static_cast<DynTarget>(std::lround(v[BandParamId(
        band, a ? BandField::kDynTarget : BandField::kDynBTarget)]));
    p.range = SlotToPhysical(
        DynRangeMapping(p.target),
        v[BandParamId(band, a ? BandField::kDynRange : BandField::kDynBRange)]);
    p.on = type != BandType::kOff && p.range != 0.0;
    return p;
  }

  static std::string RangeLabel(const Port& p) {
    static const char kLetters[] = {'G', 'F', 'M'};
    const double r = std::abs(p.range);
    char num[16];
    if (r >= 10.0 || std::abs(r - std::round(r)) < 0.05) {
      std::snprintf(num, sizeof(num), "%d", static_cast<int>(std::lround(r)));
    } else {
      std::snprintf(num, sizeof(num), "%.1f", r);
    }
    std::string s(1, kLetters[std::clamp(static_cast<int>(p.target), 0, 2)]);
    s += p.range < 0.0 ? "\xe2\x88\x92" : "+";
    return s + num;
  }

  Hit HitTest(float x, float y) const;
  bool BandLit(const ParamValues& v, int band) const;
  bool GroupLit(const ParamValues& v, int group) const;

  EditorModel& model_;
  Callbacks cb_;
  iplug::igraphics::IColor background_ = theme::kPanel;
  bool sc_connected_ = false;
  Hit hover_;
};

inline RoutingMap::Hit RoutingMap::HitTest(float x, float y) const {
  Hit h;
  if (!mRECT.Contains(x, y)) return h;
  const float hw = HeadWidth();
  for (int gi = 0; gi < kNumGroups; ++gi) {
    const float tx = TrackX(gi);
    if (y >= mRECT.T + kHeadTop && y < mRECT.T + kHeadBottom &&
        std::abs(x - tx) <= hw / 2.f) {
      h.part = Part::kHead;
      h.group = gi;
      return h;
    }
    for (int s = 0; s < 3; ++s) {
      if (std::abs(y - BusY(s)) <= 6.f && std::abs(x - tx) <= 9.f) {
        h.part = Part::kSource;
        h.group = gi;
        h.source = s;
        return h;
      }
    }
  }
  if (y < mRECT.T + kRowsTop) return h;
  const int band = std::clamp(
      static_cast<int>((y - mRECT.T - kRowsTop) / Block()), 0, kNumBands - 1);
  h.band = band;
  if (x < PortX() - 6.f) {
    h.part = Part::kBand;
    return h;
  }
  const int slot = y < RowY(band) + Block() * 0.48f ? 0 : 1;
  const float spacing = TrackX(1) - TrackX(0);
  for (int gi = 0; gi < kNumGroups; ++gi) {
    const float tx = TrackX(gi);
    if (x >= tx - 9.f && x < tx - 9.f + std::min(spacing, 38.f)) {
      h.part = Part::kCrossing;
      h.slot = slot;
      h.group = gi;
      return h;
    }
  }
  h.part = Part::kBand;
  return h;
}

inline bool RoutingMap::BandLit(const ParamValues& v, int band) const {
  if (hover_.band == band) return true;
  if (hover_.group < 0 || hover_.part == Part::kCrossing) return false;
  for (int s = 0; s < 2; ++s) {
    const Port p = PortOf(v, band, s);
    if (p.on && p.group == hover_.group) return true;
  }
  return false;
}

inline bool RoutingMap::GroupLit(const ParamValues& v, int group) const {
  if (hover_.group == group) return true;
  if (hover_.band < 0 || hover_.part == Part::kCrossing) return false;
  for (int s = 0; s < 2; ++s) {
    const Port p = PortOf(v, hover_.band, s);
    if (p.on && p.group == group) return true;
  }
  return false;
}

inline void RoutingMap::Draw(iplug::igraphics::IGraphics& g) {
  namespace ig = iplug::igraphics;
  const ParamValues v = model_.Values();
  const int sel_band = model_.SelectedBand();
  const int sel_group = cb_.selected_group ? cb_.selected_group() : 0;
  const GroupControls d = model_.LiveControls();
  const float block = Block();
  const float px = PortX();
  const bool any_hover = hover_.band >= 0 || hover_.group >= 0;
  const ig::IColor lit_row = theme::kPanelRaised.WithOpacity(0.7f);

  std::array<int, kNumGroups> input{};
  for (int gi = 0; gi < kNumGroups; ++gi) {
    input[gi] = std::clamp(
        static_cast<int>(std::lround(v[GroupParamId(gi, GroupField::kInput)])),
        0, 2);
  }

  // Band rows.
  for (int b = 0; b < kNumBands; ++b) {
    const ig::IRECT row(mRECT.L, RowY(b), mRECT.R, RowY(b) + block - 2.f);
    if (b == sel_band) {
      g.FillRoundRect(theme::kPanelRaised, row, 3.f);
    } else if (BandLit(v, b)) {
      g.FillRoundRect(lit_row, row, 3.f);
    }
  }

  // Source lines and their labels.
  static const char* const kSources[] = {"INPUT", "BAND SUM", "SIDECHAIN"};
  const float bus_l = mRECT.L + 58.f, bus_r = TrackX(kNumGroups - 1) + 14.f;
  const ig::IColor sc_color =
      sc_connected_ ? theme::kTextDim : theme::kCaution.WithOpacity(0.6f);
  for (int s = 0; s < 3; ++s) {
    const ig::IColor c = s == 2 ? sc_color : theme::kTextDim;
    const float y = BusY(s);
    if (s == 2 && !sc_connected_) {
      g.DrawDottedLine(c, bus_l, y, bus_r, y, nullptr, 2.5f, 4.f);
    } else {
      g.DrawLine(c, bus_l, y, bus_r, y, nullptr, 2.5f);
    }
    g.DrawText(theme::Text(8.f, c),
               s == 2 && !sc_connected_ ? "SC \xc2\xb7 NONE" : kSources[s],
               ig::IRECT(mRECT.L + 2.f, y - 5.f, bus_l - 2.f, y + 5.f));
  }

  // Each group's feed drops from its source into its head, filleted on both
  // sides; it breaks the lines it crosses.
  for (int gi = 0; gi < kNumGroups; ++gi) {
    const float x = TrackX(gi), y = BusY(input[gi]);
    const float head_t = mRECT.T + kHeadTop;
    const bool sc = input[gi] == 2;
    const ig::IColor c = sc ? sc_color : theme::kTextDim;
    g.DrawLine(background_, x, y + kCorner, x, head_t, nullptr, 7.f);
    for (const float side : {-1.f, 1.f}) {
      g.PathClear();
      g.PathMoveTo(x + side * kCorner, y);
      g.PathQuadraticBezierTo(x, y, x, y + kCorner);
      g.PathStroke(c, 2.5f);
    }
    if (sc && !sc_connected_) {
      g.DrawDottedLine(c, x, y + kCorner, x, head_t, nullptr, 2.5f, 4.f);
    } else {
      g.DrawLine(c, x, y + kCorner, x, head_t, nullptr, 2.5f);
    }
  }

  // Ports.
  std::array<std::array<Port, 2>, kNumBands> ports{};
  for (int b = 0; b < kNumBands; ++b) {
    for (int s = 0; s < 2; ++s) ports[b][s] = PortOf(v, b, s);
  }

  // Group tracks, from the head down to the lowest band that joins them.
  for (int gi = 0; gi < kNumGroups; ++gi) {
    float bottom = mRECT.T + kHeadBottom + 8.f;
    for (int b = 0; b < kNumBands; ++b) {
      for (int s = 0; s < 2; ++s) {
        if (ports[b][s].on && ports[b][s].group == gi) {
          bottom = std::max(bottom, PortY(b, s) - kCorner);
        }
      }
    }
    const ig::IColor c = gi == sel_group   ? theme::kAccent
                         : GroupLit(v, gi) ? theme::kText
                                           : theme::kGridStrong;
    const float x = TrackX(gi);
    g.DrawLine(c, x, mRECT.T + kHeadBottom, x, bottom, nullptr, 3.f);
  }

  // Group heads: shape, name, detect mode, live D and the applied gain.
  static const char* const kDetect[] = {"Level", "Vowel", "Pitch"};
  const float hw = HeadWidth();
  for (int gi = 0; gi < kNumGroups; ++gi) {
    const float x = TrackX(gi);
    const ig::IRECT head(x - hw / 2.f, mRECT.T + kHeadTop, x + hw / 2.f,
                         mRECT.T + kHeadBottom);
    const bool active = cb_.group_active && cb_.group_active(gi);
    const bool lit = GroupLit(v, gi);
    const float a = active ? 1.f : 0.55f;
    g.FillRoundRect(gi == sel_group ? theme::kPanelRaised : theme::kPanel, head,
                    3.f);
    g.DrawRoundRect(gi == sel_group ? theme::kAccent
                    : lit           ? theme::kText
                                    : theme::kBorder,
                    head, 3.f);
    const bool warn = input[gi] == 2 && !sc_connected_;
    const std::string name = "G" + std::to_string(gi + 1);
    const float title_w = 9.f + 3.f + 13.f + (warn ? 12.f : 0.f);
    float tx = head.MW() - title_w / 2.f;
    const float ty = head.T + 8.f;
    theme::FillGroupGlyph(g, gi, tx + 4.5f, ty, 4.f,
                          theme::kText.WithOpacity(a));
    tx += 12.f;
    g.DrawText(theme::Text(11.f, theme::kText.WithOpacity(a)), name.c_str(),
               ig::IRECT(tx, ty - 6.f, tx + 16.f, ty + 6.f));
    if (warn) {
      tx += 16.f;
      g.DrawTriangle(theme::kCaution, tx + 4.f, ty - 4.f, tx + 8.5f, ty + 4.f,
                     tx - 0.5f, ty + 4.f);
    }
    const int detect = std::clamp(
        static_cast<int>(std::lround(v[GroupParamId(gi, GroupField::kDetect)])),
        0, 2);
    g.DrawText(theme::Text(9.f, theme::kTextDim, ig::EAlign::Center),
               kDetect[detect],
               ig::IRECT(head.L, head.T + 15.f, head.R, head.T + 26.f));
    const ig::IRECT bar = ig::IRECT(head.L + hw * 0.15f, head.B - 8.f,
                                    head.R - hw * 0.15f, head.B - 5.f);
    g.FillRoundRect(theme::kPanelRaised, bar, 1.f);
    g.FillRect(active ? theme::kAccent : theme::kTextDim.WithOpacity(0.5f),
               bar.GetFromLeft(
                   static_cast<float>(std::clamp(d[gi], 0.0, 1.0) * bar.W())));
    if (active && cb_.group_gain_db) {
      const float u = std::clamp(cb_.group_gain_db(gi) / 24.f, -1.f, 1.f);
      const float mid = bar.MW(), gx = mid + u * 0.5f * bar.W();
      g.FillRect(theme::kGain, ig::IRECT(std::min(mid, gx), bar.B + 1.f,
                                         std::max(mid, gx), bar.B + 3.f));
    }
  }

  // Band lines, arrowheads and labels.
  for (int b = 0; b < kNumBands; ++b) {
    const auto type =
        static_cast<BandType>(std::lround(v[BandParamId(b, BandField::kType)]));
    const bool off = type == BandType::kOff;
    const bool bypassed = v[BandParamId(b, BandField::kBypass)] >= 0.5;
    const bool lit = BandLit(v, b) || b == sel_band;
    float a = off ? 0.45f : 1.f;
    if (any_hover && !BandLit(v, b)) a *= 0.3f;
    if (bypassed) a *= 0.55f;
    const ig::IColor color = theme::BandColor(b).WithOpacity(a);
    const ig::IColor halo = b == sel_band   ? theme::kPanelRaised
                            : BandLit(v, b) ? lit_row
                                            : background_;
    for (int s = 0; s < 2; ++s) {
      const Port& p = ports[b][s];
      const float y = PortY(b, s);
      if (!p.on) {
        g.DrawCircle(theme::kGridStrong.WithOpacity(off ? 0.5f : 1.f), px, y,
                     2.5f);
        continue;
      }
      const float x = TrackX(p.group);
      const float w = lit ? 2.6f : 2.f;
      g.DrawLine(halo, px + 3.f, y, x - kCorner, y, nullptr, 7.f);
      if (bypassed) {
        g.DrawDottedLine(color, px + 3.f, y, x - kCorner, y, nullptr, w, 3.f);
      } else {
        g.DrawLine(color, px + 3.f, y, x - kCorner, y, nullptr, w);
      }
      g.PathClear();
      g.PathMoveTo(x - kCorner, y);
      g.PathQuadraticBezierTo(x, y, x, y - kCorner);
      g.PathStroke(color, w);
      // Arrowhead where the group's signal enters the port.
      if (s == 0) {
        g.FillTriangle(color, px - 3.5f, y, px + 4.f, y - 4.f, px + 4.f,
                       y + 4.f);
      } else {
        g.FillTriangle(halo, px - 3.5f, y, px + 4.f, y - 4.f, px + 4.f,
                       y + 4.f);
        g.DrawTriangle(color, px - 3.5f, y, px + 4.f, y - 4.f, px + 4.f,
                       y + 4.f, nullptr, 1.4f);
      }
      g.DrawText(theme::Text(9.f, color), RangeLabel(p).c_str(),
                 ig::IRECT(x + 5.f, y - 6.f, x + 40.f, y + 5.f));
    }

    // Band label and A/B port letters.
    const ig::IRECT row(mRECT.L, RowY(b), mRECT.L + kLabelW,
                        RowY(b) + block - 2.f);
    const float la = off || bypassed ? 0.5f : 1.f;
    g.FillRoundRect(
        off ? theme::kBorder : theme::BandColor(b).WithOpacity(la),
        ig::IRECT(row.L + 5.f, row.MH() - 7.f, row.L + 9.f, row.MH() + 7.f),
        2.f);
    const std::string n = std::to_string(b + 1);
    g.DrawText(theme::Text(10.f, theme::kTextDim.WithOpacity(la)), n.c_str(),
               ig::IRECT(row.L + 13.f, row.T, row.L + 21.f, row.B));
    const int t = std::clamp(
        static_cast<int>(type), 0,
        static_cast<int>(
            ParamTable()[BandParamId(b, BandField::kType)].enum_labels.size()) -
            1);
    const std::string& tname =
        ParamTable()[BandParamId(b, BandField::kType)].enum_labels[t];
    const ig::IRECT name_rect(row.L + 23.f, row.T, row.R, row.B);
    g.DrawText(theme::Text(10.f, theme::kText.WithOpacity(la)),
               off ? "Off" : tname.c_str(), name_rect);
    if (bypassed && !off) {
      g.DrawLine(theme::kTextDim, name_rect.L, row.MH(),
                 std::min(name_rect.R, name_rect.L + 44.f), row.MH());
    }
    const ig::IColor letters = off ? theme::kBorder : theme::kTextDim;
    g.DrawText(
        theme::Text(8.f, letters), "A",
        ig::IRECT(px - 14.f, PortY(b, 0) - 5.f, px - 7.f, PortY(b, 0) + 4.f));
    g.DrawText(
        theme::Text(8.f, letters), "B",
        ig::IRECT(px - 14.f, PortY(b, 1) - 5.f, px - 7.f, PortY(b, 1) + 4.f));
  }

  // "+" on a hovered empty crossing; a ring on a hovered source crossing.
  if (hover_.part == Part::kCrossing) {
    const auto type = static_cast<BandType>(
        std::lround(v[BandParamId(hover_.band, BandField::kType)]));
    const Port& p = ports[hover_.band][hover_.slot];
    const float x = TrackX(hover_.group), y = PortY(hover_.band, hover_.slot);
    const ig::IRECT cell(x - 9.f, y - 5.5f,
                         x - 9.f + std::min(TrackX(1) - TrackX(0), 38.f),
                         y + 5.5f);
    g.DrawRoundRect(theme::kText.WithOpacity(0.35f), cell, 2.f);
    if (type != BandType::kOff && !(p.on && p.group == hover_.group)) {
      g.DrawText(theme::Text(11.f, theme::kText), "+",
                 cell.GetReducedFromLeft(3.f));
    }
  } else if (hover_.part == Part::kSource) {
    g.DrawCircle(theme::kText.WithOpacity(0.6f), TrackX(hover_.group),
                 BusY(hover_.source), 5.f);
  }
}

inline void RoutingMap::OnMouseDown(float x, float y,
                                    const iplug::igraphics::IMouseMod& mod) {
  const Hit h = HitTest(x, y);
  const ParamValues v = model_.Values();
  switch (h.part) {
    case Part::kNone:
      return;
    case Part::kBand:
      model_.SelectBand(h.band);
      break;
    case Part::kHead:
      if (cb_.select_group) cb_.select_group(h.group);
      break;
    case Part::kSource: {
      const int id = GroupParamId(h.group, GroupField::kInput);
      model_.SetValueGesture(id, h.source / 2.0);
      if (cb_.select_group) cb_.select_group(h.group);
      break;
    }
    case Part::kCrossing: {
      const auto type = static_cast<BandType>(
          std::lround(v[BandParamId(h.band, BandField::kType)]));
      if (type == BandType::kOff) break;
      const bool a = h.slot == 0;
      const Port p = PortOf(v, h.band, h.slot);
      const int group_id =
          BandParamId(h.band, a ? BandField::kDynGroup : BandField::kDynBGroup);
      const int target_id = BandParamId(
          h.band, a ? BandField::kDynTarget : BandField::kDynBTarget);
      const int range_id =
          BandParamId(h.band, a ? BandField::kDynRange : BandField::kDynBRange);
      const bool here = p.on && p.group == h.group;
      if (here && mod.A) {
        model_.SetValueGesture(range_id, kSlotDefault);  // Range 0.
      } else if (!here) {
        // Connect, moving the port if it was on another group.  A new
        // connection starts as a -6 dB gain.
        const bool fresh = !p.on;
        model_.BeginEdit(group_id);
        if (fresh) {
          model_.BeginEdit(target_id);
          model_.BeginEdit(range_id);
        }
        model_.SetEdit(group_id, h.group / static_cast<double>(kNumGroups - 1));
        if (fresh) {
          model_.SetEdit(target_id, 0.0);
          model_.SetEdit(
              range_id,
              SlotFromPhysical(DynRangeMapping(DynTarget::kGain), -6.0));
          model_.EndEdit(range_id);
          model_.EndEdit(target_id);
        }
        model_.EndEdit(group_id);
      }
      model_.SelectBand(h.band);
      if (cb_.select_group) cb_.select_group(h.group);
      break;
    }
  }
  SetDirty(false);
}

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_ROUTING_MAP_H_
