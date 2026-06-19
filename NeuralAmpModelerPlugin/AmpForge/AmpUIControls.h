#pragma once
#include "IControls.h"
#include "Colors.h"
#include <functional>
#include <string>
#include <cstdio>

using namespace iplug;
using namespace igraphics;

// ---- Nav bar: draws all tabs as one custom control -------------------------
class NavBarControl : public IControl
{
public:
    NavBarControl(const IRECT& bounds, std::function<void(int)> onTabChange)
    : IControl(bounds)
    , mOnTabChange(onTabChange)
    {}

    void Draw(IGraphics& g) override
    {
        const float eqW = 80.f;
        const float tW  = (mRECT.W() - eqW) / 4.f;

        static const char* kLabels[] = {"FX1", "FX2", "AMP", "IR'S", "EQ"};
        float widths[5] = {tW, tW, tW, tW, eqW};
        float x = mRECT.L;

        for (int i = 0; i < 5; i++)
        {
            const IRECT r(x, mRECT.T, x + widths[i], mRECT.B);
            const bool hov = (i == mHoveredTab);
            const IColor bg = (i == 4) ? PluginColors::HOTONE_BTN : PluginColors::HOTONE_OLIVE;
            g.FillRect(hov ? bg.WithContrast(0.12f) : bg, r);

            if (i > 0)
                g.DrawVerticalLine(PluginColors::HOTONE_BG, x, mRECT.T, mRECT.B);

            IText txt(48.f, COLOR_WHITE, "Inter-Bold");
            IRECT tr = r;
            g.DrawText(txt, kLabels[i], tr);

            x += widths[i];
        }
    }

    void OnMouseDown(float x, float y, const IMouseMod& mod) override
    {
        int tab = _TabAt(x);
        if (tab >= 0 && mOnTabChange) mOnTabChange(tab);
    }

    void OnMouseOver(float x, float y, const IMouseMod& mod) override
    {
        int t = _TabAt(x);
        if (t != mHoveredTab) { mHoveredTab = t; SetDirty(false); }
    }

    void OnMouseOut() override
    {
        if (mHoveredTab != -1) { mHoveredTab = -1; SetDirty(false); }
    }

private:
    int _TabAt(float x) const
    {
        const float eqW = 80.f;
        const float tW  = (mRECT.W() - eqW) / 4.f;
        if (x >= mRECT.R - eqW) return 4;
        int t = (int)((x - mRECT.L) / tW);
        return (t >= 0 && t < 4) ? t : -1;
    }

    std::function<void(int)> mOnTabChange;
    int mHoveredTab = -1;
};

// ---- SVG button with transparent bg (for nav arrows) -----------------------
class AmpSVGButton : public IControl
{
    using AF = std::function<void(IControl*)>;
public:
    AmpSVGButton(const IRECT& bounds, AF af, const ISVG& svg)
    : IControl(bounds)
    , mAF(af)
    , mSVG(svg)
    {}

    void Draw(IGraphics& g) override
    {
        if (GetMouseIsOver())
            g.FillRect(IColor(40, 255, 255, 255), mRECT);

        if (mSVG.IsValid())
        {
            const float sz = std::min(mRECT.W() - 8.f, 60.f);
            g.DrawSVG(mSVG, mRECT.GetCentredInside(sz));
        }
    }

    void OnMouseDown(float x, float y, const IMouseMod& mod) override
    {
        if (mAF) mAF(this);
    }

private:
    AF   mAF;
    ISVG mSVG;
};

// ---- Dark rounded box (for status row IN/OUT/BPM) -------------------------
class StatusBoxControl : public IControl
{
    using AF = std::function<void(IControl*)>;
public:
    StatusBoxControl(const IRECT& bounds, const char* text, bool clickable = false,
                     AF af = nullptr)
    : IControl(bounds)
    , mText(text)
    , mClickable(clickable)
    , mAF(af)
    {}

    void SetStr(const char* s) { mText = s; SetDirty(false); }

    void Draw(IGraphics& g) override
    {
        const IColor bg     = (mClickable && GetMouseIsOver())
                              ? IColor(255, 42, 42, 42)
                              : PluginColors::HOTONE_BTN;
        const float  cr     = 4.f;

        g.FillRoundRect(bg, mRECT, cr);
        g.DrawRoundRect(PluginColors::HOTONE_BORDER, mRECT, cr, nullptr, 1.2f);

        IText txt(13.f, COLOR_WHITE, "Inter-Regular");
        IRECT r = mRECT;
        g.DrawText(txt, mText.c_str(), r);
    }

    void OnMouseDown(float x, float y, const IMouseMod& mod) override
    {
        if (mClickable && mAF) mAF(this);
    }

private:
    std::string mText;
    bool        mClickable;
    AF          mAF;
};

// ---- "Change tone..." wide search box ------------------------------------
class ChangeToneControl : public IControl
{
    using AF = std::function<void(IControl*)>;
public:
    ChangeToneControl(const IRECT& bounds, AF af)
    : IControl(bounds)
    , mAF(af)
    {}

    void Draw(IGraphics& g) override
    {
        const IColor bg = GetMouseIsOver() ? IColor(255, 36, 36, 36) : PluginColors::HOTONE_BTN;
        const float  cr = 4.f;

        g.FillRoundRect(bg, mRECT, cr);
        g.DrawRoundRect(PluginColors::HOTONE_BORDER, mRECT, cr, nullptr, 1.2f);

        // Search icon (right side)
        const float ix = mRECT.R - 24.f;
        const float iy = mRECT.MH();
        g.DrawCircle(PluginColors::HOTONE_GREY, ix, iy, 7.f, nullptr, 1.5f);
        g.DrawLine(PluginColors::HOTONE_GREY, ix + 5.f, iy + 5.f, ix + 11.f, iy + 11.f, nullptr, 1.5f);

        IText txt(14.f, PluginColors::HOTONE_GREY, "Inter-Regular");
        txt.mAlign = EAlign::Near;
        IRECT textR(mRECT.L + 12.f, mRECT.T, mRECT.R - 36.f, mRECT.B);
        g.DrawText(txt, "Change tone...", textR);
    }

    void OnMouseDown(float x, float y, const IMouseMod& mod) override
    {
        if (mAF) mAF(this);
    }

private:
    AF mAF;
};
