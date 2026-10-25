#include "svf/Svf.h"

#include <algorithm>
#include <cmath>

#include "common/Math.h"

namespace eeq {

double qToOctaves(double q) {
    return 2.0 / kLn2 * std::asinh(1.0 / (2.0 * q));
}

double octavesToQ(double octaves) {
    return 1.0 / (2.0 * std::sinh(kLn2 / 2.0 * octaves));
}

double prewarpQ(double q, double freqHz, double sampleRate) {
    const double w0 = 2.0 * kPi * freqHz / sampleRate;
    const double octaves = qToOctaves(q) * w0 / std::sin(w0);
    return std::max(octavesToQ(octaves), 0.025);
}

SvfCoeffs makeSvf(double freqHz, double k, double m0, double m1, double m2, double sampleRate) {
    SvfCoeffs c;
    c.g = std::tan(kPi * clampFreq(freqHz, sampleRate) / sampleRate);
    c.k = k;
    c.m0 = m0;
    c.m1 = m1;
    c.m2 = m2;
    return c;
}

SvfCoeffs designSvf(SvfShape shape, double freqHz, double q, double gainDb, double sampleRate) {
    const double f = clampFreq(freqHz, sampleRate);
    q = std::clamp(q, 0.025, 100.0);
    SvfCoeffs c;
    c.g = std::tan(kPi * f / sampleRate);
    c.k = 1.0 / q;
    switch (shape) {
        case SvfShape::LowPass:
            c.m0 = 0.0, c.m1 = 0.0, c.m2 = 1.0;
            break;
        case SvfShape::HighPass:
            c.m0 = 1.0, c.m1 = -c.k, c.m2 = -1.0;
            break;
        case SvfShape::BandPass:
            c.k = 1.0 / prewarpQ(q, f, sampleRate);
            c.m0 = 0.0, c.m1 = c.k, c.m2 = 0.0;
            break;
        case SvfShape::Notch:
            c.k = 1.0 / prewarpQ(q, f, sampleRate);
            c.m0 = 1.0, c.m1 = -c.k, c.m2 = 0.0;
            break;
        case SvfShape::AllPass:
            c.m0 = 1.0, c.m1 = -2.0 * c.k, c.m2 = 0.0;
            break;
        case SvfShape::Bell: {
            const double a = std::pow(10.0, gainDb / 40.0);
            c.k = 1.0 / (prewarpQ(q, f, sampleRate) * a);
            c.m0 = 1.0, c.m1 = c.k * (a * a - 1.0), c.m2 = 0.0;
            break;
        }
        case SvfShape::LowShelf: {
            const double a = std::pow(10.0, gainDb / 40.0);
            c.g /= std::sqrt(a);
            c.m0 = 1.0, c.m1 = c.k * (a - 1.0), c.m2 = a * a - 1.0;
            break;
        }
        case SvfShape::HighShelf: {
            const double a = std::pow(10.0, gainDb / 40.0);
            c.g *= std::sqrt(a);
            c.m0 = a * a, c.m1 = c.k * (1.0 - a) * a, c.m2 = 1.0 - a * a;
            break;
        }
    }
    return c;
}

std::complex<double> svfResponse(const SvfCoeffs& c, double freqHz, double sampleRate) {
    const double w = std::tan(kPi * std::min(freqHz, 0.4999 * sampleRate) / sampleRate);
    const std::complex<double> p(0.0, w / c.g);
    const std::complex<double> den = p * p + c.k * p + 1.0;
    return c.m0 + (c.m1 * p + c.m2) / den;
}

void SvfCell::snap(const SvfCoeffs& c) {
    cur_ = target_ = c;
    remaining_ = 0;
    updateDerived();
}

void SvfCell::setTarget(const SvfCoeffs& c, int rampSamples) {
    target_ = c;
    if (rampSamples <= 1) {
        snap(c);
        return;
    }
    const double inv = 1.0 / rampSamples;
    step_.g = (c.g - cur_.g) * inv;
    step_.k = (c.k - cur_.k) * inv;
    step_.m0 = (c.m0 - cur_.m0) * inv;
    step_.m1 = (c.m1 - cur_.m1) * inv;
    step_.m2 = (c.m2 - cur_.m2) * inv;
    remaining_ = rampSamples;
}

void SvfCell::advanceRamp() {
    if (--remaining_ == 0) {
        cur_ = target_;
    } else {
        cur_.g += step_.g;
        cur_.k += step_.k;
        cur_.m0 += step_.m0;
        cur_.m1 += step_.m1;
        cur_.m2 += step_.m2;
    }
    updateDerived();
}

void SvfCell::updateDerived() {
    a1_ = 1.0 / (1.0 + cur_.g * (cur_.g + cur_.k));
    a2_ = cur_.g * a1_;
    a3_ = cur_.g * a2_;
}

}  // namespace eeq
