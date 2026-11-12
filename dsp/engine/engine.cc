#include "dsp/engine/engine.h"

#include <algorithm>
#include <cmath>

#include "dsp/common/denormals.h"
#include "dsp/common/math.h"

namespace eeq {

namespace {

// A bypassed band is treated as Off so that toggling bypass crossfades.
BandType EffectiveType(const BandParams& bp) {
  return bp.bypass ? BandType::kOff : bp.type;
}

}  // namespace

void Engine::Prepare(double sample_rate) {
  fs_ = sample_rate;
  const int max_delay =
      static_cast<int>(std::ceil(kMaxLookaheadMs * fs_ / 1000.0));
  for (DelayLine& d : delay_) d.Prepare(max_delay);
  for (BandSlot& s : slots_) s.Prepare(fs_);
  for (DynamicsGroup& g : groups_) g.Prepare(fs_);
  prepared_ = true;
  params_dirty_ = true;
  Reset();
}

void Engine::Reset() {
  for (DelayLine& d : delay_) d.Reset();
  bypass_state_ = params_.global.bypass;
  bypass_mix_.Snap(bypass_state_ ? 1.0 : 0.0);
  output_gain_.Snap(DbToGain(params_.global.output_gain_db));
  idle_bypass_ = false;
  for (DetectorFilter& f : band_detectors_) f.Reset();
  for (DynamicsGroup& g : groups_) g.Reset();
  ResetBands();
}

void Engine::ResetBands() {
  for (BandSlot& s : slots_) s.Reset();
  for (DetectorFilter& f : solo_filters_) f.Reset();
  control_phase_ = 0;
  ApplyStaticDesigns(true);
  ControlUpdate(true);
}

int Engine::BypassFadeSamples() const {
  return std::max(1,
                  static_cast<int>(std::lround(kBypassFadeMs * fs_ / 1000.0)));
}

void Engine::FadeInFromDry() {
  bypass_mix_.Snap(1.0);
  bypass_mix_.SetTarget(bypass_state_ ? 1.0 : 0.0, BypassFadeSamples());
}

void Engine::SetParams(const EngineParams& p) {
  params_ = p;
  params_dirty_ = true;
}

void Engine::ApplyStaticDesigns(bool snap_now) {
  const int ramp = snap_now ? 1 : kControlBlock;
  for (int g = 0; g < kNumGroups; ++g) groups_[g].SetParams(params_.groups[g]);
  for (int b = 0; b < kNumBands; ++b) {
    const BandParams& bp = params_.bands[b];
    const DetectorDesign d = DesignDetector(bp.type, bp.x, bp.z, fs_);
    band_detectors_[b].Apply(d, ramp);
    solo_filters_[b].Apply(d, ramp);
  }
  const SplitSide side = SplitSideOf(params_);
  split_xover_band_ = side.crossover;
  split_gain_.SetTarget(side.bands > 0 ? 1.0 : 0.0,
                        snap_now ? 1 : BypassFadeSamples());
  const double la =
      std::clamp(params_.global.lookahead_ms, 0.0, kMaxLookaheadMs);
  latency_ = static_cast<int>(std::lround(la * fs_ / 1000.0));
  output_gain_.SetTarget(DbToGain(params_.global.output_gain_db), ramp);
  if (params_.global.bypass != bypass_state_) {
    bypass_state_ = params_.global.bypass;
    bypass_mix_.SetTarget(bypass_state_ ? 1.0 : 0.0,
                          snap_now ? 1 : BypassFadeSamples());
  }
  params_dirty_ = false;
}

void Engine::ControlUpdate(bool snap_now) {
  const double scale = params_.global.global_scale;
  for (int b = 0; b < kNumBands; ++b) {
    const BandParams& bp = params_.bands[b];
    const bool ms_chan = bp.chan == Chan::kMid || bp.chan == Chan::kSide;
    const bool mono = params_.global.output_mode == OutputMode::kSplitLr;
    auto controls = [&](int group) {
      std::array<double, kNumChannels> dyn{};
      if (group < 0 || group >= kNumGroups) return dyn;
      const DynamicsGroup& g = groups_[group];
      dyn = {g.control(0), g.control(1)};
      if (ms_chan || mono) dyn[0] = dyn[1] = 0.5 * (dyn[0] + dyn[1]);
      return dyn;
    };
    const std::array<double, kNumChannels> dyn_a = controls(bp.dyn_group);
    const std::array<double, kNumChannels> dyn_b = controls(bp.dyn_b_group);
    std::array<ResolvedBand, kNumChannels> r{};
    for (int ch = 0; ch < kNumChannels; ++ch) {
      r[ch] = ResolveBand(bp, dyn_a[ch], dyn_b[ch], scale);
    }
    slots_[b].Update(EffectiveType(bp), r, snap_now);
  }
}

Engine::Frame Engine::DetectorInputFrame(int g, const Frame& dry,
                                         const Frame& sc) {
  switch (params_.groups[g].input) {
    case DetectorInput::kInternalDry:
      return dry;
    case DetectorInput::kExternalSc:
      return sc;
    case DetectorInput::kInternalBandSum: {
      Frame sum{};
      for (int b = 0; b < kNumBands; ++b) {
        const BandParams& bp = params_.bands[b];
        if (!SubscribesTo(bp, g) || bp.type == BandType::kOff) continue;
        for (int ch = 0; ch < kNumChannels; ++ch)
          sum[ch] += band_detectors_[b].Process(ch, dry[ch]);
      }
      return sum;
    }
  }
  return dry;
}

// Detectors read the undelayed signals.
void Engine::ProcessDetectors(const Frame& dry, const Frame& sc) {
  for (int g = 0; g < kNumGroups; ++g) {
    const Frame d = DetectorInputFrame(g, dry, sc);
    groups_[g].Process(d[0], d[1]);
  }
}

void Engine::ProcessChain(const Frame& chain_in, Frame& split, Frame& rest,
                          Frame& solo, bool& any_solo) {
  const OutputMode mode = params_.global.output_mode;
  const bool split_mode = mode != OutputMode::kStereo;
  // Split L/R plays a mono signal, so per-band Chan doesn't apply.
  const bool use_chan = mode != OutputMode::kSplitLr;
  const double split_gain = split_gain_.Next();
  Frame main = chain_in, xover_comp{0.0, 0.0};
  split = {0.0, 0.0};
  solo = {0.0, 0.0};
  any_solo = false;

  for (int b = 0; b < kNumBands; ++b) {
    if (split_mode && b == kSplitBands) {
      // Bands 1..kSplitBands have picked the split side; the rest of the
      // chain shapes what's left.
      split = {split_gain * main[0], split_gain * main[1]};
      if (split_xover_band_ >= 0) {
        main = xover_comp;
      } else {
        main = {chain_in[0] - split[0], chain_in[1] - split[1]};
      }
    }
    const BandParams& bp = params_.bands[b];
    BandSlot& slot = slots_[b];
    if (bp.solo && bp.type != BandType::kOff) {
      any_solo = true;
      for (int ch = 0; ch < kNumChannels; ++ch)
        solo[ch] += solo_filters_[b].Process(ch, main[ch]);
    }
    const BandType type = EffectiveType(bp);
    if (type == BandType::kOff && !slot.IsFading()) continue;

    // Choose the domain (L/R or M/S) and which components the band touches.
    const Chan chan = use_chan ? bp.chan : Chan::kStereo;
    const bool ms = chan == Chan::kMid || chan == Chan::kSide;
    Frame v = main;
    if (ms) v = {0.5 * (main[0] + main[1]), 0.5 * (main[0] - main[1])};
    const bool active[kNumChannels] = {
        chan == Chan::kStereo || chan == Chan::kLeft || chan == Chan::kMid,
        chan == Chan::kStereo || chan == Chan::kRight || chan == Chan::kSide,
    };

    // Components the band doesn't touch pass through, and have no
    // complement.
    Frame new_main = v, comp{0.0, 0.0};
    for (int ch = 0; ch < kNumChannels; ++ch) {
      if (!active[ch]) continue;
      const BandOutput o = slot.Process(ch, v[ch]);
      new_main[ch] = o.y;
      comp[ch] = o.comp;
    }
    slot.Advance();

    if (ms) {
      main = {new_main[0] + new_main[1], new_main[0] - new_main[1]};
      comp = {comp[0] + comp[1], comp[0] - comp[1]};
    } else {
      main = new_main;
    }
    if (b == split_xover_band_) xover_comp = comp;
  }
  rest = main;
}

Engine::Frame Engine::Encode(const Frame& split, const Frame& rest) const {
  switch (params_.global.output_mode) {
    case OutputMode::kStereo:
      return rest;
    case OutputMode::kSplitLr:
      if (params_.global.sum_split) {
        const double sum = split[0] + rest[0];
        return {sum, sum};
      }
      return {split[0], rest[0]};
    case OutputMode::kSplitSide:
      return split;
    case OutputMode::kResidual:
      return rest;
  }
  return rest;
}

void Engine::Process(const float* const* in, const float* const* sidechain,
                     float* const* out, int num_samples) {
  ScopedNoDenormals no_denormals;
  if (params_dirty_) ApplyStaticDesigns(false);

  const GlobalParams& gp = params_.global;
  // Split L/R plays the split side and the residual of a mono signal.
  const bool mono = gp.output_mode == OutputMode::kSplitLr;

  // Fully bypassed: pass the (delayed) input.  The detectors keep running so
  // dynamics pick up where they are on release; the band chain is skipped.
  if (bypass_state_ && bypass_mix_.settled()) {
    idle_bypass_ = true;
    for (int i = 0; i < num_samples; ++i) {
      const Frame x = {in[0][i], in[1][i]};
      const Frame sc =
          sidechain ? Frame{sidechain[0][i], sidechain[1][i]} : Frame{0.0, 0.0};
      Frame dry = x;
      if (mono) dry[0] = dry[1] = 0.5 * (x[0] + x[1]);
      ProcessDetectors(dry, sc);
      for (int ch = 0; ch < kNumChannels; ++ch)
        out[ch][i] = static_cast<float>(delay_[ch].Process(x[ch], latency_));
    }
    return;
  }
  // Leaving bypass: the band chain's state is stale, so restart it.  The
  // bypass crossfade (already started) hides the filters warming up.
  if (idle_bypass_) {
    idle_bypass_ = false;
    ResetBands();
  }

  int listen_group = -1;
  for (int g = 0; g < kNumGroups && listen_group < 0; ++g)
    if (params_.groups[g].sc_listen) listen_group = g;

  for (int i = 0; i < num_samples; ++i) {
    if (control_phase_ == 0) ControlUpdate(false);
    if (++control_phase_ == kControlBlock) control_phase_ = 0;

    const Frame x = {in[0][i], in[1][i]};
    const Frame sc =
        sidechain ? Frame{sidechain[0][i], sidechain[1][i]} : Frame{0.0, 0.0};
    Frame dry = x;
    if (mono) dry[0] = dry[1] = 0.5 * (x[0] + x[1]);

    ProcessDetectors(dry, sc);

    // Audio path is delayed by the lookahead.
    const Frame xd = {delay_[0].Process(x[0], latency_),
                      delay_[1].Process(x[1], latency_)};
    Frame chain_in = xd;
    if (mono) chain_in[0] = chain_in[1] = 0.5 * (xd[0] + xd[1]);

    Frame split, rest, solo;
    bool any_solo = false;
    ProcessChain(chain_in, split, rest, solo, any_solo);
    if (any_solo) split = rest = solo;

    Frame y = Encode(split, rest);
    if (gp.delta) {
      const Frame dry_enc = Encode({0.0, 0.0}, chain_in);
      y = {y[0] - dry_enc[0], y[1] - dry_enc[1]};
    }
    const double gain = output_gain_.Next();
    y = {y[0] * gain, y[1] * gain};
    if (listen_group >= 0)
      y = {groups_[listen_group].filtered(0),
           groups_[listen_group].filtered(1)};

    const double b = bypass_mix_.Next();
    out[0][i] = static_cast<float>((1.0 - b) * y[0] + b * xd[0]);
    out[1][i] = static_cast<float>((1.0 - b) * y[1] + b * xd[1]);
  }
}

}  // namespace eeq
