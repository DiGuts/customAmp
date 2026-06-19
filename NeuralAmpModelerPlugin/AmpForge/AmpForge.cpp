#include <algorithm> // std::clamp, std::min
#include <cmath> // pow
#include <filesystem>
#include <iostream>
#include <utility>

#include "Colors.h"
#include "../NeuralAmpModelerCore/NAM/activations.h"
#include "../NeuralAmpModelerCore/NAM/get_dsp.h"
// clang-format off
// These includes need to happen in this order or else the latter won't know
// a bunch of stuff.
#include "AmpForge.h"
#include "IPlug_include_in_plug_src.h"
// clang-format on
#include "architecture.hpp"

#include "AmpForgeControls.h"
#include "AmpParamBox.h"
#include "AmpUIControls.h"
#ifdef _WIN32
#include <shlobj.h>
#include "ToneLibrary.h"
#include "Tone3000Browser.h"
#endif

using namespace iplug;
using namespace igraphics;

const double kDCBlockerFrequency = 5.0;

// Hotone Ampero Mini dark/amber theme
const IVColorSpec colorSpec{
  PluginColors::HOTONE_BG,                        // Background
  PluginColors::HOTONE_AMBER,                     // Foreground (knob tracks)
  PluginColors::HOTONE_DIM,                       // Pressed
  PluginColors::HOTONE_BORDER,                    // Frame
  PluginColors::HOTONE_AMBER.WithOpacity(0.45f),  // Highlight
  IColor(255, 0, 0, 0),                           // Shadow
  PluginColors::HOTONE_AMBER,                     // Extra1
  COLOR_RED,                                      // Extra2 (clip)
  PluginColors::HOTONE_DIM,                       // Extra3
};

const IVStyle style = IVStyle{
  true,  // Show label
  true,  // Show value
  colorSpec,
  IText(13.f, PluginColors::HOTONE_GREY,  "Inter-Regular"),  // label text
  IText(13.f, PluginColors::HOTONE_AMBER, "Inter-Regular"),  // value text
  DEFAULT_HIDE_CURSOR,
  true,  // draw frame
  false,
  DEFAULT_EMBOSS,
  0.3f,  // roundness
  1.5f,  // frame thickness
  DEFAULT_SHADOW_OFFSET,
  DEFAULT_WIDGET_FRAC,
  DEFAULT_WIDGET_ANGLE
};

const IVStyle titleStyle = DEFAULT_STYLE
  .WithColor(EVColor::kBG, PluginColors::HOTONE_BG)
  .WithValueText(IText(26.f, COLOR_WHITE, "Michroma-Regular"))
  .WithDrawFrame(false)
  .WithShadowOffset(1.f);

const IVStyle radioButtonStyle = style
  .WithColor(EVColor::kON,  PluginColors::HOTONE_AMBER)
  .WithColor(EVColor::kOFF, PluginColors::HOTONE_BTN)
  .WithColor(EVColor::kX1,  PluginColors::HOTONE_DIM);

EMsgBoxResult _ShowMessageBox(iplug::igraphics::IGraphics* pGraphics, const char* str, const char* caption,
                              EMsgBoxType type)
{
#ifdef OS_MAC
  // macOS is backwards?
  return pGraphics->ShowMessageBox(caption, str, type);
#else
  return pGraphics->ShowMessageBox(str, caption, type);
#endif
}

const std::string kCalibrateInputParamName = "CalibrateInput";
const bool kDefaultCalibrateInput = false;
const std::string kInputCalibrationLevelParamName = "InputCalibrationLevel";
const double kDefaultInputCalibrationLevel = 12.0;


