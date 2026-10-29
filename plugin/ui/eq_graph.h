#ifndef EVERYTHINGEQ_PLUGIN_UI_EQ_GRAPH_H_
#define EVERYTHINGEQ_PLUGIN_UI_EQ_GRAPH_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include "IControl.h"
#include "dsp/bands/band_design.h"
#include "dsp/bands/band_processor.h"
#include "dsp/bands/vowel.h"
#include "model/response_curves.h"
#include "model/slot_mapping.h"
#include "plugin/ui/editor_model.h"
#include "plugin/ui/theme.h"

namespace eeq {

// The EQ graph (ABOUT.md section 4): log-frequency grid, per-band curves, the
// composite curve, and draggable band nodes.  Dynamic bands also show a
// ghost composite at full dynamics (D = 1) and a live composite at the
// current group control values.
//
// Nodes: frequency bands drag frequency (horizontal, in Hz) and gain
// (vertical, in dB); filters sit on their curve at the cutoff; Vowel drags
// Morph and Formant Gain; Fletcher-Munson is pinned at 1 kHz and drags Phon;
// K-Weight has no node.  The wheel adjusts Q (Tract Scale for Vowel, Width for
// Band Shelf).  A selected Band Shelf also shows handles at its edges (the
// half-gain points); dragging one moves that edge and keeps the other fixed.
// Double-clicking empty space adds a Peak band in the first free slot.
//
// Option-dragging a node sets its Dyn Range, and Cmd-dragging (Ctrl on
// Windows) its Dyn B Range, along the target's axis: vertical for Gain,
// horizontal for Freq and Morph.  The selected band shows where each
// assignment takes it at full dynamics as a ring labeled A or B.
class EqGraph final : public iplug::igraphics::IControl {
 public:
  static constexpr double kMinHz = 20.0;
  static constexpr double kMaxHz = 20000.0;
  static constexpr double kRangeDb = 18.0;
  static constexpr float kNodeRadius = 6.f;
  static constexpr float kEdgeHalfW = 2.5f;
  static constexpr float kEdgeHalfH = 7.f;

  EqGraph(const iplug::igraphics::IRECT& bounds, EditorModel& model)
      : IControl(bounds), model_(model) {}

  void OnResize() override { freqs_.clear(); }

  enum class AnalyzerMode { kOff, kPre, kPost, kPreAndPost };

  // Whether any active band moves with group g, and the gain change (dB)
  // its bands apply right now: the largest deviation of their combined
  // response from the static one, negative for a cut.  As of Refresh().
  bool group_active(int g) const { return group_active_[g]; }
  float group_gain_db(int g) const { return group_gain_db_[g]; }

  // Display frequencies of the curves (also used for the analyzer).
  const std::vector<double>& freqs() const { return freqs_; }

  // Spectra (dBFS, tilted) at freqs(); empty vectors hide them.
  void SetSpectrum(AnalyzerMode mode, const std::vector<float>& pre,
                   const std::vector<float>& post) {
    analyzer_mode_ = mode;
    spectrum_pre_ = pre;
    spectrum_post_ = post;
    SetDirty(false);
  }

  // Called from the plugin's idle loop.
  void Refresh() {
    const ParamValues v = model_.Values();
    const GroupControls live = model_.LiveControls();
    const int points = std::max(32, static_cast<int>(mRECT.W() / 2.f));
    const bool changed = v != values_ || live != live_ ||
                         freqs_.size() != static_cast<size_t>(points) ||
                         model_.SampleRate() != sample_rate_;
    if (!changed) return;
    values_ = v;
    live_ = live;
    sample_rate_ = model_.SampleRate();
    if (freqs_.size() != static_cast<size_t>(points)) {
      freqs_ = DisplayFrequencies(points, kMinHz, kMaxHz);
    }
    params_ = BuildEngineParams(values_, false);
    ComputeChainResponse(params_, GroupControls{}, sample_rate_, freqs_,
                         static_);
    any_dynamic_ = false;
    group_active_.fill(false);
    group_gain_db_.fill(0.f);
    for (const BandParams& bp : params_.bands) {
      if (bp.type == BandType::kOff || bp.bypass) continue;
      for (int g = 0; g < kNumGroups; ++g) {
        if (!SubscribesTo(bp, g)) continue;
        any_dynamic_ = true;
        group_active_[g] = true;
      }
    }
    if (any_dynamic_) {
      GroupControls full;
      full.fill(1.0);
      ComputeChainResponse(params_, full, sample_rate_, freqs_, ghost_);
      ComputeChainResponse(params_, live_, sample_rate_, freqs_, live_curves_);
      ComputeGroupGains();
    }
    ComputeLiveMarkers();
    SetDirty(false);
  }

