#ifndef EVERYTHINGEQ_TOOLS_RENDER_SIGNALS_H_
#define EVERYTHINGEQ_TOOLS_RENDER_SIGNALS_H_

// Deterministic synthetic test signals for the use-case renders.  They are
// crude stand-ins for real recordings, but exercise the same dynamics.

#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "dsp/bands/vowel.h"
#include "dsp/common/math.h"
#include "dsp/svf/svf.h"
#include "tools/render/wav.h"

namespace signals {

struct Gen {
  double fs;
  size_t n;
  std::mt19937 rng;
  // mt19937 output is standardized; distributions are not, so map it by hand
  // to keep renders identical across standard libraries.
  double White() { return rng() * (2.0 / 4294967296.0) - 1.0; }
  double Time(size_t i) const { return static_cast<double>(i) / fs; }
};

inline wav::Audio Mono(const std::vector<double>& x, double fs) {
  wav::Audio a;
  a.sample_rate = fs;
  a.l.resize(x.size()), a.r.resize(x.size());
  for (size_t i = 0; i < x.size(); ++i)
    a.l[i] = a.r[i] = static_cast<float>(x[i]);
  return a;
}

// Snare: noise burst (stick attack) + decaying 190 Hz body/ring, varying
// velocity.
inline std::vector<double> Snare(Gen& g, double bpm) {
  std::vector<double> x(g.n, 0.0);
  const size_t step = static_cast<size_t>(g.fs * 60.0 / bpm);
  eeq::SvfCell hp;
  hp.Snap(eeq::DesignSvf(eeq::SvfShape::kHighPass, 2500.0, 0.7, 0.0, g.fs));
  int hit = 0;
  for (size_t start = step / 2; start < g.n; start += step, ++hit) {
    const double vel =
        (hit % 4 == 3) ? 0.15 : (hit % 2 ? 0.9 : 0.6);  // with ghost notes
    for (size_t i = start; i < std::min(g.n, start + step); ++i) {
      const double t = g.Time(i - start);
      const double attack = hp.Process(g.White()) * std::exp(-t / 0.004);
      const double body =
          std::sin(2 * eeq::kPi * 190.0 * t) * std::exp(-t / 0.12);
      const double rattle = g.White() * 0.3 * std::exp(-t / 0.06);
      x[i] += vel * (0.8 * attack + 0.6 * body + rattle);
    }
  }
  return x;
}

// Bass: band-limited saw notes with a fast pluck envelope.
inline std::vector<double> Bass(Gen& g) {
  const double notes[] = {41.2, 55.0, 61.7, 49.0};
  std::vector<double> x(g.n, 0.0);
  const size_t len = static_cast<size_t>(g.fs * 0.5);
  for (size_t i = 0; i < g.n; ++i) {
    const size_t k = i / len;
    const double f = notes[k % 4], t = g.Time(i % len);
    double s = 0.0;
    for (int h = 1; h * f < 6000.0; ++h)
      s += std::sin(2 * eeq::kPi * h * f * t) / h;
    x[i] =
        0.3 * s * (0.4 + 0.6 * std::exp(-t / 0.15)) * std::min(1.0, t / 0.002);
  }
  return x;
}

// Pink-ish noise whose level steps between loud and quiet sections.
inline std::vector<double> LevelSteps(Gen& g) {
  std::vector<double> x(g.n);
  double b0 = 0, b1 = 0, b2 = 0;
  for (size_t i = 0; i < g.n; ++i) {
    const double w = g.White();
    b0 = 0.99765 * b0 + w * 0.0990460;
    b1 = 0.96300 * b1 + w * 0.2965164;
    b2 = 0.57000 * b2 + w * 1.0526913;
    const double pink = (b0 + b1 + b2 + w * 0.1848) * 0.2;
    const double level_db =
        (static_cast<size_t>(g.Time(i) / 1.5) % 2) ? -36.0 : -12.0;
    x[i] = pink * eeq::DbToGain(level_db);
  }
  return x;
}

// Dense keys bed: detuned saw chords.
inline std::vector<double> Keys(Gen& g) {
  const double chord[] = {220.0, 277.2, 329.6, 440.0, 554.4};
  std::vector<double> x(g.n, 0.0);
  for (size_t i = 0; i < g.n; ++i) {
    double s = 0.0;
    for (double f : chord)
      for (int h = 1; h * f < 8000.0; ++h)
        s += std::sin(2 * eeq::kPi * h * f * 1.003 * g.Time(i)) / h;
    x[i] = 0.06 * s;
  }
  return x;
}

// Vocal-ish sidechain: glottal buzz through /a/-/e/ formants, syllable gated.
inline std::vector<double> Vocal(Gen& g) {
  std::vector<double> x(g.n, 0.0);
  eeq::SvfCell f[3];
  double phase = 0.0;
  for (size_t i = 0; i < g.n; ++i) {
    const double t = g.Time(i);
    if (i % 64 == 0) {
      const eeq::Formants fm =
          eeq::VowelFormants(2.0 + std::sin(2 * eeq::kPi * 0.5 * t));
      for (int k = 0; k < 3; ++k)
        f[k].SetTarget(
            eeq::DesignSvf(eeq::SvfShape::kBandPass, fm.hz[k], 8.0, 0, g.fs),
            64);
    }
    phase += 160.0 / g.fs;
    if (phase >= 1.0) phase -= 1.0;
    const double buzz = 1.0 - 2.0 * phase;
    const double syllable = std::max(0.0, std::sin(2 * eeq::kPi * 2.0 * t));
    double s = 0.0;
    for (int k = 0; k < 3; ++k) s += f[k].Process(buzz);
    x[i] = 0.5 * s * syllable;
  }
  return x;
}

// Kick drum over a constant low rumble.
inline std::vector<double> KickRumble(Gen& g, double bpm) {
  std::vector<double> x(g.n, 0.0);
  const size_t step = static_cast<size_t>(g.fs * 60.0 / bpm);
  double ph = 0.0;
  for (size_t i = 0; i < g.n; ++i) {
    const double t = g.Time(i % step);
    const double f = 50.0 + 120.0 * std::exp(-t / 0.03);
    ph += f / g.fs;
    const double kick = std::sin(2 * eeq::kPi * ph) * std::exp(-t / 0.25);
    const double rumble =
        0.08 * std::sin(2 * eeq::kPi * 25.0 * g.Time(i)) + 0.02 * g.White();
    x[i] = 0.7 * kick + rumble;
  }
  return x;
}

// Plucked guitar-ish notes (Karplus-Strong) with varying pick velocity.
inline std::vector<double> Pluck(Gen& g, double bpm) {
  const double notes[] = {110.0, 146.8, 164.8, 196.0,
                          220.0, 164.8, 146.8, 123.5};
  const double vel[] = {1.0, 0.35, 0.7, 0.25, 0.9, 0.5, 0.3, 0.8};
  std::vector<double> x(g.n, 0.0);
  const size_t step = static_cast<size_t>(g.fs * 60.0 / bpm / 2);
  int k = 0;
  for (size_t start = 0; start < g.n; start += step, ++k) {
    const size_t period = static_cast<size_t>(g.fs / notes[k % 8]);
    std::vector<double> line(period);
    for (double& v : line) v = g.White() * vel[k % 8];
    size_t p = 0;
    for (size_t i = start; i < std::min(g.n, start + step); ++i) {
      const size_t q = (p + 1) % period;
      const double out = line[p];
      line[p] = 0.996 * 0.5 * (line[p] + line[q]);
      p = q;
      x[i] += 0.6 * out;
    }
  }
  return x;
}

// Tempo pulses for the sidechain auto-wah trick: short 2 kHz tone bursts.
inline std::vector<double> Pulses(Gen& g, double bpm, double division) {
  std::vector<double> x(g.n, 0.0);
  const double period = 60.0 / bpm / division;
  for (size_t i = 0; i < g.n; ++i) {
    const double t = std::fmod(g.Time(i), period);
    x[i] = 0.5 * std::sin(2 * eeq::kPi * 2000.0 * g.Time(i)) *
           std::exp(-t / (0.25 * period));
  }
  return x;
}

inline wav::Audio Generate(const std::string& kind, double fs, double seconds,
                           uint32_t seed, double bpm) {
  Gen g{fs, static_cast<size_t>(fs * seconds), std::mt19937(seed)};
  if (kind == "snare") return Mono(Snare(g, bpm), fs);
  if (kind == "bass") return Mono(Bass(g), fs);
  if (kind == "levelSteps") return Mono(LevelSteps(g), fs);
  if (kind == "keys") return Mono(Keys(g), fs);
  if (kind == "vocal") return Mono(Vocal(g), fs);
  if (kind == "kickRumble") return Mono(KickRumble(g, bpm), fs);
  if (kind == "pluck") return Mono(Pluck(g, bpm), fs);
  if (kind == "pulses") return Mono(Pulses(g, bpm, 2.0), fs);
  if (kind == "noise") {
    std::vector<double> x(g.n);
    for (double& v : x) v = 0.25 * g.White();
    return Mono(x, fs);
  }
  throw std::runtime_error("unknown generator: " + kind);
}

}  // namespace signals

#endif  // EVERYTHINGEQ_TOOLS_RENDER_SIGNALS_H_
