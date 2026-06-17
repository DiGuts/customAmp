#pragma once
#define _USE_MATH_DEFINES
#include <array>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 10-band graphic EQ (65Hz, 125Hz, 250Hz, 500Hz, 1kHz, 2kHz, 4kHz, 8kHz, 16kHz)
// + HPF and LPF
// Each band: RBJ cookbook peaking biquad, ±12 dB range.
// HPF/LPF: 2nd-order Butterworth.

namespace dsp
{
namespace param_eq
{

struct Biquad
{
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

  void Reset() { x1 = x2 = y1 = y2 = 0; }

  double Process(double x)
  {
    double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x;
    y2 = y1; y1 = y;
    return y;
  }
};

// RBJ peaking EQ biquad coefficients
inline void CalcPeaking(Biquad& bq, double fc, double gainDb, double Q, double sr)
{
  const double A  = std::pow(10.0, gainDb / 40.0);
  const double w0 = 2.0 * M_PI * fc / sr;
  const double cosw = std::cos(w0);
  const double sinw = std::sin(w0);
  const double alpha = sinw / (2.0 * Q);

  const double b0 =  1.0 + alpha * A;
  const double b1 = -2.0 * cosw;
  const double b2 =  1.0 - alpha * A;
  const double a0 =  1.0 + alpha / A;
  const double a1 = -2.0 * cosw;
  const double a2 =  1.0 - alpha / A;

  bq.b0 = b0 / a0; bq.b1 = b1 / a0; bq.b2 = b2 / a0;
  bq.a1 = a1 / a0; bq.a2 = a2 / a0;
}

// 2nd-order Butterworth HPF
inline void CalcHPF(Biquad& bq, double fc, double sr)
{
  const double w0    = 2.0 * M_PI * fc / sr;
  const double cosw  = std::cos(w0);
  const double sinw  = std::sin(w0);
  const double alpha = sinw / std::sqrt(2.0); // Q=0.707

  const double b0 =  (1.0 + cosw) / 2.0;
  const double b1 = -(1.0 + cosw);
  const double b2 =  (1.0 + cosw) / 2.0;
  const double a0 =   1.0 + alpha;
  const double a1 =  -2.0 * cosw;
  const double a2 =   1.0 - alpha;

  bq.b0 = b0/a0; bq.b1 = b1/a0; bq.b2 = b2/a0;
  bq.a1 = a1/a0; bq.a2 = a2/a0;
}

// 2nd-order Butterworth LPF
inline void CalcLPF(Biquad& bq, double fc, double sr)
{
  const double w0    = 2.0 * M_PI * fc / sr;
  const double cosw  = std::cos(w0);
  const double sinw  = std::sin(w0);
  const double alpha = sinw / std::sqrt(2.0);

  const double b0 =  (1.0 - cosw) / 2.0;
  const double b1 =   1.0 - cosw;
  const double b2 =  (1.0 - cosw) / 2.0;
  const double a0 =   1.0 + alpha;
  const double a1 =  -2.0 * cosw;
  const double a2 =   1.0 - alpha;

  bq.b0 = b0/a0; bq.b1 = b1/a0; bq.b2 = b2/a0;
  bq.a1 = a1/a0; bq.a2 = a2/a0;
}

static constexpr int kNumBands = 9;
static constexpr double kBandFreqs[kNumBands] = {65, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
static constexpr double kBandQ = 1.41; // ~1 octave

class ParametricEQ
{
public:
  ParametricEQ() = default;

  void Reset(double sampleRate, int /*maxBlockSize*/)
  {
    mSampleRate = sampleRate;
    for (auto& bq : mBands) bq.Reset();
    mHPF.Reset();
    mLPF.Reset();
    _RecalcAll();
  }

  // gainDb: ±12 dB per band (index 0..kNumBands-1)
  void SetBandGain(int band, double gainDb)
  {
    if (band < 0 || band >= kNumBands) return;
    mGainDb[band] = gainDb;
    CalcPeaking(mBands[band], kBandFreqs[band], gainDb, kBandQ, mSampleRate);
  }

  // hpfFreq = 0 → bypass; otherwise cutoff in Hz
  void SetHPF(double freq)
  {
    mHPFFreq = freq;
    mHPFActive = (freq > 10.0);
    if (mHPFActive) CalcHPF(mHPF, freq, mSampleRate);
  }

  // lpfFreq = 0 → bypass; otherwise cutoff in Hz
  void SetLPF(double freq)
  {
    mLPFFreq = freq;
    mLPFActive = (freq > 10.0 && freq < mSampleRate / 2.0 - 100.0);
    if (mLPFActive) CalcLPF(mLPF, freq, mSampleRate);
  }

  double Process(double x)
  {
    if (mHPFActive) x = mHPF.Process(x);
    for (int i = 0; i < kNumBands; ++i)
      x = mBands[i].Process(x);
    if (mLPFActive) x = mLPF.Process(x);
    return x;
  }

  double GetBandGain(int band) const { return (band >= 0 && band < kNumBands) ? mGainDb[band] : 0.0; }
  double GetHPFFreq() const { return mHPFFreq; }
  double GetLPFFreq() const { return mLPFFreq; }

private:
  void _RecalcAll()
  {
    for (int i = 0; i < kNumBands; ++i)
      CalcPeaking(mBands[i], kBandFreqs[i], mGainDb[i], kBandQ, mSampleRate);
    if (mHPFActive) CalcHPF(mHPF, mHPFFreq, mSampleRate);
    if (mLPFActive) CalcLPF(mLPF, mLPFFreq, mSampleRate);
  }

  double mSampleRate = 48000.0;
  std::array<Biquad, kNumBands> mBands;
  double mGainDb[kNumBands] = {};
  Biquad mHPF, mLPF;
  double mHPFFreq = 0.0, mLPFFreq = 0.0;
  bool mHPFActive = false, mLPFActive = false;
};

} // namespace param_eq
} // namespace dsp
