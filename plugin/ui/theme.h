#ifndef EVERYTHINGEQ_PLUGIN_UI_THEME_H_
#define EVERYTHINGEQ_PLUGIN_UI_THEME_H_

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

}  // namespace eeq::theme

#endif  // EVERYTHINGEQ_PLUGIN_UI_THEME_H_
