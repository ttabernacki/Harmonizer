#include "TestFramework.h"
#include "PanningEngine.h"
#include "VoicePool.h"
#include <cmath>
#include <array>
#include <vector>
#include <cstring>

static constexpr double kSR = 44100.0;
static constexpr int    kBlock = 512;
static constexpr float  kPi = 3.14159265358979323846f;

// Helper: prepare a VoicePool and activate specific notes, then return its voices
static void setupVoices(VoicePool& pool, const std::vector<int>& notes, float inputPitch)
{
    pool.prepare(kSR, kBlock);
    pool.setAttackMs(0.0f);
    pool.setReleaseMs(10.0f);
    if (!notes.empty())
        pool.updateNotes(notes.data(), static_cast<int>(notes.size()), inputPitch);
}

// ============================================================================
// Single voice should be centered
// ============================================================================

TEST(PanningEngine_SingleVoice_Centered)
{
    VoicePool pool;
    setupVoices(pool, { 60 }, 261.63f);
    const auto& voices = pool.getVoices();

    PanningEngine panning;
    panning.prepare(kSR, kBlock);
    panning.setWidth(1.0f);
    panning.updatePanning(voices);

    // Create a constant voice buffer for the active voice
    std::array<std::vector<float>, kMaxVoices> storage;
    std::array<const float*, kMaxVoices> ptrs;
    for (int v = 0; v < kMaxVoices; ++v)
    {
        storage[static_cast<size_t>(v)].assign(kBlock, 0.0f);
        if (voices[static_cast<size_t>(v)].isActive())
            std::fill(storage[static_cast<size_t>(v)].begin(), storage[static_cast<size_t>(v)].end(), 1.0f);
        ptrs[static_cast<size_t>(v)] = storage[static_cast<size_t>(v)].data();
    }

    std::vector<float> leftOut(kBlock, 0.0f);
    std::vector<float> rightOut(kBlock, 0.0f);

    // Apply panning multiple times to let smoothing converge
    for (int i = 0; i < 10; ++i)
    {
        std::fill(leftOut.begin(), leftOut.end(), 0.0f);
        std::fill(rightOut.begin(), rightOut.end(), 0.0f);
        panning.applyPanning(ptrs.data(), voices, leftOut.data(), rightOut.data(), kBlock);
        panning.updatePanning(voices);
    }

    // At center pan, L and R should be equal (cos(pi/4) = sin(pi/4) ≈ 0.707)
    float lastL = leftOut[static_cast<size_t>(kBlock - 1)];
    float lastR = rightOut[static_cast<size_t>(kBlock - 1)];
    EXPECT_NEAR(lastL, lastR, 0.05f);
}

// ============================================================================
// Constant-power panning: gainL^2 + gainR^2 ≈ 1
// ============================================================================

TEST(PanningEngine_ConstantPower_SumsToOne)
{
    VoicePool pool;
    setupVoices(pool, { 60 }, 261.63f);
    const auto& voices = pool.getVoices();

    PanningEngine panning;
    panning.prepare(kSR, kBlock);
    panning.setWidth(1.0f);

    // Update panning multiple times
    for (int i = 0; i < 20; ++i)
        panning.updatePanning(voices);

    // Feed a constant 1.0 signal through the active voice
    std::array<std::vector<float>, kMaxVoices> storage;
    std::array<const float*, kMaxVoices> ptrs;
    for (int v = 0; v < kMaxVoices; ++v)
    {
        storage[static_cast<size_t>(v)].assign(kBlock, 0.0f);
        if (voices[static_cast<size_t>(v)].isActive())
            std::fill(storage[static_cast<size_t>(v)].begin(), storage[static_cast<size_t>(v)].end(), 1.0f);
        ptrs[static_cast<size_t>(v)] = storage[static_cast<size_t>(v)].data();
    }

    std::vector<float> leftOut(kBlock, 0.0f);
    std::vector<float> rightOut(kBlock, 0.0f);
    panning.applyPanning(ptrs.data(), voices, leftOut.data(), rightOut.data(), kBlock);

    // Check constant power on last sample (after smoothing converges)
    float L = leftOut[static_cast<size_t>(kBlock - 1)];
    float R = rightOut[static_cast<size_t>(kBlock - 1)];
    float power = L * L + R * R;
    EXPECT_NEAR(power, 1.0f, 0.1f);  // should be close to 1.0
}

// ============================================================================
// Width = 0: all voices centered
// ============================================================================

TEST(PanningEngine_WidthZero_AllCentered)
{
    VoicePool pool;
    setupVoices(pool, { 48, 60, 72 }, 261.63f);
    const auto& voices = pool.getVoices();

    PanningEngine panning;
    panning.prepare(kSR, kBlock);
    panning.setWidth(0.0f);  // all centered

    for (int i = 0; i < 20; ++i)
        panning.updatePanning(voices);

    // Feed constant signals
    std::array<std::vector<float>, kMaxVoices> storage;
    std::array<const float*, kMaxVoices> ptrs;
    for (int v = 0; v < kMaxVoices; ++v)
    {
        storage[static_cast<size_t>(v)].assign(kBlock, 0.0f);
        if (voices[static_cast<size_t>(v)].isActive())
            std::fill(storage[static_cast<size_t>(v)].begin(), storage[static_cast<size_t>(v)].end(), 1.0f);
        ptrs[static_cast<size_t>(v)] = storage[static_cast<size_t>(v)].data();
    }

    std::vector<float> leftOut(kBlock, 0.0f);
    std::vector<float> rightOut(kBlock, 0.0f);
    panning.applyPanning(ptrs.data(), voices, leftOut.data(), rightOut.data(), kBlock);

    // At center pan, L ≈ R for all voices
    float L = leftOut[static_cast<size_t>(kBlock - 1)];
    float R = rightOut[static_cast<size_t>(kBlock - 1)];
    EXPECT_NEAR(L, R, 0.05f);
}

