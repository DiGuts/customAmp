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

    // Margins (design space @ 1280): 16 top, 20 sides, 20 between, 20 bottom
    static constexpr float kML  = 20.f;  // left
    static constexpr float kMR  = 20.f;  // right
    static constexpr float kMT  = 16.f;  // top
    static constexpr float kMB  = 20.f;  // bottom
    static constexpr float kGap = 20.f;  // between buttons
    static constexpr int   kTabs = 4;

    IRECT _BtnRect(int i) const
    {
        const float innerW = mRECT.W() - kML - kMR - kGap * (kTabs - 1);
        const float btnW   = innerW / kTabs;
        const float x      = mRECT.L + kML + i * (btnW + kGap);
        return IRECT(x, mRECT.T + kMT, x + btnW, mRECT.B - kMB);
    }

    void Draw(IGraphics& g) override
    {
        static const char* kLabels[] = {"FX1", "FX2", "AMP", "IR'S"};

        for (int i = 0; i < kTabs; i++)
        {
            const IRECT r   = _BtnRect(i);
            const bool  hov = (i == mHoveredTab);
            g.FillRect(hov ? PluginColors::HOTONE_BTN.WithContrast(0.10f) : PluginColors::HOTONE_BTN, r);
            g.DrawRect(PluginColors::HOTONE_BORDER, r, nullptr, 5.f);

            IText txt(48.f, COLOR_WHITE, "Inter-Bold");
            g.DrawText(txt, kLabels[i], r);
        }
    }

    void OnMouseDown(float x, float y, const IMouseMod& mod) override
    {
        int tab = _TabAt(x, y);
        if (tab >= 0 && mOnTabChange) mOnTabChange(tab);
    }

    void OnMouseOver(float x, float y, const IMouseMod& mod) override
    {
        int t = _TabAt(x, y);
        if (t != mHoveredTab) { mHoveredTab = t; SetDirty(false); }
    }

    void OnMouseOut() override
    {
        if (mHoveredTab != -1) { mHoveredTab = -1; SetDirty(false); }
    }

private:
    int _TabAt(float x, float y) const
    {
        for (int i = 0; i < kTabs; i++)
            if (_BtnRect(i).Contains(x, y)) return i;
        return -1;
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
        if (mIsPressed)
            g.FillRect(PluginColors::HOTONE_PRESSED, mRECT);
        else if (GetMouseIsOver())
            g.FillRect(IColor(40, 255, 255, 255), mRECT);

        if (mSVG.IsValid())
        {
            const float sz = std::min(mRECT.W() - 8.f, 60.f);
            // default = #FFFFFF, pressed = #1D1B20
            const IColor col = mIsPressed ? PluginColors::HOTONE_ARROW : IColor(255, 255, 255, 255);
            g.DrawSVG(mSVG, mRECT.GetCentredInside(sz), nullptr, &col, &col);
        }
    }

    void OnMouseDown(float x, float y, const IMouseMod& mod) override
    {
        mIsPressed = true;
        SetDirty(false);
        if (mAF) mAF(this);
    }

    void OnMouseUp(float x, float y, const IMouseMod& mod) override
    {
        mIsPressed = false;
        SetDirty(false);
    }

private:
    AF   mAF;
    ISVG mSVG;
    bool mIsPressed = false;
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
                              ? PluginColors::HOTONE_PANEL.WithContrast(0.08f)
                              : PluginColors::HOTONE_PANEL;
        const float  cr     = 4.f;

        g.FillRoundRect(bg, mRECT, cr);
        g.DrawRoundRect(PluginColors::HOTONE_BORDER_DIM, mRECT, cr, nullptr, 1.f);

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

// ---- Outer olive panel with thick golden border (slot 3 container) --------
class AmpOutlinePanel : public IControl
{
public:
    AmpOutlinePanel(const IRECT& bounds) : IControl(bounds) { mIgnoreMouse = true; }

    void Draw(IGraphics& g) override
    {
        g.FillRoundRect(PluginColors::HOTONE_BTN, mRECT, 6.f);          // olive #383A27
        g.DrawRoundRect(PluginColors::HOTONE_BORDER, mRECT, 6.f, nullptr, 5.f); // golden #7E7037 w5
    }
};

// ---- Labeled on/off toggle button (GATE / TONE EQ) ------------------------
// Small label on top, bordered button below showing "on"/"off".
class LabeledToggleControl : public IControl
{
public:
    LabeledToggleControl(const IRECT& bounds, int paramIdx, const char* label)
    : IControl(bounds, paramIdx)
    , mLabel(label)
    {}