  void Draw(iplug::igraphics::IGraphics& g) override {
    namespace ig = iplug::igraphics;
    g.FillRect(theme::kBackground, mRECT);
    DrawGrid(g);
    if (freqs_.empty()) return;  // Not refreshed yet.
    DrawSpectrum(g);

    const int selected = model_.SelectedBand();
    for (int b = 0; b < kNumBands; ++b) {
      const std::vector<float>& c = static_.band_db[b];
      if (c.empty()) continue;
      const bool bypassed = params_.bands[b].bypass;
      const ig::IColor color = theme::BandColor(b);
      if (b == selected && !bypassed) FillCurve(g, c, color.WithOpacity(0.18f));
      StrokeCurve(g, c, color.WithOpacity(bypassed ? 0.2f : 0.55f),
                  b == selected ? 1.5f : 1.f);
    }
    if (any_dynamic_) {
      StrokeCurve(g, ghost_.total_db, theme::kText.WithOpacity(0.25f), 1.f);
      StrokeCurve(g, static_.total_db, theme::kText.WithOpacity(0.5f), 1.f);
      StrokeCurve(g, live_curves_.total_db, theme::kAccent, 2.f);
    } else {
      StrokeCurve(g, static_.total_db, theme::kText, 2.f);
    }

    DrawLiveMarkers(g);

    if (selected >= 0) {
      for (int side = 0; side < 2; ++side) {
        const std::optional<ig::IVec2> e = EdgePosition(selected, side);
        if (!e) continue;
        g.FillRoundRect(theme::BandColor(selected),
                        ig::IRECT(e->x - kEdgeHalfW, e->y - kEdgeHalfH,
                                  e->x + kEdgeHalfW, e->y + kEdgeHalfH),
                        1.5f);
      }
    }
    if (selected >= 0) DrawDynNodes(g, selected);
    for (int b = 0; b < kNumBands; ++b) {
      const std::optional<ig::IVec2> p = NodePosition(b);
      if (!p) continue;
      const ig::IColor color = theme::BandColor(b);
      const bool bypassed = params_.bands[b].bypass;
      g.FillCircle(color.WithOpacity(bypassed ? 0.35f : 1.f), p->x, p->y,
                   kNodeRadius);
      if (b == selected) {
        g.DrawCircle(theme::kText, p->x, p->y, kNodeRadius + 2.5f, nullptr,
                     1.5f);
      }
      const std::string label = std::to_string(b + 1);
      g.DrawText(theme::Text(9.f, theme::kBackground, ig::EAlign::Center),
                 label.c_str(),
                 ig::IRECT(p->x - 8, p->y - 6, p->x + 8, p->y + 6));
    }
  }