AmpForge::AmpForge(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  _InitToneStack();
  nam::activations::Activation::enable_fast_tanh();
#ifdef _WIN32
  mToneLib.SetApiKey("7c917fc6-8327-4de4-b236-86fedf0548f5");
#endif
  GetParam(kInputLevel)->InitGain("Input", 0.0, -20.0, 20.0, 0.1);
  GetParam(kToneBass)->InitDouble("Bass", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneMid)->InitDouble("Middle", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneTreble)->InitDouble("Treble", 5.0, 0.0, 10.0, 0.1);
  GetParam(kOutputLevel)->InitGain("Output", 0.0, -40.0, 40.0, 0.1);
  GetParam(kNoiseGateThreshold)->InitGain("Threshold", -80.0, -100.0, 0.0, 0.1);
  GetParam(kNoiseGateActive)->InitBool("NoiseGateActive", true);
  GetParam(kEQActive)->InitBool("ToneStack", true);
  GetParam(kOutputMode)->InitEnum("OutputMode", 1, {"Raw", "Normalized", "Calibrated"}); // TODO DRY w/ control
  GetParam(kIRToggle)->InitBool("IRToggle", true);
  GetParam(kCalibrateInput)->InitBool(kCalibrateInputParamName.c_str(), kDefaultCalibrateInput);
  GetParam(kInputCalibrationLevel)
    ->InitDouble(kInputCalibrationLevelParamName.c_str(), kDefaultInputCalibrationLevel, -60.0, 60.0, 0.1, "dBu");
  GetParam(kSlim)->InitDouble("Slim", 0.0, 0.0, 1.0, 0.01);

  // Doubler
  GetParam(kDoublerActive)->InitBool("DoublerActive", false);
  GetParam(kDoublerDelayMs)->InitDouble("DoublerDelay", 6.0, 0.5, 30.0, 0.1, "ms");
  // Transpose stub (passthrough)
  GetParam(kTranspose)->InitInt("Transpose", 0, -12, 12, "st");
  // Input mode
  GetParam(kInputModeStereo)->InitBool("InputModeStereo", false);
  // Parametric EQ bands (±12 dB)
  GetParam(kEQBand0)->InitDouble("EQ 65Hz",  0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kEQBand1)->InitDouble("EQ 125Hz", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kEQBand2)->InitDouble("EQ 250Hz", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kEQBand3)->InitDouble("EQ 500Hz", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kEQBand4)->InitDouble("EQ 1kHz",  0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kEQBand5)->InitDouble("EQ 2kHz",  0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kEQBand6)->InitDouble("EQ 4kHz",  0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kEQBand7)->InitDouble("EQ 8kHz",  0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kEQBand8)->InitDouble("EQ 16kHz", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kEQHPFFreq)->InitDouble("HPF Freq", 0.0, 0.0, 1000.0, 1.0, "Hz");
  GetParam(kEQLPFFreq)->InitDouble("LPF Freq", 0.0, 0.0, 20000.0, 10.0, "Hz");
  GetParam(kEQPageActive)->InitBool("EQPageActive", false);
  // Tuner
  GetParam(kTunerLive)->InitBool("TunerLive", false);
  GetParam(kTunerRef)->InitDouble("TunerRef", 440.0, 400.0, 480.0, 0.5, "Hz");

  mNoiseGateTrigger.AddListener(&mNoiseGateGain);

  mMakeGraphicsFunc = [&]() {

#ifdef OS_IOS
    auto scaleFactor = GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT) * 0.85f;
#else
    auto scaleFactor = 1.0f;
#endif

    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, scaleFactor);
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    pGraphics->AttachTextEntryControl();
    pGraphics->EnableMouseOver(true);
    pGraphics->EnableTooltips(true);
    pGraphics->EnableMultiTouch(true);

    pGraphics->LoadFont("Roboto-Regular",            ROBOTO_FN);
    pGraphics->LoadFont("Michroma-Regular",          MICHROMA_FN);
    pGraphics->LoadFont("Inter-Regular",             INTER_REGULAR_FN);
    pGraphics->LoadFont("Inter-Bold",                INTER_BOLD_FN);
    pGraphics->LoadFont("Inter-ExtraBold",           INTER_EXTRABOLD_FN);
    pGraphics->LoadFont("BarlowSemiCondensed-Black", BARLOW_BLACK_FN);

    // ---- Load assets ----
    const auto gearSVG        = pGraphics->LoadSVG(GEAR_FN);
    const auto fileSVG        = pGraphics->LoadSVG(FILE_FN);
    const auto globeSVG       = pGraphics->LoadSVG(GLOBE_ICON_FN);
    const auto crossSVG       = pGraphics->LoadSVG(CLOSE_BUTTON_FN);
    const auto rightArrowSVG  = pGraphics->LoadSVG(RIGHT_ARROW_FN);
    const auto leftArrowSVG   = pGraphics->LoadSVG(LEFT_ARROW_FN);
    const auto irIconOnSVG    = pGraphics->LoadSVG(IR_ICON_ON_FN);
    const auto irIconOffSVG   = pGraphics->LoadSVG(IR_ICON_OFF_FN);
    const auto slimIconSVG    = pGraphics->LoadSVG(SLIMMABLE_ICON_FN);

    const auto backgroundBitmap          = pGraphics->LoadBitmap(BACKGROUND_FN);
    const auto fileBackgroundBitmap      = pGraphics->LoadBitmap(FILEBACKGROUND_FN);
    const auto inputLevelBackgroundBitmap = pGraphics->LoadBitmap(INPUTLEVELBACKGROUND_FN);
    const auto knobBackgroundBitmap      = pGraphics->LoadBitmap(KNOBBACKGROUND_FN);
    const auto switchHandleBitmap        = pGraphics->LoadBitmap(SLIDESWITCHHANDLE_FN);
    const auto meterBackgroundBitmap     = pGraphics->LoadBitmap(METERBACKGROUND_FN);
    // ampHeadBitmap / ampKnobBitmap removed — AMP page now uses vector Hotone-style UI

    const auto b = pGraphics->GetBounds();  // 1280 x 800

    // ---- Zone geometry (1280x800, Figma-measured) ----
    // Nav band = 16px top + 100px buttons + 20px bottom = 136px
    const float kNavH      = 136.f;  // full nav band incl. 16/20 margins
    const float kStatusH   = 84.f;   // Status row — Change tone fills full 84px
    const float kBotStripH = 163.f;  // 145px boxes + 18px bottom margin (Figma)

    const IRECT navRect         = b.GetFromTop(kNavH);
    const IRECT statusRect      = b.GetFromTop(kNavH + kStatusH).GetFromBottom(kStatusH);
    const IRECT ampMainRect     = b.GetReducedFromTop(kNavH + kStatusH).GetReducedFromBottom(kBotStripH);
    const IRECT botStripRect    = b.GetFromBottom(kBotStripH);
    const IRECT fullContentRect = b.GetReducedFromTop(kNavH + kStatusH);

    // ---- Global background ---- (solid fill — guarantees no OS gray shows through transparent gaps)
    pGraphics->AttachPanelBackground(PluginColors::HOTONE_BG);
    pGraphics->AttachControl(new IVPanelControl(b, "",
      DEFAULT_STYLE
        .WithColor(EVColor::kBG, PluginColors::HOTONE_BG)
        .WithColor(EVColor::kFG, COLOR_TRANSPARENT)
        .WithDrawFrame(false)));

    // ---- NAV BAR: custom-drawn single control ----
    pGraphics->AttachControl(new NavBarControl(navRect,
      [pGraphics](int tab) {
        static const char* grps[] = {"PAGE_P1","PAGE_P2","PAGE_AMP","PAGE_IR","PAGE_EQ"};
        for (int j = 0; j < 5; j++)
          pGraphics->ForControlInGroup(grps[j], [j, tab](IControl* c) { c->Hide(j != tab); });
        pGraphics->SetAllControlsDirty();
      }));

    // ---- STATUS ROW: IN | OUT | Change tone... | T3K | BPM ----
    {
      // Dark BG strip
      pGraphics->AttachControl(new IVPanelControl(statusRect, "",
        DEFAULT_STYLE
          .WithColor(EVColor::kBG, PluginColors::HOTONE_BG)
          .WithColor(EVColor::kFG, COLOR_TRANSPARENT)
          .WithDrawFrame(false)));

      // Status row heights — Figma: ChangeTone=84px full height, IN/OUT/BPM=61px centred
      const float sBoxH = 61.f;
      const float sT_box = statusRect.T + (kStatusH - sBoxH) * 0.5f;
      const float sB_box = sT_box + sBoxH;
      const float sT_full = statusRect.T;
      const float sB_full = statusRect.B;

      // IN | OUT — joined single box with center separator (Figma image4)
      // x=20 left margin, total width 404 (IN 196 + OUT 212 - shared edge → 404)
      pGraphics->AttachControl(
        new InOutBoxControl(IRECT(20.f, sT_box, 424.f, sB_box)),
        kCtrlTagInputLevelDisplay);

      // Change tone — x=432, w=568, full row height 84px (Figma: 568×84)
      pGraphics->AttachControl(new ChangeToneControl(
        IRECT(432.f, sT_full, 1000.f, sB_full),
        [this, pGraphics](IControl*) {
          WDL_String file, dir;
          pGraphics->PromptForFile(file, dir, EFileAction::Open, "nam",
            [this, pGraphics](const WDL_String& f, const WDL_String&) {
              if (f.GetLength())
              {
                const std::string msg = _StageModel(f);
                if (!msg.empty())
                {
                  std::stringstream ss;
                  ss << "Failed to load NAM model:\n\n" << msg;
                  _ShowMessageBox(pGraphics, ss.str().c_str(), "Failed to load model!", kMB_OK);
                }
              }
            });
        }));

      // Tone3000 browse button
      pGraphics->AttachControl(new StatusBoxControl(
        IRECT(1098.f, sT_box, 1148.f, sB_box), "...", true,
        [pGraphics](IControl*) {
#ifdef _WIN32
          if (auto* br = pGraphics->GetControlWithTag(kCtrlTagTone3000Browser))
            br->Hide(false);
          pGraphics->SetAllControlsDirty();
#endif
        }));

      // BPM — x=1156, w=116
      pGraphics->AttachControl(
        new StatusBoxControl(IRECT(1156.f, sT_box, 1272.f, sB_box), "BPM: 120"));

      // Hidden meter controls — needed for TransmitData
      const IRECT offscreen = IRECT(-4.f, -4.f, -2.f, -2.f);
      pGraphics->AttachControl(
        new NAMMeterControl(offscreen, meterBackgroundBitmap, style),
        kCtrlTagInputMeter)->Hide(true);
      pGraphics->AttachControl(
        new NAMMeterControl(offscreen, meterBackgroundBitmap, style),
        kCtrlTagOutputMeter)->Hide(true);
    }

    // ---- PAGE_FX1 (Pedal Slot 1) ----
    {
      pGraphics->AttachControl(new IVPanelControl(fullContentRect, "",
        DEFAULT_STYLE.WithColor(EVColor::kBG, PluginColors::HOTONE_PANEL).WithColor(EVColor::kFG, COLOR_TRANSPARENT).WithDrawFrame(false)),
        -1, "PAGE_P1");
      pGraphics->AttachControl(
        new IVLabelControl(fullContentRect.GetFromTop(40.f), "FX1 — PEDAL SLOT 1", titleStyle),
        -1, "PAGE_P1");

      auto loadPedal1 = [&](const WDL_String& fileName, const WDL_String&) {
        if (fileName.GetLength()) _StagePedal1(fileName);
      };
      pGraphics->AttachControl(
        new NAMFileBrowserControl(
          fullContentRect.GetMidVPadded(18.f).GetMidHPadded(260.f).GetVShifted(10.f),
          kMsgTagClearPedal1, "Load Pedal 1 (.nam)...", "nam",
          loadPedal1, style, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
          fileBackgroundBitmap, globeSVG, "Get NAM Models", "https://www.neuralampmodeler.com/"),
        kCtrlTagPedal1FileBrowser, "PAGE_P1");
    }

    // ---- PAGE_FX2 (Pedal Slot 2) ----
    {
      pGraphics->AttachControl(new IVPanelControl(fullContentRect, "",
        DEFAULT_STYLE.WithColor(EVColor::kBG, PluginColors::HOTONE_PANEL).WithColor(EVColor::kFG, COLOR_TRANSPARENT).WithDrawFrame(false)),
        -1, "PAGE_P2");
      pGraphics->AttachControl(
        new IVLabelControl(fullContentRect.GetFromTop(40.f), "FX2 — PEDAL SLOT 2", titleStyle),
        -1, "PAGE_P2");

      auto loadPedal2 = [&](const WDL_String& fileName, const WDL_String&) {
        if (fileName.GetLength()) _StagePedal2(fileName);
      };
      pGraphics->AttachControl(
        new NAMFileBrowserControl(
          fullContentRect.GetMidVPadded(18.f).GetMidHPadded(260.f).GetVShifted(10.f),
          kMsgTagClearPedal2, "Load Pedal 2 (.nam)...", "nam",
          loadPedal2, style, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
          fileBackgroundBitmap, globeSVG, "Get NAM Models", "https://www.neuralampmodeler.com/"),
        kCtrlTagPedal2FileBrowser, "PAGE_P2");
    }

    // ---- PAGE_AMP ----
    {
      pGraphics->AttachControl(new IVPanelControl(ampMainRect, "",
        DEFAULT_STYLE.WithColor(EVColor::kBG, PluginColors::HOTONE_BG).WithColor(EVColor::kFG, COLOR_TRANSPARENT).WithDrawFrame(false)),
        -1, "PAGE_AMP");

      // Arrow buttons — Figma: 120×390, centered vertically in main area
      const float arrowW = 120.f;
      const float arrowH = 390.f;
      const float arrowT = ampMainRect.T + (ampMainRect.H() - arrowH) * 0.5f;
      const float arrowB = arrowT + arrowH;

      pGraphics->AttachControl(new AmpSVGButton(
        IRECT(ampMainRect.L, arrowT, ampMainRect.L + arrowW, arrowB),
        [this](IControl*) { _NavigateModel(-1); }, leftArrowSVG),
        -1, "PAGE_AMP");

      pGraphics->AttachControl(new AmpSVGButton(
        IRECT(ampMainRect.R - arrowW, arrowT, ampMainRect.R, arrowB),
        [this](IControl*) { _NavigateModel(1); }, rightArrowSVG),
        -1, "PAGE_AMP");

      // Large model name — Figma: 1057×314, centered between arrows vertically centred
      const IVStyle bigNameStyle = DEFAULT_STYLE
        .WithColor(EVColor::kBG, COLOR_TRANSPARENT)
        .WithColor(EVColor::kFG, COLOR_TRANSPARENT)
        .WithDrawFrame(false)
        .WithValueText(IText(200.f, COLOR_WHITE, "BarlowSemiCondensed-Black"));

      const float nameH   = 314.f;
      const float nameVOff = (ampMainRect.H() - nameH) * 0.5f;
      pGraphics->AttachControl(
        new IVLabelControl(
          IRECT(ampMainRect.L + arrowW,
                ampMainRect.T + nameVOff,
                ampMainRect.R - arrowW,
                ampMainRect.T + nameVOff + nameH),
          "NO MODEL LOADED", bigNameStyle),
        kCtrlTagAmpModelName, "PAGE_AMP");

      // Slim icon (hidden until slimmable model loaded)
      pGraphics->AttachControl(
        new NAMSquareButtonControl(
          IRECT(ampMainRect.R - 50.f, ampMainRect.T + 6.f, ampMainRect.R - 8.f, ampMainRect.T + 44.f),
          DefaultClickActionFunc, slimIconSVG),
        kCtrlTagSlimmableIcon, "PAGE_AMP")
        ->SetAnimationEndActionFunction([pGraphics](IControl*) {
          if (auto* bd = pGraphics->GetControlWithTag(kCtrlTagSlimOverlayBackdrop)) bd->Hide(false);
          if (auto* kn = pGraphics->GetControlWithTag(kCtrlTagSlimKnob))           kn->Hide(false);
          pGraphics->SetAllControlsDirty();
        })
        ->Hide(true);

      // Bottom strip — Figma: 3×400px boxes, 20px side margins, 20px gaps, 18px bottom margin
      const float bsMX  = 20.f;   // side margin
      const float bsGap = 20.f;   // gap between boxes
      const float bsBoxH = 145.f; // box height (18px bottom margin already in botStripRect)
      const float bsW   = (b.W() - 2.f * bsMX - 2.f * bsGap) / 3.f;  // = 400 @ 1280
      const float bsT   = botStripRect.T;
      const float bsB   = botStripRect.T + bsBoxH;
      const float x0 = bsMX;
      const float x1 = x0 + bsW + bsGap;
      const float x2 = x1 + bsW + bsGap;

      pGraphics->AttachControl(
        new AmpParamBoxControl(IRECT(x0, bsT, x0 + bsW, bsB),
                               kNoiseGateThreshold, "NR Threshold"),
        -1, "PAGE_AMP");
      pGraphics->AttachControl(
        new AmpParamBoxControl(IRECT(x1, bsT, x1 + bsW, bsB),
                               kOutputLevel, "AMP - Master"),
        -1, "PAGE_AMP");

      // Slot 3: Gate + Tone EQ — outer golden panel + 2 labeled toggle buttons
      {
        const IRECT slot3(x2, bsT, x2 + bsW, bsB);

        // Outer panel: olive bg + thick golden border (matches nav buttons)
        pGraphics->AttachControl(new AmpOutlinePanel(slot3), -1, "PAGE_AMP");

        const float padX = 14.f;  // inner side padding
        const float padY = 10.f;  // inner top/bottom padding
        const float gap  = 10.f;  // gap between the two rows
        const float rowH = (slot3.H() - 2.f * padY - gap) * 0.5f;
        const float rTop = slot3.T + padY;

        pGraphics->AttachControl(new LabeledToggleControl(
          IRECT(slot3.L + padX, rTop, slot3.R - padX, rTop + rowH),
          kNoiseGateActive, "GATE"), -1, "PAGE_AMP");
        pGraphics->AttachControl(new LabeledToggleControl(
          IRECT(slot3.L + padX, rTop + rowH + gap, slot3.R - padX, rTop + 2.f * rowH + gap),
          kEQActive, "TONE EQ"), -1, "PAGE_AMP");
      }
    }

    // ---- PAGE_IR (IR / Cab loader) ----
    {
      pGraphics->AttachControl(new IVPanelControl(fullContentRect, "",
        DEFAULT_STYLE.WithColor(EVColor::kBG, PluginColors::HOTONE_PANEL).WithColor(EVColor::kFG, COLOR_TRANSPARENT).WithDrawFrame(false)),
        -1, "PAGE_IR");
      pGraphics->AttachControl(
        new IVLabelControl(fullContentRect.GetFromTop(40.f), "IR / CAB LOADER", titleStyle),
        -1, "PAGE_IR");

      auto loadIR = [&](const WDL_String& fileName, const WDL_String&) {
        if (fileName.GetLength())
        {
          mIRPath = fileName;
          const dsp::wav::LoadReturnCode rc = _StageIR(fileName);
          if (rc != dsp::wav::LoadReturnCode::SUCCESS)
          {
            std::stringstream msg;
            msg << "Failed to load IR: " << fileName.Get() << "\n"
                << dsp::wav::GetMsgForLoadReturnCode(rc);
            _ShowMessageBox(GetUI(), msg.str().c_str(), "Failed to load IR!", kMB_OK);
          }
        }
      };

      const IRECT irBrowserRect = fullContentRect.GetFromTop(110.f).GetFromBottom(40.f).GetMidHPadded(280.f);
      pGraphics->AttachControl(
        new NAMFileBrowserControl(irBrowserRect, kMsgTagClearIR, "Select IR (.wav)...", "wav",
                                  loadIR, style, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
                                  fileBackgroundBitmap, globeSVG, "Get IRs", "https://www.neuralampmodeler.com/"),
        kCtrlTagIRFileBrowser, "PAGE_IR");

      const IRECT irSwitchRect = fullContentRect.GetFromTop(160.f).GetFromBottom(30.f).GetMidHPadded(28.f);
      pGraphics->AttachControl(
        new ISVGSwitchControl(irSwitchRect, {irIconOffSVG, irIconOnSVG}, kIRToggle),
        -1, "PAGE_IR");
    }

    // ---- PAGE_EQ (9-band graphic EQ + HPF/LPF) ----
    {
      pGraphics->AttachControl(new IVPanelControl(fullContentRect, "",
        DEFAULT_STYLE.WithColor(EVColor::kBG, PluginColors::HOTONE_PANEL).WithColor(EVColor::kFG, COLOR_TRANSPARENT).WithDrawFrame(false)),
        -1, "PAGE_EQ");
      pGraphics->AttachControl(
        new IVLabelControl(fullContentRect.GetFromTop(34.f), "PARAMETRIC EQ", titleStyle),
        -1, "PAGE_EQ");

      static const char* kBandLabels[] = {"65", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
      const int   nBands      = 9;
      const float sliderAreaW = fullContentRect.W() - 60.f;
      const float sliderW     = sliderAreaW / (nBands + 2);
      const float sliderH     = fullContentRect.H() - 76.f;
      const float sliderT     = fullContentRect.T + 38.f;
      const float sliderL     = fullContentRect.L + 30.f;

      for (int i = 0; i < nBands; i++)
      {
        const float sx = sliderL + i * sliderW;
        pGraphics->AttachControl(
          new IVSliderControl(IRECT(sx, sliderT, sx + sliderW - 4.f, sliderT + sliderH),
                              kEQBand0 + i, kBandLabels[i], DEFAULT_STYLE, true, EDirection::Vertical),
          -1, "PAGE_EQ");
      }

      const float knobSz = sliderW - 4.f;
      const float hpfX   = sliderL + nBands * sliderW + 4.f;
      pGraphics->AttachControl(
        new IVKnobControl(IRECT(hpfX, sliderT + 10.f, hpfX + knobSz, sliderT + 10.f + knobSz),
                          kEQHPFFreq, "HPF", DEFAULT_STYLE),
        -1, "PAGE_EQ");

      const float lpfX = hpfX + sliderW;
      pGraphics->AttachControl(
        new IVKnobControl(IRECT(lpfX, sliderT + 10.f, lpfX + knobSz, sliderT + 10.f + knobSz),
                          kEQLPFFreq, "LPF", DEFAULT_STYLE),
        -1, "PAGE_EQ");

      pGraphics->AttachControl(
        new IVSwitchControl(fullContentRect.GetFromBottom(42.f).GetMidHPadded(64.f),
                            kEQPageActive, "EQ On", style),
        -1, "PAGE_EQ");
    }

    // ---- SETTINGS OVERLAY (hidden) ----
    pGraphics->AttachControl(
      new NAMSettingsPageControl(b, backgroundBitmap, inputLevelBackgroundBitmap, switchHandleBitmap,
                                 crossSVG, style, radioButtonStyle),
      kCtrlTagSettingsBox)->Hide(true);

    // Settings gear (corner)
    pGraphics->AttachControl(new NAMCircleButtonControl(
      CornerButtonArea(b),
      [pGraphics](IControl*) {
        pGraphics->GetControlWithTag(kCtrlTagSettingsBox)->As<NAMSettingsPageControl>()->HideAnimated(false);
      }, gearSVG));

    // ---- SLIM OVERLAY (hidden) ----
    {
      auto hideSlimOverlay = [pGraphics](IControl*) {
        if (auto* bd = pGraphics->GetControlWithTag(kCtrlTagSlimOverlayBackdrop)) bd->Hide(true);
        if (auto* kn = pGraphics->GetControlWithTag(kCtrlTagSlimKnob))           kn->Hide(true);
        pGraphics->SetAllControlsDirty();
      };
      const auto slimKnobArea = b.GetCentredInside(100.f, NAM_KNOB_HEIGHT + 24.f);
      pGraphics->AttachControl(new NAMSlimOverlayBackdropControl(b, hideSlimOverlay),
                               kCtrlTagSlimOverlayBackdrop)->Hide(true);
      pGraphics->AttachControl(new NAMKnobControl(slimKnobArea, kSlim, "Slim", style, knobBackgroundBitmap),
                               kCtrlTagSlimKnob)->Hide(true);
    }

    // ---- TUNER OVERLAY (hidden) ----
    {
      const IRECT tunerRect = b.GetCentredInside(300.f, 200.f);
      pGraphics->AttachControl(
        new IVPanelControl(tunerRect, "",
          DEFAULT_STYLE
            .WithColor(EVColor::kBG, PluginColors::HOTONE_PANEL)
            .WithDrawFrame(true)),
        kCtrlTagTunerDisplay)->Hide(true);
      pGraphics->AttachControl(
        new IVLabelControl(tunerRect.GetFromTop(60.f).GetMidVPadded(20.f), "--", titleStyle),
        kCtrlTagTunerNote)->Hide(true);
      pGraphics->AttachControl(
        new IVLabelControl(tunerRect.GetMidVPadded(14.f), "---Hz", style),
        kCtrlTagTunerFreq)->Hide(true);
      pGraphics->AttachControl(
        new IVLabelControl(tunerRect.GetFromBottom(60.f).GetMidVPadded(14.f), "0 cents", style),
        kCtrlTagTunerCents)->Hide(true);
      pGraphics->AttachControl(
        new IVSwitchControl(tunerRect.GetFromBottom(36.f).GetMidHPadded(60.f),
                            kTunerLive, "Live", style))->Hide(true);
    }

    // ---- TONE3000 BROWSER OVERLAY (hidden) ----
#ifdef _WIN32
    {
      auto onModelLoaded = [this](const std::string& filePath) {
        WDL_String wdlPath(filePath.c_str());
        const std::string msg = _StageModel(wdlPath);
        if (!msg.empty())
        {
          std::stringstream ss;
          ss << "Failed to load downloaded model:\n\n" << msg;
          if (auto* ui = GetUI())
            _ShowMessageBox(ui, ss.str().c_str(), "Failed to load model!", kMB_OK);
        }
      };
      pGraphics->AttachControl(
        new Tone3000BrowserControl(b, mToneLib, onModelLoaded, style),
        kCtrlTagTone3000Browser)->Hide(true);
    }
#endif

    // ---- Initial page: AMP ----
    {
      static const char* allGroups[] = {"PAGE_P1", "PAGE_P2", "PAGE_AMP", "PAGE_IR", "PAGE_EQ"};
      for (int i = 0; i < 5; i++)
        pGraphics->ForControlInGroup(allGroups[i], [i](IControl* c) { c->Hide(i != kPageAmp); });
    }

    // ---- Global mouse flags ----
    pGraphics->ForAllControlsFunc([](IControl* pControl) {
      pControl->SetMouseEventsWhenDisabled(true);
      pControl->SetMouseOverWhenDisabled(true);
    });
  };
}

