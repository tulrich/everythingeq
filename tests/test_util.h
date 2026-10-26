#ifndef EVERYTHINGEQ_TESTS_TEST_UTIL_H_
#define EVERYTHINGEQ_TESTS_TEST_UTIL_H_

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iterator>
#include <random>
#include <vector>

#include "dsp/common/math.h"
#include "dsp/engine/engine.h"

namespace eeq::test {

inline double MagDb(std::complex<double> z) {
  return 20.0 * std::log10(std::max(std::abs(z), 1e-30));
}

// DFT of a (decayed) impulse response at one frequency.
inline std::complex<double> DftAt(const std::vector<double>& h, double freq_hz,
                                  double fs) {
  const double w = 2.0 * kPi * freq_hz / fs;
  const std::complex<double> step = std::polar(1.0, -w);
  std::complex<double> rot = 1.0, acc = 0.0;
  for (size_t n = 0; n < h.size(); ++n) {
    acc += h[n] * rot;
    rot *= step;
    if ((n & 1023) == 0)
      rot /= std::abs(rot);  // keep the rotator on the unit circle
  }
  return acc;
}

inline std::vector<double> LogFreqs(double lo, double hi, int n) {
  std::vector<double> f(n);
  for (int i = 0; i < n; ++i)
    f[i] = lo * std::pow(hi / lo, static_cast<double>(i) / (n - 1));
  return f;
}

struct Stereo {
  std::vector<float> l, r;
  explicit Stereo(size_t n = 0) : l(n, 0.0f), r(n, 0.0f) {}
  size_t size() const { return l.size(); }
};

inline Stereo Noise(size_t n, double amp, uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> dist(-amp, amp);
  Stereo s(n);
  for (size_t i = 0; i < n; ++i)
    s.l[i] = static_cast<float>(dist(rng)),
    s.r[i] = static_cast<float>(dist(rng));
  return s;
}

inline Stereo Sine(size_t n, double hz, double amp, double fs) {
  Stereo s(n);
  for (size_t i = 0; i < n; ++i)
    s.l[i] = s.r[i] =
        static_cast<float>(amp * std::sin(2.0 * kPi * hz * i / fs));
  return s;
}

inline Stereo Delayed(const Stereo& x, size_t d) {
  Stereo y(x.size());
  for (size_t i = d; i < x.size(); ++i)
    y.l[i] = x.l[i - d], y.r[i] = x.r[i - d];
  return y;
}

// Run an engine over a whole buffer in blocks.
inline Stereo Run(Engine& e, const Stereo& in, const Stereo* sc = nullptr,
                  int block = 256) {
  Stereo out(in.size());
  for (size_t pos = 0; pos < in.size(); pos += block) {
    const int n = static_cast<int>(std::min<size_t>(block, in.size() - pos));
    const float* ip[2] = {in.l.data() + pos, in.r.data() + pos};
    float* op[2] = {out.l.data() + pos, out.r.data() + pos};
    const float* sp[2] = {nullptr, nullptr};
    if (sc) sp[0] = sc->l.data() + pos, sp[1] = sc->r.data() + pos;
    e.Process(ip, sc ? sp : nullptr, op, n);
  }
  return out;
}

inline double MaxAbsDiff(const Stereo& a, const Stereo& b, size_t from = 0) {
  double m = 0.0;
  for (size_t i = from; i < a.size(); ++i)
    m = std::max({m, std::abs(double(a.l[i]) - b.l[i]),
                  std::abs(double(a.r[i]) - b.r[i])});
  return m;
}

inline double MaxAbs(const Stereo& a, size_t from = 0) {
  double m = 0.0;
  for (size_t i = from; i < a.size(); ++i)
    m = std::max({m, std::abs(double(a.l[i])), std::abs(double(a.r[i]))});
  return m;
}

inline bool AllFinite(const Stereo& a) {
  for (size_t i = 0; i < a.size(); ++i)
    if (!std::isfinite(a.l[i]) || !std::isfinite(a.r[i])) return false;
  return true;
}

}  // namespace eeq::test

#endif  // EVERYTHINGEQ_TESTS_TEST_UTIL_H_