    void Draw(IGraphics& g) override
    {
        // Label (top)
        IText lblTxt(13.f, PluginColors::HOTONE_GREY, "Inter-Regular");
        IRECT lblArea = mRECT.GetFromTop(20.f);
        g.DrawText(lblTxt, mLabel.c_str(), lblArea);

        // Button (below label)
        IRECT btn = mRECT.GetReducedFromTop(22.f);
        const bool on = GetValue() > 0.5;
        const IColor bg = on ? PluginColors::HOTONE_BTN.WithContrast(0.06f)
                             : PluginColors::HOTONE_BTN;
        g.FillRoundRect(bg, btn, 4.f);
        g.DrawRoundRect(PluginColors::HOTONE_BORDER, btn, 4.f, nullptr, 1.5f);

        IText valTxt(15.f, COLOR_WHITE, "Inter-ExtraBold");
        g.DrawText(valTxt, on ? "on" : "off", btn);
    }

    void OnMouseDown(float x, float y, const IMouseMod& mod) override
    {
        SetValue(GetValue() > 0.5 ? 0.0 : 1.0);
        SetDirty(true);
    }

private:
    std::string mLabel;
};

// ---- Joined IN | OUT box with center separator ---------------------------
class InOutBoxControl : public IControl
{
public:
    InOutBoxControl(const IRECT& bounds)
    : IControl(bounds)
    {}

    void SetInStr(const char* s)  { mIn = s;  SetDirty(false); }
    void SetOutStr(const char* s) { mOut = s; SetDirty(false); }

    void Draw(IGraphics& g) override
    {
        const float cr = 4.f;
        g.FillRoundRect(PluginColors::HOTONE_PANEL, mRECT, cr);
        g.DrawRoundRect(PluginColors::HOTONE_BORDER_DIM, mRECT, cr, nullptr, 1.f);

        // vertical separator (center)
        g.DrawVerticalLine(PluginColors::HOTONE_BORDER_DIM, mRECT.MW(), mRECT.T + 8.f, mRECT.B - 8.f, nullptr, 1.f);

        IText txt(13.f, COLOR_WHITE, "Inter-Regular");
        IRECT lr(mRECT.L, mRECT.T, mRECT.MW(), mRECT.B);
        IRECT rr(mRECT.MW(), mRECT.T, mRECT.R, mRECT.B);
        g.DrawText(txt, mIn.c_str(),  lr);
        g.DrawText(txt, mOut.c_str(), rr);
    }

private:
    std::string mIn  = "IN: 0 dB";
    std::string mOut = "OUT: 0 dB";
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
        const float cr   = 4.f;
        const float pad  = 13.f;            // 13px top/bottom/right pad (Figma)
        const float iconW = 58.f;           // 58×58 magnifier box (Figma)

        // Text field — left region, 58px tall centred (Figma: 469×58)
        IRECT field(mRECT.L, mRECT.T + pad, mRECT.R - iconW - pad, mRECT.B - pad);
        const IColor bg = GetMouseIsOver() ? PluginColors::HOTONE_BTN.WithContrast(0.06f)
                                           : PluginColors::HOTONE_BTN;
        g.FillRoundRect(bg, field, cr);
        g.DrawRoundRect(PluginColors::HOTONE_BORDER, field, cr, nullptr, 1.2f);

        IText txt(20.f, PluginColors::HOTONE_GREY, "Inter-Bold");
        txt.mAlign = EAlign::Near;
        IRECT textR = field.GetReducedFromLeft(16.f);
        g.DrawText(txt, "Change tone...", textR);

        // Magnifier — 58×58 box at right
        IRECT iconBox(mRECT.R - iconW, mRECT.MH() - iconW * 0.5f, mRECT.R, mRECT.MH() + iconW * 0.5f);
        const float ix = iconBox.MW() - 4.f;
        const float iy = iconBox.MH() - 4.f;
        g.DrawCircle(PluginColors::HOTONE_GREY, ix, iy, 11.f, nullptr, 2.f);
        g.DrawLine(PluginColors::HOTONE_GREY, ix + 8.f, iy + 8.f, ix + 16.f, iy + 16.f, nullptr, 2.f);
    }

    void OnMouseDown(float x, float y, const IMouseMod& mod) override
    {
        if (mAF) mAF(this);
    }

private:
    AF mAF;
};
