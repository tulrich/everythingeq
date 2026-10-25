#pragma once

#include "IControl.h"

#include <chrono>
#include <cmath>

// Phase 0 Spike B: redraws an analyzer-sized load every frame (a 2048-point
// filled spectrum plus ten 512-point band curves) and reports the CPU-side
// draw time and the measured frame interval.  Removed once the real UI lands.
class SpikeStressControl final : public iplug::igraphics::IControl
{
public:
  explicit SpikeStressControl(const iplug::igraphics::IRECT& bounds)
  : IControl(bounds)
  {
    mIgnoreMouse = true;
  }

  bool IsDirty() override { return true; }  // animate every frame

  void Draw(iplug::igraphics::IGraphics& g) override
  {
    using namespace iplug::igraphics;
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();

    const IRECT r = mRECT;
    g.FillRect(IColor(255, 24, 26, 30), r);
    mPhase += 0.05f;

    // Spectrum: filled path.
    constexpr int kSpectrumPoints = 2048;
    g.PathClear();
    g.PathMoveTo(r.L, r.B);
    for (int i = 0; i < kSpectrumPoints; ++i)
    {
      const float u = static_cast<float>(i) / (kSpectrumPoints - 1);
      const float level = 0.5f + 0.25f * std::sin(40.f * u + mPhase) * std::sin(7.f * u - 0.3f * mPhase) +
                          0.1f * std::sin(173.f * u + 3.f * mPhase);
      g.PathLineTo(r.L + u * r.W(), r.B - level * r.H() * 0.8f);
    }
    g.PathLineTo(r.R, r.B);
    g.PathClose();
    g.PathFill(IColor(110, 120, 140, 170));

    // Band curves: stroked paths.
    constexpr int kBands = 10;
    constexpr int kCurvePoints = 512;
    for (int b = 0; b < kBands; ++b)
    {
      const float centre = (b + 0.5f) / kBands;
      const float gain = 0.25f * std::sin(mPhase + b);
      g.PathClear();
      for (int i = 0; i < kCurvePoints; ++i)
      {
        const float u = static_cast<float>(i) / (kCurvePoints - 1);
        const float d = (u - centre) * 12.f;
        const float y = r.MH() - gain * r.H() / (1.f + d * d);
        if (i == 0) g.PathMoveTo(r.L + u * r.W(), y);
        else g.PathLineTo(r.L + u * r.W(), y);
      }
      g.PathStroke(IColor::FromHSLA(static_cast<float>(b) / kBands, 0.7f, 0.6f), 1.5f);
    }

    // Stats (exponentially smoothed).
    const auto t1 = Clock::now();
    const double drawMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (mLastFrame.time_since_epoch().count() != 0)
    {
      const double frameMs = std::chrono::duration<double, std::milli>(t0 - mLastFrame).count();
      mFrameMs += 0.05 * (frameMs - mFrameMs);
    }
    mLastFrame = t0;
    mDrawMs += 0.05 * (drawMs - mDrawMs);

    WDL_String str;
    str.SetFormatted(128, "Spike B: draw %.2f ms (CPU side), frame interval %.1f ms (%.0f fps), scale %.2f",
                     mDrawMs, mFrameMs, mFrameMs > 0. ? 1000. / mFrameMs : 0., g.GetDrawScale() * g.GetScreenScale());
    g.DrawText(IText(14.f, COLOR_WHITE, "Roboto-Regular", EAlign::Near, EVAlign::Top), str.Get(), r.GetPadded(-8.f));
  }

private:
  float mPhase = 0.f;
  double mDrawMs = 0.;
  double mFrameMs = 0.;
  std::chrono::steady_clock::time_point mLastFrame{};
};
