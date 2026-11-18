#include "dsp/engine/gain_match.h"

#include <algorithm>
#include <cmath>

#include "dsp/bands/band_design.h"
#include "dsp/common/math.h"

namespace eeq {

void GainMatch::Prepare(double sample_rate) {
  fs_ = sample_rate;
  glide_ = OnePoleCoeff(kGlideSeconds, fs_);
  const BandDesign k = DesignKWeighting(fs_);
  for (BandProcessor* p : {&k_in_[0], &k_in_[1], &k_out_[0], &k_out_[1]}) {
    p->Reset();
    p->Apply(k, 1);
  }
  Reset();
  gain_ = 1.0;
}

void GainMatch::Reset() {
  in_power_ = in_weight_ = 0.0;
  out_power_ = out_weight_ = {};
}

void GainMatch::SelectSlot(int slot) {
  slot = std::clamp(slot, 0, 1);
  if (slot == slot_) return;
  if (out_weight_[slot] == 0.0) {
    out_power_[slot] = out_power_[slot_];
    out_weight_[slot] = out_weight_[slot_];
  }
  slot_ = slot;
}

double GainMatch::MeanSquare(BandProcessor* k, const float* const x[2], int n) {
  double sum = 0.0;
  for (int c = 0; c < 2; ++c) {
    for (int i = 0; i < n; ++i) {
      const double y = k[c].Process(x[c][i]).y;
      sum += y * y;
    }
  }
  return sum / (2.0 * n);
}

void GainMatch::Process(const float* const in[2], float* const out[2], int n,
                        bool enabled, bool hold) {
  if (n <= 0) return;
  if (enabled && !was_enabled_) Reset();
  was_enabled_ = enabled;
  double target = 1.0;
  if (enabled && !hold) {
    const double p_in = MeanSquare(k_in_.data(), in, n);
    const double p_out = MeanSquare(k_out_.data(), out, n);
    if (p_in > kSilencePower) {
      const double a = 1.0 - std::exp(-n / (kTauSeconds * fs_));
      in_power_ += a * (p_in - in_power_);
      in_weight_ += a * (1.0 - in_weight_);
      out_power_[slot_] += a * (p_out - out_power_[slot_]);
      out_weight_[slot_] += a * (1.0 - out_weight_[slot_]);
    }
    if (out_weight_[slot_] > 0.0) {
      const double limit = DbToGain(kMaxDb);
      const double ratio =
          (in_power_ / in_weight_) /
          std::max(out_power_[slot_] / out_weight_[slot_], 1e-30);
      target = std::clamp(std::sqrt(ratio), 1.0 / limit, limit);
    }
  }
  if (target == 1.0 && std::abs(gain_ - 1.0) < 1e-6) {
    gain_ = 1.0;
    return;
  }
  for (int i = 0; i < n; ++i) {
    gain_ += glide_ * (target - gain_);
    out[0][i] = static_cast<float>(out[0][i] * gain_);
    out[1][i] = static_cast<float>(out[1][i] * gain_);
  }
}

double GainMatch::gain_db() const { return GainToDb(gain_); }

}  // namespace eeq