AmpForge::~AmpForge()
{
  _DeallocateIOPointers();
}

void AmpForge::ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames)
{
  const size_t numChannelsExternalIn = (size_t)NInChansConnected();
  const size_t numChannelsExternalOut = (size_t)NOutChansConnected();
  const size_t numChannelsInternal = kNumChannelsInternal;
  const size_t numFrames = (size_t)nFrames;
  const double sampleRate = GetSampleRate();

  // Disable floating point denormals
  std::fenv_t fe_state;
  std::feholdexcept(&fe_state);
  disable_denormals();

  _PrepareBuffers(numChannelsInternal, numFrames);
  // Input is collapsed to mono in preparation for the NAM.
  _ProcessInput(inputs, numFrames, numChannelsExternalIn, numChannelsInternal);
  _ApplyDSPStaging();
  const bool noiseGateActive = GetParam(kNoiseGateActive)->Value();
  const bool toneStackActive = GetParam(kEQActive)->Value();

  // Feed tuner with raw pre-gate mono input (live tuner, always-on if enabled)
  if (GetParam(kTunerLive)->Bool())
  {
    for (size_t s = 0; s < numFrames; ++s)
      mTuner.Process(mInputPointers[0][s]);
    ++mTunerUpdateCounter;
    // ~30fps update cadence at 512-frame blocks
    if (mTunerUpdateCounter >= 3)
    {
      mTunerUpdateCounter = 0;
      _SendTunerResult();
    }
  }

  // Noise gate trigger
  sample** triggerOutput = mInputPointers;
  if (noiseGateActive)
  {
    const double time = 0.01;
    const double threshold = GetParam(kNoiseGateThreshold)->Value();
    const double ratio = 0.1;
    const double openTime = 0.005;
    const double holdTime = 0.01;
    const double closeTime = 0.05;
    const dsp::noise_gate::TriggerParams triggerParams(time, threshold, ratio, openTime, holdTime, closeTime);
    mNoiseGateTrigger.SetParams(triggerParams);
    mNoiseGateTrigger.SetSampleRate(sampleRate);
    triggerOutput = mNoiseGateTrigger.Process(mInputPointers, numChannelsInternal, numFrames);
  }

  // Pedal slot 1 (pre-amp)
  sample** pedal1Out = triggerOutput;
  if (mPedal1 != nullptr)
  {
    mPedal1->process(triggerOutput, mOutputPointers, nFrames);
    pedal1Out = mOutputPointers;
  }

  // Pedal slot 2 (chained after slot 1, pre-amp)
  sample** pedal2Out = pedal1Out;
  if (mPedal2 != nullptr)
  {
    mPedal2->process(pedal1Out, mOutputPointers, nFrames);
    pedal2Out = mOutputPointers;
  }

  // Amp NAM model
  if (mModel != nullptr)
  {
    mModel->process(pedal2Out, mOutputPointers, nFrames);
  }
  else
  {
    _FallbackDSP(pedal2Out, mOutputPointers, numChannelsInternal, numFrames);
  }

  // Noise gate gain (envelope follower applied to amp output)
  sample** gateGainOutput =
    noiseGateActive ? mNoiseGateGain.Process(mOutputPointers, numChannelsInternal, numFrames) : mOutputPointers;

  // Amp tone stack (bass/mid/treble)
  sample** toneStackOutPointers = (toneStackActive && mToneStack != nullptr)
                                    ? mToneStack->Process(gateGainOutput, numChannelsInternal, nFrames)
                                    : gateGainOutput;

  // IR convolution
  sample** irPointers = toneStackOutPointers;
  if (mIR != nullptr && GetParam(kIRToggle)->Value())
    irPointers = mIR->Process(toneStackOutPointers, numChannelsInternal, numFrames);

  // DC blocker HPF
  const recursive_linear_filter::HighPassParams highPassParams(sampleRate, kDCBlockerFrequency);
  mHighPass.SetParams(highPassParams);
  sample** hpfPointers = mHighPass.Process(irPointers, numChannelsInternal, numFrames);

  // Parametric / graphic EQ (post-IR, post-DC-blocker)
  sample** eqPointers = hpfPointers;
  if (GetParam(kEQPageActive)->Bool())
  {
    for (size_t s = 0; s < numFrames; ++s)
      hpfPointers[0][s] = static_cast<sample>(mParamEQ.Process(hpfPointers[0][s]));
    eqPointers = hpfPointers;
  }

  // Restore floating point state before output
  std::feupdateenv(&fe_state);

  // Doubler: mono in → stereo (L=dry, R=delayed) if active and stereo output
  const bool doublerActive = GetParam(kDoublerActive)->Bool();
  if (doublerActive && numChannelsExternalOut >= 2)
  {
    for (size_t s = 0; s < numFrames; ++s)
    {
      double outL, outR;
      mDoubler.Process(eqPointers[0][s], outL, outR);
      const double gain = mOutputGain;
#ifdef APP_API
      outputs[0][s] = std::clamp(gain * outL, -1.0, 1.0);
      outputs[1][s] = std::clamp(gain * outR, -1.0, 1.0);
#else
      outputs[0][s] = gain * outL;
      outputs[1][s] = gain * outR;
#endif
    }
    // Fill any extra channels with silence
    for (size_t c = 2; c < numChannelsExternalOut; ++c)
      for (size_t s = 0; s < numFrames; ++s)
        outputs[c][s] = 0.0;
  }
  else
  {
    _ProcessOutput(eqPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
  }

  _UpdateMeters(mInputPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
}

void AmpForge::OnReset()
{
  const auto sampleRate = GetSampleRate();
  const int maxBlockSize = GetBlockSize();

  // Tail is because the HPF DC blocker has a decay.
  // 10 cycles should be enough to pass the VST3 tests checking tail behavior.
  // I'm ignoring the model & IR, but it's not the end of the world.
  const int tailCycles = 10;
  SetTailSize(tailCycles * (int)(sampleRate / kDCBlockerFrequency));
  mInputSender.Reset(sampleRate);
  mOutputSender.Reset(sampleRate);
  _ResetModelAndIR(sampleRate, GetBlockSize());
  mToneStack->Reset(sampleRate, maxBlockSize);
  mParamEQ.Reset(sampleRate, maxBlockSize);
  mDoubler.Reset(sampleRate, maxBlockSize);
  mTuner.Reset(sampleRate, maxBlockSize);
  mDoubler.SetDelayMs(GetParam(kDoublerDelayMs)->Value());
  _UpdateLatency();
}

void AmpForge::OnIdle()
{
  mInputSender.TransmitData(*this);
  mOutputSender.TransmitData(*this);

#ifdef _WIN32
  if (auto* pGraphics = GetUI())
    if (auto* ctrl = pGraphics->GetControlWithTag(kCtrlTagTone3000Browser))
      static_cast<Tone3000BrowserControl*>(ctrl)->Poll();
#endif

  if (mNewModelLoadedInDSP)
  {
    if (auto* pGraphics = GetUI())
    {
      _UpdateControlsFromModel();
      mNewModelLoadedInDSP = false;
    }
  }
  if (mModelCleared)
  {
    if (auto* pGraphics = GetUI())
    {
      // FIXME -- need to disable only the "normalized" model
      // pGraphics->GetControlWithTag(kCtrlTagOutputMode)->SetDisabled(false);
      static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->ClearModelInfo();
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimmableIcon))
        p->Hide(true);
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        p->Hide(true);
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimKnob))
        p->Hide(true);
      pGraphics->SetAllControlsDirty();
      mModelCleared = false;
    }
  }
}

