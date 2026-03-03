#include "TestFramework.h"
#include "PitchDetector.h"
#include <cmath>
#include <vector>

static constexpr double kTestSampleRate = 44100.0;
static constexpr int    kTestBlockSize  = 512;
static constexpr float  kPi = 3.14159265358979323846f;

// Generate a sine wave at a given frequency, filling the buffer
static std::vector<float> makeSine(float freqHz, int numSamples, float amplitude = 0.5f,
                                   double sampleRate = kTestSampleRate)
{
    std::vector<float> buf(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        buf[static_cast<size_t>(i)] = amplitude * std::sin(2.0f * kPi * freqHz * t);
    }
    return buf;
}

// Feed enough audio to fill the internal buffer and trigger at least one detection
static float feedAndDetect(PitchDetector& pd, float freqHz, double sampleRate = kTestSampleRate)
{
    // Feed several blocks to fill the analysis window (which may be >1536 at high sample rates)
    int totalSamples = static_cast<int>(sampleRate / 4);  // 250ms of audio
    auto sine = makeSine(freqHz, totalSamples, 0.5f, sampleRate);
    float lastPitch = -1.0f;
    for (int offset = 0; offset < totalSamples; offset += kTestBlockSize)
    {
        int n = std::min(kTestBlockSize, totalSamples - offset);
        float p = pd.detectPitch(sine.data() + offset, n);
        if (p > 0.0f) lastPitch = p;
    }
    return lastPitch;
}

// ============================================================================
// Basic detection
// ============================================================================

TEST(PitchDetector_DetectA440)
{
    PitchDetector pd;
    pd.prepare(kTestSampleRate, kTestBlockSize);

    float detected = feedAndDetect(pd, 440.0f);
    EXPECT_GT(detected, 0.0f);
    EXPECT_NEAR(detected, 440.0f, 3.0f);  // within 3 Hz
}

TEST(PitchDetector_DetectA220)
{
    PitchDetector pd;
    pd.prepare(kTestSampleRate, kTestBlockSize);

    float detected = feedAndDetect(pd, 220.0f);
    EXPECT_GT(detected, 0.0f);
    EXPECT_NEAR(detected, 220.0f, 3.0f);
}

TEST(PitchDetector_DetectC3_131Hz)
{
    PitchDetector pd;
    pd.prepare(kTestSampleRate, kTestBlockSize);

    float detected = feedAndDetect(pd, 130.81f);
    EXPECT_GT(detected, 0.0f);
    EXPECT_NEAR(detected, 130.81f, 3.0f);
}

TEST(PitchDetector_DetectHighPitch_1000Hz)
{
    PitchDetector pd;
    pd.prepare(kTestSampleRate, kTestBlockSize);

    float detected = feedAndDetect(pd, 1000.0f);
    EXPECT_GT(detected, 0.0f);
    EXPECT_NEAR(detected, 1000.0f, 5.0f);
}

// ============================================================================
// Silence and noise rejection
// ============================================================================

TEST(PitchDetector_Silence_ReturnsNegative)
{
    PitchDetector pd;
    pd.prepare(kTestSampleRate, kTestBlockSize);

    // Feed many blocks of silence
    std::vector<float> silence(kTestBlockSize, 0.0f);
    float lastPitch = -1.0f;
    for (int i = 0; i < 20; ++i)
        lastPitch = pd.detectPitch(silence.data(), kTestBlockSize);

    EXPECT_LT(lastPitch, 0.0f);  // should be -1.0f
}

TEST(PitchDetector_VeryQuietSignal_ReturnsNegative)
{
    PitchDetector pd;
    pd.prepare(kTestSampleRate, kTestBlockSize);

    // Amplitude well below the RMS threshold (0.01)
    auto quiet = makeSine(440.0f, kTestBlockSize * 20, 0.001f);
    float lastPitch = -1.0f;
    for (int offset = 0; offset < static_cast<int>(quiet.size()); offset += kTestBlockSize)
    {
        int n = std::min(kTestBlockSize, static_cast<int>(quiet.size()) - offset);
        lastPitch = pd.detectPitch(quiet.data() + offset, n);
    }
    EXPECT_LT(lastPitch, 0.0f);
}

// ============================================================================
// Sample rate scaling
// ============================================================================

TEST(PitchDetector_48kHz_DetectsA440)
{
    PitchDetector pd;
    pd.prepare(48000.0, kTestBlockSize);

    float detected = feedAndDetect(pd, 440.0f, 48000.0);
    EXPECT_GT(detected, 0.0f);
    EXPECT_NEAR(detected, 440.0f, 3.0f);
}

TEST(PitchDetector_96kHz_DetectsLowPitch)
{
    // This tests the buffer-size-scaling fix: at 96kHz the old code
    // couldn't detect below ~125 Hz
    PitchDetector pd;
    pd.prepare(96000.0, kTestBlockSize);

    float detected = feedAndDetect(pd, 80.0f, 96000.0);
    EXPECT_GT(detected, 0.0f);
    EXPECT_NEAR(detected, 80.0f, 3.0f);
}

TEST(PitchDetector_48kHz_DetectsLowPitch)
{
    PitchDetector pd;
    pd.prepare(48000.0, kTestBlockSize);

    float detected = feedAndDetect(pd, 65.0f, 48000.0);
    EXPECT_GT(detected, 0.0f);
    EXPECT_NEAR(detected, 65.0f, 3.0f);
}

// ============================================================================
// Frequency range boundaries
// ============================================================================

TEST(PitchDetector_LowBoundary_60Hz)
{
    PitchDetector pd;
    pd.prepare(kTestSampleRate, kTestBlockSize);

    float detected = feedAndDetect(pd, 62.0f);  // just above min
    EXPECT_GT(detected, 0.0f);
    EXPECT_NEAR(detected, 62.0f, 3.0f);
}

TEST(PitchDetector_HighBoundary_1400Hz)
{
    PitchDetector pd;
    pd.prepare(kTestSampleRate, kTestBlockSize);

    float detected = feedAndDetect(pd, 1400.0f);  // just below max
    EXPECT_GT(detected, 0.0f);
    EXPECT_NEAR(detected, 1400.0f, 10.0f);
}

// ============================================================================
// Re-prepare resets state
// ============================================================================

TEST(PitchDetector_RePrepare_ResetsState)
{
    PitchDetector pd;
    pd.prepare(kTestSampleRate, kTestBlockSize);

    feedAndDetect(pd, 440.0f);

    // Re-prepare should reset internal state
    pd.prepare(kTestSampleRate, kTestBlockSize);

    // Feed silence — should not return the old cached pitch
    std::vector<float> silence(kTestBlockSize, 0.0f);
    float pitch = -1.0f;
    for (int i = 0; i < 20; ++i)
        pitch = pd.detectPitch(silence.data(), kTestBlockSize);

    EXPECT_LT(pitch, 0.0f);
}
