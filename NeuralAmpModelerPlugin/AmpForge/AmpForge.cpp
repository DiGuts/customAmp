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

using namespace iplug;
using namespace igraphics;

const double kDCBlockerFrequency = 5.0;

// Styles
const IVColorSpec colorSpec{
  DEFAULT_BGCOLOR, // Background
  PluginColors::NAM_THEMECOLOR, // Foreground
  PluginColors::NAM_THEMECOLOR.WithOpacity(0.3f), // Pressed
  PluginColors::NAM_THEMECOLOR.WithOpacity(0.4f), // Frame
  PluginColors::MOUSEOVER, // Highlight
  DEFAULT_SHCOLOR, // Shadow
  PluginColors::NAM_THEMECOLOR, // Extra 1
  COLOR_RED, // Extra 2 --> color for clipping in meters
  PluginColors::NAM_THEMECOLOR.WithContrast(0.1f), // Extra 3
};

const IVStyle style =
  IVStyle{true, // Show label
          true, // Show value
          colorSpec,
          {DEFAULT_TEXT_SIZE + 3.f, EVAlign::Middle, PluginColors::NAM_THEMEFONTCOLOR}, // Knob label text5
          {DEFAULT_TEXT_SIZE + 3.f, EVAlign::Bottom, PluginColors::NAM_THEMEFONTCOLOR}, // Knob value text
          DEFAULT_HIDE_CURSOR,
          DEFAULT_DRAW_FRAME,
          false,
          DEFAULT_EMBOSS,
          0.2f,
          2.f,
          DEFAULT_SHADOW_OFFSET,
          DEFAULT_WIDGET_FRAC,
          DEFAULT_WIDGET_ANGLE};
const IVStyle titleStyle =
  DEFAULT_STYLE.WithValueText(IText(30, COLOR_WHITE, "Michroma-Regular")).WithDrawFrame(false).WithShadowOffset(2.f);
