#ifndef EVERYTHINGEQ_DSP_DYNAMICS_TRACKER_H_
#define EVERYTHINGEQ_DSP_DYNAMICS_TRACKER_H_

#include <array>
#include <vector>

#include "dsp/engine/params.h"
#include "dsp/svf/svf.h"

namespace eeq {

// Vowel and pitch tracking for a dynamics group (ABOUT.md section 3.B,
// `Detect`).  The mono detector input is low-passed and decimated to about
// 12 kHz; every hop (~5 ms) the latest ~43 ms are analyzed:
//   Pitch: YIN (difference function, cumulative-mean normalization, absolute
//          threshold, parabolic interpolation) between 50 Hz and 1.5 kHz.
//   Vowel: the same YIN voicing decision, then an LPC envelope (order 12,
//          pre-emphasis, Hann window) whose first two peaks are F1 and F2,
//          projected onto the Vowel band's OO..EE morph path in log-frequency.
// Estimates update only while the input is voiced and above a level floor;
// otherwise the last estimate is held.
//
// Prepare() allocates; Reset() and Push() don't.
class Tracker {
 public:
  static constexpr double kTargetRate = 12000.0;
  static constexpr int kWindow = 512;  // Decimated samples per analysis.
  static constexpr int kHop = 64;
  static constexpr int kYinWidth = 256;  // Integration window.
  static constexpr double kYinThreshold = 0.2;
  static constexpr int kLpcOrder = 12;
  static constexpr double kLpcSeconds = 0.03;
  static constexpr double kMinPitchHz = 50.0;
  static constexpr double kMaxPitchHz = 1500.0;
  static constexpr double kLevelFloorDb = -50.0;  // RMS, dBFS
  // Pitch output: value = log2(f0 / kPitchRefHz) / kPitchOctaves, so a band
  // at kPitchRefHz with DynRange +8 oct (Dyn Target Freq) follows f0.
  static constexpr double kPitchRefHz = 27.5;
  static constexpr double kPitchOctaves = 8.0;

  void Prepare(double sample_rate);
  void Reset();
  void set_mode(DetectMode mode) { mode_ = mode; }

  // Feeds one input sample; returns true when an analysis ran.
  bool Push(double x);

  // The latest estimate (held through unvoiced input) as a control value in
  // [0, 1]: morph / 4 (Vowel) or the pitch mapping above (Pitch).
  double value() const { return value_; }
  bool voiced() const { return voiced_; }  // The last analysis was voiced.
  double pitch_hz() const { return pitch_hz_; }
  double morph() const { return morph_; }
  double f1() const { return f1_; }
  double f2() const { return f2_; }

  static double PitchToValue(double hz);
  static double ValueToPitch(double value);

 private:
  void Analyze();
  bool AnalyzePitch(double& hz);
  bool AnalyzeVowel(double& morph);

  DetectMode mode_ = DetectMode::kLevel;
  double fs_ = 48000.0;
  double fs_d_ = 12000.0;
  int decimation_ = 4;
  int phase_ = 0;
  int hop_count_ = 0;
  int filled_ = 0;
  std::array<SvfCell, 2> anti_alias_{};

  std::vector<double> ring_;  // kWindow decimated samples.
  int write_ = 0;
  std::vector<double> frame_;  // Oldest to newest.
  std::vector<double> diff_;   // YIN difference / CMND.
  int lpc_len_ = 360;
  std::vector<double> lpc_window_;  // Hann.
  std::vector<double> lpc_frame_;
  // LPC envelope evaluation: cos/sin(w * j) per frequency and lag.
  std::vector<double> env_hz_, env_cos_, env_sin_, env_db_;
  // The morph path in log-frequency: (ln F1, ln F2) per step.
  std::vector<std::array<double, 2>> morph_path_;

  double value_ = 0.0;
  bool voiced_ = false;
  double pitch_hz_ = 0.0, morph_ = 0.0, f1_ = 0.0, f2_ = 0.0;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_DYNAMICS_TRACKER_H_
