#pragma once

#include "../AudioDSPTools/dsp/ImpulseResponse.h"
#include "../AudioDSPTools/dsp/NoiseGate.h"
#include "../AudioDSPTools/dsp/dsp.h"
#include "../AudioDSPTools/dsp/wav.h"
#include "../AudioDSPTools/dsp/ResamplingContainer/ResamplingContainer.h"
#include "../NeuralAmpModelerCore/NAM/dsp.h"
#include "../NeuralAmpModelerCore/NAM/slimmable.h"

#include "Colors.h"
#include "ToneStack.h"
#include "dsp/ParametricEQ.h"
#include "dsp/Doubler.h"
#include "dsp/Tuner.h"

#include "IPlug_include_in_plug_hdr.h"
#include "ISender.h"

const int kNumPresets = 1;
constexpr size_t kNumChannelsInternal = 1;

// ---- Nav page indices (top bar icons 1-5) --------------------------------
enum ENavPage
{
  kPagePedal1 = 0,
  kPagePedal2,
  kPageAmp,      // default
  kPageIR,
  kPageEQ,
  kNumPages
};

// ---- Parameters ----------------------------------------------------------
enum EParams
{
  // Amp controls (first 6 keep same index as NAM for legacy compat)
  kInputLevel = 0,
  kNoiseGateThreshold,
  kToneBass,
  kToneMid,
  kToneTreble,
  kOutputLevel,
  // Toggles
  kNoiseGateActive,
  kEQActive,
  kIRToggle,
  // Input calibration (kept from NAM)
  kCalibrateInput,
  kInputCalibrationLevel,
  kOutputMode,
  kSlim,
  // Doubler
  kDoublerActive,
  kDoublerDelayMs,
  // Transpose (semitones, stub – passthrough for now)
  kTranspose,
  // Input mode (0=Mono, 1=Stereo)
  kInputModeStereo,
  // Parametric EQ – 9 band gains + HPF + LPF cutoffs
  kEQBand0,  // 65Hz
  kEQBand1,  // 125Hz
  kEQBand2,  // 250Hz
  kEQBand3,  // 500Hz
  kEQBand4,  // 1kHz
  kEQBand5,  // 2kHz
  kEQBand6,  // 4kHz
  kEQBand7,  // 8kHz
  kEQBand8,  // 16kHz
  kEQHPFFreq,
  kEQLPFFreq,
  kEQPageActive,
  // Tuner
  kTunerLive,  // always-on live tuner
  kTunerRef,   // reference pitch Hz (default 440)
  kNumParams
};

const int numAmpKnobs = 6;  // Input, NoiseGate, Bass, Mid, Treble, Output

// ---- UI control tags -----------------------------------------------------
enum ECtrlTags
{
  kCtrlTagModelFileBrowser = 0,
  kCtrlTagIRFileBrowser,
  kCtrlTagPedal1FileBrowser,
  kCtrlTagPedal2FileBrowser,
  kCtrlTagInputMeter,
  kCtrlTagOutputMeter,
  kCtrlTagSettingsBox,
  kCtrlTagOutputMode,
  kCtrlTagCalibrateInput,
  kCtrlTagInputCalibrationLevel,
  kCtrlTagSlimmableIcon,
  kCtrlTagSlimOverlayBackdrop,
  kCtrlTagSlimKnob,
  kCtrlTagNavPage,      // tabbar control
  kCtrlTagTunerDisplay, // tuner overlay display
  kCtrlTagTunerFreq,    // frequency readout label
  kCtrlTagTunerNote,    // note name label
  kCtrlTagTunerCents,   // cents label
  kNumCtrlTags
};

// ---- Message tags --------------------------------------------------------
enum EMsgTags
{
  kMsgTagClearModel = 0,
  kMsgTagClearIR,
  kMsgTagHighlightColor,
  kMsgTagLoadFailed,
  kMsgTagLoadedModel,
  kMsgTagLoadedIR,
  // Pedal slots
  kMsgTagClearPedal1,
  kMsgTagClearPedal2,
  kMsgTagLoadedPedal1,
  kMsgTagLoadedPedal2,
  // Tuner result (DSP -> UI)
  kMsgTagTunerResult,
  kNumMsgTags
};

// ---- Tuner message payload -----------------------------------------------
struct TunerMsgPayload
{
  bool   detected;
  double frequency;
  double centsOff;
  int    noteIndex;
  int    octave;
  char   noteName[4];
};

