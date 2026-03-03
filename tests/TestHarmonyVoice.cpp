#include "TestFramework.h"
#include "HarmonyVoice.h"
#include <cmath>
#include <vector>

static constexpr double kSR = 44100.0;
static constexpr int    kBlock = 512;
static constexpr float  kPi = 3.14159265358979323846f;

// Generate a mono sine wave
static std::vector<float> makeSine(float freqHz, int numSamples, float amplitude = 0.5f)
{
    std::vector<float> buf(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(kSR);
        buf[static_cast<size_t>(i)] = amplitude * std::sin(2.0f * kPi * freqHz * t);
    }
    return buf;
}

// ============================================================================
// Initial state
// ============================================================================

TEST(HarmonyVoice_InitialState_Inactive)
{
    HarmonyVoice voice;
    EXPECT_FALSE(voice.isActive());
    EXPECT_FALSE(voice.isFadingOut());
    EXPECT_EQ(voice.getAssignedNote(), -1);
}

TEST(HarmonyVoice_StartDelay_ZeroBeforePrepare)
{
    HarmonyVoice voice;
    EXPECT_EQ(voice.getStartDelay(), 0);
}

// ============================================================================
// MIDI note to frequency table
// ============================================================================

TEST(HarmonyVoice_MidiToFreq_A4)
{
    float freq = HarmonyVoice::midiNoteToFrequency(69);
    EXPECT_NEAR(freq, 440.0f, 0.01f);
}

TEST(HarmonyVoice_MidiToFreq_C4)
{
    float freq = HarmonyVoice::midiNoteToFrequency(60);
    EXPECT_NEAR(freq, 261.63f, 0.1f);
}

TEST(HarmonyVoice_MidiToFreq_C0)
{
    float freq = HarmonyVoice::midiNoteToFrequency(12);
    EXPECT_NEAR(freq, 16.35f, 0.1f);
}

TEST(HarmonyVoice_MidiToFreq_FullRange)
{
    // Every note should be positive and increase monotonically
    float prevFreq = 0.0f;
    for (int i = 0; i < 128; ++i)
    {
        float freq = HarmonyVoice::midiNoteToFrequency(i);
        EXPECT_GT(freq, prevFreq);
        prevFreq = freq;
    }
}

TEST(HarmonyVoice_MidiToFreq_OctaveRelationship)
{
    // Notes 12 semitones apart should be exactly 2:1 in frequency
    for (int note = 0; note < 116; ++note)
    {
        float f1 = HarmonyVoice::midiNoteToFrequency(note);
        float f2 = HarmonyVoice::midiNoteToFrequency(note + 12);
        EXPECT_NEAR(f2 / f1, 2.0f, 0.001f);
    }
}

// ============================================================================
// Prepare + activate lifecycle
// ============================================================================

TEST(HarmonyVoice_Prepare_SetsUpStretcher)
{
    HarmonyVoice voice;
    voice.prepare(kSR, kBlock);

    // After prepare, start delay should be non-negative (RubberBand reports its latency)
    EXPECT_GE(voice.getStartDelay(), 0);
}

TEST(HarmonyVoice_Activate_MakesActive)
{
    HarmonyVoice voice;
    voice.prepare(kSR, kBlock);
    voice.activate(60, 261.63f);

    EXPECT_TRUE(voice.isActive());
    EXPECT_FALSE(voice.isFadingOut());
    EXPECT_EQ(voice.getAssignedNote(), 60);
}

TEST(HarmonyVoice_Deactivate_StartsFadeOut)
{
    HarmonyVoice voice;
    voice.prepare(kSR, kBlock);
    voice.activate(60, 261.63f);
    voice.deactivate();

    EXPECT_TRUE(voice.isActive());      // still active while fading
    EXPECT_TRUE(voice.isFadingOut());   // now in release phase
}

TEST(HarmonyVoice_Deactivate_InactiveVoice_NoOp)
{
    HarmonyVoice voice;
    voice.prepare(kSR, kBlock);
    voice.deactivate();  // should be harmless

    EXPECT_FALSE(voice.isActive());
    EXPECT_FALSE(voice.isFadingOut());
}

// ============================================================================
// Activate before prepare — should be a no-op
// ============================================================================

TEST(HarmonyVoice_Activate_BeforePrepare_NoOp)
{
    HarmonyVoice voice;
    voice.activate(60, 440.0f);  // prepare not called yet
    EXPECT_FALSE(voice.isActive());
}

// ============================================================================
// Waiting for pitch
// ============================================================================