  void OnMouseDown(float x, float y,
                   const iplug::igraphics::IMouseMod& mod) override {
    drag_edge_ = HitEdge(x, y);
    if (drag_edge_ >= 0) {
      drag_band_ = model_.SelectedBand();
      const std::array<double, 2> edges = ShelfEdges(drag_band_);
      drag_x_ = FreqToX(edges[drag_edge_]);
      drag_fixed_hz_ = edges[1 - drag_edge_];
      for (int id : DragParams(drag_band_)) model_.BeginEdit(id);
      return;
    }
    drag_band_ = HitNode(x, y);
    if (drag_band_ < 0) return;
    model_.SelectBand(drag_band_);
    drag_dyn_ = mod.A ? 0 : DynBModifier(mod) ? 1 : -1;
    if (drag_dyn_ >= 0) {
      const int id = DynRangeId(drag_band_, drag_dyn_);
      drag_range_ =
          SlotToPhysical(DynRangeMapping(DynTargetOf(drag_dyn_)), values_[id]);
      model_.BeginEdit(id);
      SetDirty(false);
      return;
    }
    // Drag relative to the node's own position, so it doesn't jump.
    const auto p = NodePosition(drag_band_);
    drag_x_ = p ? p->x : x;
    drag_y_ = p ? p->y : y;
    for (int id : DragParams(drag_band_)) model_.BeginEdit(id);
    SetDirty(false);
  }

  void OnMouseDrag(float x, float y, float dx, float dy,
                   const iplug::igraphics::IMouseMod& mod) override {
    if (drag_band_ < 0) return;
    const float gear = mod.S ? 0.1f : 1.f;
    drag_x_ += dx * gear;
    drag_y_ += dy * gear;
    if (drag_dyn_ >= 0) {
      DragDynRange(dx * gear, dy * gear);
      Refresh();
      return;
    }
    if (drag_edge_ >= 0) {
      DragEdge();
      Refresh();
      return;
    }
    const BandParams& bp = params_.bands[drag_band_];
    const int x_id = BandParamId(drag_band_, BandField::kX);
    const int y_id = BandParamId(drag_band_, BandField::kY);
    if (const auto m = SlotMappingFor(bp.type, Slot::kX)) {
      if (std::string(m->unit) == "Hz") {
        model_.SetEdit(x_id, SlotFromPhysical(*m, XToFreq(drag_x_)));
      } else {  // Vowel morph: full width = full range.
        model_.SetEdit(
            x_id, std::clamp(values_[x_id] + dx * gear / mRECT.W(), 0.0, 1.0));
      }
    }
    if (DragsQ(bp.type)) {
      const SlotMapping q = *SlotMappingFor(bp.type, Slot::kZ);
      model_.SetEdit(
          BandParamId(drag_band_, BandField::kZ),
          SlotFromPhysical(
              q, std::pow(10.0, YToDb(drag_y_) / QDbScale(drag_band_))));
    } else if (const auto m = SlotMappingFor(bp.type, Slot::kY)) {
      if (std::string(m->unit) == "dB") {
        model_.SetEdit(y_id, SlotFromPhysical(*m, YToDb(drag_y_)));
      } else {  // Phon: full height = full range.
        model_.SetEdit(
            y_id, std::clamp(values_[y_id] - dy * gear / mRECT.H(), 0.0, 1.0));
      }
    }
    Refresh();
  }

  void OnMouseUp(float x, float y,
                 const iplug::igraphics::IMouseMod& mod) override {
    if (drag_band_ < 0) return;
    for (int id : DragParams(drag_band_)) model_.EndEdit(id);
    drag_band_ = -1;
    drag_edge_ = -1;
    drag_dyn_ = -1;
    SetDirty(false);
  }

  void OnMouseWheel(float x, float y, const iplug::igraphics::IMouseMod& mod,
                    float d) override {
    int band = HitNode(x, y);
    if (band < 0) band = model_.SelectedBand();
    if (band < 0 || !SlotMappingFor(params_.bands[band].type, Slot::kZ)) {
      return;
    }
    const int z_id = BandParamId(band, BandField::kZ);
    const double step = mod.S ? 0.002 : 0.02;
    model_.SetValueGesture(z_id,
                           std::clamp(values_[z_id] + d * step, 0.0, 1.0));
    Refresh();
  }

