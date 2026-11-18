#include "dsp/dynamics/dynamics_group.h"

#include <algorithm>
#include <cmath>

#include "dsp/common/math.h"

namespace eeq {

void DynamicsGroup::Prepare(double sample_rate) {
  fs_ = sample_rate;
  initialized_ = false;
  tracker_.Prepare(fs_);
  spectrum_.Prepare(fs_);
  SetParams(p_);
  Reset();
}

void DynamicsGroup::Reset() {
  for (BandProcessor& f : sc_) f.Reset();
  sig_a_.fill(kFloorPower);
  sig_b_.fill(kFloorPower);
  control_.fill(0.0);
  trigger_.fill(0.0);
  filtered_.fill(0.0);
  hold_.fill(0);
  tracker_.Reset();
  spectrum_.Reset();
}

void DynamicsGroup::DesignScFilter(bool snap_now) {
  BandDesign d;
  const double hz = std::clamp(p_.sc_hz, kMinFreq, kMaxFreq);
  const double q = std::clamp(p_.sc_q, kMinQ, kMaxQ);
  switch (p_.sc_filter) {
    case ScFilter::kOff:
      break;
    case ScFilter::kBp:
      d = DesignBand(BandType::kBp, hz, 0.0, q, 0, fs_);
      break;
    case ScFilter::kLp:
      d = DesignBand(BandType::kLp, hz, 0.0, q, 0, fs_);
      break;
    case ScFilter::kHp:
      d = DesignBand(BandType::kHp, hz, 0.0, q, 0, fs_);
      break;
    case ScFilter::kTilt:
      d = DesignShelfTilt(hz, kScTiltDb, fs_);
      break;
    case ScFilter::kKWeight:
      d = DesignKWeighting(fs_);
      break;
  }
  for (BandProcessor& f : sc_) f.Apply(d, snap_now ? 1 : kControlBlock);
}

void DynamicsGroup::SetParams(const GroupParams& p) {
  p_ = p;
  p_.alpha = std::clamp(p_.alpha, 0.0, 1.0);
  p_.link = std::clamp(p_.link, 0.0, 1.0);
  p_.knee_db = std::max(p_.knee_db, 0.0);
  coeff_a_ = OnePoleCoeff(kFastTauMs / 1000.0, fs_);
  coeff_b_ =
      OnePoleCoeff(std::clamp(p_.rms_window_ms, 1.0, 400.0) / 1000.0, fs_);
  coeff_attack_ = OnePoleCoeff(std::max(p_.attack_ms, 0.0) / 1000.0, fs_);
  coeff_release_ = OnePoleCoeff(std::max(p_.release_ms, 0.0) / 1000.0, fs_);
  hold_samples_ =
      static_cast<int>(std::lround(std::max(p_.hold_ms, 0.0) * fs_ / 1000.0));
  const double block_s = kControlBlock / fs_;
  block_attack_ =
      1.0 - std::exp(-block_s / std::max(p_.attack_ms / 1000.0, block_s));
  block_release_ =
      1.0 - std::exp(-block_s / std::max(p_.release_ms / 1000.0, block_s));
  hold_blocks_ = hold_samples_ / kControlBlock;
  spectrum_.SetBallistics(p_.attack_ms, p_.release_ms);
  teff_ = (1.0 - p_.alpha) * p_.threshold_db + p_.alpha * p_.rise_db;
  tracker_.set_mode(p_.detect);
  DesignScFilter(!initialized_);
  initialized_ = true;
}

double DynamicsGroup::GainComputer(double trigger_db, double teff_db,
                                   double knee_db, TriggerMode mode) {
  const double over =
      mode == TriggerMode::kAbove ? trigger_db - teff_db : teff_db - trigger_db;
  if (knee_db <= 0.0) return over > 0.0 ? 1.0 : 0.0;
  return std::clamp(over / knee_db + 0.5, 0.0, 1.0);
}

double DynamicsGroup::SpectralTarget(double hz) const {
  const double mean = p_.alpha > 0.0 ? spectrum_.MeanDb() : 0.0;
  return GainComputer(spectrum_.DbAt(hz) - p_.alpha * mean, teff_, p_.knee_db,
                      p_.mode);
}

double DynamicsGroup::SpectralPeakOffset(double hz, double range_oct) const {
  const double r = std::abs(range_oct);
  if (r <= 0.0) return 0.0;
  const double peak = spectrum_.PeakHz(hz * std::exp2(-r), hz * std::exp2(r));
  return std::clamp(spectrum_.Presence() * std::log2(peak / hz) / range_oct,
                    -1.0, 1.0);
}

double DynamicsGroup::Follow(double target, SpectralFollower& f) const {
  if (target >= f.d) {
    f.d += block_attack_ * (target - f.d);
    f.hold = hold_blocks_;
  } else if (f.hold > 0) {
    --f.hold;
  } else {
    f.d += block_release_ * (target - f.d);
  }
  return f.d;
}

void DynamicsGroup::Process(double in0, double in1) {
  filtered_[0] = sc_[0].Process(in0).y;
  filtered_[1] = sc_[1].Process(in1).y;
  const double p0 = filtered_[0] * filtered_[0];
  const double p1 = filtered_[1] * filtered_[1];
  const double pmax = std::max(p0, p1);
  const double pw[kNumChannels] = {p0 + p_.link * (pmax - p0),
                                   p1 + p_.link * (pmax - p1)};

  const bool tracking = p_.detect != DetectMode::kLevel;
  if (tracking) tracker_.Push(0.5 * (filtered_[0] + filtered_[1]));
  if (spectral()) spectrum_.Process(0.5 * (filtered_[0] + filtered_[1]));

  for (int ch = 0; ch < kNumChannels; ++ch) {
    sig_a_[ch] += coeff_a_ * (pw[ch] - sig_a_[ch]);
    sig_b_[ch] += coeff_b_ * (pw[ch] - sig_b_[ch]);
    const double a_db = 10.0 * std::log10(std::max(sig_a_[ch], kFloorPower));
    const double b_db = 10.0 * std::log10(std::max(sig_b_[ch], kFloorPower));
    trigger_[ch] = a_db - p_.alpha * b_db;

    double& d = control_[ch];
    if (tracking) {
      // Glide toward the tracked value: Attack upward, Release downward.
      const double target = tracker_.value();
      d += (target >= d ? coeff_attack_ : coeff_release_) * (target - d);
      meter_.trigger_min = std::min(meter_.trigger_min, trigger_[ch]);
      meter_.trigger_max = std::max(meter_.trigger_max, trigger_[ch]);
      meter_.control_max = std::max(meter_.control_max, d);
      continue;
    }
    const double target =
        GainComputer(trigger_[ch], teff_, p_.knee_db, p_.mode);
    if (target >= d) {
      d += coeff_attack_ * (target - d);
      hold_[ch] = hold_samples_;
    } else if (hold_[ch] > 0) {
      --hold_[ch];
    } else {
      d += coeff_release_ * (target - d);
    }
    meter_.trigger_min = std::min(meter_.trigger_min, trigger_[ch]);
    meter_.trigger_max = std::max(meter_.trigger_max, trigger_[ch]);
    meter_.control_max = std::max(meter_.control_max, d);
  }
  meter_.valid = true;
}

}  // namespace eeq
