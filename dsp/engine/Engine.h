#pragma once

#include <array>

#include "bands/BandProcessor.h"
#include "dynamics/DynamicsGroup.h"
#include "engine/DelayLine.h"
#include "engine/Params.h"

namespace eeq {

// The complete Everything EQ signal path for one plugin instance.
//
// Thread model: prepare(), setParams() and process() are called from the
// audio thread (or with the audio thread stopped).  process() does not
// allocate, lock or block.
class Engine {
public:
    static constexpr int kBypassFadeSamples = 64;

    // Allocates.  Must be called before process() and on sample-rate change.
    void prepare(double sampleRate);
    void reset();

    // Takes effect at the next control sub-block.
    void setParams(const EngineParams& p);
    const EngineParams& params() const { return params_; }

    // Stereo in/out.  sidechain may be null (treated as silence).
    // in and out may alias.
    void process(const float* const* in, const float* const* sidechain, float* const* out, int numSamples);

    // Reported plugin latency (lookahead), in samples.
    int latencySamples() const { return latency_; }

    double sampleRate() const { return fs_; }
    const DynamicsGroup& group(int g) const { return groups_[g]; }
    const BandSlot& band(int b) const { return slots_[b]; }

private:
    using Frame = std::array<double, kNumChannels>;

    void controlUpdate(bool snapNow);
    void applyStaticDesigns(bool snapNow);
    Frame detectorInput(int g, const Frame& dry, const Frame& sc);
    void processChain(const Frame& chainIn, Frame& main, Frame& aux, Frame& solo, bool& anySolo);
    Frame encode(const Frame& main, const Frame& aux) const;

    double fs_ = 48000.0;
    bool prepared_ = false;
    EngineParams params_{};
    bool paramsDirty_ = true;
    int latency_ = 0;
    int controlPhase_ = 0;

    std::array<BandSlot, kNumBands> slots_{};
    std::array<DetectorFilter, kNumBands> bandDetectors_{};
    std::array<DetectorFilter, kNumBands> soloFilters_{};
    std::array<DynamicsGroup, kNumGroups> groups_{};
    std::array<DelayLine, kNumChannels> delay_{};

    LinearRamp bypassMix_;
    LinearRamp outputGain_;
    bool bypassState_ = false;
};

}  // namespace eeq
