#pragma once
#include <array>
#include <cmath>
#include <cstring>

// Stereo doubler: short fixed delay on right channel → widens mono signal.
// delayMs: 0.5..30ms range
namespace dsp
{

class Doubler
{
public:
  static constexpr int kMaxDelaySamples = 4096; // ~85ms @ 48k

  Doubler() { mBuffer.fill(0.0f); }

  void Reset(double sampleRate, int /*maxBlockSize*/)
  {
    mSampleRate = sampleRate;
    mBuffer.fill(0.0f);
    mWritePos = 0;
    _UpdateDelay();
  }

  void SetDelayMs(double ms)
  {
    mDelayMs = ms;
    _UpdateDelay();
  }

  // in = mono sample; outL/outR = stereo out (dry L, delayed R)
  void Process(double in, double& outL, double& outR)
  {
    mBuffer[mWritePos] = static_cast<float>(in);
    const int readPos = (mWritePos - mDelaySamples + kMaxDelaySamples) % kMaxDelaySamples;
    outL = in;
    outR = mBuffer[readPos];
    mWritePos = (mWritePos + 1) % kMaxDelaySamples;
  }

  double GetDelayMs() const { return mDelayMs; }

private:
  void _UpdateDelay()
  {
    mDelaySamples = static_cast<int>(mDelayMs * mSampleRate / 1000.0);
    if (mDelaySamples < 1) mDelaySamples = 1;
    if (mDelaySamples >= kMaxDelaySamples) mDelaySamples = kMaxDelaySamples - 1;
  }

  std::array<float, kMaxDelaySamples> mBuffer;
  int mWritePos = 0;
  int mDelaySamples = 288; // ~6ms @ 48k
  double mDelayMs = 6.0;
  double mSampleRate = 48000.0;
};

} // namespace dsp
