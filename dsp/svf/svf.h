#ifndef EVERYTHINGEQ_DSP_SVF_SVF_H_
#define EVERYTHINGEQ_DSP_SVF_SVF_H_

#include <complex>

namespace eeq {

// Coefficients of one linear trapezoidal (TPT/ZDF) state variable filter
// cell, following Andrew Simper's "SvfLinearTrapOptimised2" formulation.
//
// The cell computes v1 (band) and v2 (low) from the input v0; the output is
// the linear mix  m0*v0 + m1*v1 + m2*v2.  Every filter shape is a choice of
// (g, k, m0, m1, m2), so the state stays meaningful while coefficients move.
//
// The digital response equals the analog prototype
//     H(p) = m0 + (m1*p + m2) / (p^2 + k*p + 1),   p = j*tan(pi*f/fs) / g
// exactly (trapezoidal integration == bilinear transform).
struct SvfCoeffs {
  double g = 0.0;   // tan(pi * fc / fs)
  double k = 2.0;   // damping, 1/Q
  double m0 = 1.0;  // input mix
  double m1 = 0.0;  // band mix
  double m2 = 0.0;  // low mix

  static SvfCoeffs Identity() { return {}; }
};

enum class SvfShape {
  kLowPass,
  kHighPass,
  kBandPass,  // unit gain at the center frequency
  kNotch,
  kAllPass,
  kBell,
  kLowShelf,
  kHighShelf,
};

// Design a single cell.  gain_db is used by Bell / LowShelf / HighShelf only.
// Bell, BandPass and Notch Q values are prewarped so the digital bandwidth
// (in octaves) matches the requested Q near Nyquist.
SvfCoeffs DesignSvf(SvfShape shape, double freq_hz, double q, double gain_db,
                    double sample_rate);

// Raw coefficient constructor (no prewarp); used for fixed designs such as
// the ITU-R BS.1770 K-weighting stages.
SvfCoeffs MakeSvf(double freq_hz, double k, double m0, double m1, double m2,
                  double sample_rate);

// Exact complex frequency response of a cell at freq_hz.
std::complex<double> SvfResponse(const SvfCoeffs& c, double freq_hz,
                                 double sample_rate);

// Convert a filter Q into bandwidth in octaves and back (RBJ convention).
double QToOctaves(double q);
double OctavesToQ(double octaves);

// Q that, used in the bilinear (trapezoidal) design at freq_hz, gives the
// digital bandwidth an analog filter of quality q would have.
double PrewarpQ(double q, double freq_hz, double sample_rate);

// One channel of an SVF cell, with per-sample linear coefficient ramps.
class SvfCell {
 public:
  struct Outputs {
    double low;
    double band;
    double high;
  };

  void Reset() { ic1eq_ = ic2eq_ = 0.0; }

  // Jump to coefficients immediately (no ramp).
  void Snap(const SvfCoeffs& c);

  // Ramp linearly to target over ramp_samples (>= 1).
  void SetTarget(const SvfCoeffs& c, int ramp_samples);

  const SvfCoeffs& target() const { return target_; }

  // Process one sample through the mix.
  double Process(double v0) {
    const Outputs o = Tick(v0);
    return cur_.m0 * v0 + cur_.m1 * o.band + cur_.m2 * o.low;
  }

  // Advance the state and return the raw low/band/high outputs.  The mix
  // coefficients are ignored (but still ramped).
  Outputs Tick(double v0) {
    if (remaining_ > 0) AdvanceRamp();
    const double v3 = v0 - ic2eq_;
    const double v1 = a1_ * ic1eq_ + a2_ * v3;
    const double v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;
    ic1eq_ = 2.0 * v1 - ic1eq_;
    ic2eq_ = 2.0 * v2 - ic2eq_;
    return {v2, v1, v0 - cur_.k * v1 - v2};
  }

 private:
  void AdvanceRamp();
  void UpdateDerived();

  SvfCoeffs cur_;
  SvfCoeffs target_;
  SvfCoeffs step_;
  int remaining_ = 0;
  double a1_ = 1.0, a2_ = 0.0, a3_ = 0.0;
  double ic1eq_ = 0.0, ic2eq_ = 0.0;
};

}  // namespace eeq

#endif  // EVERYTHINGEQ_DSP_SVF_SVF_H_
