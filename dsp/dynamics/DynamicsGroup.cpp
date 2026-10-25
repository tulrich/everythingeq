#include "dynamics/DynamicsGroup.h"

#include <algorithm>
#include <cmath>

#include "common/Math.h"

namespace eeq {

void DynamicsGroup::prepare(double sampleRate) {
    fs_ = sampleRate;
    initialized_ = false;
    setParams(p_);
    reset();
}

void DynamicsGroup::reset() {
    for (BandProcessor& f : sc_) f.reset();
    sA_.fill(kFloorPower);
    sB_.fill(kFloorPower);
    d_.fill(0.0);
    trig_.fill(0.0);
    filtered_.fill(0.0);
    hold_.fill(0);
}

void DynamicsGroup::designScFilter(bool snapNow) {
    BandDesign d;
    const double hz = std::clamp(p_.scHz, kMinFreq, kMaxFreq);
    const double q = std::clamp(p_.scQ, kMinQ, kMaxQ);
    switch (p_.scFilter) {
        case ScFilter::Off:
            break;
        case ScFilter::BP:
            d = designBand(BandType::BP, hz, 0.0, q, 0, fs_);
            break;
        case ScFilter::LP:
            d = designBand(BandType::LP12, hz, 0.0, q, 0, fs_);
            break;
        case ScFilter::HP:
            d = designBand(BandType::HP12, hz, 0.0, q, 0, fs_);
            break;
        case ScFilter::Tilt:
            d = designBand(BandType::Tilt, hz, kScTiltDb, 0.0, 0, fs_);
            break;
        case ScFilter::KWeight:
            d = designBand(BandType::KWeight, 0.0, 0.0, 0.0, 0, fs_);
            break;
    }
    for (BandProcessor& f : sc_) f.apply(d, snapNow ? 1 : kControlBlock);
}

void DynamicsGroup::setParams(const GroupParams& p) {
    p_ = p;
    p_.alpha = std::clamp(p_.alpha, 0.0, 1.0);
    p_.link = std::clamp(p_.link, 0.0, 1.0);
    p_.kneeDb = std::max(p_.kneeDb, 0.0);
    cA_ = onePoleCoeff(kFastTauMs / 1000.0, fs_);
    cB_ = onePoleCoeff(std::clamp(p_.rmsWindowMs, 1.0, 400.0) / 1000.0, fs_);
    cAtt_ = onePoleCoeff(std::max(p_.attackMs, 0.0) / 1000.0, fs_);
    cRel_ = onePoleCoeff(std::max(p_.releaseMs, 0.0) / 1000.0, fs_);
    holdSamples_ = static_cast<int>(std::lround(std::max(p_.holdMs, 0.0) * fs_ / 1000.0));
    teff_ = (1.0 - p_.alpha) * p_.thresholdDb + p_.alpha * p_.riseDb;
    designScFilter(!initialized_);
    initialized_ = true;
}

double DynamicsGroup::gainComputer(double triggerDb, double teffDb, double kneeDb, TriggerMode mode) {
    const double over = mode == TriggerMode::Above ? triggerDb - teffDb : teffDb - triggerDb;
    if (kneeDb <= 0.0) return over > 0.0 ? 1.0 : 0.0;
    return std::clamp(over / kneeDb + 0.5, 0.0, 1.0);
}

void DynamicsGroup::process(double in0, double in1) {
    filtered_[0] = sc_[0].process(in0).y;
    filtered_[1] = sc_[1].process(in1).y;
    const double p0 = filtered_[0] * filtered_[0];
    const double p1 = filtered_[1] * filtered_[1];
    const double pmax = std::max(p0, p1);
    const double pw[kNumChannels] = {p0 + p_.link * (pmax - p0), p1 + p_.link * (pmax - p1)};

    for (int ch = 0; ch < kNumChannels; ++ch) {
        sA_[ch] += cA_ * (pw[ch] - sA_[ch]);
        sB_[ch] += cB_ * (pw[ch] - sB_[ch]);
        const double aDb = 10.0 * std::log10(std::max(sA_[ch], kFloorPower));
        const double bDb = 10.0 * std::log10(std::max(sB_[ch], kFloorPower));
        trig_[ch] = aDb - p_.alpha * bDb;

        const double target = gainComputer(trig_[ch], teff_, p_.kneeDb, p_.mode);
        double& d = d_[ch];
        if (target >= d) {
            d += cAtt_ * (target - d);
            hold_[ch] = holdSamples_;
        } else if (hold_[ch] > 0) {
            --hold_[ch];
        } else {
            d += cRel_ * (target - d);
        }
    }
}

}  // namespace eeq