  void OnMouseDblClick(float x, float y,
                       const iplug::igraphics::IMouseMod& mod) override {
    if (HitNode(x, y) >= 0) return;
    for (int b = 0; b < kNumBands; ++b) {
      if (params_.bands[b].type != BandType::kOff) continue;
      const SlotMapping freq = *SlotMappingFor(BandType::kPeak, Slot::kX);
      const SlotMapping gain = *SlotMappingFor(BandType::kPeak, Slot::kY);
      const int type_id = BandParamId(b, BandField::kType);
      model_.SetValueGesture(type_id, static_cast<double>(BandType::kPeak) /
                                          ParamTable()[type_id].max);
      model_.SetValueGesture(BandParamId(b, BandField::kX),
                             SlotFromPhysical(freq, XToFreq(x)));
      model_.SetValueGesture(BandParamId(b, BandField::kY),
                             SlotFromPhysical(gain, YToDb(y)));
      model_.SelectBand(b);
      Refresh();
      return;
    }
  }

 private:
  // Cmd on Mac (which iPlug reports as both buttons) or Ctrl.
  static bool DynBModifier(const iplug::igraphics::IMouseMod& mod) {
    return mod.C || (mod.L && mod.R);
  }

  // Dyn assignment `dyn` (0 = A, 1 = B) of the dragged band.
  static int DynRangeId(int band, int dyn) {
    return BandParamId(band,
                       dyn == 0 ? BandField::kDynRange : BandField::kDynBRange);
  }
  DynTarget DynTargetOf(int dyn) const {
    const BandParams& bp = params_.bands[drag_band_];
    return dyn == 0 ? bp.dyn_target : bp.dyn_b_target;
  }

  // Moves the dragged Dyn Range with the pointer, in the units of the axis
  // its target moves the node along.
  void DragDynRange(float dx, float dy) {
    const DynTarget target = DynTargetOf(drag_dyn_);
    const double oct_per_px = std::log2(kMaxHz / kMinHz) / mRECT.W();
    switch (target) {
      case DynTarget::kGain:
        drag_range_ -= dy * 2.0 * kRangeDb / mRECT.H();
        break;
      case DynTarget::kFreq:
        drag_range_ += dx * oct_per_px;
        break;
      case DynTarget::kMorph:
        if (params_.bands[drag_band_].type == BandType::kVowel) {
          // As the node's own Morph drag: full width = full range.
          const SlotMapping m = *SlotMappingFor(BandType::kVowel, Slot::kX);
          drag_range_ += dx / mRECT.W() * (m.max - m.min);
        } else {  // Width, octaves.
          drag_range_ += dx * oct_per_px;
        }
        break;
    }
    const SlotMapping m = DynRangeMapping(target);
    drag_range_ = std::clamp(drag_range_, m.min, m.max);
    model_.SetEdit(DynRangeId(drag_band_, drag_dyn_),
                   SlotFromPhysical(m, drag_range_));
  }

  // Where Dyn assignment `dyn` alone takes a band's node at full dynamics.
  std::optional<iplug::igraphics::IVec2> DynNodePosition(int band,
                                                         int dyn) const {
    BandParams bp = params_.bands[band];
    if ((dyn == 0 ? bp.dyn_range : bp.dyn_b_range) == 0.0) return std::nullopt;
    const ResolvedBand rb =
        ResolveBand(bp, dyn == 0 ? 1.0 : 0.0, dyn == 0 ? 0.0 : 1.0,
                    params_.global.global_scale);
    bp.x = rb.x, bp.y = rb.y, bp.z = rb.z;
    return NodePosition(band, bp);
  }