// ---- Resampling wrapper --------------------------------------------------
double GetNAMSampleRate(const std::unique_ptr<nam::DSP>& model)
{
  const double assumedSampleRate = 48000.0;
  const double reported = model->GetExpectedSampleRate();
  return reported <= 0.0 ? assumedSampleRate : reported;
}

class ResamplingNAM : public nam::DSP
{
public:
  ResamplingNAM(std::unique_ptr<nam::DSP> encapsulated, const double expected_sample_rate)
  : nam::DSP(encapsulated->NumInputChannels(), encapsulated->NumOutputChannels(), expected_sample_rate)
  , mEncapsulated(std::move(encapsulated))
  , mResampler(GetNAMSampleRate(mEncapsulated))
  {
    auto ProcessBlockFunc = [&](NAM_SAMPLE** input, NAM_SAMPLE** output, int numFrames) {
      mEncapsulated->process(input, output, numFrames);
    };
    mBlockProcessFunc = ProcessBlockFunc;

    if (mEncapsulated->HasLoudness())  SetLoudness(mEncapsulated->GetLoudness());
    if (mEncapsulated->HasInputLevel()) SetInputLevel(mEncapsulated->GetInputLevel());
    if (mEncapsulated->HasOutputLevel()) SetOutputLevel(mEncapsulated->GetOutputLevel());

    int maxBlockSize = 2048;
    Reset(expected_sample_rate, maxBlockSize);
  }

  ~ResamplingNAM() = default;

  void prewarm() override { mEncapsulated->prewarm(); }

  void process(NAM_SAMPLE** input, NAM_SAMPLE** output, const int num_frames) override
  {
    if (num_frames > mMaxExternalBlockSize)
      throw std::runtime_error("More frames were provided than the max expected!");
    if (!NeedToResample())
      mEncapsulated->process(input, output, num_frames);
    else
      mResampler.ProcessBlock(input, output, num_frames, mBlockProcessFunc);
  }

  int GetLatency() const { return NeedToResample() ? mResampler.GetLatency() : 0; }

  void Reset(const double sampleRate, const int maxBlockSize) override
  {
    mExpectedSampleRate  = sampleRate;
    mMaxExternalBlockSize = maxBlockSize;
    mResampler.Reset(sampleRate, maxBlockSize);
    const double upRatio = sampleRate / GetEncapsulatedSampleRate();
    const auto maxEncBlockSize = static_cast<int>(std::ceil(static_cast<double>(maxBlockSize) / upRatio));
    mEncapsulated->ResetAndPrewarm(sampleRate, maxEncBlockSize);
  }

  double GetEncapsulatedSampleRate() const { return GetNAMSampleRate(mEncapsulated); }
  nam::SlimmableModel* GetSlimmableModel() { return dynamic_cast<nam::SlimmableModel*>(mEncapsulated.get()); }
  const nam::SlimmableModel* GetSlimmableModel() const { return dynamic_cast<const nam::SlimmableModel*>(mEncapsulated.get()); }

private:
  bool NeedToResample() const { return GetExpectedSampleRate() != GetEncapsulatedSampleRate(); }
  std::unique_ptr<nam::DSP> mEncapsulated;
  dsp::ResamplingContainer<NAM_SAMPLE, 1, 12> mResampler;
  int mMaxExternalBlockSize = 0;
  std::function<void(NAM_SAMPLE**, NAM_SAMPLE**, int)> mBlockProcessFunc;
};

// ---- Level meter sender -------------------------------------------------
class NAMSender : public iplug::IPeakAvgSender<>
{
public:
  NAMSender() : iplug::IPeakAvgSender<>(-90.0, true, 5.0f, 1.0f, 300.0f, 500.0f) {}
};

// ---- Main plugin class --------------------------------------------------
class AmpForge final : public iplug::Plugin
{
public:
  AmpForge(const iplug::InstanceInfo& info);
  ~AmpForge();

  void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;

  bool SerializeState(iplug::IByteChunk& chunk) const override;
  int  UnserializeState(const iplug::IByteChunk& chunk, int startPos) override;
  void OnUIOpen() override;
  bool OnHostRequestingSupportedViewConfiguration(int width, int height) override { return true; }

