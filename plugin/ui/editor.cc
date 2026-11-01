#include "plugin/ui/editor.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>

#include "IControls.h"
#include "model/slot_mapping.h"
#include "plugin/ui/band_ribbon.h"
#include "plugin/ui/choice_field.h"
#include "plugin/ui/eq_graph.h"
#include "plugin/ui/fader.h"
#include "plugin/ui/level_meter.h"
#include "plugin/ui/param_field.h"
#include "plugin/ui/preset_bar.h"
#include "plugin/ui/routing_map.h"
#include "plugin/ui/theme.h"
#include "plugin/ui/widgets.h"

namespace eeq {
namespace {

namespace ig = iplug::igraphics;

constexpr float kRow = 18.f;
constexpr float kPitch = 21.f;
constexpr float kGap = 4.f;
constexpr int kField = static_cast<int>(ParamField::Style::kField);
constexpr int kToggle = static_cast<int>(ParamField::Style::kToggle);
constexpr int kDial = static_cast<int>(ParamField::Style::kDial);
constexpr int kSegments = static_cast<int>(ParamField::Style::kSegments);

// The docked panel's color over the background (kPanel at 60%), which the
// routing map uses to break lines where they cross.
const ig::IColor kDockedPanel(255, 34, 37, 42);

int Int(double v) { return static_cast<int>(std::lround(v)); }

std::string Upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(c));
  return s;
}

ig::IRECT ToIRECT(const Rect& r) { return ig::IRECT(r.l, r.t, r.r, r.b); }

// Shows `c` at `r`, or hides it.
void Place(ig::IControl* c, bool show, const ig::IRECT& r = ig::IRECT()) {
  if (!c) return;
  if (show) {
    const ig::IRECT& cur = c->GetRECT();
    if (cur.L != r.L || cur.T != r.T || cur.R != r.R || cur.B != r.B) {
      c->SetTargetAndDrawRECTs(r);
    }
  }
  if (c->IsHidden() == show) c->Hide(!show);
}

ig::IRECT Row(const Rect& r, int row, float top_offset = 0.f) {
  const float t = r.t + top_offset + row * kPitch;
  return ig::IRECT(r.l, t, r.r, t + kRow);
}

}  // namespace

Editor::Editor(ig::IGraphics& graphics, EditorModel& model)
    : graphics_(graphics), model_(model) {
  graphics_.AttachPanelBackground(theme::kBackground);
  graphics_.EnableTooltips(true);
  // Attach order is drawing order: the dynamics drawer must cover the graph
  // and strip.
  BuildTop();
  BuildGraphArea();
  BuildStrip();
  BuildFooter();
  BuildPanel();
  Layout();
  OnBandSelected(model_.SelectedBand());
}

void Editor::BuildTop() {
  ribbon_ = new BandRibbon(ig::IRECT(), model_);
  graphics_.AttachControl(ribbon_);
  preset_bar_ = new PresetBar(ig::IRECT(), model_);
  graphics_.AttachControl(preset_bar_);
}

void Editor::BuildGraphArea() {
  graph_ = new EqGraph(ig::IRECT(), model_);
  graphics_.AttachControl(graph_);
  input_meter_ = new LevelMeter(ig::IRECT(), "In", 2);
  graphics_.AttachControl(input_meter_);
  output_meter_ = new LevelMeter(ig::IRECT(), "Out", 2);
  graphics_.AttachControl(output_meter_);
  fader_ =
      new Fader(ig::IRECT(), GlobalParamId(GlobalField::kOutputGain), "Gain");
  fader_->SetTooltip(
      "Output Gain. Drag (Shift = fine); double-click or Option-click for "
      "0 dB.");
  graphics_.AttachControl(fader_);
  // The selected band's live dynamic offset, over the graph's corner.
  now_readout_ = new ig::ITextControl(
      ig::IRECT(), "", theme::Text(11.f, theme::kTextDim, ig::EAlign::Near));
  now_readout_->SetIgnoreMouse(true);
  graphics_.AttachControl(now_readout_);
  mini_label_ = new MiniLabel(ig::IRECT());
  graphics_.AttachControl(mini_label_);
}

ParamField* Editor::AddBandField(BandField f, const char* label,
                                 float label_width, int style) {
  auto* c = new ParamField(ig::IRECT(), BandParamId(0, f), label, label_width,
                           static_cast<ParamField::Style>(style));
  graphics_.AttachControl(c);
  band_fields_.push_back({f, c});
  return c;
}

