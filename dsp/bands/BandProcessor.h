#pragma once

#include <array>

#include "bands/BandDesign.h"
#include "common/Math.h"
#include "engine/Params.h"
#include "svf/Svf.h"

namespace eeq {

// Linear ramp for a scalar gain.
class LinearRamp {
public:
    void snap(double v) { cur_ = target_ = v, remaining_ = 0; }
    void setTarget(double v, int samples) {
        target_ = v;
        if (samples <= 1) return snap(v);
        step_ = (v - cur_) / samples;
        remaining_ = samples;
    }
    double next() {
        if (remaining_ > 0) cur_ = --remaining_ == 0 ? target_ : cur_ + step_;
        return cur_;
    }
    double value() const { return cur_; }

private:
    double cur_ = 0.0, target_ = 0.0, step_ = 0.0;
    int remaining_ = 0;
};

struct BandOutput {
    double y;     // main band output
    double comp;  // complementary crossover output (Crossover topology only)
};

// One channel of a band: up to 4 SVF cells with coefficient ramps.
class BandProcessor {
public:
    void reset();
    // Apply a design.  If the topology or cell count changes, the processor
    // snaps (and should be crossfaded by the caller).
    void apply(const BandDesign& d, int rampSamples);
    BandOutput process(double x);
    BandTopology topology() const { return topology_; }

private:
    BandTopology topology_ = BandTopology::Passthrough;
    int numCells_ = 0;
    bool crossoverHigh_ = false;
    std::array<SvfCell, 4> cells_{};
    LinearRamp post_;
};

// Static band parameters resolved for one channel after dynamics.
struct ResolvedBand {
    double x = 1000.0, y = 0.0, z = kButterworthQ;
    int option = 0;
};

// A stereo band slot.  Handles dirty-checking, coefficient ramps, and
// crossfading when the band type changes.
class BandSlot {
public:
    static constexpr int kFadeSamples = 64;

    void prepare(double sampleRate);
    void reset();
    // Called once per control sub-block.  `snapNow` jumps without ramping.
    void update(BandType type, const std::array<ResolvedBand, kNumChannels>& p, bool snapNow);
    // Process one channel sample.  Call advance() once per frame afterwards.
    BandOutput process(int ch, double x);
    void advance() {
        if (fade_ > 0) --fade_;
    }
    BandType type() const { return type_; }
    bool fading() const { return fade_ > 0; }
    const BandDesign& design(int ch) const { return design_[ch]; }

private:
    static bool dirty(const ResolvedBand& a, const ResolvedBand& b);

    double fs_ = 48000.0;
    BandType type_ = BandType::Off;
    bool initialized_ = false;
    std::array<ResolvedBand, kNumChannels> last_{};
    std::array<BandDesign, kNumChannels> design_{};
    std::array<BandProcessor, kNumChannels> cur_{};
    std::array<BandProcessor, kNumChannels> prev_{};
    int fade_ = 0;
};

// Stereo detector / solo filter (parallel sum of up to 3 cells).
class DetectorFilter {
public:
    void reset();
    void apply(const DetectorDesign& d, int rampSamples);
    double process(int ch, double x);

private:
    int numCells_ = 0;
    std::array<std::array<SvfCell, 3>, kNumChannels> cells_{};
};

}  // namespace eeq