const IVStyle radioButtonStyle =
  style
    .WithColor(EVColor::kON, PluginColors::NAM_THEMECOLOR) // Pressed buttons and their labels
    .WithColor(EVColor::kOFF, PluginColors::NAM_THEMECOLOR.WithOpacity(0.1f)) // Unpressed buttons
    .WithColor(EVColor::kX1, PluginColors::NAM_THEMECOLOR.WithOpacity(0.6f)); // Unpressed buttons' labels

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

    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    pGraphics->LoadFont("Michroma-Regular", MICHROMA_FN);

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

    const auto b = pGraphics->GetBounds();

    // ---- Zone geometry (PLUG_WIDTH x PLUG_HEIGHT) ----
    const float kTopBarH = 52.f;
    const float kNavH    = 44.f;
    const float kBotBarH = 40.f;

    const IRECT topBarRect  = b.GetFromTop(kTopBarH);
    const IRECT navRect     = b.GetFromTop(kTopBarH + kNavH).GetFromBottom(kNavH);
    const IRECT contentRect = b.GetReducedFromTop(kTopBarH + kNavH).GetReducedFromBottom(kBotBarH);
    const IRECT botBarRect  = b.GetFromBottom(kBotBarH);

    // ---- Background ----
    pGraphics->AttachBackground(BACKGROUND_FN);

    // ---- TOP BAR ----
    {
      // Plugin name
      pGraphics->AttachControl(new IVLabelControl(
        topBarRect.GetFromLeft(160.f).GetMidVPadded(14.f).GetHShifted(12.f),
        "AMPFORGE", titleStyle));

      // Input meter
      pGraphics->AttachControl(
        new NAMMeterControl(topBarRect.GetFromLeft(18.f).GetHShifted(180.f).GetMidVPadded(22.f),
                            meterBackgroundBitmap, style),
        kCtrlTagInputMeter);

      // Input Gain knob
      pGraphics->AttachControl(new IVKnobControl(
        topBarRect.GetFromLeft(50.f).GetHShifted(202.f).GetMidVPadded(22.f),
        kInputLevel, "IN", style));

      // Noise Gate toggle
      pGraphics->AttachControl(new NAMSwitchControl(
        topBarRect.GetFromLeft(48.f).GetHShifted(258.f).GetMidVPadded(14.f),
        kNoiseGateActive, "GATE", style, switchHandleBitmap));

      // Noise Gate Threshold knob
      pGraphics->AttachControl(new IVKnobControl(
        topBarRect.GetFromLeft(50.f).GetHShifted(310.f).GetMidVPadded(22.f),
        kNoiseGateThreshold, "THR", style));

      // Mono/Stereo toggle (centre-left)
      pGraphics->AttachControl(new NAMSwitchControl(
        topBarRect.GetMidHPadded(38.f).GetMidVPadded(14.f).GetHShifted(-50.f),
        kInputModeStereo, "STEREO", style, switchHandleBitmap));

      // Doubler toggle (centre-right)
      pGraphics->AttachControl(new NAMSwitchControl(
        topBarRect.GetMidHPadded(38.f).GetMidVPadded(14.f).GetHShifted(50.f),
        kDoublerActive, "DBL", style, switchHandleBitmap));

      // Output Gain knob
      pGraphics->AttachControl(new IVKnobControl(
        topBarRect.GetFromRight(50.f).GetHShifted(-60.f).GetMidVPadded(22.f),
        kOutputLevel, "OUT", style));

      // Output meter
      pGraphics->AttachControl(
        new NAMMeterControl(topBarRect.GetFromRight(18.f).GetHShifted(-38.f).GetMidVPadded(22.f),
                            meterBackgroundBitmap, style),
        kCtrlTagOutputMeter);
    }

    // ---- NAV BAR: 5 page tabs ----
    {
      static const char* kTabLabels[] = {"PEDAL 1", "PEDAL 2", "AMP", "IR / CAB", "EQ"};
      const float tabW = b.W() / 5.f;
      for (int i = 0; i < 5; i++)
      {
        const int pageIdx = i;
        const IRECT tabRect = IRECT(b.L + i * tabW, navRect.T, b.L + (i + 1) * tabW, navRect.B);
        pGraphics->AttachControl(new IVButtonControl(tabRect,
          [pGraphics, pageIdx](IControl*) {
            static const char* grps[] = {"PAGE_P1", "PAGE_P2", "PAGE_AMP", "PAGE_IR", "PAGE_EQ"};
            for (int j = 0; j < 5; j++)
              pGraphics->ForControlInGroup(grps[j], [j, pageIdx](IControl* c) { c->Hide(j != pageIdx); });
            pGraphics->SetAllControlsDirty();
          }, kTabLabels[i], style));
      }
    }

    // ---- CONTENT: PAGE_P1 (Pedal Slot 1) ----
    {
      pGraphics->AttachControl(
        new IVLabelControl(contentRect.GetFromTop(36.f), "PEDAL SLOT 1", titleStyle),
        -1, "PAGE_P1");

      auto loadPedal1 = [&](const WDL_String& fileName, const WDL_String&) {
        if (fileName.GetLength()) _StagePedal1(fileName);
      };
      pGraphics->AttachControl(
        new NAMFileBrowserControl(
          contentRect.GetMidVPadded(18.f).GetMidHPadded(230.f).GetVShifted(10.f),
          kMsgTagClearPedal1, "Load Pedal 1 (.nam)...", "nam",
          loadPedal1, style, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
          fileBackgroundBitmap, globeSVG, "Get NAM Models", "https://www.neuralampmodeler.com/"),
        kCtrlTagPedal1FileBrowser, "PAGE_P1");
    }

    // ---- CONTENT: PAGE_P2 (Pedal Slot 2) ----
    {
      pGraphics->AttachControl(
        new IVLabelControl(contentRect.GetFromTop(36.f), "PEDAL SLOT 2", titleStyle),
        -1, "PAGE_P2");

      auto loadPedal2 = [&](const WDL_String& fileName, const WDL_String&) {
        if (fileName.GetLength()) _StagePedal2(fileName);
      };
      pGraphics->AttachControl(
        new NAMFileBrowserControl(
          contentRect.GetMidVPadded(18.f).GetMidHPadded(230.f).GetVShifted(10.f),
          kMsgTagClearPedal2, "Load Pedal 2 (.nam)...", "nam",
          loadPedal2, style, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
          fileBackgroundBitmap, globeSVG, "Get NAM Models", "https://www.neuralampmodeler.com/"),
        kCtrlTagPedal2FileBrowser, "PAGE_P2");
    }

    // ---- CONTENT: PAGE_AMP (Amp model + ToneStack) ----
    {
      auto loadModel = [&](const WDL_String& fileName, const WDL_String&) {
        if (fileName.GetLength())
        {
          const std::string msg = _StageModel(fileName);
          if (msg.size())
          {
            std::stringstream ss;
            ss << "Failed to load NAM model:\n\n" << msg;
            _ShowMessageBox(GetUI(), ss.str().c_str(), "Failed to load model!", kMB_OK);
          }
        }
      };

      // Amp model file browser
      pGraphics->AttachControl(
        new NAMFileBrowserControl(
          contentRect.GetFromTop(40.f).GetMidHPadded(230.f),
          kMsgTagClearModel, "Select amp model...", "nam",
          loadModel, style, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
          fileBackgroundBitmap, globeSVG, "Get NAM Models", "https://www.neuralampmodeler.com/"),
        kCtrlTagModelFileBrowser, "PAGE_AMP");

      // ToneStack knobs row
      const float knobSz  = 88.f;
      const float knobTop = contentRect.T + 52.f;
      struct { int p; const char* lbl; } toneKnobs[] = {
        {kToneBass,   "BASS"},
        {kToneMid,    "MID"},
        {kToneTreble, "TREBLE"},
      };
      const float toneRowW = 3.f * knobSz + 2.f * 12.f;
      float kx = contentRect.MW() - toneRowW * 0.5f;
      for (auto& k : toneKnobs)
      {
        pGraphics->AttachControl(
          new NAMKnobControl(IRECT(kx, knobTop, kx + knobSz, knobTop + knobSz),
                             k.p, k.lbl, style, knobBackgroundBitmap),
          -1, "PAGE_AMP");
        kx += knobSz + 12.f;
      }

      // Noise Gate active toggle (left)
      pGraphics->AttachControl(
        new NAMSwitchControl(
          IRECT(contentRect.L + 20.f, contentRect.T + 52.f, contentRect.L + 90.f, contentRect.T + 76.f),
          kNoiseGateActive, "Noise Gate", style, switchHandleBitmap),
        -1, "PAGE_AMP");

      // ToneStack active toggle (right)
      pGraphics->AttachControl(
        new NAMSwitchControl(
          IRECT(contentRect.R - 90.f, contentRect.T + 52.f, contentRect.R - 20.f, contentRect.T + 76.f),
          kEQActive, "Tone EQ", style, switchHandleBitmap),
        -1, "PAGE_AMP");

      // Slim slimmable icon (hidden until model supports it)
      const IRECT slimIconRect = IRECT(contentRect.R - 70.f, contentRect.B - 40.f,
                                       contentRect.R - 10.f, contentRect.B - 8.f);
      pGraphics->AttachControl(
        new NAMSquareButtonControl(slimIconRect, DefaultClickActionFunc, slimIconSVG),
        kCtrlTagSlimmableIcon, "PAGE_AMP")
        ->SetAnimationEndActionFunction([pGraphics](IControl*) {
          if (auto* bd = pGraphics->GetControlWithTag(kCtrlTagSlimOverlayBackdrop)) bd->Hide(false);
          if (auto* kn = pGraphics->GetControlWithTag(kCtrlTagSlimKnob))           kn->Hide(false);
          pGraphics->SetAllControlsDirty();
        })
        ->Hide(true);
    }

    // ---- CONTENT: PAGE_IR (IR / Cab loader) ----
    {
      pGraphics->AttachControl(
        new IVLabelControl(contentRect.GetFromTop(36.f), "IR / CAB LOADER", titleStyle),
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

      const IRECT irBrowserRect = contentRect.GetFromTop(90.f).GetFromBottom(40.f).GetMidHPadded(230.f);
      pGraphics->AttachControl(
        new NAMFileBrowserControl(irBrowserRect, kMsgTagClearIR, "Select IR (.wav)...", "wav",
                                  loadIR, style, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
                                  fileBackgroundBitmap, globeSVG, "Get IRs", "https://www.neuralampmodeler.com/"),
        kCtrlTagIRFileBrowser, "PAGE_IR");

      // IR bypass toggle
      const IRECT irSwitchRect = contentRect.GetFromTop(140.f).GetFromBottom(30.f).GetMidHPadded(28.f);
      pGraphics->AttachControl(
        new ISVGSwitchControl(irSwitchRect, {irIconOffSVG, irIconOnSVG}, kIRToggle),
        -1, "PAGE_IR");
    }

    // ---- CONTENT: PAGE_EQ (9-band graphic EQ + HPF/LPF) ----
    {
      pGraphics->AttachControl(
        new IVLabelControl(contentRect.GetFromTop(30.f), "PARAMETRIC EQ", titleStyle),
        -1, "PAGE_EQ");

      static const char* kBandLabels[] = {"65", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
      const int   nBands    = 9;
      const float sliderAreaW = contentRect.W() - 60.f;
      const float sliderW   = sliderAreaW / (nBands + 2);
      const float sliderH   = contentRect.H() - 64.f;
      const float sliderT   = contentRect.T + 32.f;
      const float sliderL   = contentRect.L + 30.f;

      for (int i = 0; i < nBands; i++)
      {
        const float sx = sliderL + i * sliderW;
        pGraphics->AttachControl(
          new IVSliderControl(IRECT(sx, sliderT, sx + sliderW - 4.f, sliderT + sliderH),
                              kEQBand0 + i, kBandLabels[i], DEFAULT_STYLE, true, EDirection::Vertical),
          -1, "PAGE_EQ");
      }

      const float knobSz = sliderW - 4.f;

      // HPF knob
      const float hpfX = sliderL + nBands * sliderW + 4.f;
      pGraphics->AttachControl(
        new IVKnobControl(IRECT(hpfX, sliderT + 10.f, hpfX + knobSz, sliderT + 10.f + knobSz),
                          kEQHPFFreq, "HPF", DEFAULT_STYLE),
        -1, "PAGE_EQ");

      // LPF knob
      const float lpfX = hpfX + sliderW;
      pGraphics->AttachControl(
        new IVKnobControl(IRECT(lpfX, sliderT + 10.f, lpfX + knobSz, sliderT + 10.f + knobSz),
                          kEQLPFFreq, "LPF", DEFAULT_STYLE),
        -1, "PAGE_EQ");

      // EQ page bypass toggle
      pGraphics->AttachControl(
        new NAMSwitchControl(contentRect.GetFromBottom(30.f).GetMidHPadded(55.f),
                             kEQPageActive, "EQ On", style, switchHandleBitmap),
        -1, "PAGE_EQ");
    }

    // ---- BOTTOM BAR ----
    {
      // Tuner open/close button
      pGraphics->AttachControl(new IVButtonControl(
        botBarRect.GetFromLeft(80.f).GetMidVPadded(12.f).GetHShifted(8.f),
        [pGraphics](IControl*) {
          auto* overlay = pGraphics->GetControlWithTag(kCtrlTagTunerDisplay);
          if (!overlay) return;
          const bool nowHide = !overlay->IsHidden();
          pGraphics->GetControlWithTag(kCtrlTagTunerDisplay)->Hide(nowHide);
          pGraphics->GetControlWithTag(kCtrlTagTunerNote)->Hide(nowHide);
          pGraphics->GetControlWithTag(kCtrlTagTunerFreq)->Hide(nowHide);
          pGraphics->GetControlWithTag(kCtrlTagTunerCents)->Hide(nowHide);
          pGraphics->SetAllControlsDirty();
        }, "TUNER", style));

      // Settings gear (bottom-right corner)
      pGraphics->AttachControl(new NAMCircleButtonControl(
        CornerButtonArea(b),
        [pGraphics](IControl*) {
          pGraphics->GetControlWithTag(kCtrlTagSettingsBox)->As<NAMSettingsPageControl>()->HideAnimated(false);
        }, gearSVG));
    }

    // ---- SETTINGS OVERLAY (hidden) ----
    pGraphics->AttachControl(
      new NAMSettingsPageControl(b, backgroundBitmap, inputLevelBackgroundBitmap, switchHandleBitmap,
                                 crossSVG, style, radioButtonStyle),
      kCtrlTagSettingsBox)->Hide(true);

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
      const IRECT tunerRect = b.GetCentredInside(280.f, 190.f);
      pGraphics->AttachControl(
        new IVPanelControl(tunerRect, "", DEFAULT_STYLE),
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
      // Live Tuner always-on toggle
      pGraphics->AttachControl(
        new NAMSwitchControl(tunerRect.GetFromBottom(30.f).GetMidHPadded(55.f),
                             kTunerLive, "Live", style, switchHandleBitmap))->Hide(true);
    }

    // ---- Initial page: AMP visible, all others hidden ----
    {
      static const char* allGroups[] = {"PAGE_P1", "PAGE_P2", "PAGE_AMP", "PAGE_IR", "PAGE_EQ"};
      for (int i = 0; i < 5; i++)
        pGraphics->ForControlInGroup(allGroups[i], [i](IControl* c) { c->Hide(i != kPageAmp); });
    }

    // ---- Global mouse-over flags ----
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
        // Tone knobs live in PAGE_AMP group; disable/enable by param index range
        for (int pi : {(int)kToneBass, (int)kToneMid, (int)kToneTreble})
          if (auto* c = pGraphics->GetControlWithParamIdx(pi)) c->SetDisabled(!active);
        break;
      case kIRToggle: pGraphics->GetControlWithTag(kCtrlTagIRFileBrowser)->SetDisabled(!active); break;
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