  // Rings at the selected band's full-dynamics positions, joined to its
  // node.
  void DrawDynNodes(iplug::igraphics::IGraphics& g, int band) {
    namespace ig = iplug::igraphics;
    const std::optional<ig::IVec2> p = NodePosition(band);
    if (!p || params_.bands[band].bypass) return;
    const ig::IColor color = theme::BandColor(band);
    for (int dyn = 0; dyn < 2; ++dyn) {
      const std::optional<ig::IVec2> q = DynNodePosition(band, dyn);
      if (!q || std::hypot(q->x - p->x, q->y - p->y) < 2.f) continue;
      const bool dragging = drag_band_ == band && drag_dyn_ == dyn;
      g.DrawLine(color.WithOpacity(0.5f), p->x, p->y, q->x, q->y);
      g.FillCircle(theme::kBackground, q->x, q->y, kNodeRadius);
      g.DrawCircle(color, q->x, q->y, kNodeRadius, nullptr,
                   dragging ? 2.f : 1.25f);
      g.DrawText(theme::Text(9.f, color, ig::EAlign::Center),
                 dyn == 0 ? "A" : "B",
                 ig::IRECT(q->x - 8, q->y - 6, q->x + 8, q->y + 6));
    }
  }

  // Where a band's frequency has moved to, for bands whose dynamics move it
  // away from their node (e.g. Dyn Target Freq following pitch).
  void ComputeLiveMarkers() {
    live_marker_hz_.fill(0.0);
    for (int b = 0; b < kNumBands; ++b) {
      const BandParams& bp = params_.bands[b];
      if (bp.type == BandType::kOff || bp.bypass) continue;
      const auto m = SlotMappingFor(bp.type, Slot::kX);
      if (!m || std::string(m->unit) != "Hz") continue;
      auto control = [&](int group) {
        return group >= 0 && group < kNumGroups
                   ? std::clamp(live_[group], 0.0, 1.0)
                   : 0.0;
      };
      const double hz =
          ResolveBand(bp, control(bp.dyn_group), control(bp.dyn_b_group),
                      params_.global.global_scale)
              .x;
      // Only once it is visibly apart from the node.
      if (std::abs(std::log2(hz / bp.x)) > 0.03) live_marker_hz_[b] = hz;
    }
  }

  // A thin line in the band's color at its live frequency, with a dot where
  // it meets the live curve and the band number at the top.
  void DrawLiveMarkers(iplug::igraphics::IGraphics& g) {
    namespace ig = iplug::igraphics;
    const std::vector<float>& total =
        any_dynamic_ ? live_curves_.total_db : static_.total_db;
    for (int b = 0; b < kNumBands; ++b) {
      const double hz = live_marker_hz_[b];
      if (hz < kMinHz || hz > kMaxHz) continue;
      const ig::IColor color = theme::BandColor(b);
      const float x = FreqToX(hz);
      g.DrawLine(color.WithOpacity(0.5f), x, mRECT.T + 12.f, x, mRECT.B);
      if (!total.empty()) {
        const double u = std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz);
        const size_t i =
            std::min(total.size() - 1,
                     static_cast<size_t>(std::lround(u * (total.size() - 1))));
        g.FillCircle(color, x, DbToY(total[i]), 3.f);
      }
      const std::string label = std::to_string(b + 1);
      g.DrawText(theme::Text(9.f, color, ig::EAlign::Center), label.c_str(),
                 ig::IRECT(x - 8.f, mRECT.T + 1.f, x + 8.f, mRECT.T + 12.f));
    }
  }

  void ComputeGroupGains() {
    // Silence (e.g. a BP band at its floor) counts as -60 dB, so opening it
    // reads +60 dB rather than an arbitrary huge number.
    constexpr float kFloorDb = -60.f;
    std::vector<float> delta(freqs_.size());
    for (int g = 0; g < kNumGroups; ++g) {
      if (!group_active_[g]) continue;
      std::fill(delta.begin(), delta.end(), 0.f);
      for (int b = 0; b < kNumBands; ++b) {
        const BandParams& bp = params_.bands[b];
        if (!SubscribesTo(bp, g) || bp.bypass) continue;
        const std::vector<float>& live = live_curves_.band_db[b];
        const std::vector<float>& still = static_.band_db[b];
        if (live.size() != delta.size() || still.size() != delta.size()) {
          continue;
        }
        for (size_t i = 0; i < delta.size(); ++i) {
          delta[i] +=
              std::max(live[i], kFloorDb) - std::max(still[i], kFloorDb);
        }
      }
      float extreme = 0.f;
      for (float d : delta) {
        if (std::abs(d) > std::abs(extreme)) extreme = d;
      }
      group_gain_db_[g] = extreme;
    }
  }

