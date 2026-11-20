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
#include "plugin/ui/band_ribbon.h"
#include "plugin/ui/editor_model.h"
#include "plugin/ui/theme.h"

namespace eeq {

// The dynamics routing, drawn like a subway map (ABOUT.md section 4).
// Bands are columns, left to right like the ribbon above the graph; groups
// are rows.  Each source (Input, Band Sum, Sidechain) is one line down from
// its label at the top left; each group's feed branches off it and turns
// right into the group's head.  An assignment is a line in the band's color
// that runs right from the head along the group's row, then turns down with
// a rounded corner and drops to the band's port at the bottom, ending in an
// arrowhead (filled = Dyn A, the left port; hollow = Dyn B, the right one).
// A group's lines share its row, the longer ones underneath, so the stretch
// before each turn shows that band's color.  Under the ports: each band's
// type icon.
//
// Every crossing of a group row and a port column has a faint dot.  Click
// an empty crossing to connect that port to the group (Dyn On, Range 0; a
// port connected elsewhere moves, keeping its range), and a joined one to
// disconnect it; either selects the band and group.  Click a source crossing to
// change the group's input, a group head to select it, a band's foot to
// select it.
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
        "Routing. Click an empty crossing to connect a band's Dyn A (left) "
        "or Dyn B (right) port to a group; click a connection to remove "
        "it. Click where a group's feed meets a source line (IN = "
        "input, SUM = band sum, SC = sidechain) to choose what it listens "
        "to.");
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
  static_assert(kNumMapGroups == kNumGroups);
  enum class Part { kNone, kBand, kHead, kCrossing, kSource };
  struct Hit {
    Part part = Part::kNone;
    int band = -1, slot = -1, group = -1, source = -1;
  };
  struct Port {
    bool on = false;  // Dyn On, and the band isn't Off.
    int group = 0;
    DynTarget target = DynTarget::kGain;
    double range = 0.0;
  };

  // Geometry (see kMapTopHeight and kMapBottomHeight).
  static constexpr float kBusX[3] = {9.f, 33.f, 57.f};
  static constexpr float kHeadL = 65.f;
  static constexpr float kHeadW = 48.f;
  static constexpr float kBandsL = kHeadL + kHeadW + 10.f;
  static constexpr float kCorner = 6.f;
  static constexpr float kArrow = 7.5f;

  float Row() const {
    return std::max(kMapMinRow, (mRECT.H() - kMapTopHeight - kMapBottomHeight) /
                                    kNumGroups);
  }
  float RowsTop() const { return mRECT.T + kMapTopHeight; }
  float RowsBottom() const { return RowsTop() + kNumGroups * Row(); }
  float GroupY(int group) const { return RowsTop() + (group + 0.5f) * Row(); }
  float BusX(int source) const { return mRECT.L + kBusX[source]; }
  iplug::igraphics::IRECT Head(int group) const {
    const float t = RowsTop() + group * Row();
    return iplug::igraphics::IRECT(mRECT.L + kHeadL, t + 2.f,
                                   mRECT.L + kHeadL + kHeadW, t + Row() - 2.f);
  }
  float ColW() const {
    return std::max(0.f, (mRECT.R - 2.f - mRECT.L - kBandsL) / kNumBands);
  }
  float ColL(int band) const { return mRECT.L + kBandsL + band * ColW(); }
  float PortX(int band, int slot) const {
    return ColL(band) + ColW() * (slot == 0 ? 0.3f : 0.7f);
  }
  float PortY() const { return RowsBottom() + 9.f; }  // Arrow tips.
  iplug::igraphics::IRECT IconRect(int band) const {
    return iplug::igraphics::IRECT(ColL(band) + 2.f, PortY() + 4.f,
                                   ColL(band) + ColW() - 2.f, mRECT.B - 1.f);
  }

  Port PortOf(const ParamValues& v, int band, int slot) const {
    Port p;
    const bool a = slot == 0;
    p.group = static_cast<int>(std::lround(v[BandParamId(
        band, a ? BandField::kDynGroup : BandField::kDynBGroup)]));
    p.target = static_cast<DynTarget>(std::lround(v[BandParamId(
        band, a ? BandField::kDynTarget : BandField::kDynBTarget)]));
    p.range = SlotToPhysical(
        DynRangeMapping(p.target),
        v[BandParamId(band, a ? BandField::kDynRange : BandField::kDynBRange)]);
    p.on =
        v[BandParamId(band, a ? BandField::kDynOn : BandField::kDynBOn)] >= 0.5;
    return p;
  }

  // `top` over `bottom` at opacity `t`, opaque.
  static iplug::igraphics::IColor Over(const iplug::igraphics::IColor& top,
                                       const iplug::igraphics::IColor& bottom,
                                       float t) {
    auto mix = [t](int a, int b) {
      return static_cast<int>(std::lround(a * t + b * (1.f - t)));
    };
    return iplug::igraphics::IColor(255, mix(top.R, bottom.R),
                                    mix(top.G, bottom.G), mix(top.B, bottom.B));
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
  for (int gi = 0; gi < kNumGroups; ++gi) {
    if (Head(gi).Contains(x, y)) {
      h.part = Part::kHead;
      h.group = gi;
      return h;
    }
    for (int s = 0; s < 3; ++s) {
      if (std::abs(x - BusX(s)) <= 8.f &&
          std::abs(y - GroupY(gi)) <= Row() / 2.f) {
        h.part = Part::kSource;
        h.group = gi;
        h.source = s;
        return h;
      }
    }
  }
  const float cw = ColW();
  if (x < mRECT.L + kBandsL || cw <= 0.f) return h;
  const int band = std::clamp(static_cast<int>((x - mRECT.L - kBandsL) / cw), 0,
                              kNumBands - 1);
  h.band = band;
  if (y >= RowsTop() && y < RowsBottom()) {
    h.part = Part::kCrossing;
    h.group = std::clamp(static_cast<int>((y - RowsTop()) / Row()), 0,
                         kNumGroups - 1);
    h.slot = x < ColL(band) + cw / 2.f ? 0 : 1;
    return h;
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
  const float cw = ColW();
  const float py = PortY();
  const bool any_hover = hover_.band >= 0 || hover_.group >= 0;
  const ig::IColor lit_col = Over(theme::kPanelRaised, background_, 0.6f);

  std::array<int, kNumGroups> input{};
  for (int gi = 0; gi < kNumGroups; ++gi) {
    input[gi] = std::clamp(
        static_cast<int>(std::lround(v[GroupParamId(gi, GroupField::kInput)])),
        0, 2);
  }
  std::array<std::array<Port, 2>, kNumBands> ports{};
  for (int b = 0; b < kNumBands; ++b) {
    for (int s = 0; s < 2; ++s) ports[b][s] = PortOf(v, b, s);
  }

  // Band columns: the selected one raised, hovered or related ones lit.
  for (int b = 0; b < kNumBands; ++b) {
    const ig::IRECT col(ColL(b) + 1.f, mRECT.T, ColL(b) + cw - 1.f, mRECT.B);
    if (b == sel_band) {
      g.FillRoundRect(theme::kPanelRaised, col, 3.f);
    } else if (BandLit(v, b)) {
      g.FillRoundRect(lit_col, col, 3.f);
    }
  }

  // Sources: one line down from each label as far as its lowest group, with
  // each group's feed branching off and turning right into the head.  Feeds
  // share their source's line, so unused groups' (dark) go underneath.
  static const char* const kSources[] = {"IN", "SUM", "SC"};
  const ig::IColor sc_color =
      sc_connected_ ? theme::kTextDim : theme::kCaution.WithOpacity(0.7f);
  const float bus_t = mRECT.T + 19.f;
  std::array<bool, kNumGroups> used{};  // Some band follows the group.
  for (int b = 0; b < kNumBands; ++b) {
    for (int s = 0; s < 2; ++s) {
      if (ports[b][s].on)
        used[std::clamp(ports[b][s].group, 0, kNumGroups - 1)] = true;
    }
  }
  auto feed_color = [&](int gi) {
    if (!used[gi]) return theme::kGridStrong;
    return input[gi] == 2 ? sc_color : theme::kTextDim;
  };
  for (int s = 0; s < 3; ++s) {
    const bool fed = std::find(input.begin(), input.end(), s) != input.end();
    const ig::IColor c = s == 2 ? sc_color : theme::kTextDim;
    g.DrawText(
        theme::Text(theme::kSmall, fed ? c : c.WithOpacity(0.5f),
                    ig::EAlign::Center),
        kSources[s],
        ig::IRECT(BusX(s) - 15.f, mRECT.T, BusX(s) + 15.f, mRECT.T + 17.f));
    // Faint dots where a group could listen to this source.
    for (int gi = 0; gi < kNumGroups; ++gi) {
      g.FillCircle(theme::kTextDim.WithOpacity(0.4f), BusX(s), GroupY(gi),
                   1.6f);
    }
  }
  // Drops first, so the feeds' runs break the other sources' lines.
  for (const bool lit : {false, true}) {
    for (int gi = 0; gi < kNumGroups; ++gi) {
      if (used[gi] != lit) continue;
      const ig::IColor c = feed_color(gi);
      const float x = BusX(input[gi]), y = GroupY(gi);
      if (input[gi] == 2 && !sc_connected_) {
        g.DrawDottedLine(c, x, bus_t, x, y - kCorner, nullptr, 2.f, 4.f);
      } else {
        g.DrawLine(c, x, bus_t, x, y - kCorner, nullptr, 2.f);
      }
    }
  }
  for (int gi = 0; gi < kNumGroups; ++gi) {
    const bool sc = input[gi] == 2;
    const ig::IColor c = feed_color(gi);
    const float x = BusX(input[gi]), y = GroupY(gi);
    const float head_l = Head(gi).L;
    g.DrawLine(background_, x + kCorner, y, head_l, y, nullptr, 4.5f);
    g.PathClear();
    g.PathMoveTo(x, y - kCorner);
    g.PathQuadraticBezierTo(x, y, x + kCorner, y);
    g.PathStroke(c, 2.f);
    if (sc && !sc_connected_) {
      g.DrawDottedLine(c, x + kCorner, y, head_l, y, nullptr, 2.f, 4.f);
    } else {
      g.DrawLine(c, x + kCorner, y, head_l, y, nullptr, 2.f);
    }
  }

  for (int gi = 0; gi < kNumGroups; ++gi) {
    if (!used[gi]) {
      // An unused group shows a dark stub (the selected one in the accent).
      const float l = Head(gi).R;
      g.DrawLine(gi == sel_group ? theme::kAccent : theme::kGridStrong, l,
                 GroupY(gi), l + 8.f, GroupY(gi), nullptr, 3.f);
    }
  }

  // Group heads: shape, name, detect mode (its initial), live D and the
  // applied gain.
  static const char* const kDetect[] = {"L", "V", "P", "S"};
  for (int gi = 0; gi < kNumGroups; ++gi) {
    const ig::IRECT head = Head(gi);
    const bool active = cb_.group_active && cb_.group_active(gi);
    const bool lit = GroupLit(v, gi);
    const float a = active ? 1.f : 0.55f;
    g.FillRoundRect(gi == sel_group ? theme::kPanelRaised : theme::kPanel, head,
                    3.f);
    g.DrawRoundRect(gi == sel_group ? theme::kAccent
                    : lit           ? theme::kText
                                    : theme::kBorder,
                    head, 3.f);
    const float ty = std::min(head.T + 9.f, head.MH() - 2.f);
    theme::FillGroupGlyph(g, gi, head.L + 8.f, ty, 3.5f,
                          theme::kText.WithOpacity(a));
    const std::string name = "G" + std::to_string(gi + 1);
    g.DrawText(theme::Text(theme::kName, theme::kText.WithOpacity(a)),
               name.c_str(),
               ig::IRECT(head.L + 14.f, ty - 7.f, head.L + 34.f, ty + 7.f));
    if (input[gi] == 2 && !sc_connected_) {
      const float tx = head.R - 12.f;
      g.DrawTriangle(theme::kCaution, tx + 4.f, ty - 4.f, tx + 8.5f, ty + 4.f,
                     tx - 0.5f, ty + 4.f);
    } else {
      const int detect =
          std::clamp(static_cast<int>(
                         std::lround(v[GroupParamId(gi, GroupField::kDetect)])),
                     0, 3);
      g.DrawText(theme::Text(theme::kValue, theme::kTextDim, ig::EAlign::Far),
                 kDetect[detect],
                 ig::IRECT(head.L + 30.f, ty - 7.f, head.R - 5.f, ty + 7.f));
    }
    const ig::IRECT bar(head.L + 5.f, head.B - 6.f, head.R - 5.f, head.B - 4.f);
    g.FillRect(theme::kPanelRaised, bar);
    g.FillRect(active ? theme::kAccent : theme::kTextDim.WithOpacity(0.5f),
               bar.GetFromLeft(
                   static_cast<float>(std::clamp(d[gi], 0.0, 1.0) * bar.W())));
    if (active && cb_.group_gain_db) {
      const float u = std::clamp(cb_.group_gain_db(gi) / 24.f, -1.f, 1.f);
      const float mid = bar.MW(), gx = mid + u * 0.5f * bar.W();
      g.FillRect(theme::kGain, ig::IRECT(std::min(mid, gx), bar.B + 0.5f,
                                         std::max(mid, gx), bar.B + 2.f));
    }
  }

  // Faint dots where the crossings are; band lines cover the joined ones.
  for (int b = 0; b < kNumBands; ++b) {
    for (int s = 0; s < 2; ++s) {
      for (int gi = 0; gi < kNumGroups; ++gi) {
        g.FillCircle(theme::kTextDim.WithOpacity(0.4f), PortX(b, s), GroupY(gi),
                     1.6f);
      }
    }
  }

  // Band lines, arrowheads and icons.
  auto band_color = [&](int b) {
    float a = 1.f;
    if (any_hover && !BandLit(v, b)) a *= 0.3f;
    // Bypassed bands' lines are darker (not dashed, which distracts).
    if (v[BandParamId(b, BandField::kBypass)] >= 0.5) a *= 0.4f;
    return theme::BandColor(b).WithOpacity(a);
  };
  auto line_width = [&](int b) {
    return BandLit(v, b) || b == sel_band ? 3.6f : 3.f;
  };
  // Rows first, so the drops below break the rows they cross; right to left,
  // so the shorter runs lie on top.
  for (int b = kNumBands - 1; b >= 0; --b) {
    for (int s = 1; s >= 0; --s) {
      const Port& p = ports[b][s];
      if (!p.on) continue;
      const float y = GroupY(p.group);
      g.DrawLine(band_color(b), Head(p.group).R, y, PortX(b, s) - kCorner, y,
                 nullptr, line_width(b));
    }
  }
  for (int b = 0; b < kNumBands; ++b) {
    const auto type =
        static_cast<BandType>(std::lround(v[BandParamId(b, BandField::kType)]));
    const bool bypassed = v[BandParamId(b, BandField::kBypass)] >= 0.5;
    const ig::IColor color = band_color(b);
    const ig::IColor halo = b == sel_band   ? theme::kPanelRaised
                            : BandLit(v, b) ? lit_col
                                            : background_;
    for (int s = 0; s < 2; ++s) {
      const Port& p = ports[b][s];
      const float x = PortX(b, s);
      if (!p.on) {
        g.DrawCircle(theme::kGridStrong, x, py - 3.f, 2.5f);
        continue;
      }
      const float y = GroupY(p.group);
      const float w = line_width(b);
      g.DrawLine(halo, x, y + kCorner, x, py - kArrow, nullptr, w + 2.5f);
      g.DrawLine(color, x, y + kCorner, x, py - kArrow, nullptr, w);
      g.PathClear();
      g.PathMoveTo(x - kCorner, y);
      g.PathQuadraticBezierTo(x, y, x, y + kCorner);
      g.PathStroke(color, w);
      // Arrowhead where the group's signal enters the port.
      if (s == 0) {
        g.FillTriangle(color, x, py, x - 4.f, py - kArrow, x + 4.f,
                       py - kArrow);
      } else {
        g.FillTriangle(halo, x, py, x - 4.f, py - kArrow, x + 4.f, py - kArrow);
        g.DrawTriangle(color, x, py, x - 4.f, py - kArrow, x + 4.f, py - kArrow,
                       nullptr, 1.4f);
      }
    }
    const ig::IRECT icon = IconRect(b);
    BandRibbon::DrawIcon(
        g, type, icon, theme::BandColor(b).WithOpacity(bypassed ? 0.4f : 1.f));
  }

  // "+" on a hovered empty crossing; a ring on a hovered source crossing.
  if (hover_.part == Part::kCrossing) {
    const Port& p = ports[hover_.band][hover_.slot];
    const float x = PortX(hover_.band, hover_.slot), y = GroupY(hover_.group);
    const float hw = std::min(cw * 0.25f, 7.f);
    const ig::IRECT cell(x - hw, y - 7.f, x + hw, y + 7.f);
    g.DrawRoundRect(theme::kText.WithOpacity(0.35f), cell, 2.f);
    if (!(p.on && p.group == hover_.group)) {
      g.DrawText(theme::Text(theme::kValue, theme::kText, ig::EAlign::Center),
                 "+", cell);
    }
  } else if (hover_.part == Part::kSource) {
    g.DrawCircle(theme::kText.WithOpacity(0.6f), BusX(hover_.source),
                 GroupY(hover_.group), 5.f);
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
      const bool a = h.slot == 0;
      const Port p = PortOf(v, h.band, h.slot);
      const int on_id =
          BandParamId(h.band, a ? BandField::kDynOn : BandField::kDynBOn);
      const int group_id =
          BandParamId(h.band, a ? BandField::kDynGroup : BandField::kDynBGroup);
      if (p.on && p.group == h.group) {
        model_.SetValueGesture(on_id, 0.0);  // Disconnect.
      } else if (p.on) {
        // Move the port to this group, keeping its target and range.
        model_.SetValueGesture(group_id,
                               h.group / static_cast<double>(kNumGroups - 1));
      } else {
        const auto edits = ConnectEdits(v, h.band, h.slot, h.group);
        for (const auto& [id, value] : edits) model_.BeginEdit(id);
        for (const auto& [id, value] : edits) model_.SetEdit(id, value);
        for (const auto& [id, value] : edits) model_.EndEdit(id);
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
