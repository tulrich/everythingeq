#pragma once

#include "IControl.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

// Vertical segmented peak meter (ABOUT.md §4): green below -12 dBFS, yellow
// up to 0 dBFS, red above.  Scale runs to +6 dBFS because float signals can
// exceed full scale.  A numeric readout shows the recent peak; once any peak
// exceeds 0 dBFS it latches (red) until clicked or ResetClip() is called
// (e.g. on transport start).
//
// UI thread only.  Feed it per-channel linear peaks gathered since the last
// call with AddPeaks(); ballistics are computed from wall-clock time.
class LevelMeter final : public iplug::igraphics::IControl
{
public:
  static constexpr int kMaxChannels = 4;
  static constexpr float kMinDb = -60.f;
  static constexpr float kMaxDb = 6.f;
  static constexpr float kYellowDb = -12.f;
  static constexpr float kRedDb = 0.f;
  static constexpr float kFallDbPerSec = 20.f;
  static constexpr float kHoldSec = 1.5f;

  LevelMeter(const iplug::igraphics::IRECT& bounds, const char* label, std::vector<std::string> channelNames)
  : IControl(bounds), mLabel(label), mNames(std::move(channelNames))
  {
    mNumChannels = std::min(static_cast<int>(mNames.size()), kMaxChannels);
    mLevelDb.fill(kMinDb);
    mHoldDb.fill(kMinDb);
    mHoldAge.fill(0.f);
    mActive.fill(true);
    SetTooltip("Click to reset the peak readout");
  }

  void SetChannelActive(int ch, bool active)
  {
    if (ch < mNumChannels && mActive[ch] != active) { mActive[ch] = active; SetDirty(false); }
  }

  void AddPeaks(const float* linearPeaks)
  {
    const auto now = std::chrono::steady_clock::now();
    float dt = mLastUpdate.time_since_epoch().count() ? std::chrono::duration<float>(now - mLastUpdate).count() : 0.f;
    dt = std::min(dt, 0.25f);
    mLastUpdate = now;

    for (int ch = 0; ch < mNumChannels; ++ch)
    {
      const float db = linearPeaks[ch] > 0.f ? 20.f * std::log10(linearPeaks[ch]) : -200.f;
      mLevelDb[ch] = std::max(db, std::max(mLevelDb[ch] - kFallDbPerSec * dt, -200.f));
      if (db >= mHoldDb[ch]) { mHoldDb[ch] = db; mHoldAge[ch] = 0.f; }
      else if ((mHoldAge[ch] += dt) > kHoldSec) mHoldDb[ch] = std::max(mHoldDb[ch] - kFallDbPerSec * dt, -200.f);
      if (db > kRedDb) mClipped = true;
      if (mClipped) mLatchedDb = std::max(mLatchedDb, db);
    }
    SetDirty(false);
  }

  void ResetClip()
  {
    mClipped = false;
    mLatchedDb = -200.f;
    mHoldDb.fill(-200.f);
    SetDirty(false);
  }

