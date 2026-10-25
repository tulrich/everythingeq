#include "bands/BandDesign.h"

#include <algorithm>
#include <cmath>

#include "bands/FletcherMunson.h"
#include "bands/Vowel.h"
#include "common/Math.h"

namespace eeq {

namespace {

double clampF(double hz) { return std::clamp(hz, kMinFreq, kMaxFreq); }
double clampQ(double q) { return std::clamp(q, kMinQ, kMaxQ); }
double clampGain(double db) { return std::clamp(db, -kMaxGainDb, kMaxGainDb); }

BandDesign serial(std::initializer_list<SvfCoeffs> cells, double postGain = 1.0) {
    BandDesign d;
    d.topology = BandTopology::Serial;
    for (const SvfCoeffs& c : cells) d.cells[d.numCells++] = c;
    d.postGain = postGain;
    return d;
}

// ITU-R BS.1770 K-weighting expressed exactly as two SVF cells.
BandDesign kWeighting(double fs) {
    const double shelfHz = 1681.974450955533;
    const double shelfQ = 0.7071752369554196;
    const double vh = std::pow(10.0, 3.999843853973347 / 20.0);
    const double vb = std::pow(vh, 0.4996667741545416);
    const SvfCoeffs shelf = makeSvf(shelfHz, 1.0 / shelfQ, vh, (vb - vh) / shelfQ, 1.0 - vh, fs);
    const double hpQ = 0.5003270373238773;
    const SvfCoeffs hp = makeSvf(38.13547087602444, 1.0 / hpQ, 1.0, -1.0 / hpQ, -1.0, fs);
    return serial({shelf, hp});
}

BandDesign fletcherMunson(double phonDelta, double fs) {
    phonDelta = std::clamp(phonDelta, -kMaxPhonDelta, kMaxPhonDelta);
    BandDesign d = serial({});
    for (const FmCell& c : kFmCells)
        d.cells[d.numCells++] = designSvf(c.shape, c.hz, c.q, c.dbPerPhon * phonDelta, fs);
    // Anchor the contour to 0 dB at 1 kHz.
    d.postGain = 1.0 / std::abs(bandResponse(d, 1000.0, fs));
    return d;
}

BandDesign vowel(double morph, double gainDb, double scale, int option, double fs) {
    const Formants f = vowelFormants(morph);
    scale = std::clamp(scale, kTractScaleMin, kTractScaleMax);
    gainDb = clampGain(gainDb);
    BandDesign d = serial({});
    for (int i = 0; i < 3; ++i) {
        const double g = option == static_cast<int>(VowelWeighting::Flat)
                             ? gainDb
                             : gainDb * dbToGain(f.weightDb[i]);
        d.cells[d.numCells++] = designSvf(SvfShape::Bell, f.hz[i] * scale, kFormantQ[i], g, fs);
    }
    return d;
}

}  // namespace

BandDesign designBand(BandType type, double x, double y, double z, int option, double fs) {
    switch (type) {
        case BandType::Off:
            return {};
        case BandType::Peak:
            return serial({designSvf(SvfShape::Bell, clampF(x), clampQ(z), clampGain(y), fs)});
        case BandType::LowShelf:
            return serial({designSvf(SvfShape::LowShelf, clampF(x), clampQ(z), clampGain(y), fs)});
        case BandType::HighShelf:
            return serial({designSvf(SvfShape::HighShelf, clampF(x), clampQ(z), clampGain(y), fs)});
        case BandType::LP12:
            return serial({designSvf(SvfShape::LowPass, clampF(x), clampQ(z), 0.0, fs)});
        case BandType::HP12:
            return serial({designSvf(SvfShape::HighPass, clampF(x), clampQ(z), 0.0, fs)});
        case BandType::Notch:
            return serial({designSvf(SvfShape::Notch, clampF(x), clampQ(z), 0.0, fs)});
        case BandType::BP: {
            const double gain = y <= kBpOffDb ? 0.0 : dbToGain(std::min(y, kMaxGainDb));
            return serial({designSvf(SvfShape::BandPass, clampF(x), clampQ(z), 0.0, fs)}, gain);
        }
        case BandType::LP24_LR:
        case BandType::HP24_LR: {
            BandDesign d;
            d.topology = BandTopology::Crossover;
            d.numCells = 3;
            const SvfCoeffs lp = designSvf(SvfShape::LowPass, clampF(x), kButterworthQ, 0.0, fs);
            const SvfCoeffs hp = designSvf(SvfShape::HighPass, clampF(x), kButterworthQ, 0.0, fs);
            d.cells = {lp, lp, hp, SvfCoeffs{}};
            d.crossoverHigh = type == BandType::HP24_LR;
            return d;
        }
        case BandType::Tilt: {
            const double half = std::clamp(y, -kMaxTiltDb, kMaxTiltDb) / 2.0;
            return serial({designSvf(SvfShape::LowShelf, clampF(x), kTiltShelfQ, -half, fs),
                           designSvf(SvfShape::HighShelf, clampF(x), kTiltShelfQ, half, fs)});
        }
        case BandType::FletcherMunson:
            return fletcherMunson(y, fs);
        case BandType::Vowel:
            return vowel(x, y, z, option, fs);
        case BandType::KWeight:
            return kWeighting(fs);
    }
    return {};
}

namespace {

std::complex<double> crossoverResponse(const BandDesign& d, bool high, double f, double fs) {
    if (high) {
        SvfCoeffs splitHp = d.cells[0];
        splitHp.m0 = 1.0, splitHp.m1 = -splitHp.k, splitHp.m2 = -1.0;
        return svfResponse(splitHp, f, fs) * svfResponse(d.cells[2], f, fs);
    }
    return svfResponse(d.cells[0], f, fs) * svfResponse(d.cells[1], f, fs);
}

}  // namespace

std::complex<double> bandResponse(const BandDesign& d, double f, double fs) {
    switch (d.topology) {
        case BandTopology::Passthrough:
            return 1.0;
        case BandTopology::Crossover:
            return crossoverResponse(d, d.crossoverHigh, f, fs);
        case BandTopology::Serial: {
            std::complex<double> h = d.postGain;
            for (int i = 0; i < d.numCells; ++i) h *= svfResponse(d.cells[i], f, fs);
            return h;
        }
    }
    return 1.0;
}

std::complex<double> bandComplementResponse(const BandDesign& d, double f, double fs) {
    if (d.topology != BandTopology::Crossover) return 0.0;
    return crossoverResponse(d, !d.crossoverHigh, f, fs);
}

DetectorDesign designDetector(BandType type, double x, double z, double fs) {
    DetectorDesign d;
    auto add = [&](SvfShape shape, double hz, double q) {
        d.cells[d.numCells++] = designSvf(shape, hz, q, 0.0, fs);
    };
    switch (type) {
        case BandType::Peak:
        case BandType::Notch:
        case BandType::BP:
            add(SvfShape::BandPass, clampF(x), clampQ(z));
            break;
        case BandType::LowShelf:
        case BandType::LP12:
        case BandType::LP24_LR:
            add(SvfShape::LowPass, clampF(x), kButterworthQ);
            break;
        case BandType::HighShelf:
        case BandType::HP12:
        case BandType::HP24_LR:
            add(SvfShape::HighPass, clampF(x), kButterworthQ);
            break;
        case BandType::Vowel: {
            const Formants f = vowelFormants(x);
            const double scale = std::clamp(z, kTractScaleMin, kTractScaleMax);
            for (int i = 0; i < 3; ++i) add(SvfShape::BandPass, f.hz[i] * scale, kFormantQ[i]);
            break;
        }
        case BandType::Off:
        case BandType::Tilt:
        case BandType::FletcherMunson:
        case BandType::KWeight:
            break;
    }
    return d;
}

std::complex<double> detectorResponse(const DetectorDesign& d, double f, double fs) {
    if (d.numCells == 0) return 1.0;
    std::complex<double> h = 0.0;
    for (int i = 0; i < d.numCells; ++i) h += svfResponse(d.cells[i], f, fs);
    return h;
}

}  // namespace eeq
