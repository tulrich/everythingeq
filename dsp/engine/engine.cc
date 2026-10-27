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
  ResetProcessing();
}

void Engine::ResetProcessing() {
  for (BandSlot& s : slots_) s.Reset();
  for (DetectorFilter& f : band_detectors_) f.Reset();
  for (DetectorFilter& f : solo_filters_) f.Reset();
  for (DynamicsGroup& g : groups_) g.Reset();
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
    std::array<double, kNumChannels> dyn{};
    if (bp.dyn_group >= 0 && bp.dyn_group < kNumGroups) {
      const DynamicsGroup& g = groups_[bp.dyn_group];
      dyn = {g.control(0), g.control(1)};
      const bool ms_chan = bp.chan == Chan::kMid || bp.chan == Chan::kSide;
      const bool mono = params_.global.output_mode != OutputMode::kStereo;
      if (ms_chan || mono) dyn[0] = dyn[1] = 0.5 * (dyn[0] + dyn[1]);
    }
    std::array<ResolvedBand, kNumChannels> r{};
    for (int ch = 0; ch < kNumChannels; ++ch) {
      r[ch] = ResolveBand(bp, dyn[ch], scale);
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
        if (bp.dyn_group != g || bp.type == BandType::kOff) continue;
        for (int ch = 0; ch < kNumChannels; ++ch)
          sum[ch] += band_detectors_[b].Process(ch, dry[ch]);
      }
      return sum;
    }
  }
  return dry;
}

void Engine::ProcessChain(const Frame& chain_in, Frame& main, Frame& aux,
                          Frame& solo, bool& any_solo) {
  main = chain_in;
  aux = {0.0, 0.0};
  solo = {0.0, 0.0};
  any_solo = false;
  const bool stereo = params_.global.output_mode == OutputMode::kStereo;

  for (int b = 0; b < kNumBands; ++b) {
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
    const Chan chan = stereo ? bp.chan : Chan::kStereo;
    const bool ms = chan == Chan::kMid || chan == Chan::kSide;
    Frame v = main;
    if (ms) v = {0.5 * (main[0] + main[1]), 0.5 * (main[0] - main[1])};
    const bool active[kNumChannels] = {
        chan == Chan::kStereo || chan == Chan::kLeft || chan == Chan::kMid,
        chan == Chan::kStereo || chan == Chan::kRight || chan == Chan::kSide,
    };

    Frame new_main = v;
    Frame aux_add{0.0, 0.0};
    for (int ch = 0; ch < kNumChannels; ++ch) {
      if (!active[ch]) continue;
      const BandOutput o = slot.Process(ch, v[ch]);
      if (bp.tap == Tap::kMain) {
        new_main[ch] = o.y;
      } else if (IsCrossoverType(type)) {
        aux_add[ch] = o.y;
        new_main[ch] = o.comp;
      } else if (IsFilterType(type)) {
        aux_add[ch] = o.y;
        new_main[ch] = v[ch] - o.y;
      } else {
        aux_add[ch] = o.y - v[ch];
      }
    }
    slot.Advance();

    if (ms) {
      main = {new_main[0] + new_main[1], new_main[0] - new_main[1]};
      aux[0] += aux_add[0] + aux_add[1];
      aux[1] += aux_add[0] - aux_add[1];
    } else {
      main = new_main;
      aux[0] += aux_add[0];
      aux[1] += aux_add[1];
    }
  }
}

Engine::Frame Engine::Encode(const Frame& main, const Frame& aux) const {
  switch (params_.global.output_mode) {
    case OutputMode::kStereo:
      return {main[0] + aux[0], main[1] + aux[1]};
    case OutputMode::kSplitLr:
      return {main[0], aux[0]};
    case OutputMode::kSplitMs:
      return {0.5 * (main[0] + aux[0]), 0.5 * (main[0] - aux[0])};
  }
  return main;
}

void Engine::Process(const float* const* in, const float* const* sidechain,
                     float* const* out, int num_samples) {
  ScopedNoDenormals no_denormals;
  if (params_dirty_) ApplyStaticDesigns(false);

  // Fully bypassed: pass the (delayed) input and skip everything else.
  if (bypass_state_ && bypass_mix_.settled()) {
    idle_bypass_ = true;
    for (int i = 0; i < num_samples; ++i) {
      for (int ch = 0; ch < kNumChannels; ++ch)
        out[ch][i] =
            static_cast<float>(delay_[ch].Process(in[ch][i], latency_));
    }
    return;
  }
  // Leaving bypass: the skipped state is stale, so restart it.  The bypass
  // crossfade (already started) hides the filters warming up.
  if (idle_bypass_) {
    idle_bypass_ = false;
    ResetProcessing();
  }

  const GlobalParams& gp = params_.global;
  const bool split = gp.output_mode != OutputMode::kStereo;
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
    if (split) dry[0] = dry[1] = 0.5 * (x[0] + x[1]);

    // Detectors read the undelayed signals.
    for (int g = 0; g < kNumGroups; ++g) {
      const Frame d = DetectorInputFrame(g, dry, sc);
      groups_[g].Process(d[0], d[1]);
    }

    // Audio path is delayed by the lookahead.
    const Frame xd = {delay_[0].Process(x[0], latency_),
                      delay_[1].Process(x[1], latency_)};
    Frame chain_in = xd;
    if (split) chain_in[0] = chain_in[1] = 0.5 * (xd[0] + xd[1]);

    Frame main, aux, solo;
    bool any_solo = false;
    ProcessChain(chain_in, main, aux, solo, any_solo);
    if (any_solo) main = solo, aux = {0.0, 0.0};

    Frame y = Encode(main, aux);
    if (gp.delta) {
      const Frame dry_enc = Encode(chain_in, {0.0, 0.0});
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
