#ifndef EVERYTHINGEQ_PLUGIN_UI_THEME_H_
#define EVERYTHINGEQ_PLUGIN_UI_THEME_H_

#include <cmath>

#include "IGraphics.h"
#include "IGraphicsStructs.h"

namespace eeq::theme {

namespace ig = iplug::igraphics;

inline constexpr const char* kFont = "Roboto-Regular";

// Inactive controls stay in place at this opacity (ABOUT.md section 4).
inline constexpr float kInactiveOpacity = 0.35f;

inline const ig::IColor kBackground(255, 29, 31, 35);
inline const ig::IColor kPanel(255, 38, 41, 47);
inline const ig::IColor kPanelRaised(255, 48, 52, 59);
inline const ig::IColor kBorder(255, 62, 67, 76);
inline const ig::IColor kGrid(255, 52, 57, 65);
inline const ig::IColor kGridStrong(255, 76, 82, 92);
inline const ig::IColor kText(255, 220, 222, 227);
inline const ig::IColor kTextDim(255, 140, 146, 156);
inline const ig::IColor kAccent(255, 92, 170, 255);
inline const ig::IColor kOn(255, 240, 180, 60);    // Toggle "on" (Solo, etc.).
inline const ig::IColor kWarn(255, 225, 75, 60);   // Bypass.
inline const ig::IColor kGain(255, 250, 215, 60);  // Applied gain meters.
inline const ig::IColor kCaution(255, 240, 180, 60);  // Warnings.

// One hue per band slot.
inline ig::IColor BandColor(int band) {
  static const ig::IColor kColors[] = {
      {255, 255, 107, 107}, {255, 255, 169, 77},  {255, 250, 220, 90},
      {255, 140, 220, 100}, {255, 70, 210, 170},  {255, 80, 190, 240},
      {255, 110, 140, 255}, {255, 170, 120, 250}, {255, 230, 110, 220},
      {255, 200, 200, 200}};
  return kColors[band % 10];
}

inline ig::IText Text(float size, const ig::IColor& color = kText,
                      ig::EAlign align = ig::EAlign::Near,
                      ig::EVAlign valign = ig::EVAlign::Middle) {
  return ig::IText(size, color, kFont, align, valign);
}

// Groups are told apart by shape, not hue: G1 circle, G2 square, G3
// triangle, G4 diamond.
inline void FillGroupGlyph(ig::IGraphics& g, int group, float cx, float cy,
                           float r, const ig::IColor& color) {
  switch (group % 4) {
    case 0:
      g.FillCircle(color, cx, cy, r);
      break;
    case 1:
      g.FillRect(color, ig::IRECT(cx - 0.85f * r, cy - 0.85f * r,
                                  cx + 0.85f * r, cy + 0.85f * r));
      break;
    case 2:
      g.FillTriangle(color, cx, cy - r, cx + r, cy + 0.85f * r, cx - r,
                     cy + 0.85f * r);
      break;
    default: {
      float xs[] = {cx, cx + r, cx, cx - r};
      float ys[] = {cy - r, cy, cy + r, cy};
      g.FillConvexPolygon(color, xs, ys, 4);
      break;
    }
  }
}

// A point at `degrees` clockwise from 12 o'clock (IGraphics arc angles).
inline ig::IVec2 Polar(float cx, float cy, float r, float degrees) {
  const float a = degrees * 3.14159265f / 180.f;
  return ig::IVec2(cx + r * std::sin(a), cy - r * std::cos(a));
}

}  // namespace eeq::theme

#endif  // EVERYTHINGEQ_PLUGIN_UI_THEME_H_
