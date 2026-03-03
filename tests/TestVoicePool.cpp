#include "TestFramework.h"
#include "VoicePool.h"
#include <cmath>
#include <vector>
#include <array>
#include <cstring>

static constexpr double kSR = 44100.0;
static constexpr int    kBlock = 512;
static constexpr float  kPi = 3.14159265358979323846f;

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
// Initialization
// ============================================================================

TEST(VoicePool_InitiallyNoActiveVoices)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);
    EXPECT_EQ(pool.getActiveVoiceCount(), 0);
}

TEST(VoicePool_GetStartDelay_NonNegative)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);
    EXPECT_GE(pool.getStartDelay(), 0);
}

// ============================================================================
// Voice allocation
// ============================================================================

TEST(VoicePool_SingleNote_ActivatesOneVoice)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);

    int notes[] = { 60 };
    pool.updateNotes(notes, 1, 261.63f);

    EXPECT_EQ(pool.getActiveVoiceCount(), 1);
}

TEST(VoicePool_MultipleNotes_ActivatesMultipleVoices)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);

    int notes[] = { 60, 64, 67 };
    pool.updateNotes(notes, 3, 261.63f);

    EXPECT_EQ(pool.getActiveVoiceCount(), 3);
}

TEST(VoicePool_NoteRelease_DeactivatesVoice)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);

    // Activate 3 notes
    int notes3[] = { 60, 64, 67 };
    pool.updateNotes(notes3, 3, 261.63f);
    EXPECT_EQ(pool.getActiveVoiceCount(), 3);

    // Release one note
    int notes2[] = { 60, 67 };
    pool.updateNotes(notes2, 2, 261.63f);
    EXPECT_EQ(pool.getActiveVoiceCount(), 2);
}

TEST(VoicePool_AllNotesReleased_NoActiveVoices)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);

    int notes[] = { 60, 64, 67 };
    pool.updateNotes(notes, 3, 261.63f);

    // Release all
    pool.updateNotes(nullptr, 0, 261.63f);
    EXPECT_EQ(pool.getActiveVoiceCount(), 0);
}

TEST(VoicePool_MaxVoices_DoesNotExceedLimit)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);

    // Try to activate more than kMaxVoices (12)
    int notes[16];
    for (int i = 0; i < 16; ++i)
        notes[i] = 40 + i;

    pool.updateNotes(notes, 16, 261.63f);

    // Should be capped at kMaxVoices
    EXPECT_LE(pool.getActiveVoiceCount(), kMaxVoices);
    EXPECT_EQ(pool.getActiveVoiceCount(), kMaxVoices);
}

// ============================================================================
// Voice reuse: same notes persist across calls
// ============================================================================

TEST(VoicePool_SameNotes_NoReallocation)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);

    int notes[] = { 60, 64, 67 };
    pool.updateNotes(notes, 3, 261.63f);
    EXPECT_EQ(pool.getActiveVoiceCount(), 3);

    // Same notes again
    pool.updateNotes(notes, 3, 261.63f);
    EXPECT_EQ(pool.getActiveVoiceCount(), 3);

    // Verify the voice array still has the same assigned notes
    const auto& voices = pool.getVoices();
    bool has60 = false, has64 = false, has67 = false;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (voices[static_cast<size_t>(i)].isActive() && !voices[static_cast<size_t>(i)].isFadingOut())
        {
            if (voices[static_cast<size_t>(i)].getAssignedNote() == 60) has60 = true;
            if (voices[static_cast<size_t>(i)].getAssignedNote() == 64) has64 = true;
            if (voices[static_cast<size_t>(i)].getAssignedNote() == 67) has67 = true;
        }
    }
    EXPECT_TRUE(has60);
    EXPECT_TRUE(has64);
    EXPECT_TRUE(has67);
}

// ============================================================================
// Rendering
// ============================================================================

TEST(VoicePool_RenderVoices_InactiveVoicesOutputZero)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);

    // No notes active
    auto input = makeSine(440.0f, kBlock);
    std::array<std::vector<float>, kMaxVoices> storage;
    std::array<float*, kMaxVoices> ptrs;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        storage[static_cast<size_t>(i)].resize(kBlock, 99.0f);
        ptrs[static_cast<size_t>(i)] = storage[static_cast<size_t>(i)].data();
    }

    pool.renderVoices(input.data(), ptrs.data(), kBlock);

    // All outputs should be zero
    for (int v = 0; v < kMaxVoices; ++v)
    {
        for (int s = 0; s < kBlock; ++s)
            EXPECT_NEAR(ptrs[static_cast<size_t>(v)][s], 0.0f, 1e-6f);
    }
}

