#include "model/spectrum_analyzer.h"

#include <algorithm>
#include <cmath>

namespace eeq {
namespace {
constexpr double kTwoPi = 6.283185307179586476925;
}  // namespace

Fft::Fft(int size) : size_(size), bit_reverse_(size), twiddles_(size / 2) {
  int bits = 0;
  while ((1 << bits) < size) ++bits;
  for (int i = 0; i < size; ++i) {
    int r = 0;
    for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
    bit_reverse_[i] = r;
  }
  for (int i = 0; i < size / 2; ++i) {
    const double a = -kTwoPi * i / size;
    twiddles_[i] = {static_cast<float>(std::cos(a)),
                    static_cast<float>(std::sin(a))};
  }
}

void Fft::Forward(std::vector<std::complex<float>>& data) const {
  for (int i = 0; i < size_; ++i) {
    if (i < bit_reverse_[i]) std::swap(data[i], data[bit_reverse_[i]]);
  }
  for (int len = 2; len <= size_; len <<= 1) {
    const int half = len / 2, step = size_ / len;
    for (int start = 0; start < size_; start += len) {
      for (int k = 0; k < half; ++k) {
        const std::complex<float> t =
            twiddles_[k * step] * data[start + k + half];
        data[start + k + half] = data[start + k] - t;
        data[start + k] += t;
      }
    }
  }
}

SpectrumAnalyzer::SpectrumAnalyzer(int fft_size)
    : fft_(fft_size),
      history_(fft_size, 0.f),
      window_(fft_size),
      work_(fft_size),
      bins_db_(fft_size / 2 + 1) {
  double sum = 0.0;
  for (int i = 0; i < fft_size; ++i) {
    window_[i] =
        static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * i / fft_size));
    sum += window_[i];
  }
  // A sine of amplitude A peaks at A * sum(window) / 2 in its bin.
  window_gain_ = static_cast<float>(2.0 / sum);
}

void SpectrumAnalyzer::AddSamples(const float* samples, int count) {
  const int n = static_cast<int>(history_.size());
  for (int i = 0; i < count; ++i) {
    history_[write_pos_] = samples[i];
    if (++write_pos_ == n) write_pos_ = 0;
  }
}

void SpectrumAnalyzer::Clear() {
  std::fill(history_.begin(), history_.end(), 0.f);
  smoothed_.clear();
}

void SpectrumAnalyzer::Compute(const std::vector<double>& freqs,
                               double sample_rate, double tilt_db_per_octave,
                               double dt_seconds, std::vector<float>& out_db) {
  const int n = fft_.size();
  for (int i = 0; i < n; ++i) {
    work_[i] = {history_[(write_pos_ + i) % n] * window_[i], 0.f};
  }
  fft_.Forward(work_);
  for (int k = 0; k <= n / 2; ++k) {
    const float mag = std::abs(work_[k]) * window_gain_;
    bins_db_[k] = std::max(kFloorDb, 20.f * std::log10(mag + 1e-12f));
  }

  // Each display point takes the loudest bin between its neighbors'
  // geometric midpoints (or the nearest bin where points are denser).
  const size_t m = freqs.size();
  out_db.resize(m);
  const double bin_hz = sample_rate / n;
  for (size_t i = 0; i < m; ++i) {
    const double lo = i > 0 ? std::sqrt(freqs[i - 1] * freqs[i]) : freqs[i];
    const double hi = i + 1 < m ? std::sqrt(freqs[i] * freqs[i + 1]) : freqs[i];
    int k0 = static_cast<int>(std::floor(lo / bin_hz));
    int k1 = static_cast<int>(std::ceil(hi / bin_hz));
    k0 = std::clamp(k0, 0, n / 2);
    k1 = std::clamp(k1, k0, n / 2);
    float db = kFloorDb;
    if (k1 - k0 <= 1) {
      const double kf = std::clamp(freqs[i] / bin_hz, 0.0, n / 2.0);
      const int ka = static_cast<int>(kf);
      const int kb = std::min(ka + 1, n / 2);
      const float t = static_cast<float>(kf - ka);
      db = bins_db_[ka] + t * (bins_db_[kb] - bins_db_[ka]);
    } else {
      for (int k = k0; k <= k1; ++k) db = std::max(db, bins_db_[k]);
    }
    out_db[i] = db + static_cast<float>(tilt_db_per_octave *
                                        std::log2(freqs[i] / 1000.0));
  }

  // Ballistics: instant attack, linear release.
  if (smoothed_.size() != m) smoothed_ = out_db;
  const float fall = static_cast<float>(kReleaseDbPerSecond * dt_seconds);
  for (size_t i = 0; i < m; ++i) {
    smoothed_[i] = std::max(out_db[i], smoothed_[i] - fall);
    out_db[i] = smoothed_[i];
  }
}

}  // namespace eeq
