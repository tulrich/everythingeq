#include "plugin/ui/editor.h"

#include <cctype>
#include <cmath>
#include <functional>
#include <optional>

#include "IControls.h"
#include "model/slot_mapping.h"
#include "plugin/ui/band_ribbon.h"
#include "plugin/ui/eq_graph.h"
#include "plugin/ui/level_meter.h"
#include "plugin/ui/param_field.h"
#include "plugin/ui/theme.h"

namespace eeq {
namespace {

namespace ig = iplug::igraphics;

constexpr float kMargin = 8.f;
constexpr float kRow = 24.f;
constexpr float kGap = 4.f;

int Int(double v) { return static_cast<int>(std::lround(v)); }

std::string Upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(c));
  return s;
}

// A small panel background with an optional section title.
class Panel final : public ig::IControl {
 public:
  Panel(const ig::IRECT& bounds, const char* title = "")
      : IControl(bounds), title_(title) {
    mIgnoreMouse = true;
  }
  void Draw(ig::IGraphics& g) override {
    g.FillRoundRect(theme::kPanel.WithOpacity(0.6f), mRECT, 4.f);
    if (*title_) {
      g.DrawText(theme::Text(10.f, theme::kTextDim), title_,
                 mRECT.GetFromTop(16.f).GetHPadded(-6.f));
    }
  }

 private:
  const char* title_;
};

class SectionLabel final : public ig::IControl {
 public:
  SectionLabel(const ig::IRECT& bounds, const char* text)
      : IControl(bounds), text_(text) {
    mIgnoreMouse = true;
  }
  void Draw(ig::IGraphics& g) override {
    g.DrawText(theme::Text(9.f, theme::kAccent), text_, mRECT);
    g.DrawLine(theme::kBorder, mRECT.L, mRECT.B - 1.f, mRECT.R, mRECT.B - 1.f);
  }

 private:
  const char* text_;
};

// One dynamics-group tab, with a bar showing the group's live D.
class GroupTab final : public ig::IControl {
 public:
  GroupTab(const ig::IRECT& bounds, int group, Editor& editor,
           EditorModel& model)
      : IControl(bounds), group_(group), editor_(editor), model_(model) {}
  void Draw(ig::IGraphics& g) override {
    const bool selected = editor_.selected_group() == group_;
    g.FillRoundRect(selected ? theme::kPanelRaised : theme::kPanel, mRECT, 3.f);
    g.DrawRoundRect(selected ? theme::kAccent : theme::kBorder, mRECT, 3.f);
    const double d = model_.LiveControls()[group_];
    g.FillRect(theme::kAccent.WithOpacity(0.6f),
               mRECT.GetFromBottom(3.f).GetHPadded(-3.f).GetFromLeft(
                   static_cast<float>(d) * (mRECT.W() - 6.f)));
    const std::string label = "G" + std::to_string(group_ + 1);
    g.DrawText(theme::Text(11.f, theme::kText, ig::EAlign::Center),
               label.c_str(), mRECT);
  }
  void OnMouseDown(float x, float y, const ig::IMouseMod& mod) override {
    editor_.SelectGroup(group_);
  }
  bool IsDirty() override { return true; }  // Live D bar.

 private:
  int group_;
  Editor& editor_;
  EditorModel& model_;
};

}  // namespace

Editor::Editor(ig::IGraphics& graphics, EditorModel& model)
    : graphics_(graphics), model_(model) {
  graphics_.AttachPanelBackground(theme::kBackground);
  const ig::IRECT b = graphics_.GetBounds().GetPadded(-kMargin);

  // Top band ribbon.
  ribbon_ = new BandRibbon(b.GetFromTop(26.f), model_);
  graphics_.AttachControl(ribbon_);

  const ig::IRECT body = b.GetReducedFromTop(34.f);
  const ig::IRECT left = body.GetFromLeft(674.f);
  const ig::IRECT right = body.GetReducedFromLeft(682.f);

  BuildBandEditor(left.GetFromTop(3 * kRow + 2 * kGap));

  // Graph with the input meter at its left edge and the output meter at its
  // right (ABOUT.md section 4).
  const ig::IRECT graph_area =
      left.GetReducedFromTop(3 * kRow + 3 * kGap).GetReducedFromBottom(56.f);
  constexpr float kMeterWidth = 32.f;
  input_meter_ = new LevelMeter(graph_area.GetFromLeft(kMeterWidth), "In", 2);
  output_meter_ =
      new LevelMeter(graph_area.GetFromRight(kMeterWidth), "Out", 2);
  graph_ = new EqGraph(graph_area.GetHPadded(-(kMeterWidth + kGap)), model_);
  graphics_.AttachControl(graph_);
  graphics_.AttachControl(input_meter_);
  graphics_.AttachControl(output_meter_);

  BuildFooter(left.GetFromBottom(48.f));
  BuildDynamicsPanel(right);
  OnBandSelected(model_.SelectedBand());
}

