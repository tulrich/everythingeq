#pragma once

#include <array>

#include "bands/BandProcessor.h"
#include "engine/Params.h"

namespace eeq {

// One shared dynamics group (ABOUT.md section 3.B):
//   SC filter -> x^2 -> stereo link -> sigA / sigB one-pole envelopes (dB)
//   -> alpha continuum trigger -> knee gain computer -> attack/hold/release
//   -> control signal D in [0, 1] per channel.
class DynamicsGroup {
public:
    static constexpr double kFastTauMs = 1.0;  // sigA (D5)
    static constexpr double kFloorPower = 1e-7;  // -70 dB
    static constexpr double kScTiltDb = 6.0;  // SC "Tilt" filter: +6 dB high-minus-low

    void prepare(double sampleRate);
    void reset();
    void setParams(const GroupParams& p);

    // Feed one stereo detector-input sample (before the group SC filter).
    void process(double in0, double in1);

    double control(int ch) const { return d_[ch]; }
    double trigger(int ch) const { return trig_[ch]; }
    double filtered(int ch) const { return filtered_[ch]; }  // SC listen signal
    double effectiveThreshold() const { return teff_; }

    // Pure gain-computer mapping, exposed for tests and the UI meter.
    static double gainComputer(double triggerDb, double teffDb, double kneeDb, TriggerMode mode);

private:
    void designScFilter(bool snapNow);

    double fs_ = 48000.0;
    GroupParams p_{};
    bool initialized_ = false;
    std::array<BandProcessor, kNumChannels> sc_{};
    double cA_ = 1.0, cB_ = 1.0, cAtt_ = 1.0, cRel_ = 1.0;
    int holdSamples_ = 0;
    double teff_ = 0.0;

    std::array<double, kNumChannels> sA_{}, sB_{}, d_{}, trig_{}, filtered_{};
    std::array<int, kNumChannels> hold_{};
};

}  // namespace eeq
