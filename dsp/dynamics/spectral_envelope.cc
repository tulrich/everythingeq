#include "dsp/dynamics/spectral_envelope.h"

#include <algorithm>
#include <cmath>

#include "dsp/common/math.h"

namespace eeq {
namespace {

double Coefficient(double ms, double fs) {
  return 1.0 - std::exp(-1000.0 / (std::max(ms, 0.1) * fs));
}

double Position(double hz) {
  return std::log2(hz / kSpectralLoHz) / SpectralSpacingOctaves();
}

// Steps per band when searching for a peak.
constexpr int kPeakSteps = 8;

}  // namespace

double SpectralSpacingOctaves() {
  return std::log2(kSpectralHiHz / kSpectralLoHz) / (kSpectralBands - 1);
}

double SpectralBandHz(int i) {
  return kSpectralLoHz * std::exp2(i * SpectralSpacingOctaves());
}

void SpectralEnvelope::Prepare(double sample_rate) {
  fs_ = sample_rate;
  // Each cell is -1.5 dB halfway (in log frequency) to the next band, where
  // |H|^2 = 1 / (1 + (Q (x - 1/x))^2).
  const double x = std::exp2(0.5 * SpectralSpacingOctaves());
  const double q = std::sqrt(1.0 / std::pow(10.0, -0.15) - 1.0) / (x - 1.0 / x);
  for (int i = 0; i < kSpectralBands; ++i) {
    const SvfCoeffs c =
        DesignSvf(SvfShape::kBandPass, std::min(SpectralBandHz(i), 0.45 * fs_),
                  q, 0.0, fs_);
    for (SvfCell& cell : cells_[i]) cell.Snap(c);
  }
  Reset();
}

void SpectralEnvelope::Reset() {
  for (auto& band : cells_) {
    for (SvfCell& c : band) c.Reset();
  }
  env_.fill(0.0);
}

void SpectralEnvelope::SetBallistics(double attack_ms, double release_ms) {
  // Floors keep the low bands' ripple (at twice their frequency) out of
  // the levels.
  attack_ = Coefficient(std::max(attack_ms, 5.0), fs_);
  release_ = Coefficient(std::max(release_ms, 20.0), fs_);
}

void SpectralEnvelope::Process(double x) {
  for (int i = 0; i < kSpectralBands; ++i) {
    const double y = cells_[i][1].Process(cells_[i][0].Process(x));
    const double p = y * y;
    env_[i] += (p > env_[i] ? attack_ : release_) * (p - env_[i]);
  }
}

double SpectralEnvelope::BandDb(int i) const {
  return std::max(10.0 * std::log10(env_[i] + 1e-30), kFloorDb);
}

double SpectralEnvelope::LevelDb() const {
  double total = 0.0;
  for (double e : env_) total += e;
  return std::max(10.0 * std::log10(total + 1e-30), kFloorDb);
}

double SpectralEnvelope::Presence() const {
  return std::clamp(
      (LevelDb() - kSpectralGateLoDb) / (kSpectralGateHiDb - kSpectralGateLoDb),
      0.0, 1.0);
}

double SpectralEnvelope::MeanDb() const {
  double peak = kFloorDb;
  for (int i = 0; i < kSpectralBands; ++i) peak = std::max(peak, BandDb(i));
  double mean = 0.0;
  for (int i = 0; i < kSpectralBands; ++i)
    mean += std::max(BandDb(i), peak - kMeanFloorDb);
  return mean / kSpectralBands;
}

double SpectralEnvelope::DbAtPosition(double p) const {
  // Catmull-Rom through the band levels, so a peak between two centers
  // moves smoothly.
  p = std::clamp(p, 0.0, static_cast<double>(kSpectralBands - 1));
  const int i = std::min(static_cast<int>(p), kSpectralBands - 2);
  const double t = p - i;
  auto at = [this](int k) {
    return BandDb(std::clamp(k, 0, kSpectralBands - 1));
  };
  const double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
  return y1 + 0.5 * t *
                  (y2 - y0 +
                   t * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 +
                        t * (3.0 * (y1 - y2) + y3 - y0)));
}

double SpectralEnvelope::DbAt(double hz) const {
  return DbAtPosition(Position(std::max(hz, 1.0)));
}

double SpectralEnvelope::PeakHz(double lo_hz, double hi_hz) const {
  const double lo = std::max(Position(std::max(lo_hz, 1.0)), 0.0);
  const double hi = std::min(Position(std::max(hi_hz, 1.0)),
                             static_cast<double>(kSpectralBands - 1));
  if (lo > hi) return std::sqrt(lo_hz * hi_hz);
  const int steps = std::max(1, static_cast<int>((hi - lo) * kPeakSteps));
  double best_p = lo, best = DbAtPosition(lo);
  for (int s = 1; s <= steps; ++s) {
    const double p = lo + (hi - lo) * s / steps;
    const double db = DbAtPosition(p);
    if (db > best) best = db, best_p = p;
  }
  return kSpectralLoHz * std::exp2(best_p * SpectralSpacingOctaves());
}

SpectralLevels SpectralEnvelope::Levels() const {
  SpectralLevels l{};
  for (int i = 0; i < kSpectralBands; ++i) l[i] = static_cast<float>(BandDb(i));
  return l;
}

}  // namespace eeq