#ifdef _WIN32
void AmpForge::_ScanModelFiles()
{
  namespace fs = std::filesystem;
  mModelFiles.clear();
  char path[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, path)))
  {
    std::string dir = std::string(path) + "\\AmpForge";
    try {
      for (const auto& entry : fs::directory_iterator(dir))
        if (entry.path().extension() == ".nam")
          mModelFiles.push_back(entry.path().string());
      std::sort(mModelFiles.begin(), mModelFiles.end());
    }
    catch (...) {}
  }
}

void AmpForge::_NavigateModel(int delta)
{
  _ScanModelFiles();
  if (mModelFiles.empty()) return;
  if (mCurrentModelIdx < 0) mCurrentModelIdx = 0;
  else mCurrentModelIdx = std::clamp(mCurrentModelIdx + delta, 0, (int)mModelFiles.size() - 1);
  WDL_String path(mModelFiles[mCurrentModelIdx].c_str());
  const std::string err = _StageModel(path);
  if (!err.empty())
  {
    if (auto* ui = GetUI())
    {
      std::stringstream ss;
      ss << "Failed to load model:\n\n" << err;
      _ShowMessageBox(ui, ss.str().c_str(), "Load error", kMB_OK);
    }
  }
}
#endif

bool AmpForge::SerializeState(IByteChunk& chunk) const
{
  // If this isn't here when unserializing, then we know we're dealing with something before v0.8.0.
  WDL_String header("###AmpForge###"); // Don't change this!
  chunk.PutStr(header.Get());
  // Plugin version, so we can load legacy serialized states in the future!
  WDL_String version(PLUG_VERSION_STR);
  chunk.PutStr(version.Get());
  chunk.PutStr(mNAMPath.Get());
  chunk.PutStr(mIRPath.Get());
  chunk.PutStr(mPedal1Path.Get());
  chunk.PutStr(mPedal2Path.Get());
  return SerializeParams(chunk);
}