void Editor::BuildStrip() {
  strip_panel_ = new StripPanel(ig::IRECT(), model_);
  graphics_.AttachControl(strip_panel_);
  type_field_ = AddBandField(BandField::kType, "", 0.f, kField);
  type_field_->SetTooltip("Band type.");
  tap_field_ = AddBandField(BandField::kTap, "TAP", 26.f, kField);
  tap_field_->SetTooltip(
      "Main: the band filters the signal. Sum: its output is mixed back in "
      "parallel (ABOUT.md section 3.D).");
  chan_field_ = AddBandField(BandField::kChan, "CH", 18.f, kField);
  chan_field_->SetTooltip("Which channels the band processes.");
  option_field_ = AddBandField(BandField::kOption, "OPTION", 46.f, kField);
  bypass_field_ = AddBandField(BandField::kBypass, "BYP", 0.f, kToggle);
  solo_field_ = AddBandField(BandField::kSolo, "SOLO", 0.f, kToggle);
  dials_[0] = AddBandField(BandField::kX, "FREQ", 0.f, kDial);
  dials_[1] = AddBandField(BandField::kY, "GAIN", 0.f, kDial);
  dials_[2] = AddBandField(BandField::kZ, "Q", 0.f, kDial);
  for (ParamField* d : dials_) {
    d->SetTooltip("Drag (Shift = fine) or scroll; double-click to type.");
  }

  for (int slot = 0; slot < 2; ++slot) {
    const bool a = slot == 0;
    Card& c = cards_[slot];
    c.panel = new CardPanel(ig::IRECT(), a ? "DYN A" : "DYN B");
    c.panel->SetTitleInsetRight(18.f);
    graphics_.AttachControl(c.panel);
    c.group = AddBandField(a ? BandField::kDynGroup : BandField::kDynBGroup, "",
                           0.f, kField);
    c.group->SetTooltip("The dynamics group this assignment follows.");
    c.target = AddBandField(a ? BandField::kDynTarget : BandField::kDynBTarget,
                            "", 0.f, kSegments);
    c.target->SetTooltip(
        "What the group moves: Gain (dB), Freq (octaves) or Morph (Vowel "
        "morph; width for Band Shelf and Tilt).");
    c.target_menu = AddBandField(
        a ? BandField::kDynTarget : BandField::kDynBTarget, "", 0.f, kField);
    c.range_dial = AddBandField(
        a ? BandField::kDynRange : BandField::kDynBRange, "RANGE", 0.f, kDial);
    c.range_dial->SetTooltip(
        "How far the target moves at full dynamics (D = 100%). Also: "
        "Option-drag (Dyn A) or Command-drag (Dyn B) a node in the graph.");
    c.range_field = AddBandField(
        a ? BandField::kDynRange : BandField::kDynBRange, "", 0.f, kField);
    if (a) {
      c.makeup = AddBandField(BandField::kDynMakeup, "MAKEUP", 46.f, kField);
      c.makeup->SetTooltip(
          "Broadband gain that offsets the band's dynamic gain change (0% = "
          "none, 100% = full).");
    }
    c.disconnect = new TextButton(
        ig::IRECT(), "\xc3\x97",
        [this, a] {
          const int band = std::max(0, model_.SelectedBand());
          model_.SetValueGesture(BandParamId(band, a ? BandField::kDynRange
                                                     : BandField::kDynBRange),
                                 kSlotDefault);
        },
        TextButton::Style::kGhost);
    c.disconnect->SetTooltip("Disconnect (sets the range to 0).");
    graphics_.AttachControl(c.disconnect);
    c.connect = new TextButton(
        ig::IRECT(), "+ Connect", [this, slot] { Connect(slot); },
        TextButton::Style::kOutline);
    graphics_.AttachControl(c.connect);
  }
}

void Editor::BuildFooter() {
  footer_panel_ = new Panel(ig::IRECT());
  graphics_.AttachControl(footer_panel_);
  auto field = [&](GlobalField f, const char* label, float label_width,
                   int style = kField) {
    auto* c = new ParamField(ig::IRECT(), GlobalParamId(f), label, label_width,
                             static_cast<ParamField::Style>(style));
    graphics_.AttachControl(c);
    return c;
  };
  footer_row1_.push_back(field(GlobalField::kOutputMode, "OUTPUT", 46.f));
  footer_row1_.push_back(field(GlobalField::kLookahead, "LOOKAHEAD", 64.f));
  ParamField* scale = field(GlobalField::kGlobalScale, "SCALE", 40.f);
  scale->SetTooltip(
      "Scales every band's gain, static and dynamic: 0% is flat, 50% is half "
      "the curve in dB, negative inverts it. Freq and Morph aren't scaled, "
      "so pitch and vowel tracking stay in tune.");
  footer_row1_.push_back(scale);

  footer_row2_.push_back(field(GlobalField::kDelta, "\xce\x94", 0.f, kToggle));
  footer_row2_widths_.push_back(36.f);
  footer_row2_.push_back(field(GlobalField::kBypass, "BYPASS", 0.f, kToggle));
  footer_row2_widths_.push_back(64.f);
  auto* analyzer = new ChoiceField(ig::IRECT(), "ANALYZER", 56.f,
                                   {"Off", "Pre", "Post", "Pre+Post"},
                                   analyzer_mode_, [this](int i) {
                                     analyzer_mode_ = i;
                                     pre_analyzer_.Clear();
                                     post_analyzer_.Clear();
                                   });
  graphics_.AttachControl(analyzer);
  footer_row2_.push_back(analyzer);
  footer_row2_widths_.push_back(150.f);
  static const double kTilts[] = {0.0, 3.0, 4.5, 6.0};
  auto* tilt = new ChoiceField(
      ig::IRECT(), "TILT", 28.f, {"0 dB", "3 dB", "4.5 dB", "6 dB"}, 0,
      [this](int i) { tilt_db_per_octave_ = kTilts[i]; });
  graphics_.AttachControl(tilt);
  footer_row2_.push_back(tilt);
  footer_row2_widths_.push_back(96.f);
  status_ = new ig::ITextControl(
      ig::IRECT(), "", theme::Text(9.f, theme::kTextDim, ig::EAlign::Far));
  graphics_.AttachControl(status_);
}