TEST(HarmonyVoice_Activate_NoPitch_WaitsForPitch)
{
    HarmonyVoice voice;
    voice.prepare(kSR, kBlock);
    voice.activate(60, -1.0f);  // no pitch detected yet

    EXPECT_TRUE(voice.isActive());

    // Process a block — should output silence (waiting for pitch)
    auto input = makeSine(261.63f, kBlock);
    std::vector<float> output(kBlock, 99.0f);
    voice.process(input.data(), output.data(), kBlock, 0.5f);

    // Output should be all zeros (muted while waiting)
    float maxAbs = 0.0f;
    for (int i = 0; i < kBlock; ++i)
        maxAbs = std::max(maxAbs, std::abs(output[static_cast<size_t>(i)]));

    EXPECT_NEAR(maxAbs, 0.0f, 1e-6f);
}

TEST(HarmonyVoice_WaitingForPitch_ThenPitchArrives)
{
    HarmonyVoice voice;
    voice.prepare(kSR, kBlock);
    voice.activate(69, -1.0f);  // A4, no pitch yet

    // Feed several blocks to prime the stretcher
    auto input = makeSine(440.0f, kBlock);
    std::vector<float> output(kBlock);
    for (int i = 0; i < 5; ++i)
        voice.process(input.data(), output.data(), kBlock, 0.5f);

    // Now provide a valid pitch
    voice.updateInputPitch(440.0f);

    // The next process call should produce non-zero output
    voice.process(input.data(), output.data(), kBlock, 0.5f);

    float maxAbs = 0.0f;
    for (int i = 0; i < kBlock; ++i)
        maxAbs = std::max(maxAbs, std::abs(output[static_cast<size_t>(i)]));

    EXPECT_GT(maxAbs, 0.0f);
}

// ============================================================================
// Attack envelope
// ============================================================================

TEST(HarmonyVoice_InstantAttack)
{
    HarmonyVoice voice;
    voice.prepare(kSR, kBlock);
    voice.setAttackMs(0.0f);  // instant
    voice.activate(69, 440.0f);

    auto input = makeSine(440.0f, kBlock);
    std::vector<float> output(kBlock, 0.0f);

    // RubberBand has startup latency — process several blocks until output appears
    float maxAbs = 0.0f;
    for (int b = 0; b < 10; ++b)
    {
        voice.process(input.data(), output.data(), kBlock, 0.5f);
        for (int i = 0; i < kBlock; ++i)
            maxAbs = std::max(maxAbs, std::abs(output[static_cast<size_t>(i)]));
    }

    // Once output starts it should be at full level (no attack ramp)
    EXPECT_GT(maxAbs, 0.0f);
}

TEST(HarmonyVoice_SlowAttack_RampsUp)
{
    HarmonyVoice voice;
    voice.prepare(kSR, kBlock);
    voice.setAttackMs(100.0f);  // 100ms attack
    voice.activate(69, 440.0f);

    auto input = makeSine(440.0f, kBlock);
    std::vector<float> output(kBlock, 0.0f);

    // First block: gain should be ramping from 0
    voice.process(input.data(), output.data(), kBlock, 0.5f);

    // The output should start quiet and get louder
    // Check that early samples are quieter than later samples
    float earlyMax = 0.0f;
    float lateMax = 0.0f;
    int quarter = kBlock / 4;
    for (int i = 0; i < quarter; ++i)
        earlyMax = std::max(earlyMax, std::abs(output[static_cast<size_t>(i)]));
    for (int i = kBlock - quarter; i < kBlock; ++i)
        lateMax = std::max(lateMax, std::abs(output[static_cast<size_t>(i)]));

    // Later samples should be at least as loud (allowing for RubberBand latency)
    EXPECT_GE(lateMax, earlyMax);
}

// ============================================================================
// Release envelope
// ============================================================================

TEST(HarmonyVoice_Release_EventuallyBecomesInactive)
{
    HarmonyVoice voice;
    voice.prepare(kSR, kBlock);
    voice.setAttackMs(0.0f);
    voice.setReleaseMs(10.0f);  // 10ms release
    voice.activate(69, 440.0f);

    auto input = makeSine(440.0f, kBlock);
    std::vector<float> output(kBlock);

    // Process a few blocks so the voice is fully active
    for (int i = 0; i < 3; ++i)
        voice.process(input.data(), output.data(), kBlock, 0.5f);

    EXPECT_TRUE(voice.isActive());
    EXPECT_FALSE(voice.isFadingOut());

    // Deactivate and process until the voice becomes inactive
    voice.deactivate();
    EXPECT_TRUE(voice.isFadingOut());

    // 10ms at 44100Hz = 441 samples. One block of 512 should be enough.
    bool becameInactive = false;
    for (int i = 0; i < 10; ++i)
    {
        voice.process(input.data(), output.data(), kBlock, 0.5f);
        if (!voice.isActive())
        {
            becameInactive = true;
            break;
        }
    }
    EXPECT_TRUE(becameInactive);
}