int AmpForge::UnserializeState(const IByteChunk& chunk, int startPos)
{
  // Look for the expected header. If it's there, then we'll know what to do.
  WDL_String header;
  int pos = startPos;
  pos = chunk.GetStr(header, pos);

  const char* kExpectedHeader = "###AmpForge###";
  if (strcmp(header.Get(), kExpectedHeader) == 0)
  {
    return _UnserializeStateWithKnownVersion(chunk, pos);
  }
  else
  {
    return _UnserializeStateWithUnknownVersion(chunk, startPos);
  }
}

void AmpForge::OnUIOpen()
{
  Plugin::OnUIOpen();

  if (mNAMPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
    // If it's not loaded yet, then mark as failed.
    // If it's yet to be loaded, then the completion handler will set us straight once it runs.
    if (mModel == nullptr && mStagedModel == nullptr)
      SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);
    // Update the large center label on the AMP page
    if (auto* lbl = GetUI()->GetControlWithTag(kCtrlTagAmpModelName))
    {
      const std::string stem = std::filesystem::path(mNAMPath.Get()).stem().string();
      lbl->As<ITextControl>()->SetStr(stem.c_str());
    }
  }

  // Refresh IN/OUT dB labels with current param values
  {
    if (auto* ctrl = GetUI()->GetControlWithTag(kCtrlTagInputLevelDisplay))
    {
      auto* box = static_cast<InOutBoxControl*>(ctrl);
      WDL_String din, dout;
      GetParam(kInputLevel)->GetDisplay(din, true);
      GetParam(kOutputLevel)->GetDisplay(dout, true);
      box->SetInStr((std::string("IN: ") + din.Get()).c_str());
      box->SetOutStr((std::string("OUT: ") + dout.Get()).c_str());
    }
  }

  if (mIRPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, mIRPath.GetLength(), mIRPath.Get());
    if (mIR == nullptr && mStagedIR == nullptr)
      SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadFailed);
  }

  if (mPedal1Path.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagPedal1FileBrowser, kMsgTagLoadedPedal1,
                               mPedal1Path.GetLength(), mPedal1Path.Get());
    if (mPedal1 == nullptr && mStagedPedal1 == nullptr)
      SendControlMsgFromDelegate(kCtrlTagPedal1FileBrowser, kMsgTagLoadFailed);
  }

  if (mPedal2Path.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagPedal2FileBrowser, kMsgTagLoadedPedal2,
                               mPedal2Path.GetLength(), mPedal2Path.Get());
    if (mPedal2 == nullptr && mStagedPedal2 == nullptr)
      SendControlMsgFromDelegate(kCtrlTagPedal2FileBrowser, kMsgTagLoadFailed);
  }

  if (mModel != nullptr)
  {
    _UpdateControlsFromModel();
  }
}