void Editor::BuildPanel() {
  drawer_tab_ = new DrawerTab(
      ig::IRECT(), model_, [this](int g) { return graph_->group_active(g); },
      [this] {
        drawer_open_ = !drawer_open_;
        Layout();
      });
  graphics_.AttachControl(drawer_tab_);

  panel_bg_ = new Panel(ig::IRECT());
  graphics_.AttachControl(panel_bg_);
  routing_title_ = new ig::ITextControl(ig::IRECT(), "ROUTING",
                                        theme::Text(9.f, theme::kTextDim));
  routing_title_->SetIgnoreMouse(true);
  graphics_.AttachControl(routing_title_);
  RoutingMap::Callbacks cb;
  cb.select_group = [this](int g) { SelectGroup(g); };
  cb.selected_group = [this] { return selected_group_; };
  cb.group_active = [this](int g) { return graph_->group_active(g); };
  cb.group_gain_db = [this](int g) { return graph_->group_gain_db(g); };
  routing_ = new RoutingMap(ig::IRECT(), model_, std::move(cb));
  graphics_.AttachControl(routing_);
  legend_ = new ig::ITextControl(
      ig::IRECT(),
      "G gain \xc2\xb7 F freq \xc2\xb7 M morph \xc2\xb7 Option-click removes",
      theme::Text(9.f, theme::kTextDim));
  legend_->SetIgnoreMouse(true);
  graphics_.AttachControl(legend_);
  group_header_ = new GroupHeader(ig::IRECT(), model_, selected_group_);
  graphics_.AttachControl(group_header_);
  warning_ =
      new ig::ITextControl(ig::IRECT(), "", theme::Text(10.f, theme::kCaution));
  warning_->SetIgnoreMouse(true);
  graphics_.AttachControl(warning_);
  scope_ = new DetectorScope(ig::IRECT(), model_, history_, *graph_,
                             selected_group_);
  graphics_.AttachControl(scope_);
  sidechain_meter_ = new LevelMeter(ig::IRECT(), "SC", 2);
  sidechain_meter_->SetTooltip(
      "Sidechain input level. Click to reset the peak readout.");
  graphics_.AttachControl(sidechain_meter_);

  static const char* const kTitles[] = {"BALLISTICS", "TRIGGER", "SOURCE"};
  for (int i = 0; i < 3; ++i) {
    sections_[i].header = new SectionHeader(ig::IRECT(), kTitles[i],
                                            [this, i] { ToggleSection(i); });
    sections_[i].priority = 3 - i;
    graphics_.AttachControl(sections_[i].header);
  }
  auto add = [&](int section, GroupField f, const char* label,
                 const char* tooltip, int style = kField) {
    auto* c = new ParamField(ig::IRECT(), GroupParamId(0, f), label, 52.f,
                             static_cast<ParamField::Style>(style));
    c->SetTooltip(tooltip);
    graphics_.AttachControl(c);
    sections_[section].fields.push_back(static_cast<int>(group_fields_.size()));
    group_fields_.push_back({f, c});
  };
  add(0, GroupField::kAttack, "Attack",
      "How fast D rises toward the trigger's target (Vowel/Pitch: toward the "
      "tracked value).");
  add(0, GroupField::kHold, "Hold",
      "How long D holds before releasing once the trigger drops.");
  add(0, GroupField::kRelease, "Release",
      "How fast D falls back (Vowel/Pitch: toward a lower tracked value).");
  add(0, GroupField::kLink, "Link",
      "Stereo link: 100% detects L and R together, 0% separately.");
  add(1, GroupField::kThreshold, "Threshold",
      "Level (dBFS) the detector must cross at Alpha 0. Greyed at Alpha 1.");
  add(1, GroupField::kRise, "Rise",
      "dB above the recent average a transient must jump at Alpha 1. Greyed "
      "at Alpha 0.");
  add(1, GroupField::kKnee, "Knee",
      "Width (dB) of the ramp around the threshold; 0 = hard on/off.");
  add(1, GroupField::kMode, "Mode",
      "Above: fire when the trigger is above the threshold. Below: fire when "
      "it is below (e.g. lift lows when the mix gets quiet).");
  add(1, GroupField::kAlpha, "Alpha",
      "0 = level detection (compressor-like). 1 = transient detection: how "
      "far the fast level jumps above the RMS average, the same for soft and "
      "hard hits. In between blends the two.");
  add(1, GroupField::kRmsWindow, "RMS Win",
      "Time constant of the slow average used by transient detection.");
  add(2, GroupField::kInput, "Input",
      "What the detector listens to: the dry input, only the frequency "
      "regions of this group's bands (Band-Sum), or the external sidechain. "
      "Also: click a source line crossing in the routing map.");
  add(2, GroupField::kDetect, "Detect",
      "Level: D follows the trigger (compressor-like). Vowel: D follows the "
      "sung vowel of the detector input (D = Morph / 4; a Vowel band at OO "
      "with Dyn Target Morph and Dyn Range +4 follows it). Pitch: D follows "
      "the pitch (a band at 27.5 Hz with Dyn Target Freq and Dyn Range "
      "+8 oct follows f0; 55 Hz follows the 2nd harmonic). Tracking holds "
      "its last value through silence and unvoiced sounds; use a clean, "
      "single-voice sidechain.");
  add(2, GroupField::kScFilter, "SC Filter",
      "Optional filter on the detector input.");
  add(2, GroupField::kScHz, "SC Hz", "Sidechain filter frequency.");
  add(2, GroupField::kScQ, "SC Q", "Sidechain filter Q (BP, LP, HP).");
  add(2, GroupField::kScListen, "SC LISTEN",
      "Listen to the filtered detector input.", kToggle);
}