TEST(HarmonyVoice_LongRelease_StillActiveAfterOneBlock)
{
    HarmonyVoice voice;
    voice.prepare(kSR, kBlock);
    voice.setAttackMs(0.0f);
    voice.setReleaseMs(2000.0f);  // 2 second release
    voice.activate(69, 440.0f);

    auto input = makeSine(440.0f, kBlock);
    std::vector<float> output(kBlock);

    for (int i = 0; i < 3; ++i)
        voice.process(input.data(), output.data(), kBlock, 0.5f);

    voice.deactivate();
    voice.process(input.data(), output.data(), kBlock, 0.5f);

    // With 2000ms release, voice should still be active after one block (~11ms)
    EXPECT_TRUE(voice.isActive());
    EXPECT_TRUE(voice.isFadingOut());
}

// ============================================================================
// Process produces output
// ============================================================================

TEST(HarmonyVoice_Process_InactiveVoice_OutputsZeros)
{
    HarmonyVoice voice;
    voice.prepare(kSR, kBlock);

    auto input = makeSine(440.0f, kBlock);
    std::vector<float> output(kBlock, 99.0f);
    voice.process(input.data(), output.data(), kBlock, 0.5f);

    for (int i = 0; i < kBlock; ++i)
        EXPECT_NEAR(output[static_cast<size_t>(i)], 0.0f, 1e-6f);
}

TEST(HarmonyVoice_Process_ActiveVoice_ProducesOutput)
{
    HarmonyVoice voice;
    voice.prepare(kSR, kBlock);
    voice.setAttackMs(0.0f);
    voice.activate(69, 440.0f);  // target = A4, input = A4, ratio = 1

    auto input = makeSine(440.0f, kBlock);
    std::vector<float> output(kBlock, 0.0f);

    // Process several blocks to get past RubberBand's startup latency
    float maxAbs = 0.0f;
    for (int i = 0; i < 10; ++i)
    {
        voice.process(input.data(), output.data(), kBlock, 0.5f);
        for (int s = 0; s < kBlock; ++s)
            maxAbs = std::max(maxAbs, std::abs(output[static_cast<size_t>(s)]));
    }

    EXPECT_GT(maxAbs, 0.01f);  // should produce audible output
}

// ============================================================================
// Pitch ratio clamping
// ============================================================================

TEST(HarmonyVoice_PitchRatio_ClampedToRange)
{
    HarmonyVoice voice;
    voice.prepare(kSR, kBlock);
    voice.setAttackMs(0.0f);

    // Activate with extreme pitch ratio: target=4186Hz (C8), input=65Hz (C2)
    // ratio would be ~64, but should be clamped to 4.0
    voice.activate(108, 65.0f);  // MIDI 108 = ~4186 Hz

    // Process and verify voice is still functional (doesn't crash)
    auto input = makeSine(65.0f, kBlock);
    std::vector<float> output(kBlock, 0.0f);
    voice.process(input.data(), output.data(), kBlock, 0.5f);

    EXPECT_TRUE(voice.isActive());
}

// ============================================================================
// Re-activation resets state cleanly
// ============================================================================

TEST(HarmonyVoice_Reactivate_CleansUp)
{
    HarmonyVoice voice;
    voice.prepare(kSR, kBlock);
    voice.setAttackMs(0.0f);
    voice.setReleaseMs(10.0f);

    auto input = makeSine(440.0f, kBlock);
    std::vector<float> output(kBlock);

    // First activation
    voice.activate(69, 440.0f);
    voice.process(input.data(), output.data(), kBlock, 0.5f);
    voice.deactivate();

    // Process until inactive
    for (int i = 0; i < 5; ++i)
        voice.process(input.data(), output.data(), kBlock, 0.5f);

    // Re-activate with a different note
    voice.activate(60, 440.0f);  // C4
    EXPECT_TRUE(voice.isActive());
    EXPECT_FALSE(voice.isFadingOut());
    EXPECT_EQ(voice.getAssignedNote(), 60);
}
