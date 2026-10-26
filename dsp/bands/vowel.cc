#include "dsp/bands/vowel.h"

#include <algorithm>
#include <cmath>

namespace eeq {

namespace {

struct VowelAnchor {
  double hz[3];
  double weight_db[3];
};

// /u/, /o/, /a/, /e/, /i/  (ABOUT.md section 3.C)
constexpr VowelAnchor kVowels[5] = {
    {{300.0, 870.0, 2250.0}, {0.0, -12.0, -24.0}},
    {{450.0, 1050.0, 2600.0}, {0.0, -9.0, -20.0}},
    {{730.0, 1090.0, 2440.0}, {0.0, -4.0, -18.0}},
    {{530.0, 1840.0, 2480.0}, {0.0, -6.0, -16.0}},
    {{270.0, 2290.0, 3010.0}, {0.0, -14.0, -18.0}},
};

}  // namespace

Formants VowelFormants(double morph) {
  morph = std::clamp(morph, 0.0, kVowelMorphMax);
  const int i0 = std::min(static_cast<int>(morph), 3);
  const double t = morph - i0;
  const VowelAnchor& a = kVowels[i0];
  const VowelAnchor& b = kVowels[i0 + 1];
  Formants f{};
  for (int i = 0; i < 3; ++i) {
    f.hz[i] = std::exp((1.0 - t) * std::log(a.hz[i]) + t * std::log(b.hz[i]));
    f.weight_db[i] = (1.0 - t) * a.weight_db[i] + t * b.weight_db[i];
  }
  return f;
}

}  // namespace eeq