void Editor::Layout() {
  const ig::IRECT b = graphics_.GetBounds();
  layout_ = ComputeEditorLayout(b.W(), b.H(), drawer_open_);
  const EditorLayout& L = layout_;

  Place(ribbon_, L.show_top, ToIRECT(L.ribbon));
  ribbon_->SetShowPower(L.ribbon_power);
  Place(preset_bar_, L.show_top, ToIRECT(L.preset).GetVPadded(-2.f));
  preset_bar_->SetShowUndo(L.preset_undo);

  Place(graph_, true, ToIRECT(L.graph));
  Place(input_meter_, L.show_meters, ToIRECT(L.in_meter));
  Place(output_meter_, L.show_meters, ToIRECT(L.out_meter));
  Place(fader_, L.show_meters, ToIRECT(L.fader));
  const bool small =
      L.tier == LayoutTier::kSmall || L.tier == LayoutTier::kMinimum;
  Place(now_readout_, !small,
        ig::IRECT(L.graph.l + 6.f, L.graph.t + 4.f,
                  std::min(L.graph.r, L.graph.l + 260.f), L.graph.t + 18.f));
  Place(mini_label_, small,
        ig::IRECT(L.graph.l + 6.f, L.graph.t + 5.f,
                  std::min(L.graph.r - 6.f, L.graph.l + 340.f),
                  L.graph.t + 23.f));

  LayoutStrip();
  LayoutFooter();

  Place(drawer_tab_, L.show_drawer_tab, ToIRECT(L.drawer_tab));
  panel_bg_->SetSolid(L.panel_overlay);
  Place(panel_bg_, L.show_panel, ToIRECT(L.panel));
  routing_->SetBackground(L.panel_overlay ? theme::kPanel : kDockedPanel);

  const ParamValues v = model_.Values();
  LayoutCards(v, true);
  LayoutPanel(v, true);
  graphics_.SetAllControlsDirty();
}

void Editor::LayoutStrip() {
  const EditorLayout& L = layout_;
  const StripLayout& s = L.strip;
  const bool show = L.show_strip;
  Place(strip_panel_, show, ToIRECT(s.shape));
  const Rect& info = s.info;
  constexpr float kTop = 18.f;  // Under "Band n".
  Place(type_field_, show, Row(info, 0, kTop));
  const ig::IRECT routing_row = Row(info, 1, kTop);
  Place(tap_field_, show && !s.compact,
        routing_row.GetGridCell(0, 1, 2).GetHPadded(-1.f));
  Place(chan_field_, show && !s.compact,
        routing_row.GetGridCell(1, 1, 2).GetHPadded(-1.f));
  Place(option_field_, show, Row(info, s.compact ? 1 : 2, kTop));
  const ig::IRECT toggles = Row(info, s.compact ? 2 : 3, kTop);
  Place(bypass_field_, show, toggles.GetGridCell(0, 1, 2).GetHPadded(-1.f));
  Place(solo_field_, show, toggles.GetGridCell(1, 1, 2).GetHPadded(-1.f));
  for (int i = 0; i < 3; ++i) Place(dials_[i], show, ToIRECT(s.dials[i]));
}

void Editor::Connect(int slot) {
  const int band = std::max(0, model_.SelectedBand());
  const bool a = slot == 0;
  const int group_id =
      BandParamId(band, a ? BandField::kDynGroup : BandField::kDynBGroup);
  const int target_id =
      BandParamId(band, a ? BandField::kDynTarget : BandField::kDynBTarget);
  const int range_id =
      BandParamId(band, a ? BandField::kDynRange : BandField::kDynBRange);
  model_.BeginEdit(group_id);
  model_.BeginEdit(target_id);
  model_.BeginEdit(range_id);
  model_.SetEdit(group_id,
                 selected_group_ / static_cast<double>(kNumGroups - 1));
  model_.SetEdit(target_id, 0.0);
  model_.SetEdit(range_id,
                 SlotFromPhysical(DynRangeMapping(DynTarget::kGain), -6.0));
  model_.EndEdit(range_id);
  model_.EndEdit(target_id);
  model_.EndEdit(group_id);
}