  void OnMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) override { ResetClip(); }

  void Draw(iplug::igraphics::IGraphics& g) override
  {
    using namespace iplug::igraphics;
    const IText labelText(13.f, COLOR_LIGHT_GRAY, "Roboto-Regular", EAlign::Center);
    const IText scaleText(10.f, COLOR_GRAY, "Roboto-Regular", EAlign::Far, EVAlign::Middle);

    IRECT r = mRECT;
    const IRECT labelR = r.ReduceFromTop(18.f);
    const IRECT readoutR = r.ReduceFromBottom(22.f).GetPadded(-2.f);
    r.ReduceFromBottom(4.f);
    r.ReduceFromTop(6.f);  // room for the +6 label
    const IRECT scaleR = r.ReduceFromLeft(22.f);
    mBarsR = r;

    g.DrawText(labelText, mLabel.c_str(), labelR);

    // Scale ticks.
    for (float db : {6.f, 0.f, -6.f, -12.f, -24.f, -36.f, -48.f, -60.f})
    {
      const float y = DbToY(db);
      WDL_String s;
      s.SetFormatted(8, db > 0.f ? "+%d" : "%d", static_cast<int>(db));
      g.DrawText(scaleText, s.Get(), IRECT(scaleR.L, y - 6.f, scaleR.R - 3.f, y + 6.f));
      g.DrawLine(COLOR_GRAY.WithOpacity(0.5f), scaleR.R - 2.f, y, scaleR.R, y);
    }

    // Bars.
    constexpr float kSegPitch = 4.f, kSegGap = 1.f, kBarGap = 3.f;
    const float barW = (mBarsR.W() - kBarGap * (mNumChannels - 1)) / mNumChannels;
    for (int ch = 0; ch < mNumChannels; ++ch)
    {
      const float x0 = mBarsR.L + ch * (barW + kBarGap);
      const IRECT barR(x0, mBarsR.T, x0 + barW, mBarsR.B);
      g.FillRect(IColor(255, 18, 18, 20), barR);
      if (!mActive[ch])
        continue;
      for (float yTop = mBarsR.B - kSegPitch; yTop >= mBarsR.T - 0.5f; yTop -= kSegPitch)
      {
        const float segDb = YToDb(yTop + 0.5f * kSegPitch);
        const bool lit = segDb <= mLevelDb[ch];
        g.FillRect(ZoneColor(segDb).WithOpacity(lit ? 1.f : 0.12f),
                   IRECT(barR.L, yTop, barR.R, yTop + kSegPitch - kSegGap));
      }
      if (mHoldDb[ch] > kMinDb)
      {
        const float y = DbToY(std::min(mHoldDb[ch], kMaxDb));
        g.FillRect(ZoneColor(mHoldDb[ch]), IRECT(barR.L, y - 1.f, barR.R, y + 1.f));
      }
    }

    // Peak readout.
    float shownDb = -200.f;
    for (int ch = 0; ch < mNumChannels; ++ch)
      if (mActive[ch]) shownDb = std::max(shownDb, mHoldDb[ch]);
    if (mClipped) shownDb = mLatchedDb;
    g.FillRoundRect(mClipped ? IColor(255, 200, 30, 30) : IColor(255, 30, 30, 34), readoutR, 3.f);
    WDL_String s;
    if (shownDb <= -100.f) s.Set("-inf");
    else s.SetFormatted(16, "%+.1f", shownDb);
    g.DrawText(IText(13.f, COLOR_WHITE, "Roboto-Regular", EAlign::Center), s.Get(), readoutR);
  }

private:
  float DbToY(float db) const
  {
    const float u = (std::clamp(db, kMinDb, kMaxDb) - kMinDb) / (kMaxDb - kMinDb);
    return mBarsR.B - u * mBarsR.H();
  }
  float YToDb(float y) const { return kMinDb + (mBarsR.B - y) / mBarsR.H() * (kMaxDb - kMinDb); }

  static iplug::igraphics::IColor ZoneColor(float db)
  {
    if (db > kRedDb) return iplug::igraphics::IColor(255, 235, 50, 45);
    if (db > kYellowDb) return iplug::igraphics::IColor(255, 235, 200, 40);
    return iplug::igraphics::IColor(255, 60, 200, 90);
  }

  std::string mLabel;
  std::vector<std::string> mNames;
  int mNumChannels = 0;
  std::array<float, kMaxChannels> mLevelDb{};
  std::array<float, kMaxChannels> mHoldDb{};
  std::array<float, kMaxChannels> mHoldAge{};
  std::array<bool, kMaxChannels> mActive{};
  bool mClipped = false;
  float mLatchedDb = -200.f;
  iplug::igraphics::IRECT mBarsR;
  std::chrono::steady_clock::time_point mLastUpdate{};
};
