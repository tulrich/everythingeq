#include "engine/Engine.h"

#include <algorithm>
#include <cmath>

#include "common/Denormals.h"
#include "common/Math.h"

namespace eeq {

namespace {

bool hasGain(BandType t) {
    switch (t) {
        case BandType::Peak:
        case BandType::LowShelf:
        case BandType::HighShelf:
        case BandType::BP:
        case BandType::Tilt:
        case BandType::FletcherMunson:
        case BandType::Vowel:
            return true;
        default:
            return false;
    }
}

// A bypassed band is treated as Off so that toggling bypass crossfades.
BandType effectiveType(const BandParams& bp) { return bp.bypass ? BandType::Off : bp.type; }

bool hasFreq(BandType t) {
    switch (t) {
        case BandType::Off:
        case BandType::FletcherMunson:
        case BandType::Vowel:
        case BandType::KWeight:
            return false;
        default:
            return true;
    }
}

}  // namespace

void Engine::prepare(double sampleRate) {
    fs_ = sampleRate;
    const int maxDelay = static_cast<int>(std::ceil(kMaxLookaheadMs * fs_ / 1000.0));
    for (DelayLine& d : delay_) d.prepare(maxDelay);
    for (BandSlot& s : slots_) s.prepare(fs_);
    for (DynamicsGroup& g : groups_) g.prepare(fs_);
    prepared_ = true;
    paramsDirty_ = true;
    reset();
}

void Engine::reset() {
    for (BandSlot& s : slots_) s.reset();
    for (DetectorFilter& f : bandDetectors_) f.reset();
    for (DetectorFilter& f : soloFilters_) f.reset();
    for (DynamicsGroup& g : groups_) g.reset();
    for (DelayLine& d : delay_) d.reset();
    controlPhase_ = 0;
    bypassState_ = params_.global.bypass;
    bypassMix_.snap(bypassState_ ? 1.0 : 0.0);
    outputGain_.snap(dbToGain(params_.global.outputGainDb));
    applyStaticDesigns(true);
    controlUpdate(true);
}

void Engine::setParams(const EngineParams& p) {
    params_ = p;
    paramsDirty_ = true;
}

void Engine::applyStaticDesigns(bool snapNow) {
    const int ramp = snapNow ? 1 : kControlBlock;
    for (int g = 0; g < kNumGroups; ++g) groups_[g].setParams(params_.groups[g]);
    for (int b = 0; b < kNumBands; ++b) {
        const BandParams& bp = params_.bands[b];
        const DetectorDesign d = designDetector(bp.type, bp.x, bp.z, fs_);
        bandDetectors_[b].apply(d, ramp);
        soloFilters_[b].apply(d, ramp);
    }
    const double la = std::clamp(params_.global.lookaheadMs, 0.0, kMaxLookaheadMs);
    latency_ = static_cast<int>(std::lround(la * fs_ / 1000.0));
    outputGain_.setTarget(dbToGain(params_.global.outputGainDb), ramp);
    if (params_.global.bypass != bypassState_) {
        bypassState_ = params_.global.bypass;
        bypassMix_.setTarget(bypassState_ ? 1.0 : 0.0, snapNow ? 1 : kBypassFadeSamples);
    }
    paramsDirty_ = false;
}

void Engine::controlUpdate(bool snapNow) {
    const double scale = params_.global.globalScale;
    for (int b = 0; b < kNumBands; ++b) {
        const BandParams& bp = params_.bands[b];
        std::array<double, kNumChannels> dyn{};
        if (bp.dynGroup >= 0 && bp.dynGroup < kNumGroups) {
            const DynamicsGroup& g = groups_[bp.dynGroup];
            dyn = {g.control(0), g.control(1)};
            const bool msChan = bp.chan == Chan::Mid || bp.chan == Chan::Side;
            const bool mono = params_.global.outputMode != OutputMode::Stereo;
            if (msChan || mono) dyn[0] = dyn[1] = 0.5 * (dyn[0] + dyn[1]);
        }
        std::array<ResolvedBand, kNumChannels> r{};
        for (int ch = 0; ch < kNumChannels; ++ch) {
            ResolvedBand& rb = r[ch];
            rb.x = bp.x, rb.y = bp.y, rb.z = bp.z, rb.option = bp.option;
            const double mod = dyn[ch] * bp.dynRange * scale;
            if (mod == 0.0) continue;
            switch (bp.dynTarget) {
                case DynTarget::Gain:
                    if (hasGain(bp.type)) rb.y += mod;
                    break;
                case DynTarget::Freq:
                    if (hasFreq(bp.type)) rb.x *= std::exp2(mod);
                    else if (bp.type == BandType::Vowel) rb.z *= std::exp2(mod);
                    break;
                case DynTarget::Morph:
                    if (bp.type == BandType::Vowel) rb.x += mod;
                    break;
            }
        }
        slots_[b].update(effectiveType(bp), r, snapNow);
    }
}

Engine::Frame Engine::detectorInput(int g, const Frame& dry, const Frame& sc) {
    switch (params_.groups[g].input) {
        case DetectorInput::InternalDry:
            return dry;
        case DetectorInput::ExternalSC:
            return sc;
        case DetectorInput::InternalBandSum: {
            Frame sum{};
            for (int b = 0; b < kNumBands; ++b) {
                const BandParams& bp = params_.bands[b];
                if (bp.dynGroup != g || bp.type == BandType::Off) continue;
                for (int ch = 0; ch < kNumChannels; ++ch) sum[ch] += bandDetectors_[b].process(ch, dry[ch]);
            }
            return sum;
        }
    }
    return dry;
}

void Engine::processChain(const Frame& chainIn, Frame& main, Frame& aux, Frame& solo, bool& anySolo) {
    main = chainIn;
    aux = {0.0, 0.0};
    solo = {0.0, 0.0};
    anySolo = false;
    const bool stereo = params_.global.outputMode == OutputMode::Stereo;

    for (int b = 0; b < kNumBands; ++b) {
        const BandParams& bp = params_.bands[b];
        BandSlot& slot = slots_[b];
        if (bp.solo && bp.type != BandType::Off) {
            anySolo = true;
            for (int ch = 0; ch < kNumChannels; ++ch) solo[ch] += soloFilters_[b].process(ch, main[ch]);
        }
        const BandType type = effectiveType(bp);
        if (type == BandType::Off && !slot.fading()) continue;

        // Choose the domain (L/R or M/S) and which components the band touches.
        const Chan chan = stereo ? bp.chan : Chan::Stereo;
        const bool ms = chan == Chan::Mid || chan == Chan::Side;
        Frame v = main;
        if (ms) v = {0.5 * (main[0] + main[1]), 0.5 * (main[0] - main[1])};
        const bool active[kNumChannels] = {
            chan == Chan::Stereo || chan == Chan::Left || chan == Chan::Mid,
            chan == Chan::Stereo || chan == Chan::Right || chan == Chan::Side,
        };

        Frame newMain = v;
        Frame auxAdd{0.0, 0.0};
        for (int ch = 0; ch < kNumChannels; ++ch) {
            if (!active[ch]) continue;
            const BandOutput o = slot.process(ch, v[ch]);
            if (bp.tap == Tap::Main) {
                newMain[ch] = o.y;
            } else if (isCrossoverType(type)) {
                auxAdd[ch] = o.y;
                newMain[ch] = o.comp;
            } else if (isFilterType(type)) {
                auxAdd[ch] = o.y;
                newMain[ch] = v[ch] - o.y;
            } else {
                auxAdd[ch] = o.y - v[ch];
            }
        }
        slot.advance();

        if (ms) {
            main = {newMain[0] + newMain[1], newMain[0] - newMain[1]};
            aux[0] += auxAdd[0] + auxAdd[1];
            aux[1] += auxAdd[0] - auxAdd[1];
        } else {
            main = newMain;
            aux[0] += auxAdd[0];
            aux[1] += auxAdd[1];
        }
    }
}

Engine::Frame Engine::encode(const Frame& main, const Frame& aux) const {
    switch (params_.global.outputMode) {
        case OutputMode::Stereo:
            return {main[0] + aux[0], main[1] + aux[1]};
        case OutputMode::SplitLR:
            return {main[0], aux[0]};
        case OutputMode::SplitMS:
            return {0.5 * (main[0] + aux[0]), 0.5 * (main[0] - aux[0])};
    }
    return main;
}

void Engine::process(const float* const* in, const float* const* sidechain, float* const* out, int numSamples) {
    ScopedNoDenormals noDenormals;
    if (paramsDirty_) applyStaticDesigns(false);

    const GlobalParams& gp = params_.global;
    const bool split = gp.outputMode != OutputMode::Stereo;
    int listenGroup = -1;
    for (int g = 0; g < kNumGroups && listenGroup < 0; ++g)
        if (params_.groups[g].scListen) listenGroup = g;

    for (int i = 0; i < numSamples; ++i) {
        if (controlPhase_ == 0) controlUpdate(false);
        if (++controlPhase_ == kControlBlock) controlPhase_ = 0;

        const Frame x = {in[0][i], in[1][i]};
        const Frame sc = sidechain ? Frame{sidechain[0][i], sidechain[1][i]} : Frame{0.0, 0.0};
        Frame dry = x;
        if (split) dry[0] = dry[1] = 0.5 * (x[0] + x[1]);

        // Detectors read the undelayed signals.
        for (int g = 0; g < kNumGroups; ++g) {
            const Frame d = detectorInput(g, dry, sc);
            groups_[g].process(d[0], d[1]);
        }

        // Audio path is delayed by the lookahead.
        const Frame xd = {delay_[0].process(x[0], latency_), delay_[1].process(x[1], latency_)};
        Frame chainIn = xd;
        if (split) chainIn[0] = chainIn[1] = 0.5 * (xd[0] + xd[1]);

        Frame main, aux, solo;
        bool anySolo = false;
        processChain(chainIn, main, aux, solo, anySolo);
        if (anySolo) main = solo, aux = {0.0, 0.0};

        Frame y = encode(main, aux);
        if (gp.delta) {
            const Frame dryEnc = encode(chainIn, {0.0, 0.0});
            y = {y[0] - dryEnc[0], y[1] - dryEnc[1]};
        }
        const double gain = outputGain_.next();
        y = {y[0] * gain, y[1] * gain};
        if (listenGroup >= 0) y = {groups_[listenGroup].filtered(0), groups_[listenGroup].filtered(1)};

        const double b = bypassMix_.next();
        out[0][i] = static_cast<float>((1.0 - b) * y[0] + b * xd[0]);
        out[1][i] = static_cast<float>((1.0 - b) * y[1] + b * xd[1]);
    }
}

}  // namespace eeq
