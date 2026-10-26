#include "dsp/svf/svf.h"

#include <algorithm>
#include <cmath>

#include "dsp/common/math.h"

namespace eeq {

double QToOctaves(double q) { return 2.0 / kLn2 * std::asinh(1.0 / (2.0 * q)); }

double OctavesToQ(double octaves) {
  return 1.0 / (2.0 * std::sinh(kLn2 / 2.0 * octaves));
}

double PrewarpQ(double q, double freq_hz, double sample_rate) {
  const double w0 = 2.0 * kPi * freq_hz / sample_rate;
  const double octaves = QToOctaves(q) * w0 / std::sin(w0);
  return std::max(OctavesToQ(octaves), 0.025);
}

SvfCoeffs MakeSvf(double freq_hz, double k, double m0, double m1, double m2,
                  double sample_rate) {
  SvfCoeffs c;
  c.g = std::tan(kPi * ClampFreq(freq_hz, sample_rate) / sample_rate);
  c.k = k;
  c.m0 = m0;
  c.m1 = m1;
  c.m2 = m2;
  return c;
}

SvfCoeffs DesignSvf(SvfShape shape, double freq_hz, double q, double gain_db,
                    double sample_rate) {
  const double f = ClampFreq(freq_hz, sample_rate);
  q = std::clamp(q, 0.025, 100.0);
  SvfCoeffs c;
  c.g = std::tan(kPi * f / sample_rate);
  c.k = 1.0 / q;
  switch (shape) {
    case SvfShape::kLowPass:
      c.m0 = 0.0, c.m1 = 0.0, c.m2 = 1.0;
      break;
    case SvfShape::kHighPass:
      c.m0 = 1.0, c.m1 = -c.k, c.m2 = -1.0;
      break;
    case SvfShape::kBandPass:
      c.k = 1.0 / PrewarpQ(q, f, sample_rate);
      c.m0 = 0.0, c.m1 = c.k, c.m2 = 0.0;
      break;
    case SvfShape::kNotch:
      c.k = 1.0 / PrewarpQ(q, f, sample_rate);
      c.m0 = 1.0, c.m1 = -c.k, c.m2 = 0.0;
      break;
    case SvfShape::kAllPass:
      c.m0 = 1.0, c.m1 = -2.0 * c.k, c.m2 = 0.0;
      break;
    case SvfShape::kBell: {
      const double a = std::pow(10.0, gain_db / 40.0);
      c.k = 1.0 / (PrewarpQ(q, f, sample_rate) * a);
      c.m0 = 1.0, c.m1 = c.k * (a * a - 1.0), c.m2 = 0.0;
      break;
    }
    case SvfShape::kLowShelf: {
      const double a = std::pow(10.0, gain_db / 40.0);
      c.g /= std::sqrt(a);
      c.m0 = 1.0, c.m1 = c.k * (a - 1.0), c.m2 = a * a - 1.0;
      break;
    }
    case SvfShape::kHighShelf: {
      const double a = std::pow(10.0, gain_db / 40.0);
      c.g *= std::sqrt(a);
      c.m0 = a * a, c.m1 = c.k * (1.0 - a) * a, c.m2 = 1.0 - a * a;
      break;
    }
  }
  return c;
}

std::complex<double> SvfResponse(const SvfCoeffs& c, double freq_hz,
                                 double sample_rate) {
  const double w =
      std::tan(kPi * std::min(freq_hz, 0.4999 * sample_rate) / sample_rate);
  const std::complex<double> p(0.0, w / c.g);
  const std::complex<double> den = p * p + c.k * p + 1.0;
  return c.m0 + (c.m1 * p + c.m2) / den;
}

void SvfCell::Snap(const SvfCoeffs& c) {
  cur_ = target_ = c;
  remaining_ = 0;
  UpdateDerived();
}

void SvfCell::SetTarget(const SvfCoeffs& c, int ramp_samples) {
  target_ = c;
  if (ramp_samples <= 1) {
    Snap(c);
    return;
  }
  const double inv = 1.0 / ramp_samples;
  step_.g = (c.g - cur_.g) * inv;
  step_.k = (c.k - cur_.k) * inv;
  step_.m0 = (c.m0 - cur_.m0) * inv;
  step_.m1 = (c.m1 - cur_.m1) * inv;
  step_.m2 = (c.m2 - cur_.m2) * inv;
  remaining_ = ramp_samples;
}

void SvfCell::AdvanceRamp() {
  if (--remaining_ == 0) {
    cur_ = target_;
  } else {
    cur_.g += step_.g;
    cur_.k += step_.k;
    cur_.m0 += step_.m0;
    cur_.m1 += step_.m1;
    cur_.m2 += step_.m2;
  }
  UpdateDerived();
}

void SvfCell::UpdateDerived() {
  a1_ = 1.0 / (1.0 + cur_.g * (cur_.g + cur_.k));
  a2_ = cur_.g * a1_;
  a3_ = cur_.g * a2_;
}

}  // namespace eeq
