#include "PanningEngine.h"
#include <algorithm>
#include <cstring>

void PanningEngine::prepare(double sampleRate, int /*maxBlockSize*/)
{
    // Per-sample exponential smoothing coefficient for pan position changes
    float smoothSamples = static_cast<float>(sampleRate) * kPanSmoothTimeMs / 1000.0f;
    panSmoothCoeff_ = 1.0f - std::exp(-1.0f / smoothSamples);

    for (auto& ps : panStates_)
    {
        ps.targetPan = 0.0f;
        ps.currentPan = 0.0f;
    }
}

// ============================================================================
// updatePanning — assign target pan positions to active voices
// ============================================================================

void PanningEngine::updatePanning(const std::array<HarmonyVoice, kMaxVoices>& voices)
{
    // Collect active voice indices into the pre-allocated sort buffer
    int count = 0;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (voices[static_cast<size_t>(i)].isActive())
        {
            sortBuffer_[static_cast<size_t>(count)] = { i, voices[static_cast<size_t>(i)].getAssignedNote() };
            ++count;
        }
    }

    if (count == 0)
        return;

    // Sort by MIDI note: lowest note → leftmost position
    std::sort(sortBuffer_.begin(), sortBuffer_.begin() + count,
              [](const VoiceInfo& a, const VoiceInfo& b) { return a.note < b.note; });

    if (count == 1)
    {
        // Single voice: always centred (pan = 0)
        panStates_[static_cast<size_t>(sortBuffer_[0].index)].targetPan = 0.0f;
    }
    else
    {
        // Multiple voices: distribute evenly from -kPanRangeLimit to +kPanRangeLimit.
        // widthScale_ (0..1) narrows the spread toward the centre.
        //
        // Example with 3 voices and full width (kPanRangeLimit = 0.8):
        //   voice 0 (lowest note) → -0.8
        //   voice 1 (middle note) →  0.0
        //   voice 2 (highest note)→ +0.8
        float range = 2.0f * kPanRangeLimit;  // total span (e.g. 1.6)
        for (int i = 0; i < count; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(count - 1);  // 0..1
            float pan = (-kPanRangeLimit + range * t) * widthScale_;
            panStates_[static_cast<size_t>(sortBuffer_[static_cast<size_t>(i)].index)].targetPan = pan;
        }
    }
}

// ============================================================================
// applyPanning — mix voice buffers into stereo using constant-power law
// ============================================================================

void PanningEngine::applyPanning(const float* const* voiceBuffers,
                                  const std::array<HarmonyVoice, kMaxVoices>& voices,
                                  float* leftOut, float* rightOut, int numSamples)
{
    static constexpr float halfPi = 3.14159265358979323846f * 0.5f;

    for (int v = 0; v < kMaxVoices; ++v)
    {
        if (!voices[static_cast<size_t>(v)].isActive())
            continue;

        auto& ps = panStates_[static_cast<size_t>(v)];
        const float* buf = voiceBuffers[v];

        // Smooth pan position once per block (avoids per-sample trig)
        float blockCoeff = 1.0f - std::pow(1.0f - panSmoothCoeff_, static_cast<float>(numSamples));
        ps.currentPan += blockCoeff * (ps.targetPan - ps.currentPan);

        // Constant-power panning:
        //   angle maps pan ∈ [-1, +1] to [0, π/2]
        //   gainL = cos(angle)  → 1.0 at full left,  ~0.707 at centre
        //   gainR = sin(angle)  → 0.0 at full left,  ~0.707 at centre
        // The sum gainL² + gainR² = 1.0 (constant power).
        float angle = (ps.currentPan + 1.0f) * 0.5f * halfPi;
        float gainL = std::cos(angle);
        float gainR = std::sin(angle);

        for (int i = 0; i < numSamples; ++i)
        {
            leftOut[i]  += buf[i] * gainL;
            rightOut[i] += buf[i] * gainR;
        }
    }
}