  float FreqToX(double hz) const {
    const double u = std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz);
    return mRECT.L + static_cast<float>(u) * mRECT.W();
  }
  double XToFreq(float x) const {
    const double u = std::clamp((x - mRECT.L) / mRECT.W(), 0.f, 1.f);
    return kMinHz * std::pow(kMaxHz / kMinHz, u);
  }
  float DbToY(double db) const {
    const double u = std::clamp(db, -kRangeDb * 1.2, kRangeDb * 1.2) / kRangeDb;
    return mRECT.MH() - static_cast<float>(u) * 0.5f * mRECT.H();
  }
  double YToDb(float y) const {
    return (mRECT.MH() - y) / (0.5 * mRECT.H()) * kRangeDb;
  }

  // LP and HP nodes drag Q vertically (as in Logic's Channel EQ): the node
  // sits at the curve's level at the cutoff, which is Q in dB per cell (two
  // cells at 24 dB/oct).
  static bool DragsQ(BandType type) {
    return type == BandType::kLp || type == BandType::kHp;
  }
  double QDbScale(int band) const {
    return IsCrossover(params_.bands[band].type, params_.bands[band].option)
               ? 40.0
               : 20.0;
  }

  std::vector<int> DragParams(int band) const {
    if (drag_dyn_ >= 0) return {DynRangeId(band, drag_dyn_)};
    if (drag_edge_ >= 0) {
      return {BandParamId(band, BandField::kX),
              BandParamId(band, BandField::kZ)};
    }
    return {
        BandParamId(band, BandField::kX),
        BandParamId(band, DragsQ(params_.bands[band].type) ? BandField::kZ
                                                           : BandField::kY)};
  }

  // A Band Shelf's edges (Hz): the half-gain points of its two shelves.
  std::array<double, 2> ShelfEdges(int band) const {
    const BandParams& bp = params_.bands[band];
    const double h = std::exp2(0.5 * bp.z);
    return {bp.x / h, bp.x * h};
  }

  std::optional<iplug::igraphics::IVec2> EdgePosition(int band,
                                                      int side) const {
    if (freqs_.empty() || band < 0 ||
        params_.bands[band].type != BandType::kBandShelf) {
      return std::nullopt;
    }
    const double hz = std::clamp(ShelfEdges(band)[side], kMinHz, kMaxHz);
    return iplug::igraphics::IVec2(
        FreqToX(hz),
        DbToY(std::clamp(0.5 * params_.bands[band].y, -kRangeDb, kRangeDb)));
  }

  // 0 = low edge, 1 = high edge of the selected Band Shelf, or -1.
  int HitEdge(float x, float y) const {
    for (int side = 0; side < 2; ++side) {
      const auto e = EdgePosition(model_.SelectedBand(), side);
      if (e && std::abs(e->x - x) <= kEdgeHalfW + 3.f &&
          std::abs(e->y - y) <= kEdgeHalfH + 3.f) {
        return side;
      }
    }
    return -1;
  }

  void DragEdge() {
    const double min_ratio = std::exp2(kMinWidthOct);
    const double f = XToFreq(drag_x_);
    const double lo = drag_edge_ == 0 ? std::min(f, drag_fixed_hz_ / min_ratio)
                                      : drag_fixed_hz_;
    const double hi = drag_edge_ == 1 ? std::max(f, drag_fixed_hz_ * min_ratio)
                                      : drag_fixed_hz_;
    const BandType type = params_.bands[drag_band_].type;
    model_.SetEdit(
        BandParamId(drag_band_, BandField::kX),
        SlotFromPhysical(*SlotMappingFor(type, Slot::kX), std::sqrt(lo * hi)));
    model_.SetEdit(
        BandParamId(drag_band_, BandField::kZ),
        SlotFromPhysical(*SlotMappingFor(type, Slot::kZ), std::log2(hi / lo)));
  }

  int HitNode(float x, float y) const {
    // Topmost (highest index) first, but prefer the selected band.
    const int selected = model_.SelectedBand();
    auto hit = [&](int b) {
      const auto p = NodePosition(b);
      return p && std::hypot(p->x - x, p->y - y) <= kNodeRadius + 3.f;
    };
    if (selected >= 0 && hit(selected)) return selected;
    for (int b = kNumBands - 1; b >= 0; --b) {
      if (hit(b)) return b;
    }
    return -1;
  }

  std::optional<iplug::igraphics::IVec2> NodePosition(int band) const {
    return NodePosition(band, params_.bands[band]);
  }

  // The node of `band` with settings `bp` (e.g. moved by its dynamics).
  std::optional<iplug::igraphics::IVec2> NodePosition(
      int band, const BandParams& bp) const {
    if (freqs_.empty()) return std::nullopt;
    double hz = 1000.0;
    switch (bp.type) {
      case BandType::kOff:
      case BandType::kKWeight:
        return std::nullopt;
      case BandType::kFletcherMunson:
        hz = 1000.0;
        break;
      case BandType::kVowel:
        hz = VowelFormants(bp.x).hz[1] * bp.z;  // F2.
        break;
      default:
        hz = bp.x;
        break;
    }
    float y = DbToY(0.0);
    if (const auto m = SlotMappingFor(bp.type, Slot::kY)) {
      if (std::string(m->unit) == "dB") {
        y = DbToY(std::max(bp.y, -kRangeDb * 1.2));
      } else {
        y = mRECT.B -
            static_cast<float>(SlotFromPhysical(*m, bp.y)) * mRECT.H();
      }
    } else if (DragsQ(bp.type)) {
      y = DbToY(QDbScale(band) * std::log10(bp.z));
    } else {
      // Filters: sit on the band's own curve at the (static) cutoff, which
      // is about the same height wherever the cutoff is.
      const std::vector<float>& c = static_.band_db[band];
      const double cutoff = params_.bands[band].x;
      const size_t i = std::min(
          c.size() - 1, static_cast<size_t>(std::lround(
                            std::log(cutoff / kMinHz) /
                            std::log(kMaxHz / kMinHz) * (c.size() - 1))));
      y = DbToY(c.empty() ? 0.0 : c[i]);
    }
    return iplug::igraphics::IVec2(FreqToX(std::clamp(hz, kMinHz, kMaxHz)), y);
  }

  // The analyzer has its own scale: kSpectrumMinDb .. kSpectrumMaxDb dBFS.
  static constexpr float kSpectrumMinDb = -84.f;
  static constexpr float kSpectrumMaxDb = 6.f;

  float SpectrumToY(float db) const {
    const float u = std::clamp(
        (db - kSpectrumMinDb) / (kSpectrumMaxDb - kSpectrumMinDb), 0.f, 1.f);
    return mRECT.B - u * mRECT.H();
  }

  void DrawSpectrum(iplug::igraphics::IGraphics& g) {
    const bool pre = analyzer_mode_ == AnalyzerMode::kPre ||
                     analyzer_mode_ == AnalyzerMode::kPreAndPost;
    const bool post = analyzer_mode_ == AnalyzerMode::kPost ||
                      analyzer_mode_ == AnalyzerMode::kPreAndPost;
    if (pre && spectrum_pre_.size() == freqs_.size()) {
      g.PathClear();
      g.PathMoveTo(FreqToX(freqs_.front()), mRECT.B);
      for (size_t i = 0; i < freqs_.size(); ++i) {
        g.PathLineTo(FreqToX(freqs_[i]), SpectrumToY(spectrum_pre_[i]));
      }
      g.PathLineTo(FreqToX(freqs_.back()), mRECT.B);
      g.PathClose();
      g.PathFill(theme::kText.WithOpacity(0.08f));
    }
    if (post && spectrum_post_.size() == freqs_.size()) {
      g.PathClear();
      for (size_t i = 0; i < freqs_.size(); ++i) {
        const float x = FreqToX(freqs_[i]), y = SpectrumToY(spectrum_post_[i]);
        if (i == 0) {
          g.PathMoveTo(x, y);
        } else {
          g.PathLineTo(x, y);
        }
      }
      g.PathStroke(
          iplug::igraphics::IColor(255, 160, 200, 255).WithOpacity(0.45f), 1.f);
    }
  }

  void DrawGrid(iplug::igraphics::IGraphics& g) {
    namespace ig = iplug::igraphics;
    const ig::IText label = theme::Text(9.f, theme::kTextDim, ig::EAlign::Near);
    for (double hz : {20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0,
                      10000.0, 20000.0}) {
      const float x = FreqToX(hz);
      g.DrawLine(theme::kGrid, x, mRECT.T, x, mRECT.B);
      char buf[16];
      std::snprintf(buf, sizeof(buf), hz >= 1000.0 ? "%gk" : "%g",
                    hz >= 1000.0 ? hz / 1000.0 : hz);
      g.DrawText(label, buf, ig::IRECT(x + 2, mRECT.B - 14, x + 40, mRECT.B));
    }
    for (int db = -18; db <= 18; db += 6) {
      const float y = DbToY(db);
      g.DrawLine(db == 0 ? theme::kGridStrong : theme::kGrid, mRECT.L, y,
                 mRECT.R, y);
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%+d", db);
      g.DrawText(label, db == 0 ? "0 dB" : buf,
                 ig::IRECT(mRECT.L + 3, y - 12, mRECT.L + 40, y));
    }
    g.DrawRect(theme::kBorder, mRECT);
  }

  void StrokeCurve(iplug::igraphics::IGraphics& g, const std::vector<float>& c,
                   const iplug::igraphics::IColor& color, float thickness) {
    if (c.size() != freqs_.size()) return;
    g.PathClear();
    for (size_t i = 0; i < c.size(); ++i) {
      const float x = FreqToX(freqs_[i]), y = DbToY(c[i]);
      if (i == 0) {
        g.PathMoveTo(x, y);
      } else {
        g.PathLineTo(x, y);
      }
    }
    g.PathStroke(color, thickness);
  }

  void FillCurve(iplug::igraphics::IGraphics& g, const std::vector<float>& c,
                 const iplug::igraphics::IColor& color) {
    if (c.size() != freqs_.size()) return;
    const float zero = DbToY(0.0);
    g.PathClear();
    g.PathMoveTo(FreqToX(freqs_.front()), zero);
    for (size_t i = 0; i < c.size(); ++i)
      g.PathLineTo(FreqToX(freqs_[i]), DbToY(c[i]));
    g.PathLineTo(FreqToX(freqs_.back()), zero);
    g.PathClose();
    g.PathFill(color);
  }

  EditorModel& model_;
  AnalyzerMode analyzer_mode_ = AnalyzerMode::kPreAndPost;
  std::vector<float> spectrum_pre_, spectrum_post_;
  ParamValues values_{};
  GroupControls live_{};
  double sample_rate_ = 0.0;
  EngineParams params_{};
  std::vector<double> freqs_;
  ChainResponse static_, ghost_, live_curves_;
  std::array<bool, kNumGroups> group_active_{};
  std::array<float, kNumGroups> group_gain_db_{};
  bool any_dynamic_ = false;
  std::array<double, kNumBands> live_marker_hz_{};  // 0: no marker.
  int drag_band_ = -1;
  int drag_edge_ = -1;  // Band Shelf edge being dragged: 0 = low, 1 = high.
  int drag_dyn_ = -1;   // Dyn Range being dragged: 0 = A, 1 = B.
  double drag_range_ = 0.0;  // Its physical value.
  double drag_fixed_hz_ = 0.0;
  float drag_x_ = 0.f, drag_y_ = 0.f;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_PLUGIN_UI_EQ_GRAPH_H_
