#include "dsp/dynamics/tracker.h"

#include <algorithm>
#include <cmath>

#include "dsp/bands/vowel.h"
#include "dsp/common/math.h"

namespace eeq {

namespace {

constexpr int kEnvPoints = 128;
constexpr double kEnvMinHz = 150.0;
constexpr double kEnvMaxHz = 3800.0;
constexpr double kMorphStep = 0.02;
constexpr double kPreEmphasis = 0.97;
constexpr double kF2Weight = 4.0;
constexpr double kF1WithinDb = 12.0;
// In weighted natural-log units (0.3 is about a third of an octave in F1).
constexpr double kMaxPathDistance = 0.3;

}  // namespace

double Tracker::PitchToValue(double hz) {
  return std::clamp(std::log2(hz / kPitchRefHz) / kPitchOctaves, 0.0, 1.0);
}

double Tracker::ValueToPitch(double value) {
  return kPitchRefHz * std::exp2(value * kPitchOctaves);
}

void Tracker::Prepare(double sample_rate) {
  fs_ = sample_rate;
  decimation_ = std::max(1, static_cast<int>(std::lround(fs_ / kTargetRate)));
  fs_d_ = fs_ / decimation_;
  // 4th-order Butterworth anti-alias low-pass at 0.42 x the decimated rate.
  const double fc = 0.42 * fs_d_;
  anti_alias_[0].Snap(DesignSvf(SvfShape::kLowPass, fc, 0.5412, 0.0, fs_));
  anti_alias_[1].Snap(DesignSvf(SvfShape::kLowPass, fc, 1.3066, 0.0, fs_));

  ring_.assign(kWindow, 0.0);
  frame_.assign(kWindow, 0.0);
  diff_.assign(kWindow / 2 + 1, 0.0);
  lpc_len_ =
      std::min(kWindow, static_cast<int>(std::lround(kLpcSeconds * fs_d_)));
  lpc_window_.resize(lpc_len_);
  for (int i = 0; i < lpc_len_; ++i) {
    lpc_window_[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * (i + 0.5) / lpc_len_);
  }
  lpc_frame_.assign(lpc_len_, 0.0);

  env_hz_.resize(kEnvPoints);
  env_cos_.resize(kEnvPoints * (kLpcOrder + 1));
  env_sin_.resize(kEnvPoints * (kLpcOrder + 1));
  env_db_.assign(kEnvPoints, 0.0);
  for (int k = 0; k < kEnvPoints; ++k) {
    const double hz =
        kEnvMinHz * std::pow(kEnvMaxHz / kEnvMinHz,
                             static_cast<double>(k) / (kEnvPoints - 1));
    env_hz_[k] = hz;
    const double w = 2.0 * kPi * hz / fs_d_;
    for (int j = 0; j <= kLpcOrder; ++j) {
      env_cos_[k * (kLpcOrder + 1) + j] = std::cos(w * j);
      env_sin_[k * (kLpcOrder + 1) + j] = std::sin(w * j);
    }
  }

  const int steps = static_cast<int>(std::lround(kVowelMorphMax / kMorphStep));
  morph_path_.resize(steps + 1);
  for (int i = 0; i <= steps; ++i) {
    const Formants f = VowelFormants(i * kMorphStep);
    morph_path_[i] = {std::log(f.hz[0]), std::log(f.hz[1])};
  }
  Reset();
}

void Tracker::Reset() {
  for (SvfCell& c : anti_alias_) c.Reset();
  std::fill(ring_.begin(), ring_.end(), 0.0);
  write_ = 0;
  phase_ = 0;
  hop_count_ = 0;
  filled_ = 0;
  value_ = 0.0;
  voiced_ = false;
  pitch_hz_ = morph_ = f1_ = f2_ = 0.0;
}

bool Tracker::Push(double x) {
  double y = x;
  for (SvfCell& c : anti_alias_) y = c.Process(y);
  if (++phase_ < decimation_) return false;
  phase_ = 0;
  ring_[write_] = y;
  write_ = (write_ + 1) % kWindow;
  filled_ = std::min(filled_ + 1, kWindow);
  if (++hop_count_ < kHop) return false;
  hop_count_ = 0;
  if (filled_ < kWindow) return false;
  Analyze();
  return true;
}

void Tracker::Analyze() {
  for (int i = 0; i < kWindow; ++i) frame_[i] = ring_[(write_ + i) % kWindow];

  // Level gate on the newest kYinWidth samples.
  double energy = 0.0;
  for (int i = kWindow - kYinWidth; i < kWindow; ++i) {
    energy += frame_[i] * frame_[i];
  }
  const double rms = std::sqrt(energy / kYinWidth);
  double hz = 0.0;
  voiced_ = rms > DbToGain(kLevelFloorDb) && AnalyzePitch(hz);
  if (!voiced_) return;
  pitch_hz_ = hz;
  if (mode_ == DetectMode::kPitch) {
    value_ = PitchToValue(hz);
    return;
  }
  double morph = 0.0;
  if (!AnalyzeVowel(morph)) {
    voiced_ = false;
    return;
  }
  morph_ = morph;
  value_ = morph / kVowelMorphMax;
}

bool Tracker::AnalyzePitch(double& hz) {
  const int tau_min =
      std::max(2, static_cast<int>(std::floor(fs_d_ / kMaxPitchHz)));
  const int tau_max =
      std::min(kWindow - kYinWidth - 1,
               static_cast<int>(std::ceil(fs_d_ / kMinPitchHz)));
  // Difference function over the newest samples, compared with lagged ones.
  const double* x = frame_.data() + (kWindow - kYinWidth - tau_max - 1);
  diff_[0] = 1.0;
  double running = 0.0;
  for (int tau = 1; tau <= tau_max; ++tau) {
    double d = 0.0;
    const double* a = x + tau_max + 1;  // Newest kYinWidth samples.
    const double* b = a - tau;
    for (int j = 0; j < kYinWidth; ++j) {
      const double e = a[j] - b[j];
      d += e * e;
    }
    running += d;
    // Cumulative-mean normalized difference.
    diff_[tau] = running > 0.0 ? d * tau / running : 1.0;
  }
  int best = -1;
  for (int tau = tau_min; tau <= tau_max; ++tau) {
    if (diff_[tau] < kYinThreshold) {
      while (tau + 1 <= tau_max && diff_[tau + 1] < diff_[tau]) ++tau;
      best = tau;
      break;
    }
  }
  if (best < 0) return false;
  double refined = best;
  if (best > 1 && best < tau_max) {
    const double s0 = diff_[best - 1], s1 = diff_[best], s2 = diff_[best + 1];
    const double denom = s0 - 2.0 * s1 + s2;
    if (denom > 0.0) refined += 0.5 * (s0 - s2) / denom;
  }
  hz = fs_d_ / refined;
  return hz >= kMinPitchHz && hz <= kMaxPitchHz;
}

bool Tracker::AnalyzeVowel(double& morph) {
  // Pre-emphasized, windowed newest lpc_len_ samples.
  const double* x = frame_.data() + (kWindow - lpc_len_);
  for (int i = 0; i < lpc_len_; ++i) {
    const double prev = i > 0 ? x[i - 1] : x[i];
    lpc_frame_[i] = (x[i] - kPreEmphasis * prev) * lpc_window_[i];
  }
  std::array<double, kLpcOrder + 1> r{};
  for (int lag = 0; lag <= kLpcOrder; ++lag) {
    double s = 0.0;
    for (int i = lag; i < lpc_len_; ++i)
      s += lpc_frame_[i] * lpc_frame_[i - lag];
    r[lag] = s;
  }
  if (r[0] <= 1e-12) return false;
  r[0] *= 1.0 + 1e-9;  // White-noise correction.

  // Levinson-Durbin.
  std::array<double, kLpcOrder + 1> a{}, prev{};
  a[0] = 1.0;
  double error = r[0];
  for (int i = 1; i <= kLpcOrder; ++i) {
    double acc = r[i];
    for (int j = 1; j < i; ++j) acc += a[j] * r[i - j];
    const double k = -acc / error;
    prev = a;
    for (int j = 1; j < i; ++j) a[j] = prev[j] + k * prev[i - j];
    a[i] = k;
    error *= 1.0 - k * k;
    if (error <= 0.0) return false;
  }

  // Envelope 1 / |A(e^jw)|^2 in dB, then its peaks in rising frequency.
  for (int k = 0; k < kEnvPoints; ++k) {
    double re = 0.0, im = 0.0;
    const double* c = env_cos_.data() + k * (kLpcOrder + 1);
    const double* s = env_sin_.data() + k * (kLpcOrder + 1);
    for (int j = 0; j <= kLpcOrder; ++j) {
      re += a[j] * c[j];
      im -= a[j] * s[j];
    }
    env_db_[k] = -10.0 * std::log10(std::max(re * re + im * im, 1e-20));
  }
  // F1: the lowest envelope peak in its range within kF1WithinDb of the
  // strongest; F2: the strongest peak above it (weak spurious peaks between
  // formants are skipped).
  auto is_peak = [&](int k) {
    return env_db_[k] > env_db_[k - 1] && env_db_[k] >= env_db_[k + 1];
  };
  double f1_level = -1e300;
  for (int k = 1; k + 1 < kEnvPoints; ++k) {
    if (env_hz_[k] >= 200.0 && env_hz_[k] <= 1000.0 && is_peak(k)) {
      f1_level = std::max(f1_level, env_db_[k]);
    }
  }
  double f1 = 0.0, f2 = 0.0, f2_level = -1e300;
  for (int k = 1; k + 1 < kEnvPoints; ++k) {
    if (!is_peak(k)) continue;
    const double hz = env_hz_[k];
    if (f1 == 0.0) {
      if (hz >= 200.0 && hz <= 1000.0 && env_db_[k] >= f1_level - kF1WithinDb) {
        f1 = hz;
      }
    } else if (hz >= std::max(1.2 * f1, 600.0) && hz <= 3200.0 &&
               env_db_[k] > f2_level) {
      f2 = hz, f2_level = env_db_[k];
    }
  }
  if (f1 == 0.0 || f2 == 0.0) return false;
  f1_ = f1;
  f2_ = f2;

  // Nearest point on the morph path.  F2 counts more: it separates the
  // vowels along the path, while LPC pulls F1 toward a nearby harmonic of
  // higher voices.
  const double l1 = std::log(f1), l2 = std::log(f2);
  double best = 1e300;
  int best_i = 0;
  for (size_t i = 0; i < morph_path_.size(); ++i) {
    const double d1 = l1 - morph_path_[i][0], d2 = l2 - morph_path_[i][1];
    const double d = d1 * d1 + kF2Weight * d2 * d2;
    if (d < best) best = d, best_i = static_cast<int>(i);
  }
  // Formants far from every vowel on the path (e.g. a missed F1) are
  // treated as unvoiced rather than snapping to a wrong vowel.
  if (best > kMaxPathDistance * kMaxPathDistance) return false;
  morph = best_i * kMorphStep;
  return true;
}

}  // namespace eeq