void Editor::LayoutCards(const ParamValues& v, bool force) {
  const int band = std::max(0, model_.SelectedBand());
  const bool off =
      static_cast<BandType>(Int(v[BandParamId(band, BandField::kType)])) ==
      BandType::kOff;
  std::array<bool, 2> used{}, gain{};
  for (int slot = 0; slot < 2; ++slot) {
    const bool a = slot == 0;
    const auto target = static_cast<DynTarget>(Int(v[BandParamId(
        band, a ? BandField::kDynTarget : BandField::kDynBTarget)]));
    const double range = SlotToPhysical(
        DynRangeMapping(target),
        v[BandParamId(band, a ? BandField::kDynRange : BandField::kDynBRange)]);
    used[slot] = !off && range != 0.0;
    gain[slot] = used[slot] && target == DynTarget::kGain;
  }
  // Dyn Makeup is one parameter; it sits in the first card with a gain.
  const int makeup_slot = gain[0] ? 0 : gain[1] ? 1 : -1;
  const bool show = layout_.show_strip;
  const bool compact = layout_.strip.compact;
  std::array<int, 2> state{};
  for (int slot = 0; slot < 2; ++slot) {
    state[slot] = used[slot] | (compact << 1) | (show << 2) | (off << 3) |
                  ((makeup_slot == slot) << 4);
  }
  if (!force && state == card_state_) return;
  card_state_ = state;

  ParamField* makeup = cards_[0].makeup;
  Place(makeup, false);
  for (int slot = 0; slot < 2; ++slot) {
    Card& c = cards_[slot];
    const Rect& r = layout_.strip.cards[slot];
    const bool u = show && used[slot];
    Place(c.panel, show, ToIRECT(r));
    const Rect in = {r.l + 7.f, r.t + 7.f, r.r - 7.f, r.b - 7.f};
    Place(c.disconnect, u,
          ig::IRECT(in.r - 15.f, in.t - 2.f, in.r + 1.f, in.t + 14.f));
    const float y = in.t + 17.f;
    Place(c.connect, show && !used[slot],
          ig::IRECT(in.l, y, in.r, std::min(in.b, y + 40.f)));
    c.connect->SetDisabled(off);
    if (!compact) {
      Place(c.group, u, ig::IRECT(in.l, y, in.r, y + 20.f));
      Place(c.target, u, ig::IRECT(in.l, y + 24.f, in.r, y + 42.f));
      const float dial_t = y + 46.f;
      const float dial_h = std::max(0.f, in.b - dial_t + 4.f);
      const ig::IRECT dial(in.l, dial_t, in.l + std::min(56.f, dial_h + 10.f),
                           in.b + 4.f);
      Place(c.range_dial, u, dial);
      Place(c.target_menu, false);
      Place(c.range_field, false);
      if (u && makeup_slot == slot) {
        Place(makeup, true, ig::IRECT(dial.R + 4.f, in.b - kRow, in.r, in.b));
      }
    } else {
      Place(c.group, u, ig::IRECT(in.l, y, in.r, y + kRow));
      const ig::IRECT row(in.l, y + kPitch, in.r, y + kPitch + kRow);
      Place(c.target_menu, u, row.GetGridCell(0, 1, 2).GetHPadded(-1.f));
      Place(c.range_field, u, row.GetGridCell(1, 1, 2).GetHPadded(-1.f));
      Place(c.target, false);
      Place(c.range_dial, false);
    }
  }
}

void Editor::LayoutFooter() {
  const bool show = layout_.show_footer;
  const ig::IRECT r = ToIRECT(layout_.footer);
  Place(footer_panel_, show, r);
  const ig::IRECT row1 = r.GetPadded(-4.f).GetFromTop(kRow + 4.f);
  for (size_t i = 0; i < footer_row1_.size(); ++i) {
    Place(footer_row1_[i], show,
          row1.SubRectHorizontal(4, static_cast<int>(i)).GetHPadded(-3.f));
  }
  ig::IRECT row2 = r.GetPadded(-4.f).GetFromBottom(kRow).GetHPadded(-3.f);
  static const float kGapsBefore[] = {0.f, 6.f, 12.f, 6.f};
  for (size_t i = 0; i < footer_row2_.size(); ++i) {
    row2.ReduceFromLeft(kGapsBefore[i]);
    Place(footer_row2_[i], show, row2.ReduceFromLeft(footer_row2_widths_[i]));
  }
  Place(status_, show, row2);
}

void Editor::ToggleSection(int section) {
  Section& s = sections_[section];
  if (section == 1) {
    const bool level =
        static_cast<DetectMode>(Int(model_.Values()[GroupParamId(
            selected_group_, GroupField::kDetect)])) == DetectMode::kLevel;
    if (!level) return;  // Nothing to show in the tracking modes.
  }
  if (s.header->open()) {
    s.want_open = false;
  } else {
    s.want_open = true;
    s.priority = ++section_clock_;
  }
  LayoutPanel(model_.Values(), true);
}

