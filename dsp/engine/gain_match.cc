#include "dsp/engine/gain_match.h"

#include <algorithm>
#include <cmath>

#include "dsp/bands/band_design.h"

namespace eeq {

void GainMatch::Prepare(double sample_rate) {
  fs_ = sample_rate;
  k_design_ = DesignKWeighting(fs_);
  running_ = false;
}

void GainMatch::Start() {
  for (BandProcessor* p : {&k_in_[0], &k_in_[1], &k_out_[0], &k_out_[1]}) {
    p->Reset();
    p->Apply(k_design_, 1);
  }
  in_sum_ = out_sum_ = counted_ = 0.0;
  running_ = true;
}

double GainMatch::progress() const {
  return std::min(counted_ / (kSeconds * fs_), 1.0);
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

bool GainMatch::Process(const float* const in[2], const float* const out[2],
                        int n, bool hold) {
  if (!running_ || n <= 0) return false;
  // The filters run through held blocks too, so they don't jump.
  const double p_in = MeanSquare(k_in_.data(), in, n);
  const double p_out = MeanSquare(k_out_.data(), out, n);
  if (hold || p_in < kSilencePower) return false;
  in_sum_ += p_in * n;
  out_sum_ += p_out * n;
  counted_ += n;
  if (counted_ < kSeconds * fs_) return false;
  running_ = false;
  correction_db_ = std::clamp(
      10.0 * std::log10(in_sum_ / std::max(out_sum_, 1e-30)), -kMaxDb, kMaxDb);
  return true;
}

}  // namespace eeq
