#include "dsp/bands/vocoder.h"

#include <algorithm>
#include <cmath>

#include "dsp/common/math.h"

namespace eeq {
namespace {

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
    const double q = OctavesToQ(SpectralSpacingOctaves());
    Matrix m{}, inv{};
    for (int i = 0; i < kVocoderBands; ++i) {
      const SvfCoeffs c = DesignSvf(SvfShape::kBell, SpectralBandHz(i), q,
                                    kProbeDb, kReferenceRate);
      for (int j = 0; j < kVocoderBands; ++j) {
        m[j][i] = 20.0 *
                  std::log10(std::abs(
                      SvfResponse(c, SpectralBandHz(j), kReferenceRate))) /
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

void PrepareVocoderTables() { InverseInteraction(); }

void AddVocoderShape(const SpectralEnvelope& env, const GroupParams& gp,
                     VocoderMode mode, double amount, VocoderShape& shape) {
  if (amount == 0.0) return;
  if (mode == VocoderMode::kShape) {
    // Silence leaves the instrument flat; the shape fades in with the
    // level.
    const double presence = env.Presence();
    if (presence <= 0.0) return;
    double peak = SpectralEnvelope::kFloorDb;
    for (int i = 0; i < kVocoderBands; ++i)
      peak = std::max(peak, env.BandDb(i));
    const double mean = env.MeanDb();
    const double floor = peak - SpectralEnvelope::kMeanFloorDb;
    for (int i = 0; i < kVocoderBands; ++i) {
      shape[i] += static_cast<float>(presence * amount *
                                     (std::max(env.BandDb(i), floor) - mean));
    }
    return;
  }
  const double alpha = std::clamp(gp.alpha, 0.0, 1.0);
  const double teff = (1.0 - alpha) * gp.threshold_db + alpha * gp.rise_db;
  const double mean = alpha > 0.0 ? env.MeanDb() : 0.0;
  for (int i = 0; i < kVocoderBands; ++i) {
    const double trigger = env.BandDb(i) - alpha * mean;
    const double over =
        gp.mode == TriggerMode::kAbove ? trigger - teff : teff - trigger;
    if (over > 0.0) shape[i] += static_cast<float>(amount * over);
  }
}

BandDesign DesignVocoder(double shift, double gain_db,
                         const VocoderShape& shape, double sample_rate) {
  BandDesign d;
  d.topology = BandTopology::kSerial;
  d.post_gain = DbToGain(gain_db);
  const double q = OctavesToQ(SpectralSpacingOctaves());
  const Matrix& inv = InverseInteraction();
  for (int i = 0; i < kVocoderBands; ++i) {
    double gain = 0.0;
    for (int j = 0; j < kVocoderBands; ++j) gain += inv[i][j] * shape[j];
    const double hz =
        std::clamp(SpectralBandHz(i) * shift, 20.0, 0.45 * sample_rate);
    d.cells[d.num_cells++] =
        DesignSvf(SvfShape::kBell, hz, q,
                  std::clamp(gain, -kMaxGainDb, kMaxGainDb), sample_rate);
  }
  return d;
}

}  // namespace eeq