void AmpForge::OnParamChange(int paramIdx)
{
  switch (paramIdx)
  {
    // Changes to the input gain
    case kCalibrateInput:
    case kInputCalibrationLevel:
    case kInputLevel: _SetInputGain(); break;
    // Changes to the output gain
    case kOutputLevel:
    case kOutputMode: _SetOutputGain(); break;
    // Tone stack:
    case kToneBass: mToneStack->SetParam("bass", GetParam(paramIdx)->Value()); break;
    case kToneMid: mToneStack->SetParam("middle", GetParam(paramIdx)->Value()); break;
    case kToneTreble: mToneStack->SetParam("treble", GetParam(paramIdx)->Value()); break;
    case kSlim: _ApplySlimParamToLoadedNAMs(); break;
    // Doubler
    case kDoublerDelayMs: mDoubler.SetDelayMs(GetParam(kDoublerDelayMs)->Value()); break;
    // Parametric EQ bands
    case kEQBand0: case kEQBand1: case kEQBand2: case kEQBand3: case kEQBand4:
    case kEQBand5: case kEQBand6: case kEQBand7: case kEQBand8:
      mParamEQ.SetBandGain(paramIdx - kEQBand0, GetParam(paramIdx)->Value());
      break;
    case kEQHPFFreq: mParamEQ.SetHPF(GetParam(kEQHPFFreq)->Value()); break;
    case kEQLPFFreq: mParamEQ.SetLPF(GetParam(kEQLPFFreq)->Value()); break;
    default: break;
  }
}

void AmpForge::OnParamChangeUI(int paramIdx, EParamSource source)
{
  if (auto pGraphics = GetUI())
  {
    bool active = GetParam(paramIdx)->Bool();

    switch (paramIdx)
    {
      case kNoiseGateActive: pGraphics->GetControlWithParamIdx(kNoiseGateThreshold)->SetDisabled(!active); break;
      case kEQActive:
        for (int pi : {(int)kToneBass, (int)kToneMid, (int)kToneTreble})
          if (auto* c = pGraphics->GetControlWithParamIdx(pi)) c->SetDisabled(!active);
        break;
      case kIRToggle: pGraphics->GetControlWithTag(kCtrlTagIRFileBrowser)->SetDisabled(!active); break;
      case kInputLevel:
        if (auto* ctrl = pGraphics->GetControlWithTag(kCtrlTagInputLevelDisplay)) {
          WDL_String disp; GetParam(kInputLevel)->GetDisplay(disp, true);
          std::string s = "IN: "; s += disp.Get();
          static_cast<InOutBoxControl*>(ctrl)->SetInStr(s.c_str());
        }
        break;
      case kOutputLevel:
        if (auto* ctrl = pGraphics->GetControlWithTag(kCtrlTagInputLevelDisplay)) {
          WDL_String disp; GetParam(kOutputLevel)->GetDisplay(disp, true);
          std::string s = "OUT: "; s += disp.Get();
          static_cast<InOutBoxControl*>(ctrl)->SetOutStr(s.c_str());
        }
        break;
      default: break;
    }
  }
}

bool AmpForge::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  switch (msgTag)
  {
    case kMsgTagClearModel: mShouldRemoveModel = true; return true;
    case kMsgTagClearIR:    mShouldRemoveIR    = true; return true;
    case kMsgTagClearPedal1: mShouldRemovePedal1 = true; return true;
    case kMsgTagClearPedal2: mShouldRemovePedal2 = true; return true;
    case kMsgTagHighlightColor:
    {
      mHighLightColor.Set((const char*)pData);

      if (GetUI())
      {
        GetUI()->ForStandardControlsFunc([&](IControl* pControl) {
          if (auto* pVectorBase = pControl->As<IVectorBase>())
          {
            IColor color = IColor::FromColorCodeStr(mHighLightColor.Get());

            pVectorBase->SetColor(kX1, color);
            pVectorBase->SetColor(kPR, color.WithOpacity(0.3f));
            pVectorBase->SetColor(kFR, color.WithOpacity(0.4f));
            pVectorBase->SetColor(kX3, color.WithContrast(0.1f));
          }
          pControl->GetUI()->SetAllControlsDirty();
        });
      }

      return true;
    }
    default: return false;
  }
}

// Private methods ============================================================

void AmpForge::_AllocateIOPointers(const size_t nChans)
{
  if (mInputPointers != nullptr)
    throw std::runtime_error("Tried to re-allocate mInputPointers without freeing");
  mInputPointers = new sample*[nChans];
  if (mInputPointers == nullptr)
    throw std::runtime_error("Failed to allocate pointer to input buffer!\n");
  if (mOutputPointers != nullptr)
    throw std::runtime_error("Tried to re-allocate mOutputPointers without freeing");
  mOutputPointers = new sample*[nChans];
  if (mOutputPointers == nullptr)
    throw std::runtime_error("Failed to allocate pointer to output buffer!\n");
}