// ============================================================================
// Two voices: lowest note left, highest note right
// ============================================================================

TEST(PanningEngine_TwoVoices_LowestLeftHighestRight)
{
    VoicePool pool;
    setupVoices(pool, { 48, 72 }, 261.63f);
    const auto& voices = pool.getVoices();

    PanningEngine panning;
    panning.prepare(kSR, kBlock);
    panning.setWidth(1.0f);

    // Let smoothing converge
    for (int i = 0; i < 50; ++i)
        panning.updatePanning(voices);

    // Feed distinct signals: voice with note 48 gets 1.0, voice with note 72 gets 2.0
    std::array<std::vector<float>, kMaxVoices> storage;
    std::array<const float*, kMaxVoices> ptrs;
    for (int v = 0; v < kMaxVoices; ++v)
    {
        storage[static_cast<size_t>(v)].assign(kBlock, 0.0f);
        if (voices[static_cast<size_t>(v)].isActive() && !voices[static_cast<size_t>(v)].isFadingOut())
        {
            float val = (voices[static_cast<size_t>(v)].getAssignedNote() == 48) ? 1.0f : 2.0f;
            std::fill(storage[static_cast<size_t>(v)].begin(), storage[static_cast<size_t>(v)].end(), val);
        }
        ptrs[static_cast<size_t>(v)] = storage[static_cast<size_t>(v)].data();
    }

    std::vector<float> leftOut(kBlock, 0.0f);
    std::vector<float> rightOut(kBlock, 0.0f);
    panning.applyPanning(ptrs.data(), voices, leftOut.data(), rightOut.data(), kBlock);

    float L = leftOut[static_cast<size_t>(kBlock - 1)];
    float R = rightOut[static_cast<size_t>(kBlock - 1)];

    // The low note (1.0) should contribute more to L, the high note (2.0) more to R
    // So L should be relatively lower than R (since the 2.0 signal is panned right)
    EXPECT_GT(R, L);
}

// ============================================================================
// Inactive voices don't affect output
// ============================================================================

TEST(PanningEngine_InactiveVoices_NoContribution)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);
    // No notes active
    const auto& voices = pool.getVoices();

    PanningEngine panning;
    panning.prepare(kSR, kBlock);
    panning.setWidth(1.0f);
    panning.updatePanning(voices);

    // Feed non-zero data into all voice buffers
    std::array<std::vector<float>, kMaxVoices> storage;
    std::array<const float*, kMaxVoices> ptrs;
    for (int v = 0; v < kMaxVoices; ++v)
    {
        storage[static_cast<size_t>(v)].assign(kBlock, 1.0f);  // all 1.0
        ptrs[static_cast<size_t>(v)] = storage[static_cast<size_t>(v)].data();
    }

    std::vector<float> leftOut(kBlock, 0.0f);
    std::vector<float> rightOut(kBlock, 0.0f);
    panning.applyPanning(ptrs.data(), voices, leftOut.data(), rightOut.data(), kBlock);

    // Output should be all zeros since no voices are active
    for (int i = 0; i < kBlock; ++i)
    {
        EXPECT_NEAR(leftOut[static_cast<size_t>(i)], 0.0f, 1e-6f);
        EXPECT_NEAR(rightOut[static_cast<size_t>(i)], 0.0f, 1e-6f);
    }
}

// ============================================================================
// applyPanning accumulates (doesn't overwrite)
// ============================================================================

TEST(PanningEngine_ApplyPanning_Accumulates)
{
    VoicePool pool;
    setupVoices(pool, { 60 }, 261.63f);
    const auto& voices = pool.getVoices();

    PanningEngine panning;
    panning.prepare(kSR, kBlock);
    panning.setWidth(1.0f);
    panning.updatePanning(voices);

    std::array<std::vector<float>, kMaxVoices> storage;
    std::array<const float*, kMaxVoices> ptrs;
    for (int v = 0; v < kMaxVoices; ++v)
    {
        storage[static_cast<size_t>(v)].assign(kBlock, 0.0f);
        if (voices[static_cast<size_t>(v)].isActive())
            std::fill(storage[static_cast<size_t>(v)].begin(), storage[static_cast<size_t>(v)].end(), 1.0f);
        ptrs[static_cast<size_t>(v)] = storage[static_cast<size_t>(v)].data();
    }

    // Pre-fill output with 5.0
    std::vector<float> leftOut(kBlock, 5.0f);
    std::vector<float> rightOut(kBlock, 5.0f);
    panning.applyPanning(ptrs.data(), voices, leftOut.data(), rightOut.data(), kBlock);

    // Output should be > 5.0 (accumulated, not overwritten)
    EXPECT_GT(leftOut[static_cast<size_t>(kBlock - 1)], 5.0f);
    EXPECT_GT(rightOut[static_cast<size_t>(kBlock - 1)], 5.0f);
}