void Editor::LayoutPanel(const ParamValues& v, bool force) {
  const EditorLayout& L = layout_;
  const int g = selected_group_;
  const bool level =
      static_cast<DetectMode>(Int(v[GroupParamId(g, GroupField::kDetect)])) ==
      DetectMode::kLevel;
  const bool warn = SidechainMissing(v, g);
  char key[160];
  std::snprintf(
      key, sizeof(key), "%d %.0f %.0f %.0f %.0f %d %d %d%d%d %d %d %d",
      L.show_panel, L.panel.l, L.panel.t, L.panel.r, L.panel.b, warn, level,
      sections_[0].want_open, sections_[1].want_open, sections_[2].want_open,
      sections_[0].priority, sections_[1].priority, sections_[2].priority);
  if (!force && panel_key_ == key) return;
  panel_key_ = key;

  std::vector<ig::IControl*> all = {routing_title_,  routing_, legend_,
                                    group_header_,   warning_, scope_,
                                    sidechain_meter_};
  if (!L.show_panel) {
    for (ig::IControl* c : all) Place(c, false);
    for (Section& s : sections_) Place(s.header, false);
    for (GroupFieldControl& f : group_fields_) Place(f.control, false);
    return;
  }

  std::vector<PanelSection> spec(3);
  spec[0] = {2, sections_[0].want_open, sections_[0].priority};
  spec[1] = {level ? 3 : 0, level && sections_[1].want_open,
             sections_[1].priority};
  spec[2] = {3, sections_[2].want_open, sections_[2].priority};
  const PanelLayout P = ComputePanelLayout(L.panel, spec, warn, true);

  Place(routing_title_, true, ToIRECT(P.title));
  Place(routing_, true, ToIRECT(P.map));
  Place(legend_, P.show_legend, ToIRECT(P.legend));
  Place(group_header_, true, ToIRECT(P.header));
  Place(warning_, P.show_warning, ToIRECT(P.warning));
  Place(scope_, P.show_scope, ToIRECT(P.scope));
  Place(sidechain_meter_, P.show_sc_meter, ToIRECT(P.sc_meter));
  for (int i = 0; i < 3; ++i) {
    const PanelLayout::Section& ps = P.sections[i];
    Section& s = sections_[i];
    Place(s.header, true, ToIRECT(ps.header));
    s.header->SetOpen(ps.open);
    for (size_t k = 0; k < s.fields.size(); ++k) {
      ParamField* c = group_fields_[s.fields[k]].control;
      const bool show = ps.open && k < ps.cells.size();
      Place(c, show, show ? ToIRECT(ps.cells[k]) : ig::IRECT());
    }
  }
}

void Editor::SelectGroup(int group) {
  selected_group_ = std::clamp(group, 0, kNumGroups - 1);
  UpdateDynamicsPanel(model_.Values());
}

void Editor::OnBandSelected(int band) {
  if (band < 0) return;
  const ParamValues v = model_.Values();
  SelectGroup(Int(v[BandParamId(band, BandField::kDynGroup)]));
  UpdateBandEditor(v);
}

std::string Editor::Display(int param_id) const {
  WDL_String s;
  if (const iplug::IParam* p = graphics_.GetDelegate()->GetParam(param_id)) {
    p->GetDisplayWithLabel(s);
  }
  return s.Get();
}

bool Editor::SidechainMissing(const ParamValues& v, int group) const {
  return static_cast<DetectorInput>(
             Int(v[GroupParamId(group, GroupField::kInput)])) ==
             DetectorInput::kExternalSc &&
         !sc_connected_;
}

