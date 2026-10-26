#ifndef EVERYTHINGEQ_MODEL_SPECTRUM_ANALYZER_H_
#define EVERYTHINGEQ_MODEL_SPECTRUM_ANALYZER_H_

#include <complex>
#include <vector>

namespace eeq {

// In-place radix-2 complex FFT of a fixed power-of-two size.
class Fft {
 public:
  explicit Fft(int size);
  int size() const { return size_; }
  void Forward(std::vector<std::complex<float>>& data) const;

 private:
  int size_;
  std::vector<int> bit_reverse_;
  std::vector<std::complex<float>> twiddles_;
};

// Spectrum analyzer for the EQ graph (UI thread).  Feed it samples, then ask
// for a smoothed magnitude spectrum (dB) at display frequencies.  A
// full-scale sine reads 0 dB; the display tilt adds tilt_db_per_octave *
// log2(f / 1 kHz).  Peaks show at once and fall at kReleaseDbPerSecond.
class SpectrumAnalyzer {
 public:
  static constexpr float kFloorDb = -120.f;
  static constexpr float kReleaseDbPerSecond = 40.f;

  explicit SpectrumAnalyzer(int fft_size = 4096);

  void AddSamples(const float* samples, int count);
  void Clear();

  void Compute(const std::vector<double>& freqs, double sample_rate,
               double tilt_db_per_octave, double dt_seconds,
               std::vector<float>& out_db);

 private:
  Fft fft_;
  std::vector<float> history_;  // Circular, fft_size samples.
  int write_pos_ = 0;
  std::vector<float> window_;
  float window_gain_ = 1.f;
  std::vector<std::complex<float>> work_;
  std::vector<float> bins_db_;
  std::vector<float> smoothed_;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_MODEL_SPECTRUM_ANALYZER_H_