void AmpForge::_ApplyDSPStaging()
{
  // Remove marked modules
  if (mShouldRemoveModel)
  {
    mModel = nullptr;
    mNAMPath.Set("");
    mShouldRemoveModel = false;
    mModelCleared = true;
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  }
  if (mShouldRemoveIR)
  {
    mIR = nullptr;
    mIRPath.Set("");
    mShouldRemoveIR = false;
  }
  if (mShouldRemovePedal1)
  {
    mPedal1 = nullptr;
    mPedal1Path.Set("");
    mShouldRemovePedal1 = false;
  }
  if (mShouldRemovePedal2)
  {
    mPedal2 = nullptr;
    mPedal2Path.Set("");
    mShouldRemovePedal2 = false;
  }
  // Move things from staged to live
  if (mStagedModel != nullptr)
  {
    mModel = std::move(mStagedModel);
    mStagedModel = nullptr;
    mNewModelLoadedInDSP = true;
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  }
  if (mStagedIR != nullptr)
  {
    mIR = std::move(mStagedIR);
    mStagedIR = nullptr;
  }
  if (mStagedPedal1 != nullptr)
  {
    mPedal1 = std::move(mStagedPedal1);
    mStagedPedal1 = nullptr;
    mPedal1Loaded = true;
  }
  if (mStagedPedal2 != nullptr)
  {
    mPedal2 = std::move(mStagedPedal2);
    mStagedPedal2 = nullptr;
    mPedal2Loaded = true;
  }
}

void AmpForge::_DeallocateIOPointers()
{
  if (mInputPointers != nullptr)
  {
    delete[] mInputPointers;
    mInputPointers = nullptr;
  }
  if (mInputPointers != nullptr)
    throw std::runtime_error("Failed to deallocate pointer to input buffer!\n");
  if (mOutputPointers != nullptr)
  {
    delete[] mOutputPointers;
    mOutputPointers = nullptr;
  }
  if (mOutputPointers != nullptr)
    throw std::runtime_error("Failed to deallocate pointer to output buffer!\n");
}

void AmpForge::_FallbackDSP(iplug::sample** inputs, iplug::sample** outputs, const size_t numChannels,
                                    const size_t numFrames)
{
  for (auto c = 0; c < numChannels; c++)
    for (auto s = 0; s < numFrames; s++)
      mOutputArray[c][s] = mInputArray[c][s];
}

void AmpForge::_ResetModelAndIR(const double sampleRate, const int maxBlockSize)
{
  // Model
  if (mStagedModel != nullptr)
  {
    mStagedModel->Reset(sampleRate, maxBlockSize);
  }
  else if (mModel != nullptr)
  {
    mModel->Reset(sampleRate, maxBlockSize);
  }

  // IR
  if (mStagedIR != nullptr)
  {
    const double irSampleRate = mStagedIR->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mStagedIR->GetData();
      mStagedIR = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  }
  else if (mIR != nullptr)
  {
    const double irSampleRate = mIR->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mIR->GetData();
      mStagedIR = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  }
}

void AmpForge::_SetInputGain()
{
  iplug::sample inputGainDB = GetParam(kInputLevel)->Value();
  // Input calibration
  if ((mModel != nullptr) && (mModel->HasInputLevel()) && GetParam(kCalibrateInput)->Bool())
  {
    inputGainDB += GetParam(kInputCalibrationLevel)->Value() - mModel->GetInputLevel();
  }
  mInputGain = DBToAmp(inputGainDB);
}

void AmpForge::_SetOutputGain()
{
  double gainDB = GetParam(kOutputLevel)->Value();
  if (mModel != nullptr)
  {
    const int outputMode = GetParam(kOutputMode)->Int();
    switch (outputMode)
    {
      case 1: // Normalized
        if (mModel->HasLoudness())
        {
          const double loudness = mModel->GetLoudness();
          const double targetLoudness = -18.0;
          gainDB += (targetLoudness - loudness);
        }
        break;
      case 2: // Calibrated
        if (mModel->HasOutputLevel())
        {
          const double inputLevel = GetParam(kInputCalibrationLevel)->Value();
          const double outputLevel = mModel->GetOutputLevel();
          gainDB += (outputLevel - inputLevel);
        }
        break;
      case 0: // Raw
      default: break;
    }
  }
  mOutputGain = DBToAmp(gainDB);
}

void AmpForge::_ApplySlimParamToLoadedNAMs()
{
  const double v = GetParam(kSlim)->Value();
  auto apply = [v](ResamplingNAM* p) {
    if (p == nullptr)
      return;
    if (nam::SlimmableModel* s = p->GetSlimmableModel())
      s->SetSlimmableSize(v);
  };
  apply(mModel.get());
  apply(mStagedModel.get());
}

std::string AmpForge::_StageModel(const WDL_String& modelPath)
{
  WDL_String previousNAMPath = mNAMPath;
  try
  {
    auto dspPath = std::filesystem::u8path(modelPath.Get());
    std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);

    // Check that the model has 1 input and 1 output channel
    if (model->NumInputChannels() != 1)
    {
      throw std::runtime_error("Model must have 1 input channel, but has " + std::to_string(model->NumInputChannels()));
    }
    if (model->NumOutputChannels() != 1)
    {
      throw std::runtime_error("Model must have 1 output channel, but has "
                               + std::to_string(model->NumOutputChannels()));
    }

    std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    if (nam::SlimmableModel* slimmable = temp->GetSlimmableModel())
    {
      slimmable->SetSlimmableSize(GetParam(kSlim)->Value());
    }
    mStagedModel = std::move(temp);
    mNAMPath = modelPath;
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
    // Update big center label
    if (auto* ui = GetUI())
      if (auto* lbl = ui->GetControlWithTag(kCtrlTagAmpModelName))
      {
        const std::string stem = std::filesystem::path(mNAMPath.Get()).stem().string();
        lbl->As<ITextControl>()->SetStr(stem.c_str());
      }
  }
  catch (std::runtime_error& e)
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);

    if (mStagedModel != nullptr)
    {
      mStagedModel = nullptr;
    }
    mNAMPath = previousNAMPath;
    std::cerr << "Failed to read DSP module" << std::endl;
    std::cerr << e.what() << std::endl;
    return e.what();
  }
  return "";
}

dsp::wav::LoadReturnCode AmpForge::_StageIR(const WDL_String& irPath)
{
  // FIXME it'd be better for the path to be "staged" as well. Just in case the
  // path and the model got caught on opposite sides of the fence...
  WDL_String previousIRPath = mIRPath;
  const double sampleRate = GetSampleRate();
  dsp::wav::LoadReturnCode wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
  try
  {
    auto irPathU8 = std::filesystem::u8path(irPath.Get());
    mStagedIR = std::make_unique<dsp::ImpulseResponse>(irPathU8.string().c_str(), sampleRate);
    wavState = mStagedIR->GetWavState();
  }
  catch (std::runtime_error& e)
  {
    wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
    std::cerr << "Caught unhandled exception while attempting to load IR:" << std::endl;
    std::cerr << e.what() << std::endl;
  }

  if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
  {
    mIRPath = irPath;
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, mIRPath.GetLength(), mIRPath.Get());
  }
  else
  {
    if (mStagedIR != nullptr)
    {
      mStagedIR = nullptr;
    }
    mIRPath = previousIRPath;
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadFailed);
  }

  return wavState;
}

std::string AmpForge::_StagePedal1(const WDL_String& modelPath)
{
  WDL_String prev = mPedal1Path;
  try
  {
    auto path = std::filesystem::u8path(modelPath.Get());
    std::unique_ptr<nam::DSP> model = nam::get_dsp(path);
    if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
      throw std::runtime_error("Pedal model must be mono 1-in 1-out");
    auto temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    mStagedPedal1 = std::move(temp);
    mPedal1Path   = modelPath;
    SendControlMsgFromDelegate(kCtrlTagPedal1FileBrowser, kMsgTagLoadedPedal1, mPedal1Path.GetLength(), mPedal1Path.Get());
  }
  catch (std::runtime_error& e)
  {
    SendControlMsgFromDelegate(kCtrlTagPedal1FileBrowser, kMsgTagLoadFailed);
    mStagedPedal1 = nullptr;
    mPedal1Path   = prev;
    return e.what();
  }
  return "";
}