  void OnParamChange(int paramIdx) override;
  void OnParamChangeUI(int paramIdx, iplug::EParamSource source) override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

private:
  // IO helpers
  void _AllocateIOPointers(const size_t nChans);
  void _ApplyDSPStaging();
  void _DeallocateIOPointers();
  void _FallbackDSP(iplug::sample** inputs, iplug::sample** outputs, const size_t numChannels, const size_t numFrames);
  size_t _GetBufferNumChannels() const;
  size_t _GetBufferNumFrames() const;
  void _InitToneStack();
  void _PrepareBuffers(const size_t numChannels, const size_t numFrames);
  void _PrepareIOPointers(const size_t nChans);
  void _ProcessInput(iplug::sample** inputs, const size_t nFrames, const size_t nChansIn, const size_t nChansOut);
  void _ProcessOutput(iplug::sample** inputs, iplug::sample** outputs, const size_t nFrames, const size_t nChansIn, const size_t nChansOut);
  void _ResetModelAndIR(const double sampleRate, const int maxBlockSize);
  void _SetInputGain();
  void _SetOutputGain();
  void _ApplySlimParamToLoadedNAMs();

  // Model/IR staging
  std::string _StageModel(const WDL_String& dspFile);
  std::string _StagePedal1(const WDL_String& dspFile);
  std::string _StagePedal2(const WDL_String& dspFile);
  dsp::wav::LoadReturnCode _StageIR(const WDL_String& irPath);

  bool _HaveModel() const { return mModel != nullptr; }

  // Unserialization
  void _UnserializeApplyConfig(nlohmann::json& config);
  int  _UnserializeStateWithKnownVersion(const iplug::IByteChunk& chunk, int startPos);
  int  _UnserializeStateWithUnknownVersion(const iplug::IByteChunk& chunk, int startPos);

  // UI helpers
  void _UpdateControlsFromModel();
  void _UpdateLatency();
  void _UpdateMeters(iplug::sample** inputPointer, iplug::sample** outputPointer, const size_t nFrames,
                     const size_t nChansIn, const size_t nChansOut);
  void _SendTunerResult();

  // ---- Member data -------------------------------------------------------

  // Signal chain buffers
  std::vector<std::vector<iplug::sample>> mInputArray;
  std::vector<std::vector<iplug::sample>> mOutputArray;
  iplug::sample** mInputPointers  = nullptr;
  iplug::sample** mOutputPointers = nullptr;

  // Gain
  double mInputGain  = 1.0;
  double mOutputGain = 1.0;

  // Noise gate
  dsp::noise_gate::Trigger mNoiseGateTrigger;
  dsp::noise_gate::Gain    mNoiseGateGain;

  // Amp model
  std::unique_ptr<ResamplingNAM> mModel;
  std::unique_ptr<ResamplingNAM> mStagedModel;
  std::atomic<bool> mShouldRemoveModel{false};
  std::atomic<bool> mNewModelLoadedInDSP{false};
  std::atomic<bool> mModelCleared{false};

  // Pedal slot 1 (pre-amp, chained)
  std::unique_ptr<ResamplingNAM> mPedal1;
  std::unique_ptr<ResamplingNAM> mStagedPedal1;
  std::atomic<bool> mShouldRemovePedal1{false};
  std::atomic<bool> mPedal1Loaded{false};

  // Pedal slot 2 (after slot 1, pre-amp)
  std::unique_ptr<ResamplingNAM> mPedal2;
  std::unique_ptr<ResamplingNAM> mStagedPedal2;
  std::atomic<bool> mShouldRemovePedal2{false};
  std::atomic<bool> mPedal2Loaded{false};

  // IR / cab
  std::unique_ptr<dsp::ImpulseResponse> mIR;
  std::unique_ptr<dsp::ImpulseResponse> mStagedIR;
  std::atomic<bool> mShouldRemoveIR{false};

  // Tone stack
  std::unique_ptr<dsp::tone_stack::AbstractToneStack> mToneStack;

  // Post-IR high-pass (DC blocker / HPF)
  recursive_linear_filter::HighPass mHighPass;

  // Parametric EQ (post-IR)
  dsp::param_eq::ParametricEQ mParamEQ;

  // Doubler
  dsp::Doubler mDoubler;

  // Tuner (live pitch detection)
  dsp::Tuner mTuner;
  // Throttle tuner UI updates to ~30fps
  int mTunerUpdateCounter = 0;

  // File paths
  WDL_String mNAMPath;
  WDL_String mIRPath;
  WDL_String mPedal1Path;
  WDL_String mPedal2Path;

  WDL_String mHighLightColor{PluginColors::NAM_THEMECOLOR.ToColorCode()};
  std::unordered_map<std::string, double> mNAMParams = {{"Input", 0.0}, {"Output", 0.0}};

  NAMSender mInputSender, mOutputSender;
};
