#include "plugin/ui/editor.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>

#include "IControls.h"
#include "model/slot_mapping.h"
#include "plugin/ui/band_ribbon.h"
#include "plugin/ui/choice_field.h"
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
constexpr float kBandLabelWidth = 58.f;

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

// One dynamics-group tab.  The strip along its bottom shows the group's live
// D (blue, grey while no active band uses the group) and the gain its bands
// apply (yellow, from the center: leftward for a cut, rightward for a boost).
class GroupTab final : public ig::IControl {
 public:
  static constexpr float kFullScaleDb = 24.f;

  GroupTab(const ig::IRECT& bounds, int group, Editor& editor,
           EditorModel& model, const EqGraph& graph)
      : IControl(bounds),
        group_(group),
        editor_(editor),
        model_(model),
        graph_(graph) {}
  void Draw(ig::IGraphics& g) override {
    const bool selected = editor_.selected_group() == group_;
    const bool active = graph_.group_active(group_);
    g.FillRoundRect(selected ? theme::kPanelRaised : theme::kPanel, mRECT, 3.f);
    g.DrawRoundRect(selected ? theme::kAccent : theme::kBorder, mRECT, 3.f);

    const ig::IRECT strip = mRECT.GetFromBottom(6.f).GetHPadded(-3.f);
    const ig::IRECT d_bar = strip.GetFromTop(3.f);
    const double d = model_.LiveControls()[group_];
    g.FillRect(active ? theme::kAccent.WithOpacity(0.6f)
                      : theme::kTextDim.WithOpacity(0.3f),
               d_bar.GetFromLeft(static_cast<float>(d) * d_bar.W()));
    if (active) {
      const ig::IRECT gain_bar = strip.GetFromBottom(2.f);
      const float u =
          std::clamp(graph_.group_gain_db(group_) / kFullScaleDb, -1.f, 1.f);
      const float mid = gain_bar.MW();
      const float x = mid + u * 0.5f * gain_bar.W();
      g.FillRect(theme::kGain, ig::IRECT(std::min(mid, x), gain_bar.T,
                                         std::max(mid, x), gain_bar.B));
      g.DrawLine(theme::kGain.WithOpacity(0.4f), mid, strip.T, mid, strip.B);
    }
    const std::string label = "G" + std::to_string(group_ + 1);
    g.DrawText(theme::Text(11.f, active ? theme::kText : theme::kTextDim,
                           ig::EAlign::Center),
               label.c_str(), mRECT.GetReducedFromBottom(3.f));
  }
  void OnMouseDown(float x, float y, const ig::IMouseMod& mod) override {
    editor_.SelectGroup(group_);
  }
  bool IsDirty() override { return true; }  // Live meters.

 private:
  int group_;
  Editor& editor_;
  EditorModel& model_;
  const EqGraph& graph_;
};

}  // namespace

Editor::Editor(ig::IGraphics& graphics, EditorModel& model)
    : graphics_(graphics), model_(model) {
  graphics_.AttachPanelBackground(theme::kBackground);
  graphics_.EnableTooltips(true);
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
  // The selected band's live dynamic offset, over the graph's corner.
  const ig::IRECT graph_rect = graph_->GetRECT();
  now_readout_ = new ig::ITextControl(
      ig::IRECT(graph_rect.L + 6.f, graph_rect.T + 4.f, graph_rect.L + 200.f,
                graph_rect.T + 18.f),
      "", theme::Text(11.f, theme::kTextDim, ig::EAlign::Near));
  now_readout_->SetIgnoreMouse(true);
  graphics_.AttachControl(now_readout_);

  BuildFooter(left.GetFromBottom(48.f));
  BuildDynamicsPanel(right);
  OnBandSelected(model_.SelectedBand());
}