void Editor::UpdateBandEditor(const ParamValues& v) {
  const int band = std::max(0, model_.SelectedBand());
  if (band != bound_band_) {
    for (const BandFieldControl& f : band_fields_) {
      f.control->Rebind(BandParamId(band, f.field));
    }
    bound_band_ = band;
  }
  const auto type =
      static_cast<BandType>(Int(v[BandParamId(band, BandField::kType)]));
  const bool off = type == BandType::kOff;
  const int group = Int(v[BandParamId(band, BandField::kDynGroup)]);
  const auto target =
      static_cast<DynTarget>(Int(v[BandParamId(band, BandField::kDynTarget)]));
  const SlotMapping range_mapping = DynRangeMapping(target);
  const double range =
      SlotToPhysical(range_mapping, v[BandParamId(band, BandField::kDynRange)]);
  const int group_b = Int(v[BandParamId(band, BandField::kDynBGroup)]);
  const auto target_b =
      static_cast<DynTarget>(Int(v[BandParamId(band, BandField::kDynBTarget)]));
  const SlotMapping range_b_mapping = DynRangeMapping(target_b);
  const double range_b = SlotToPhysical(
      range_b_mapping, v[BandParamId(band, BandField::kDynBRange)]);
  // Follow the selected band's group when it is reassigned.
  if (group != shown_band_group_) {
    shown_band_group_ = group;
    SelectGroup(group);
  }
  const ig::IColor color = theme::BandColor(band);
  for (const BandFieldControl& f : band_fields_) {
    bool enabled = !off || f.field == BandField::kType;
    std::optional<SlotMapping> m;
    switch (f.field) {
      case BandField::kX:
        m = SlotMappingFor(type, Slot::kX);
        break;
      case BandField::kY:
        m = SlotMappingFor(type, Slot::kY);
        break;
      case BandField::kZ:
        m = SlotMappingFor(type, Slot::kZ);
        break;
      case BandField::kDynMakeup:
        // Only meaningful against a dynamic gain on a gain-type band.
        enabled = enabled && HasDynMakeup(type) &&
                  (target == DynTarget::kGain || target_b == DynTarget::kGain);
        break;
      case BandField::kOption: {
        std::vector<std::string> labels = OptionLabels(type);
        enabled = enabled && !labels.empty();
        f.control->SetLabel(labels.empty() ? "OPTION"
                                           : Upper(OptionName(type)));
        f.control->SetMenuLabels(std::move(labels));
        break;
      }
      default:
        break;
    }
    if (f.field == BandField::kX || f.field == BandField::kY ||
        f.field == BandField::kZ) {
      enabled = enabled && m.has_value();
      static const char* const kDefaults[] = {"FREQ", "GAIN", "Q"};
      std::string label = m ? Upper(m->name)
                            : kDefaults[static_cast<int>(f.field) -
                                        static_cast<int>(BandField::kX)];
      if (label == "FORMANT GAIN") label = "GAIN";
      f.control->SetLabel(label);
    }
    f.control->SetColor(color);
    if (f.control->IsDisabled() == enabled) f.control->SetDisabled(!enabled);
    f.control->SetDetents(LevelDetentCount(v, BandParamId(band, f.field)));
  }

  // NOW: the band's current dynamic offset in its target's unit, over the
  // graph and on each card.
  const double global = v[GlobalParamId(GlobalField::kGlobalScale)] / 100.0;
  auto scale = [&](DynTarget t) {  // Global Scale applies to Gain only.
    return t == DynTarget::kGain ? global : 1.0;
  };
  const GroupControls live = model_.LiveControls();
  std::string now_a, now_b;
  if (!off && range != 0.0) {
    now_a = FormatSlotValue(range_mapping, live[group] * range * scale(target));
  }
  if (!off && range_b != 0.0) {
    now_b = FormatSlotValue(range_b_mapping,
                            live[group_b] * range_b * scale(target_b));
  }
  std::string now;
  if (!now_a.empty() || !now_b.empty()) {
    now = "B" + std::to_string(band + 1) + " NOW";
    if (!now_a.empty()) now += "  " + now_a;
    if (!now_b.empty()) now += "  B: " + now_b;
  }
  now_readout_->SetStr(now.c_str());
  const int hover_group = routing_->hover_group();
  cards_[0].panel->SetNow(now_a.empty() ? "" : "now " + now_a);
  cards_[1].panel->SetNow(now_b.empty() ? "" : "now " + now_b);
  cards_[0].panel->SetHighlight(!now_a.empty() && hover_group == group);
  cards_[1].panel->SetHighlight(!now_b.empty() && hover_group == group_b);
  for (Card& c : cards_) {
    c.connect->SetLabel(off ? "Band is off"
                            : "+ Connect to G" +
                                  std::to_string(selected_group_ + 1));
  }

  // The small tiers' label: number, type and its settings.
  std::string mini = std::to_string(band + 1) + " \xc2\xb7 " +
                     ParamTable()[BandParamId(band, BandField::kType)]
                         .enum_labels[static_cast<size_t>(type)];
  if (!off) {
    for (const auto [field, slot] : {std::pair{BandField::kX, Slot::kX},
                                     std::pair{BandField::kY, Slot::kY}}) {
      if (SlotMappingFor(type, slot)) {
        mini += "  " + Display(BandParamId(band, field));
      }
    }
  }
  mini_label_->Set(color, mini);
  LayoutCards(v, false);
}

