#ifndef EVERYTHINGEQ_DSP_DYNAMICS_DYNAMICS_GROUP_H_
#define EVERYTHINGEQ_DSP_DYNAMICS_DYNAMICS_GROUP_H_

#include <array>

#include "dsp/bands/band_processor.h"
#include "dsp/dynamics/tracker.h"
#include "dsp/engine/params.h"

namespace eeq {

// One shared dynamics group (ABOUT.md section 3.B):
//   SC filter -> x^2 -> stereo link -> sigA / sigB one-pole envelopes (dB)
//   -> alpha continuum trigger -> knee gain computer -> attack/hold/release
//   -> control signal D in [0, 1] per channel.
// With Detect = Vowel or Pitch, the conditioned input (mono) feeds a Tracker
// instead and D follows its estimate through the attack/release smoothing.
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

  // Extremes since the last TakeMeter() call, over both channels: for
  // detector displays that must not miss short transients.
  struct Meter {
    double trigger_min = 0.0;
    double trigger_max = 0.0;
    double control_max = 0.0;
    bool valid = false;  // False if no samples were processed.
    // Vowel / Pitch: the tracker's latest estimate (held when unvoiced).
    double tracked = 0.0;
    bool voiced = false;
  };
  Meter TakeMeter() {
    Meter m = meter_;
    m.tracked = tracker_.value();
    m.voiced = tracker_.voiced();
    meter_ = Meter{1e9, -1e9, 0.0, false};
    return m;
  }

  double control(int ch) const { return control_[ch]; }
  double trigger(int ch) const { return trigger_[ch]; }
  double filtered(int ch) const { return filtered_[ch]; }  // SC listen signal
  double effective_threshold() const { return teff_; }
  const Tracker& tracker() const { return tracker_; }

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
  Tracker tracker_;
  Meter meter_{1e9, -1e9, 0.0, false};
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_DYNAMICS_DYNAMICS_GROUP_H_
