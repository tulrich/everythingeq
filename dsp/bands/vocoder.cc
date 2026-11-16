#include "dsp/bands/vocoder.h"

#include <algorithm>
#include <cmath>

#include "dsp/common/math.h"

namespace eeq {
namespace {

// Spacing of the bands in octaves, and the Q that spans it.
double SpacingOctaves() {
  return std::log2(kVocoderHiHz / kVocoderLoHz) / (kVocoderBands - 1);
}

double Coefficient(double ms, double fs) {
  return 1.0 - std::exp(-1000.0 / (std::max(ms, 0.1) * fs));
}

constexpr double kProbeDb = 6.0;
constexpr double kReferenceRate = 48000.0;

// Neighboring bells overlap, so each band center hears its neighbors'
// gains too (a smooth shape would come out about 1.6x too strong).  The
// inverse of the interaction matrix M (M[j][i] = what bell i alone does at
// center j, per dB of its gain) turns the wanted shape into bell gains, as
// in a graphic EQ's gain correction.
using Matrix = std::array<std::array<double, kVocoderBands>, kVocoderBands>;

const Matrix& InverseInteraction() {
  static const Matrix inverse = [] {
    const double q = OctavesToQ(SpacingOctaves());
    Matrix m{}, inv{};
    for (int i = 0; i < kVocoderBands; ++i) {
      const SvfCoeffs c = DesignSvf(SvfShape::kBell, VocoderBandHz(i), q,
                                    kProbeDb, kReferenceRate);
      for (int j = 0; j < kVocoderBands; ++j) {
        m[j][i] = 20.0 *
                  std::log10(std::abs(
                      SvfResponse(c, VocoderBandHz(j), kReferenceRate))) /
                  kProbeDb;
      }
      inv[i][i] = 1.0;
    }
    // Gauss-Jordan; M is diagonally dominant, so no pivoting is needed.
    for (int k = 0; k < kVocoderBands; ++k) {
      const double d = m[k][k];
      for (int c = 0; c < kVocoderBands; ++c) m[k][c] /= d, inv[k][c] /= d;
      for (int r = 0; r < kVocoderBands; ++r) {
        if (r == k) continue;
        const double f = m[r][k];
        for (int c = 0; c < kVocoderBands; ++c) {
          m[r][c] -= f * m[k][c];
          inv[r][c] -= f * inv[k][c];
        }
      }
    }
    return inv;
  }();
  return inverse;
}

}  // namespace

double VocoderBandHz(int i) {
  return kVocoderLoHz * std::exp2(i * SpacingOctaves());
}

void VocoderAnalysis::Prepare(double sample_rate) {
  fs_ = sample_rate;
  InverseInteraction();  // Built here rather than on the audio thread.
  const double q = OctavesToQ(SpacingOctaves());
  for (int i = 0; i < kVocoderBands; ++i) {
    cells_[i].Snap(DesignSvf(SvfShape::kBandPass,
                             std::min(VocoderBandHz(i), 0.45 * fs_), q, 0.0,
                             fs_));
  }
  Reset();
}

void VocoderAnalysis::Reset() {
  for (SvfCell& c : cells_) c.Reset();
  env_.fill(0.0);
}

void VocoderAnalysis::SetBallistics(double attack_ms, double release_ms) {
  // Floors keep the low bands' ripple (at twice their frequency) out of
  // the gains.
  attack_ = Coefficient(std::max(attack_ms, 5.0), fs_);
  release_ = Coefficient(std::max(release_ms, 20.0), fs_);
}

void VocoderAnalysis::Process(double x) {
  for (int i = 0; i < kVocoderBands; ++i) {
    const double y = cells_[i].Process(x);
    const double p = y * y;
    env_[i] += (p > env_[i] ? attack_ : release_) * (p - env_[i]);
  }
}

VocoderShape VocoderAnalysis::Shape(double amount, double range_db) const {
  VocoderShape shape{};
  std::array<double, kVocoderBands> db{};
  double total = 0.0, peak = -300.0;
  for (int i = 0; i < kVocoderBands; ++i) {
    total += env_[i];
    db[i] = 10.0 * std::log10(env_[i] + 1e-30);
    peak = std::max(peak, db[i]);
  }
  // Silence leaves the instrument flat; the shape fades in with the level.
  const double level = 10.0 * std::log10(total + 1e-30);
  const double presence = std::clamp(
      (level - kVocoderGateLoDb) / (kVocoderGateHiDb - kVocoderGateLoDb), 0.0,
      1.0);
  if (presence <= 0.0) return shape;
  double mean = 0.0;
  for (double& d : db) {
    d = std::max(d, peak - kVocoderFloorDb);
    mean += d;
  }
  mean /= kVocoderBands;
  for (int i = 0; i < kVocoderBands; ++i) {
    shape[i] = static_cast<float>(
        presence * std::clamp(amount * (db[i] - mean), -range_db, range_db));
  }
  return shape;
}

BandDesign DesignVocoder(double shift, const VocoderShape& shape,
                         double sample_rate) {
  BandDesign d;
  d.topology = BandTopology::kSerial;
  const double q = OctavesToQ(SpacingOctaves());
  const Matrix& inv = InverseInteraction();
  for (int i = 0; i < kVocoderBands; ++i) {
    double gain = 0.0;
    for (int j = 0; j < kVocoderBands; ++j) gain += inv[i][j] * shape[j];
    const double hz =
        std::clamp(VocoderBandHz(i) * shift, 20.0, 0.45 * sample_rate);
    d.cells[d.num_cells++] =
        DesignSvf(SvfShape::kBell, hz, q,
                  std::clamp(gain, -kMaxGainDb, kMaxGainDb), sample_rate);
  }
  return d;
}

}  // namespace eeq
