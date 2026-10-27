#include "dsp/bands/band_processor.h"

#include <cmath>

#include "dsp/common/math.h"

namespace eeq {
namespace {

bool HasGain(BandType t) {
  switch (t) {
    case BandType::kPeak:
    case BandType::kLowShelf:
    case BandType::kHighShelf:
    case BandType::kBandShelf:
    case BandType::kBp:
    case BandType::kTilt:
    case BandType::kFletcherMunson:
    case BandType::kVowel:
      return true;
    default:
      return false;
  }
}

bool HasFreq(BandType t) {
  switch (t) {
    case BandType::kOff:
    case BandType::kFletcherMunson:
    case BandType::kVowel:
    case BandType::kKWeight:
      return false;
    default:
      return true;
  }
}

}  // namespace

ResolvedBand ResolveBand(const BandParams& bp, double control,
                         double global_scale) {
  ResolvedBand rb;
  rb.x = bp.x, rb.y = bp.y, rb.z = bp.z, rb.option = bp.option;
  const double mod = control * bp.dyn_range * global_scale;
  if (mod == 0.0) return rb;
  switch (bp.dyn_target) {
    case DynTarget::kGain:
      if (HasGain(bp.type)) rb.y += mod;
      if (HasDynMakeup(bp.type)) rb.makeup_db = -mod * bp.dyn_makeup;
      break;
    case DynTarget::kFreq:
      if (HasFreq(bp.type)) {
        rb.x *= std::exp2(mod);
      } else if (bp.type == BandType::kVowel) {
        rb.z *= std::exp2(mod);
      }
      break;
    case DynTarget::kMorph:
      if (bp.type == BandType::kVowel) rb.x += mod;
      if (bp.type == BandType::kBandShelf || bp.type == BandType::kTilt) {
        rb.z += mod;  // Width, octaves.
      }
      break;
  }
  return rb;
}

BandDesign DesignResolvedBand(BandType type, const ResolvedBand& rb,
                              double fs) {
  BandDesign d = DesignBand(type, rb.x, rb.y, rb.z, rb.option, fs);
  if (rb.makeup_db != 0.0 && d.topology == BandTopology::kSerial) {
    d.post_gain *= DbToGain(rb.makeup_db);
  }
  return d;
}

void BandProcessor::Reset() {
  for (SvfCell& c : cells_) c.Reset();
}

void BandProcessor::Apply(const BandDesign& d, int ramp_samples) {
  const bool structural = d.topology != topology_ ||
                          d.num_cells != num_cells_ ||
                          d.crossover_high != crossover_high_;
  if (structural) {
    topology_ = d.topology;
    num_cells_ = d.num_cells;
    crossover_high_ = d.crossover_high;
    Reset();
    for (int i = 0; i < num_cells_; ++i) cells_[i].Snap(d.cells[i]);
    post_.Snap(d.post_gain);
    return;
  }
  for (int i = 0; i < num_cells_; ++i)
    cells_[i].SetTarget(d.cells[i], ramp_samples);
  post_.SetTarget(d.post_gain, ramp_samples);
}

BandOutput BandProcessor::Process(double x) {
  switch (topology_) {
    case BandTopology::kPassthrough:
      return {x, 0.0};
    case BandTopology::kSerial: {
      double v = x;
      for (int i = 0; i < num_cells_; ++i) v = cells_[i].Process(v);
      return {v * post_.Next(), 0.0};
    }
    case BandTopology::kCrossover: {
      const SvfCell::Outputs s = cells_[0].Tick(x);
      const double low = cells_[1].Tick(s.low).low;
      const double high = cells_[2].Tick(s.high).high;
      return crossover_high_ ? BandOutput{high, low} : BandOutput{low, high};
    }
  }
  return {x, 0.0};
}

void BandSlot::Prepare(double sample_rate) {
  fs_ = sample_rate;
  initialized_ = false;
  Reset();
}

void BandSlot::Reset() {
  for (int ch = 0; ch < kNumChannels; ++ch) {
    cur_[ch].Reset();
    prev_[ch].Reset();
  }
  fade_ = 0;
}

bool BandSlot::Changed(const ResolvedBand& a, const ResolvedBand& b) {
  auto rel = [](double u, double v) {
    return std::abs(u - v) > 1e-4 * std::max(std::abs(u), 1e-3);
  };
  return rel(a.x, b.x) || std::abs(a.y - b.y) > 0.005 || rel(a.z, b.z) ||
         a.option != b.option || std::abs(a.makeup_db - b.makeup_db) > 0.005;
}

void BandSlot::Update(BandType type,
                      const std::array<ResolvedBand, kNumChannels>& p,
                      bool snap_now) {
  const bool type_changed = initialized_ && type != type_;
  if (type_changed) {
    prev_ = cur_;
    fade_ = kFadeSamples;
  }
  for (int ch = 0; ch < kNumChannels; ++ch) {
    if (initialized_ && !type_changed && !Changed(p[ch], last_[ch])) continue;
    last_[ch] = p[ch];
    design_[ch] = DesignResolvedBand(type, p[ch], fs_);
    if (type_changed) cur_[ch] = BandProcessor{};
    cur_[ch].Apply(design_[ch], snap_now ? 1 : kControlBlock);
  }
  type_ = type;
  initialized_ = true;
}

BandOutput BandSlot::Process(int ch, double x) {
  const BandOutput a = cur_[ch].Process(x);
  if (fade_ == 0) return a;
  const BandOutput b = prev_[ch].Process(x);
  const double t = 1.0 - static_cast<double>(fade_) / kFadeSamples;
  return {t * a.y + (1.0 - t) * b.y, t * a.comp + (1.0 - t) * b.comp};
}

void DetectorFilter::Reset() {
  for (auto& ch_cells : cells_)
    for (SvfCell& c : ch_cells) c.Reset();
}

void DetectorFilter::Apply(const DetectorDesign& d, int ramp_samples) {
  const bool structural = d.num_cells != num_cells_;
  num_cells_ = d.num_cells;
  for (auto& ch_cells : cells_) {
    for (int i = 0; i < num_cells_; ++i) {
      if (structural) {
        ch_cells[i].Reset();
        ch_cells[i].Snap(d.cells[i]);
      } else {
        ch_cells[i].SetTarget(d.cells[i], ramp_samples);
      }
    }
  }
}

double DetectorFilter::Process(int ch, double x) {
  if (num_cells_ == 0) return x;
  double sum = 0.0;
  for (int i = 0; i < num_cells_; ++i) sum += cells_[ch][i].Process(x);
  return sum;
}

}  // namespace eeq
