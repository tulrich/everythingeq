#ifndef EVERYTHINGEQ_DSP_DYNAMICS_DYNAMICS_GROUP_H_
#define EVERYTHINGEQ_DSP_DYNAMICS_DYNAMICS_GROUP_H_

#include <array>

#include "dsp/bands/band_processor.h"
#include "dsp/engine/params.h"

namespace eeq {

// One shared dynamics group (ABOUT.md section 3.B):
//   SC filter -> x^2 -> stereo link -> sigA / sigB one-pole envelopes (dB)
//   -> alpha continuum trigger -> knee gain computer -> attack/hold/release
//   -> control signal D in [0, 1] per channel.
class DynamicsGroup {
 public:
  static constexpr double kFastTauMs = 1.0;    // sigA (D5)
  static constexpr double kFloorPower = 1e-7;  // -70 dB
  // SC "Tilt" filter: +6 dB high-minus-low.
  static constexpr double kScTiltDb = 6.0;

  void Prepare(double sample_rate);
  void Reset();
  void SetParams(const GroupParams& p);

  // Feed one stereo detector-input sample (before the group SC filter).
  void Process(double in0, double in1);

  double control(int ch) const { return control_[ch]; }
  double trigger(int ch) const { return trigger_[ch]; }
  double filtered(int ch) const { return filtered_[ch]; }  // SC listen signal
  double effective_threshold() const { return teff_; }

  // Pure gain-computer mapping, exposed for tests and the UI meter.
  static double GainComputer(double trigger_db, double teff_db, double knee_db,
                             TriggerMode mode);

 private:
  void DesignScFilter(bool snap_now);

  double fs_ = 48000.0;
  GroupParams p_{};
  bool initialized_ = false;
  std::array<BandProcessor, kNumChannels> sc_{};
  double coeff_a_ = 1.0, coeff_b_ = 1.0, coeff_attack_ = 1.0,
         coeff_release_ = 1.0;
  int hold_samples_ = 0;
  double teff_ = 0.0;

  std::array<double, kNumChannels> sig_a_{}, sig_b_{}, control_{}, trigger_{},
      filtered_{};
  std::array<int, kNumChannels> hold_{};
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_DYNAMICS_DYNAMICS_GROUP_H_