void Editor::UpdateDynamicsPanel(const ParamValues& v) {
  const int g = selected_group_;
  if (g != bound_group_) {
    for (const GroupFieldControl& f : group_fields_) {
      f.control->Rebind(GroupParamId(g, f.field));
    }
    bound_group_ = g;
  }
  const double alpha = v[GroupParamId(g, GroupField::kAlpha)];
  const auto detect =
      static_cast<DetectMode>(Int(v[GroupParamId(g, GroupField::kDetect)]));
  const bool level = detect == DetectMode::kLevel;
  const auto sc_filter =
      static_cast<ScFilter>(Int(v[GroupParamId(g, GroupField::kScFilter)]));
  const bool has_sc_hz =
      sc_filter != ScFilter::kOff && sc_filter != ScFilter::kKWeight;
  for (const GroupFieldControl& f : group_fields_) {
    bool enabled = true;
    switch (f.field) {
      case GroupField::kThreshold:
        enabled = level && alpha < 1.0;  // Greyed at alpha = 1 (ABOUT.md 3.B).
        break;
      case GroupField::kRise:
        enabled = level && alpha > 0.0;  // Greyed at alpha = 0.
        break;
      case GroupField::kHold:
      case GroupField::kKnee:
      case GroupField::kMode:
      case GroupField::kAlpha:
      case GroupField::kRmsWindow:
        enabled = level;  // The tracking modes don't use the gain computer.
        break;
      case GroupField::kScHz:
        enabled = has_sc_hz;
        break;
      case GroupField::kScQ:
        enabled = has_sc_hz && sc_filter != ScFilter::kTilt;
        break;
      default:
        break;
    }
    if (f.control->IsDisabled() == enabled) f.control->SetDisabled(!enabled);
    f.control->SetDetents(LevelDetentCount(v, GroupParamId(g, f.field)));
  }

  warning_->SetStr(SidechainMissing(v, g)
                       ? "External SC is selected but no sidechain is "
                         "connected."
                       : "");
  // One-line summaries for folded sections.
  auto id = [g](GroupField f) { return GroupParamId(g, f); };
  sections_[0].header->SetSummary(Display(id(GroupField::kAttack)) + " / " +
                                  Display(id(GroupField::kRelease)));
  sections_[1].header->SetSummary(
      level ? Display(id(GroupField::kThreshold)) + " \xc2\xb7 knee " +
                  Display(id(GroupField::kKnee))
            : std::string("not used: D follows the tracked ") +
                  (detect == DetectMode::kVowel ? "vowel" : "pitch"));
  sections_[2].header->SetSummary(Display(id(GroupField::kInput)) +
                                  " \xc2\xb7 " +
                                  Display(id(GroupField::kDetect)));
  LayoutPanel(v, false);
}

void Editor::Idle(const EditorIdleData& data) {
  input_meter_->AddPeaks(data.peaks.data());
  sidechain_meter_->AddPeaks(data.peaks.data() + 2);
  output_meter_->AddPeaks(data.peaks.data() + 4);
  if (data.transport_started) {
    input_meter_->ResetClip();
    sidechain_meter_->ResetClip();
    output_meter_->ResetClip();
  }
  input_meter_->SetChannelActive(0, data.in_connected[0]);
  input_meter_->SetChannelActive(1, data.in_connected[1]);
  sidechain_meter_->SetChannelActive(0, data.in_connected[2]);
  sidechain_meter_->SetChannelActive(1, data.in_connected[3]);
  output_meter_->SetChannelActive(0, data.out_connected[0]);
  output_meter_->SetChannelActive(1, data.out_connected[1]);
  sc_connected_ = data.in_connected[2] || data.in_connected[3];
  routing_->SetSidechainConnected(sc_connected_);

  // Detector history and analyzer input from the audio thread.
  DetectorFrame frames[64];
  for (int n; (n = model_.PopDetectorFrames(frames, 64)) > 0;) {
    for (int i = 0; i < n; ++i) history_.Add(frames[i], model_.SampleRate());
  }
  for (int post = 0; post < 2; ++post) {
    SpectrumAnalyzer& analyzer = post ? post_analyzer_ : pre_analyzer_;
    for (int n; (n = model_.PopAnalyzerSamples(
                     post == 1, samples_.data(),
                     static_cast<int>(samples_.size()))) > 0;) {
      analyzer.AddSamples(samples_.data(), n);
    }
  }

  const ParamValues v = model_.Values();
  graph_->Refresh();

  const auto now = std::chrono::steady_clock::now();
  const double dt =
      std::min(0.25, std::chrono::duration<double>(now - last_idle_).count());
  last_idle_ = now;
  const auto mode = static_cast<EqGraph::AnalyzerMode>(analyzer_mode_);
  spectrum_pre_.clear();
  spectrum_post_.clear();
  if (!graph_->freqs().empty()) {
    if (mode == EqGraph::AnalyzerMode::kPre ||
        mode == EqGraph::AnalyzerMode::kPreAndPost) {
      pre_analyzer_.Compute(graph_->freqs(), model_.SampleRate(),
                            tilt_db_per_octave_, dt, spectrum_pre_);
    }
    if (mode == EqGraph::AnalyzerMode::kPost ||
        mode == EqGraph::AnalyzerMode::kPreAndPost) {
      post_analyzer_.Compute(graph_->freqs(), model_.SampleRate(),
                             tilt_db_per_octave_, dt, spectrum_post_);
    }
  }
  graph_->SetSpectrum(mode, spectrum_pre_, spectrum_post_);
  UpdateBandEditor(v);
  UpdateDynamicsPanel(v);

  char buf[160];
  std::snprintf(
      buf, sizeof(buf), "%.1f kHz   %d in / %d out   SC %d   latency %d smp",
      model_.SampleRate() / 1000.0, data.in_connected[0] + data.in_connected[1],
      data.out_connected[0] + data.out_connected[1],
      data.in_connected[2] + data.in_connected[3], data.latency_samples);
  if (status_text_ != buf) {
    status_text_ = buf;
    status_->SetStr(buf);
  }
}

}  // namespace eeq
