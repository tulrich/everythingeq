#include "bands/BandProcessor.h"

#include <cmath>

namespace eeq {

void BandProcessor::reset() {
    for (SvfCell& c : cells_) c.reset();
}

void BandProcessor::apply(const BandDesign& d, int rampSamples) {
    const bool structural = d.topology != topology_ || d.numCells != numCells_ ||
                            d.crossoverHigh != crossoverHigh_;
    if (structural) {
        topology_ = d.topology;
        numCells_ = d.numCells;
        crossoverHigh_ = d.crossoverHigh;
        reset();
        for (int i = 0; i < numCells_; ++i) cells_[i].snap(d.cells[i]);
        post_.snap(d.postGain);
        return;
    }
    for (int i = 0; i < numCells_; ++i) cells_[i].setTarget(d.cells[i], rampSamples);
    post_.setTarget(d.postGain, rampSamples);
}

BandOutput BandProcessor::process(double x) {
    switch (topology_) {
        case BandTopology::Passthrough:
            return {x, 0.0};
        case BandTopology::Serial: {
            double v = x;
            for (int i = 0; i < numCells_; ++i) v = cells_[i].process(v);
            return {v * post_.next(), 0.0};
        }
        case BandTopology::Crossover: {
            const SvfCell::Outputs s = cells_[0].tick(x);
            const double low = cells_[1].tick(s.low).low;
            const double high = cells_[2].tick(s.high).high;
            return crossoverHigh_ ? BandOutput{high, low} : BandOutput{low, high};
        }
    }
    return {x, 0.0};
}

void BandSlot::prepare(double sampleRate) {
    fs_ = sampleRate;
    initialized_ = false;
    reset();
}

void BandSlot::reset() {
    for (int ch = 0; ch < kNumChannels; ++ch) {
        cur_[ch].reset();
        prev_[ch].reset();
    }
    fade_ = 0;
}

bool BandSlot::dirty(const ResolvedBand& a, const ResolvedBand& b) {
    auto rel = [](double u, double v) { return std::abs(u - v) > 1e-4 * std::max(std::abs(u), 1e-3); };
    return rel(a.x, b.x) || std::abs(a.y - b.y) > 0.005 || rel(a.z, b.z) || a.option != b.option;
}

void BandSlot::update(BandType type, const std::array<ResolvedBand, kNumChannels>& p, bool snapNow) {
    const bool typeChanged = initialized_ && type != type_;
    if (typeChanged) {
        prev_ = cur_;
        fade_ = kFadeSamples;
    }
    for (int ch = 0; ch < kNumChannels; ++ch) {
        if (initialized_ && !typeChanged && !dirty(p[ch], last_[ch])) continue;
        last_[ch] = p[ch];
        design_[ch] = designBand(type, p[ch].x, p[ch].y, p[ch].z, p[ch].option, fs_);
        if (typeChanged) cur_[ch] = BandProcessor{};
        cur_[ch].apply(design_[ch], snapNow ? 1 : kControlBlock);
    }
    type_ = type;
    initialized_ = true;
}

BandOutput BandSlot::process(int ch, double x) {
    const BandOutput a = cur_[ch].process(x);
    if (fade_ == 0) return a;
    const BandOutput b = prev_[ch].process(x);
    const double t = 1.0 - static_cast<double>(fade_) / kFadeSamples;
    return {t * a.y + (1.0 - t) * b.y, t * a.comp + (1.0 - t) * b.comp};
}

void DetectorFilter::reset() {
    for (auto& chCells : cells_)
        for (SvfCell& c : chCells) c.reset();
}

void DetectorFilter::apply(const DetectorDesign& d, int rampSamples) {
    const bool structural = d.numCells != numCells_;
    numCells_ = d.numCells;
    for (auto& chCells : cells_) {
        for (int i = 0; i < numCells_; ++i) {
            if (structural) {
                chCells[i].reset();
                chCells[i].snap(d.cells[i]);
            } else {
                chCells[i].setTarget(d.cells[i], rampSamples);
            }
        }
    }
}

double DetectorFilter::process(int ch, double x) {
    if (numCells_ == 0) return x;
    double sum = 0.0;
    for (int i = 0; i < numCells_; ++i) sum += cells_[ch][i].process(x);
    return sum;
}

}  // namespace eeq