std::string AmpForge::_StagePedal2(const WDL_String& modelPath)
{
  WDL_String prev = mPedal2Path;
  try
  {
    auto path = std::filesystem::u8path(modelPath.Get());
    std::unique_ptr<nam::DSP> model = nam::get_dsp(path);
    if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
      throw std::runtime_error("Pedal model must be mono 1-in 1-out");
    auto temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    mStagedPedal2 = std::move(temp);
    mPedal2Path   = modelPath;
    SendControlMsgFromDelegate(kCtrlTagPedal2FileBrowser, kMsgTagLoadedPedal2, mPedal2Path.GetLength(), mPedal2Path.Get());
  }
  catch (std::runtime_error& e)
  {
    SendControlMsgFromDelegate(kCtrlTagPedal2FileBrowser, kMsgTagLoadFailed);
    mStagedPedal2 = nullptr;
    mPedal2Path   = prev;
    return e.what();
  }
  return "";
}

void AmpForge::_SendTunerResult()
{
  const dsp::TunerResult& r = mTuner.GetResult();
  TunerMsgPayload payload;
  payload.detected   = r.detected;
  payload.frequency  = r.frequency;
  payload.centsOff   = r.centsOff;
  payload.noteIndex  = r.noteIndex;
  payload.octave     = r.octave;
  strncpy(payload.noteName, r.noteName.c_str(), 3);
  payload.noteName[3] = '\0';
  SendControlMsgFromDelegate(kCtrlTagTunerDisplay, kMsgTagTunerResult, sizeof(payload), &payload);
}

size_t AmpForge::_GetBufferNumChannels() const
{
  // Assumes input=output (no mono->stereo effects)
  return mInputArray.size();
}

size_t AmpForge::_GetBufferNumFrames() const
{
  if (_GetBufferNumChannels() == 0)
    return 0;
  return mInputArray[0].size();
}

void AmpForge::_InitToneStack()
{
  // If you want to customize the tone stack, then put it here!
  mToneStack = std::make_unique<dsp::tone_stack::BasicNamToneStack>();
}
void AmpForge::_PrepareBuffers(const size_t numChannels, const size_t numFrames)
{
  const bool updateChannels = numChannels != _GetBufferNumChannels();
  const bool updateFrames = updateChannels || (_GetBufferNumFrames() != numFrames);
  //  if (!updateChannels && !updateFrames)  // Could we do this?
  //    return;

  if (updateChannels)
  {
    _PrepareIOPointers(numChannels);
    mInputArray.resize(numChannels);
    mOutputArray.resize(numChannels);
  }
  if (updateFrames)
  {
    for (auto c = 0; c < mInputArray.size(); c++)
    {
      mInputArray[c].resize(numFrames);
      std::fill(mInputArray[c].begin(), mInputArray[c].end(), 0.0);
    }
    for (auto c = 0; c < mOutputArray.size(); c++)
    {
      mOutputArray[c].resize(numFrames);
      std::fill(mOutputArray[c].begin(), mOutputArray[c].end(), 0.0);
    }
  }
  // Would these ever get changed by something?
  for (auto c = 0; c < mInputArray.size(); c++)
    mInputPointers[c] = mInputArray[c].data();
  for (auto c = 0; c < mOutputArray.size(); c++)
    mOutputPointers[c] = mOutputArray[c].data();
}

void AmpForge::_PrepareIOPointers(const size_t numChannels)
{
  _DeallocateIOPointers();
  _AllocateIOPointers(numChannels);
}

void AmpForge::_ProcessInput(iplug::sample** inputs, const size_t nFrames, const size_t nChansIn,
                                     const size_t nChansOut)
{
  // We'll assume that the main processing is mono for now. We'll handle dual amps later.
  if (nChansOut != 1)
  {
    std::stringstream ss;
    ss << "Expected mono output, but " << nChansOut << " output channels are requested!";
    throw std::runtime_error(ss.str());
  }

  // On the standalone, we can probably assume that the user has plugged into only one input and they expect it to be
  // carried straight through. Don't apply any division over nChansIn because we're just "catching anything out there."
  // However, in a DAW, it's probably something providing stereo, and we want to take the average in order to avoid
  // doubling the loudness. (This would change w/ double mono processing)
  double gain = mInputGain;
#ifndef APP_API
  gain /= (float)nChansIn;
#endif
  // Assume _PrepareBuffers() was already called
  for (size_t c = 0; c < nChansIn; c++)
    for (size_t s = 0; s < nFrames; s++)
      if (c == 0)
        mInputArray[0][s] = gain * inputs[c][s];
      else
        mInputArray[0][s] += gain * inputs[c][s];
}

void AmpForge::_ProcessOutput(iplug::sample** inputs, iplug::sample** outputs, const size_t nFrames,
                                      const size_t nChansIn, const size_t nChansOut)
{
  const double gain = mOutputGain;
  // Assume _PrepareBuffers() was already called
  if (nChansIn != 1)
    throw std::runtime_error("Plugin is supposed to process in mono.");
  // Broadcast the internal mono stream to all output channels.
  const size_t cin = 0;
  for (auto cout = 0; cout < nChansOut; cout++)
    for (auto s = 0; s < nFrames; s++)
#ifdef APP_API // Ensure valid output to interface
      outputs[cout][s] = std::clamp(gain * inputs[cin][s], -1.0, 1.0);
#else // In a DAW, other things may come next and should be able to handle large
      // values.
      outputs[cout][s] = gain * inputs[cin][s];
#endif
}

void AmpForge::_UpdateControlsFromModel()
{
  if (mModel == nullptr)
  {
    return;
  }
  if (auto* pGraphics = GetUI())
  {
    ModelInfo modelInfo;
    modelInfo.sampleRate.known = true;
    modelInfo.sampleRate.value = mModel->GetEncapsulatedSampleRate();
    modelInfo.inputCalibrationLevel.known = mModel->HasInputLevel();
    modelInfo.inputCalibrationLevel.value = mModel->HasInputLevel() ? mModel->GetInputLevel() : 0.0;
    modelInfo.outputCalibrationLevel.known = mModel->HasOutputLevel();
    modelInfo.outputCalibrationLevel.value = mModel->HasOutputLevel() ? mModel->GetOutputLevel() : 0.0;

    static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->SetModelInfo(modelInfo);

    const bool disableInputCalibrationControls = !mModel->HasInputLevel();
    pGraphics->GetControlWithTag(kCtrlTagCalibrateInput)->SetDisabled(disableInputCalibrationControls);
    pGraphics->GetControlWithTag(kCtrlTagInputCalibrationLevel)->SetDisabled(disableInputCalibrationControls);
    {
      auto* c = static_cast<OutputModeControl*>(pGraphics->GetControlWithTag(kCtrlTagOutputMode));
      c->SetNormalizedDisable(!mModel->HasLoudness());
      c->SetCalibratedDisable(!mModel->HasOutputLevel());
    }

    if (auto* pSlimIcon = pGraphics->GetControlWithTag(kCtrlTagSlimmableIcon))
    {
      const bool show = mModel->GetSlimmableModel() != nullptr;
      pSlimIcon->Hide(!show);
    }
  }
}

void AmpForge::_UpdateLatency()
{
  int latency = 0;
  if (mModel)
  {
    latency += mModel->GetLatency();
  }
  // Other things that add latency here...

  // Feels weird to have to do this.
  if (GetLatency() != latency)
  {
    SetLatency(latency);
  }
}

void AmpForge::_UpdateMeters(sample** inputPointer, sample** outputPointer, const size_t nFrames,
                                     const size_t nChansIn, const size_t nChansOut)
{
  // Right now, we didn't specify MAXNC when we initialized these, so it's 1.
  const int nChansHack = 1;
  mInputSender.ProcessBlock(inputPointer, (int)nFrames, kCtrlTagInputMeter, nChansHack);
  mOutputSender.ProcessBlock(outputPointer, (int)nFrames, kCtrlTagOutputMeter, nChansHack);
}

// HACK
#include "Unserialization.cpp"