void Editor::BuildBandEditor(const ig::IRECT& r) {
  constexpr float kLabel = 58.f;
  auto cell = [&](int row, int col) {
    return ig::IRECT(r.L + col * r.W() / 4.f, r.T + row * (kRow + kGap),
                     r.L + (col + 1) * r.W() / 4.f,
                     r.T + row * (kRow + kGap) + kRow)
        .GetHPadded(-3.f);
  };
  auto add = [&](BandField f, const ig::IRECT& rect, const char* label,
                 ParamField::Style style = ParamField::Style::kField,
                 float label_width = kLabel) {
    auto* c =
        new ParamField(rect, BandParamId(0, f), label, label_width, style);
    graphics_.AttachControl(c);
    band_fields_.push_back({f, c});
  };
  // Row 1: what the band is.  Row 2: dynamics.  Row 3: routing.
  add(BandField::kType, cell(0, 0), "TYPE", ParamField::Style::kField, 40.f);
  add(BandField::kX, cell(0, 1), "FREQ");
  add(BandField::kY, cell(0, 2), "GAIN");
  add(BandField::kZ, cell(0, 3), "Q");
  add(BandField::kDynGroup, cell(1, 0), "DYN GRP");
  add(BandField::kDynTarget, cell(1, 1), "DYN TGT");
  add(BandField::kDynRange, cell(1, 2), "DYN RANGE", ParamField::Style::kField,
      64.f);
  now_readout_ = new ig::ITextControl(cell(1, 3), "NOW  --",
                                      theme::Text(11.f, theme::kTextDim));
  graphics_.AttachControl(now_readout_);
  add(BandField::kTap, cell(2, 0), "TAP", ParamField::Style::kField, 40.f);
  add(BandField::kChan, cell(2, 1), "CHAN");
  add(BandField::kOption, cell(2, 2), "WEIGHT");
  const ig::IRECT toggles = cell(2, 3);
  add(BandField::kBypass, toggles.GetGridCell(0, 1, 2).GetHPadded(-2.f), "BYP",
      ParamField::Style::kToggle);
  add(BandField::kSolo, toggles.GetGridCell(1, 1, 2).GetHPadded(-2.f), "SOLO",
      ParamField::Style::kToggle);
}

void Editor::BuildDynamicsPanel(const ig::IRECT& r) {
  graphics_.AttachControl(new Panel(r, "DYNAMICS GROUP"));
  const ig::IRECT inner = r.GetPadded(-6.f).GetReducedFromTop(14.f);
  const ig::IRECT tabs = inner.GetFromTop(22.f);
  for (int g = 0; g < kNumGroups; ++g) {
    auto* tab =
        new GroupTab(tabs.SubRectHorizontal(kNumGroups, g).GetHPadded(-2.f), g,
                     *this, model_);
    graphics_.AttachControl(tab);
    group_tabs_.push_back(tab);
  }

  // Fields on the left, the sidechain meter on the right.
  ig::IRECT column = inner.GetReducedFromTop(28.f);
  constexpr float kMeterWidth = 32.f;
  sidechain_meter_ = new LevelMeter(
      column.GetFromRight(kMeterWidth).GetReducedFromBottom(22.f), "SC", 2);
  graphics_.AttachControl(sidechain_meter_);
  column = column.GetReducedFromRight(kMeterWidth + kGap);

  float y = column.T;
  auto section = [&](const char* title) {
    graphics_.AttachControl(
        new SectionLabel(ig::IRECT(column.L, y, column.R, y + 14.f), title));
    y += 16.f;
  };
  auto add = [&](GroupField f, const char* label,
                 ParamField::Style style = ParamField::Style::kField) {
    const ig::IRECT rect(column.L, y, column.R, y + 21.f);
    auto* c = new ParamField(
        style == ParamField::Style::kToggle ? rect.GetFromLeft(90.f) : rect,
        GroupParamId(0, f), label, 74.f, style);
    graphics_.AttachControl(c);
    group_fields_.push_back({f, c});
    y += 24.f;
  };
  section("BALLISTICS");
  add(GroupField::kAttack, "Attack");
  add(GroupField::kHold, "Hold");
  add(GroupField::kRelease, "Release");
  section("TRIGGER & THRESHOLD");
  add(GroupField::kThreshold, "Threshold");
  add(GroupField::kRise, "Rise");
  add(GroupField::kKnee, "Knee");
  add(GroupField::kMode, "Mode");
  section("DETECTOR & CONTINUUM");
  add(GroupField::kAlpha, "Alpha");
  add(GroupField::kRmsWindow, "RMS Win");
  add(GroupField::kLink, "Link");
  section("SOURCE & SIDECHAIN");
  add(GroupField::kInput, "Input");
  add(GroupField::kScFilter, "SC Filter");
  add(GroupField::kScHz, "SC Hz");
  add(GroupField::kScQ, "SC Q");
  add(GroupField::kScListen, "SC LISTEN", ParamField::Style::kToggle);

  group_out_readout_ = new ig::ITextControl(
      ig::IRECT(column.L, inner.B - 20.f, inner.R, inner.B), "GRP OUT  0 %",
      theme::Text(11.f, theme::kText));
  graphics_.AttachControl(group_out_readout_);
}