TEST(VoicePool_RenderVoices_ActiveVoicesProduceOutput)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);

    int notes[] = { 69 };  // A4
    pool.updateNotes(notes, 1, 440.0f);

    auto input = makeSine(440.0f, kBlock);
    std::array<std::vector<float>, kMaxVoices> storage;
    std::array<float*, kMaxVoices> ptrs;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        storage[static_cast<size_t>(i)].resize(kBlock, 0.0f);
        ptrs[static_cast<size_t>(i)] = storage[static_cast<size_t>(i)].data();
    }

    // Render several blocks to get past startup latency
    float maxAbs = 0.0f;
    for (int b = 0; b < 10; ++b)
    {
        pool.renderVoices(input.data(), ptrs.data(), kBlock);
        for (int v = 0; v < kMaxVoices; ++v)
        {
            for (int s = 0; s < kBlock; ++s)
                maxAbs = std::max(maxAbs, std::abs(ptrs[static_cast<size_t>(v)][s]));
        }
    }

    EXPECT_GT(maxAbs, 0.01f);
}

// ============================================================================
// Detune distribution
// ============================================================================

TEST(VoicePool_Detune_ZeroCents_NoEffect)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);
    pool.setDetuneCents(0.0f);

    int notes[] = { 60, 64, 67 };
    pool.updateNotes(notes, 3, 261.63f);

    // Voices should all have detuneRatio = 1.0 (no way to check directly,
    // but at least verify they're all active and stable)
    EXPECT_EQ(pool.getActiveVoiceCount(), 3);
}

TEST(VoicePool_Detune_PositiveCents_VoicesStillActive)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);
    pool.setDetuneCents(25.0f);

    int notes[] = { 60, 64, 67 };
    pool.updateNotes(notes, 3, 261.63f);

    EXPECT_EQ(pool.getActiveVoiceCount(), 3);
}

// ============================================================================
// Formant shift
// ============================================================================

TEST(VoicePool_FormantShift_ZeroSemitones)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);
    pool.setFormantShiftSemitones(0.0f);

    int notes[] = { 69 };
    pool.updateNotes(notes, 1, 440.0f);
    EXPECT_EQ(pool.getActiveVoiceCount(), 1);
}

TEST(VoicePool_FormantShift_Positive)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);
    pool.setFormantShiftSemitones(6.0f);

    int notes[] = { 69 };
    pool.updateNotes(notes, 1, 440.0f);
    EXPECT_EQ(pool.getActiveVoiceCount(), 1);
}

TEST(VoicePool_FormantShift_Negative)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);
    pool.setFormantShiftSemitones(-6.0f);

    int notes[] = { 69 };
    pool.updateNotes(notes, 1, 440.0f);
    EXPECT_EQ(pool.getActiveVoiceCount(), 1);
}

// ============================================================================
// Attack / release forwarding
// ============================================================================

TEST(VoicePool_AttackRelease_Forwarded)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);

    // These should not crash
    pool.setAttackMs(50.0f);
    pool.setReleaseMs(100.0f);

    int notes[] = { 60 };
    pool.updateNotes(notes, 1, 261.63f);
    EXPECT_EQ(pool.getActiveVoiceCount(), 1);
}

// ============================================================================
// Voice stealing
// ============================================================================

TEST(VoicePool_VoiceStealing_FadingVoiceReused)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);
    pool.setReleaseMs(2000.0f);  // long release so voices stay fading

    // Fill all 12 voice slots
    int notes12[12];
    for (int i = 0; i < 12; ++i)
        notes12[i] = 48 + i;
    pool.updateNotes(notes12, 12, 261.63f);
    EXPECT_EQ(pool.getActiveVoiceCount(), 12);

    // Release all notes — voices start fading
    pool.updateNotes(nullptr, 0, 261.63f);
    EXPECT_EQ(pool.getActiveVoiceCount(), 0);

    // Render a block so fading state is applied
    auto input = makeSine(261.63f, kBlock);
    std::array<std::vector<float>, kMaxVoices> storage;
    std::array<float*, kMaxVoices> ptrs;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        storage[static_cast<size_t>(i)].resize(kBlock, 0.0f);
        ptrs[static_cast<size_t>(i)] = storage[static_cast<size_t>(i)].data();
    }
    pool.renderVoices(input.data(), ptrs.data(), kBlock);

    // Now try to activate a new note — should steal a fading slot
    int newNotes[] = { 72 };
    pool.updateNotes(newNotes, 1, 261.63f);
    EXPECT_EQ(pool.getActiveVoiceCount(), 1);
}

// ============================================================================
// getVoices() provides correct read-only access
// ============================================================================

TEST(VoicePool_GetVoices_ReturnsCorrectArray)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);

    int notes[] = { 60, 64 };
    pool.updateNotes(notes, 2, 261.63f);

    const auto& voices = pool.getVoices();
    int activeCount = 0;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (voices[static_cast<size_t>(i)].isActive() && !voices[static_cast<size_t>(i)].isFadingOut())
            ++activeCount;
    }
    EXPECT_EQ(activeCount, 2);
}
