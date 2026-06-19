#pragma once
#include "IControls.h"
#include "Colors.h"
#include <algorithm>
#include <string>
#include <cstdio>

using namespace iplug;
using namespace igraphics;

// Horizontal bar parameter control.
// Top half: param label. Bottom half: amber fill bar proportional to normalized value.
// Drag left/right to change value.
class AmpParamBoxControl : public IControl
{
public:
    AmpParamBoxControl(const IRECT& bounds, int paramIdx, const char* overrideLabel = nullptr)
    : IControl(bounds, paramIdx)
    , mLabel(overrideLabel ? overrideLabel : "")
    {}

    void Draw(IGraphics& g) override
    {
        const IColor bg     = PluginColors::HOTONE_PANEL;   // #1C1F1C
        const IColor border = PluginColors::HOTONE_BORDER_DIM; // #706330 89%
        const IColor amber  = PluginColors::HOTONE_BORDER;  // #7E7037 slider bar
        const IColor white  = IColor(255, 255, 255, 255);
        const IColor dark   = IColor(255, 10, 10, 10);

        g.FillRect(bg, mRECT);
        g.DrawRect(border, mRECT, nullptr, 1.5f);

        const float pad    = 8.f;
        const float splitY = mRECT.T + mRECT.H() * 0.46f;

        // Label
        const char* lbl = mLabel.empty()
                          ? (GetParam() ? GetParam()->GetName() : "")
                          : mLabel.c_str();
        IText labelTxt(40.f, white, "Inter-Bold");
        IRECT labelArea(mRECT.L + pad, mRECT.T + pad, mRECT.R - pad, splitY);
        g.DrawText(labelTxt, lbl, labelArea);

        // Bar background
        IRECT barOuter(mRECT.L + pad, splitY + 4.f, mRECT.R - pad, mRECT.B - pad);
        g.FillRect(dark, barOuter);
        g.DrawRect(border, barOuter, nullptr, 1.f);

        // Amber fill
        double normVal = GetValue();
        if (normVal > 0.001)
        {
            IRECT fill = barOuter;
            fill.R = fill.L + (float)(fill.W() * normVal);
            g.FillRect(amber, fill);
        }

        // Value text
        WDL_String disp;
        if (GetParam()) GetParam()->GetDisplay(disp, true);
        IText valTxt(32.f, normVal > 0.2 ? dark : white, "Inter-Bold");
        g.DrawText(valTxt, disp.Get(), barOuter);
    }

    void OnMouseDown(float x, float y, const IMouseMod& mod) override
    {
        mDragStartX   = x;
        mDragStartVal = GetValue();
    }

    void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
    {
        double newVal = mDragStartVal + (double)(x - mDragStartX) / mRECT.W();
        newVal = std::clamp(newVal, 0.0, 1.0);
        SetValue(newVal);
        SetDirty(true);
    }

private:
    std::string mLabel;
    float       mDragStartX   = 0.f;
    double      mDragStartVal = 0.0;
};
