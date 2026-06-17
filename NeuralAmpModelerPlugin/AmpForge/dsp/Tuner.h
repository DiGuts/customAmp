#pragma once
#include <vector>
#include <cmath>
#include <numeric>
#include <string>
#include <array>

// YIN pitch detector for live guitar tuner.
// Reference: de Cheveigne & Kawahara (2002) "YIN, a fundamental frequency estimator"

namespace dsp
{

struct TunerResult
{
  bool detected = false;
  double frequency = 0.0;   // Hz
  double centsOff = 0.0;    // cents deviation from nearest note (-50..+50)
  int    noteIndex = 0;     // 0=C, 1=C#, ... 11=B
  int    octave = 0;
  std::string noteName;
};

class Tuner
{
public:
  static constexpr int kWindowSize   = 4096;  // ~85ms @ 48k
  static constexpr int kMinPeriod    = 18;    // ~2.7kHz max
  static constexpr int kMaxPeriod    = 2000;  // ~24Hz min (sub-bass)
  static constexpr double kYINThresh = 0.10;

  Tuner() : mBuffer(kWindowSize, 0.0f), mDiff(kMaxPeriod, 0.0f), mCMND(kMaxPeriod, 0.0f)
  {
  }

  void Reset(double sampleRate, int /*maxBlockSize*/)
  {
    mSampleRate = sampleRate;
    std::fill(mBuffer.begin(), mBuffer.end(), 0.0f);
    mWritePos = 0;
    mFillCount = 0;
  }

  // Feed one sample; returns updated result when window ready (~every kWindowSize samples)
  void Process(double sample)
  {
    mBuffer[mWritePos] = static_cast<float>(sample);
    mWritePos = (mWritePos + 1) % kWindowSize;
    ++mFillCount;

    // Process every half-window
    if (mFillCount >= kWindowSize / 2)
    {
      mFillCount = 0;
      _Analyze();
    }
  }

  const TunerResult& GetResult() const { return mResult; }

private:
  void _Analyze()
  {
    // Step 1: Difference function
    for (int tau = 0; tau < kMaxPeriod; ++tau)
    {
      double sum = 0.0;
      for (int j = 0; j < kWindowSize / 2; ++j)
      {
        const int i1 = (mWritePos + j) % kWindowSize;
        const int i2 = (mWritePos + j + tau) % kWindowSize;
        const double d = mBuffer[i1] - mBuffer[i2];
        sum += d * d;
      }
      mDiff[tau] = static_cast<float>(sum);
    }

    // Step 2: Cumulative mean normalized difference
    mCMND[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau < kMaxPeriod; ++tau)
    {
      running += mDiff[tau];
      mCMND[tau] = (running > 0.0) ? static_cast<float>(mDiff[tau] * tau / running) : 1.0f;
    }

    // Step 3: Absolute threshold
    int tau = kMinPeriod;
    while (tau < kMaxPeriod - 1)
    {
      if (mCMND[tau] < kYINThresh)
      {
        // Parabolic interpolation
        const float a = mCMND[tau - 1];
        const float b = mCMND[tau];
        const float c = mCMND[tau + 1];
        const double shift = (c - a) / (2.0 * (2.0 * b - a - c));
        const double period = tau + shift;
        const double freq = mSampleRate / period;
        _SetResult(freq);
        return;
      }
      ++tau;
    }
    mResult.detected = false;
  }

  void _SetResult(double freq)
  {
    mResult.detected   = true;
    mResult.frequency  = freq;

    // Cents from A440
    const double semitones = 12.0 * std::log2(freq / 440.0) + 69.0; // MIDI note
    const int    nearestMidi = static_cast<int>(std::round(semitones));
    mResult.centsOff   = (semitones - nearestMidi) * 100.0;
    mResult.noteIndex  = ((nearestMidi % 12) + 12) % 12;
    mResult.octave     = nearestMidi / 12 - 1;

    static const std::array<const char*, 12> kNoteNames = {
      "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    mResult.noteName = kNoteNames[mResult.noteIndex];
  }

  double mSampleRate = 48000.0;
  std::vector<float> mBuffer;
  std::vector<float> mDiff;
  std::vector<float> mCMND;
  int mWritePos = 0;
  int mFillCount = 0;
  TunerResult mResult;
};

} // namespace dsp
