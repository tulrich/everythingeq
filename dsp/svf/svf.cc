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

namespace {

// One cell's coefficients.  With `analog`, g is the unwarped pi * f / fs and
// Q isn't prewarped: the analog prototype, for MatchedBiquad().
SvfCoeffs DesignCell(SvfShape shape, double freq_hz, double q, double gain_db,
                     double sample_rate, bool analog) {
  const double f = ClampFreq(freq_hz, sample_rate);
  q = std::clamp(q, 0.025, 100.0);
  auto prewarp = [&](double q) {
    return analog ? q : PrewarpQ(q, f, sample_rate);
  };
  SvfCoeffs c;
  c.g = analog ? kPi * f / sample_rate : std::tan(kPi * f / sample_rate);
  c.k = 1.0 / q;
  switch (shape) {
    case SvfShape::kLowPass:
      c.m0 = 0.0, c.m1 = 0.0, c.m2 = 1.0;
      break;
    case SvfShape::kHighPass:
      c.m0 = 1.0, c.m1 = -c.k, c.m2 = -1.0;
      break;
    case SvfShape::kLowPass1:  // 1 / (p + 1) = (p + 1) / (p + 1)^2.
      c.k = 2.0, c.m0 = 0.0, c.m1 = 1.0, c.m2 = 1.0;
      break;
    case SvfShape::kHighPass1:  // p / (p + 1).
      c.k = 2.0, c.m0 = 1.0, c.m1 = -1.0, c.m2 = -1.0;
      break;
    case SvfShape::kBandPass:
      c.k = 1.0 / prewarp(q);
      c.m0 = 0.0, c.m1 = c.k, c.m2 = 0.0;
      break;
    case SvfShape::kNotch:
      c.k = 1.0 / prewarp(q);
      c.m0 = 1.0, c.m1 = -c.k, c.m2 = 0.0;
      break;
    case SvfShape::kAllPass:
      c.m0 = 1.0, c.m1 = -2.0 * c.k, c.m2 = 0.0;
      break;
    case SvfShape::kBell: {
      const double a = std::pow(10.0, gain_db / 40.0);
      c.k = 1.0 / (prewarp(q) * a);
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

// |H|^2 of an analog prototype (DesignCell with `analog`) at hz.
double AnalogPower(const SvfCoeffs& c, double hz, double sample_rate) {
  const std::complex<double> p(0.0, kPi * hz / sample_rate / c.g);
  return std::norm(c.m0 + (c.m1 * p + c.m2) / (p * p + c.k * p + 1.0));
}

// (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2).
struct Biquad {
  double b0, b1, b2, a1, a2;
};

// A biquad with stable poles as a cell: the inverse of the trapezoidal
// mapping.
SvfCoeffs SvfFromBiquad(const Biquad& q) {
  const double d0 = 4.0 / (1.0 - q.a1 + q.a2);
  SvfCoeffs c;
  c.g = std::sqrt((1.0 + q.a1 + q.a2) / (1.0 - q.a1 + q.a2));
  c.k = (1.0 - q.a2) * d0 / (2.0 * c.g);
  const double n2 = d0 * (q.b0 - q.b1 + q.b2) / 4.0;
  const double n1 = d0 * (q.b0 - q.b2) / (2.0 * c.g);
  const double n0 = d0 * (q.b0 + q.b1 + q.b2) / (4.0 * c.g * c.g);
  c.m0 = n2;
  c.m1 = n1 - n2 * c.k;
  c.m2 = n0 - n2;
  return c;
}

// The analog-matched biquad (M. Vicanek, "Matched Second Order Digital
// Filters", 2016), so bells and shelves keep their analog shape near
// Nyquist instead of cramping: the prototype's poles map by z = exp(sT), and
// the zeros give the prototype's exact magnitude at DC, at fm and at Nyquist.
// In phi = sin^2(w / 2), any |b0 + b1 z^-1 + b2 z^-2|^2 is
// B0 (1 - phi) + B1 phi + B2 phi (1 - phi).
Biquad MatchedBiquad(const SvfCoeffs& analog, double fm, double fs) {
  // Poles: p^2 + k p + 1 = 0, with sT = 2 g p.
  const double k = analog.k, g = analog.g;
  const double r = std::exp(-g * k);
  const double d = k * k - 4.0;
  const double a1 =
      -2.0 * r *
      (d < 0.0 ? std::cos(g * std::sqrt(-d)) : std::cosh(g * std::sqrt(d)));
  const double a2 = r * r;
  auto power = [](double c0, double c1, double c2, double phi) {
    return c0 * (1.0 - phi) + c1 * phi + c2 * phi * (1.0 - phi);
  };
  const double a_0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
  const double a_1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
  const double a_2 = -16.0 * a2;
  const double phi = std::pow(std::sin(kPi * fm / fs), 2);
  const double b_0 = AnalogPower(analog, 0.0, fs) * a_0;
  const double b_1 = AnalogPower(analog, 0.5 * fs, fs) * a_1;
  const double b_2 = (AnalogPower(analog, fm, fs) * power(a_0, a_1, a_2, phi) -
                      power(b_0, b_1, 0.0, phi)) /
                     (phi * (1.0 - phi));
  // Factor it, minimum phase: b0 + b2 = w, b0 b2 = -b_2 / 16.
  const double s0 = std::sqrt(b_0), s1 = std::sqrt(b_1);
  const double w = 0.5 * (s0 + s1);
  const double b0 = 0.5 * (w + std::sqrt(std::max(w * w + b_2 / 4.0, 0.0)));
  return {b0, 0.5 * (s0 - s1), w - b0, a1, a2};
}

// The matched biquad is exact at fm, so fm stays where phi (1 - phi) is
// well away from zero.
constexpr double kMaxMatchFraction = 0.4;
// Three points can't place a resonant shelf's zeros, so shelves above this
// Q stay bilinear.  Q isn't modulated, so the switch never happens while a
// band plays.
constexpr double kMaxMatchedShelfQ = 2.0;

// gain / m.
Biquad Inverse(const Biquad& m, double gain) {
  return {gain / m.b0, gain * m.a1 / m.b0, gain * m.a2 / m.b0, m.b1 / m.b0,
          m.b2 / m.b0};
}

// 1 / H for a cell whose zeros are minimum phase (a boost).  The numerator
// m0 p^2 + (m0 k + m1) p + (m0 + m2) is m0 w^2 (q^2 + k' q + 1) in q = p / w,
// which becomes the new denominator.
SvfCoeffs Inverse(const SvfCoeffs& c) {
  const double w = std::sqrt((c.m0 + c.m2) / c.m0);
  const double k = (c.m0 * c.k + c.m1) / (c.m0 * w);
  SvfCoeffs i;
  i.g = c.g * w;
  i.k = k;
  i.m0 = 1.0 / c.m0;
  i.m1 = (c.k / w - k) / c.m0;
  i.m2 = (1.0 / (w * w) - 1.0) / c.m0;
  return i;
}

// The matched boost (gain_db >= 0) of a bell or shelf.  A high shelf is
// G / (low shelf of G) at the same frequency, which keeps the matched poles
// below the frequency (a high shelf's own are above it, where they'd alias
// past Nyquist).
SvfCoeffs MatchedBoost(SvfShape shape, double freq_hz, double q, double gain_db,
                       double sample_rate) {
  const double fm = std::min(ClampFreq(freq_hz, sample_rate),
                             kMaxMatchFraction * sample_rate);
  const bool high = shape == SvfShape::kHighShelf;
  const Biquad m =
      MatchedBiquad(DesignCell(high ? SvfShape::kLowShelf : shape, freq_hz, q,
                               gain_db, sample_rate, true),
                    fm, sample_rate);
  return SvfFromBiquad(high ? Inverse(m, DbToGain(gain_db)) : m);
}

}  // namespace

SvfCoeffs DesignSvf(SvfShape shape, double freq_hz, double q, double gain_db,
                    double sample_rate) {
  q = std::clamp(q, 0.025, 100.0);
  const bool shelf =
      shape == SvfShape::kLowShelf || shape == SvfShape::kHighShelf;
  if ((shape != SvfShape::kBell && !shelf) ||
      (shelf && q > kMaxMatchedShelfQ)) {
    return DesignCell(shape, freq_hz, q, gain_db, sample_rate, false);
  }
  // A cut is the exact inverse of the boost (as in the analog prototypes);
  // the boost's zeros are minimum phase, so the cut's poles stay stable.
  const SvfCoeffs boost =
      MatchedBoost(shape, freq_hz, q, std::abs(gain_db), sample_rate);
  return gain_db < 0.0 ? Inverse(boost) : boost;
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