void Editor::BuildBandEditor(const ig::IRECT& r) {
  auto cell = [&](int row, int col) {
    return ig::IRECT(r.L + col * r.W() / 4.f, r.T + row * (kRow + kGap),
                     r.L + (col + 1) * r.W() / 4.f,
                     r.T + row * (kRow + kGap) + kRow)
        .GetHPadded(-3.f);
  };
  auto add = [&](BandField f, const ig::IRECT& rect, const char* label,
                 ParamField::Style style = ParamField::Style::kField,
                 float label_width = kBandLabelWidth) {
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
  add(BandField::kDynRange, cell(1, 0), "DYN RANGE", ParamField::Style::kField,
      64.f);
  add(BandField::kDynGroup, cell(1, 1), "DYN GRP");
  add(BandField::kDynTarget, cell(1, 2), "DYN TGT");
  add(BandField::kDynMakeup, cell(1, 3), "MAKEUP");
  add(BandField::kTap, cell(2, 0), "TAP", ParamField::Style::kField, 40.f);
  add(BandField::kChan, cell(2, 1), "CHAN");
  add(BandField::kOption, cell(2, 2), "OPTION");
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
                     *this, model_, *graph_);
    tab->SetTooltip(
        "Dynamics groups. A band with a Dyn Range moves with its group (Dyn "
        "Grp) by D x Dyn Range. Blue: the group's output D (grey when no band "
        "uses the group). Yellow: the gain its bands apply right now, from "
        "the center (left = cut, right = boost, full width = 24 dB).");
    graphics_.AttachControl(tab);
    group_tabs_.push_back(tab);
  }

  // Detector scope under the tabs.
  const ig::IRECT scope = inner.GetReducedFromTop(26.f).GetFromTop(78.f);
  graphics_.AttachControl(
      new DetectorScope(scope, model_, history_, *graph_, selected_group_));

  // Fields on the left, the sidechain meter on the right.
  ig::IRECT column = inner.GetReducedFromTop(26.f + 78.f + 6.f);
  constexpr float kMeterWidth = 32.f;
  sidechain_meter_ = new LevelMeter(column.GetFromRight(kMeterWidth), "SC", 2);
  sidechain_meter_->SetTooltip(
      "Sidechain input level. Click to reset the peak readout.");
  graphics_.AttachControl(sidechain_meter_);
  column = column.GetReducedFromRight(kMeterWidth + kGap);

  float y = column.T;
  auto section = [&](const char* title) {
    graphics_.AttachControl(
        new SectionLabel(ig::IRECT(column.L, y, column.R, y + 11.f), title));
    y += 12.f;
  };
  auto add = [&](GroupField f, const char* label, const char* tooltip,
                 ParamField::Style style = ParamField::Style::kField) {
    const ig::IRECT rect(column.L, y, column.R, y + 18.f);
    auto* c = new ParamField(
        style == ParamField::Style::kToggle ? rect.GetFromLeft(90.f) : rect,
        GroupParamId(0, f), label, 66.f, style);
    c->SetTooltip(tooltip);
    graphics_.AttachControl(c);
    group_fields_.push_back({f, c});
    y += 20.f;
  };
  section("BALLISTICS");
  add(GroupField::kAttack, "Attack",
      "How fast D rises toward the trigger's target (Vowel/Pitch: toward the "
      "tracked value).");
  add(GroupField::kHold, "Hold",
      "How long D holds before releasing once the trigger drops.");
  add(GroupField::kRelease, "Release",
      "How fast D falls back (Vowel/Pitch: toward a lower tracked value).");
  section("TRIGGER");
  add(GroupField::kDetect, "Detect",
      "Level: D follows the trigger (compressor-like). Vowel: D follows the "
      "sung vowel of the detector input (D = Morph / 4; a Vowel band at OO "
      "with Dyn Target Morph and Dyn Range +4 follows it). Pitch: D follows "
      "the pitch (a band at 27.5 Hz with Dyn Target Freq and Dyn Range "
      "+8 oct follows f0; 55 Hz follows the 2nd harmonic). Tracking holds "
      "its last value through silence and unvoiced sounds; use a clean, "
      "single-voice sidechain.");
  add(GroupField::kThreshold, "Threshold",
      "Level (dBFS) the detector must cross at Alpha 0. Greyed at Alpha 1.");
  add(GroupField::kRise, "Rise",
      "dB above the recent average a transient must jump at Alpha 1. Greyed "
      "at Alpha 0.");
  add(GroupField::kKnee, "Knee",
      "Width (dB) of the ramp around the threshold; 0 = hard on/off.");
  add(GroupField::kMode, "Mode",
      "Above: fire when the trigger is above the threshold. Below: fire when "
      "it is below (e.g. lift lows when the mix gets quiet).");
  section("DETECTOR");
  add(GroupField::kAlpha, "Alpha",
      "0 = level detection (compressor-like). 1 = transient detection: how "
      "far the fast level jumps above the RMS average, the same for soft and "
      "hard hits. In between blends the two.");
  add(GroupField::kRmsWindow, "RMS Win",
      "Time constant of the slow average used by transient detection.");
  add(GroupField::kLink, "Link",
      "Stereo link: 100% detects L and R together, 0% separately.");
  section("SOURCE & SIDECHAIN");
  add(GroupField::kInput, "Input",
      "What the detector listens to: the dry input, only the frequency "
      "regions of this group's bands (Band-Sum), or the external sidechain.");
  add(GroupField::kScFilter, "SC Filter",
      "Optional filter on the detector input.");
  add(GroupField::kScHz, "SC Hz", "Sidechain filter frequency.");
  add(GroupField::kScQ, "SC Q", "Sidechain filter Q (BP, LP, HP).");
  add(GroupField::kScListen, "SC LISTEN",
      "Listen to the filtered detector input.", ParamField::Style::kToggle);
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

  ig::IRECT row2 = r.GetPadded(-4.f).GetFromBottom(18.f).GetHPadded(-3.f);
  graphics_.AttachControl(new ParamField(
      row2.ReduceFromLeft(36.f), GlobalParamId(GlobalField::kDelta), "\xce\x94",
      0.f, ParamField::Style::kToggle));
  row2.ReduceFromLeft(6.f);
  graphics_.AttachControl(new ParamField(
      row2.ReduceFromLeft(64.f), GlobalParamId(GlobalField::kBypass), "BYPASS",
      0.f, ParamField::Style::kToggle));
  row2.ReduceFromLeft(12.f);
  graphics_.AttachControl(new ChoiceField(
      row2.ReduceFromLeft(150.f), "ANALYZER", 56.f,
      {"Off", "Pre", "Post", "Pre+Post"}, analyzer_mode_, [this](int i) {
        analyzer_mode_ = i;
        pre_analyzer_.Clear();
        post_analyzer_.Clear();
      }));
  row2.ReduceFromLeft(6.f);
  static const double kTilts[] = {0.0, 3.0, 4.5, 6.0};
  graphics_.AttachControl(
      new ChoiceField(row2.ReduceFromLeft(96.f), "TILT", 28.f,
                      {"0 dB", "3 dB", "4.5 dB", "6 dB"}, 2,
                      [this](int i) { tilt_db_per_octave_ = kTilts[i]; }));
  status_ = new ig::ITextControl(
      row2, "", theme::Text(9.f, theme::kTextDim, ig::EAlign::Far));
  graphics_.AttachControl(status_);
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
  // Follow the selected band's group when it is reassigned.
  if (group != shown_band_group_) {
    shown_band_group_ = group;
    SelectGroup(group);
  }
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
        enabled = enabled && target == DynTarget::kGain && HasDynMakeup(type);
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
    if (f.control->IsDisabled() == enabled) f.control->SetDisabled(!enabled);
  }

  // NOW: the band's current dynamic offset in its target's unit.
  std::string now;
  if (range != 0.0 && !off) {
    const double scale = v[GlobalParamId(GlobalField::kGlobalScale)] / 100.0;
    const double offset = model_.LiveControls()[group] * range * scale;
    now = "B" + std::to_string(band + 1) + " NOW  " +
          FormatSlotValue(range_mapping, offset);
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
  const bool level =
      static_cast<DetectMode>(Int(v[GroupParamId(g, GroupField::kDetect)])) ==
      DetectMode::kLevel;
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
  }
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