void Editor::BuildFooter(const ig::IRECT& r) {
  graphics_.AttachControl(new Panel(r));
  const ig::IRECT row1 = r.GetPadded(-4.f).GetFromTop(kRow - 2.f);
  auto field = [&](int col, GlobalField f, const char* label,
                   float label_width) {
    graphics_.AttachControl(
        new ParamField(row1.SubRectHorizontal(4, col).GetHPadded(-3.f),
                       GlobalParamId(f), label, label_width));
  };
  field(0, GlobalField::kOutputMode, "OUTPUT", 46.f);
  field(1, GlobalField::kLookahead, "LOOKAHEAD", 64.f);
  field(2, GlobalField::kGlobalScale, "SCALE", 40.f);
  field(3, GlobalField::kOutputGain, "OUT GAIN", 56.f);

  const ig::IRECT row2 = r.GetPadded(-4.f).GetFromBottom(18.f).GetHPadded(-3.f);
  graphics_.AttachControl(
      new ParamField(row2.GetFromLeft(40.f), GlobalParamId(GlobalField::kDelta),
                     "\xce\x94", 0.f, ParamField::Style::kToggle));
  graphics_.AttachControl(
      new ParamField(row2.GetReducedFromLeft(46.f).GetFromLeft(70.f),
                     GlobalParamId(GlobalField::kBypass), "BYPASS", 0.f,
                     ParamField::Style::kToggle));
  status_ =
      new ig::ITextControl(row2.GetReducedFromLeft(126.f), "",
                           theme::Text(10.f, theme::kTextDim, ig::EAlign::Far));
  graphics_.AttachControl(status_);
}

void Editor::SelectGroup(int group) {
  selected_group_ = std::clamp(group, 0, kNumGroups - 1);
  UpdateDynamicsPanel(model_.Values());
}

void Editor::OnBandSelected(int band) {
  if (band < 0) return;
  const ParamValues v = model_.Values();
  const int group = Int(v[BandParamId(band, BandField::kDynGroup)]) - 1;
  if (group >= 0) SelectGroup(group);
  UpdateBandEditor(v);
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
  const bool dynamic = Int(v[BandParamId(band, BandField::kDynGroup)]) > 0;
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
      case BandField::kDynTarget:
      case BandField::kDynRange:
        enabled = enabled && dynamic;
        break;
      case BandField::kOption:
        enabled = type == BandType::kVowel;
        break;
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
    if (f.control->IsDisabled() == enabled) f.control->SetDisabled(!enabled);
  }

  // NOW: the band's current dynamic offset in its target's unit.
  std::string now = "NOW  --";
  if (dynamic && !off) {
    const auto target = static_cast<DynTarget>(
        Int(v[BandParamId(band, BandField::kDynTarget)]));
    const SlotMapping m = DynRangeMapping(target);
    const double range =
        SlotToPhysical(m, v[BandParamId(band, BandField::kDynRange)]);
    const double scale = v[GlobalParamId(GlobalField::kGlobalScale)] / 100.0;
    const int group = Int(v[BandParamId(band, BandField::kDynGroup)]) - 1;
    const double offset = model_.LiveControls()[group] * range * scale;
    now = "NOW  " + FormatSlotValue(m, offset);
  }
  now_readout_->SetStr(now.c_str());
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
  const auto sc_filter =
      static_cast<ScFilter>(Int(v[GroupParamId(g, GroupField::kScFilter)]));
  const bool has_sc_hz =
      sc_filter != ScFilter::kOff && sc_filter != ScFilter::kKWeight;
  for (const GroupFieldControl& f : group_fields_) {
    bool enabled = true;
    switch (f.field) {
      case GroupField::kThreshold:
        enabled = alpha < 1.0;  // Greyed at alpha = 1 (ABOUT.md 3.B).
        break;
      case GroupField::kRise:
        enabled = alpha > 0.0;  // Greyed at alpha = 0.
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
  }
  char buf[48];
  std::snprintf(buf, sizeof(buf), "GRP OUT  %d %%",
                Int(model_.LiveControls()[g] * 100.0));
  group_out_readout_->SetStr(buf);
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

  const ParamValues v = model_.Values();
  graph_->Refresh();
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
