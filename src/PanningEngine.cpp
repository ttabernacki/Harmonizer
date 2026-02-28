#include "PanningEngine.h"
#include <algorithm>
#include <cstring>

void PanningEngine::prepare(double sampleRate, int /*maxBlockSize*/)
{
    float smoothSamples = static_cast<float>(sampleRate) * kPanSmoothTimeMs / 1000.0f;
    panSmoothCoeff_ = 1.0f - std::exp(-1.0f / smoothSamples);

    for (auto& ps : panStates_)
    {
        ps.targetPan = 0.0f;
        ps.currentPan = 0.0f;
    }
}

void PanningEngine::updatePanning(const std::array<HarmonyVoice, kMaxVoices>& voices)
{
    // Collect active voice indices into pre-allocated buffer (no heap allocation)
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

    // Sort by MIDI note (low to high)
    std::sort(sortBuffer_.begin(), sortBuffer_.begin() + count,
              [](const VoiceInfo& a, const VoiceInfo& b) { return a.note < b.note; });

    if (count == 1)
    {
        // Single voice -> center
        panStates_[static_cast<size_t>(sortBuffer_[0].index)].targetPan = 0.0f;
    }
    else
    {
        // Distribute evenly from -0.8 (left) to +0.8 (right)
        for (int i = 0; i < count; ++i)
        {
            float pan = -0.8f + 1.6f * static_cast<float>(i) / static_cast<float>(count - 1);
            panStates_[static_cast<size_t>(sortBuffer_[static_cast<size_t>(i)].index)].targetPan = pan;
        }
    }
}

void PanningEngine::applyPanning(const float* const* voiceBuffers,
                                  const std::array<HarmonyVoice, kMaxVoices>& voices,
                                  float* leftOut, float* rightOut, int numSamples)
{
    for (int v = 0; v < kMaxVoices; ++v)
    {
        if (!voices[static_cast<size_t>(v)].isActive())
            continue;

        auto& ps = panStates_[static_cast<size_t>(v)];
        const float* buf = voiceBuffers[v];

        for (int i = 0; i < numSamples; ++i)
        {
            // Smooth pan position
            ps.currentPan += panSmoothCoeff_ * (ps.targetPan - ps.currentPan);

            // Constant-power panning: map pan (-1..+1) to angle (0..pi/2)
            float angle = (ps.currentPan + 1.0f) * 0.5f * (3.14159265f / 2.0f);
            float gainL = std::cos(angle);
            float gainR = std::sin(angle);

            leftOut[i]  += buf[i] * gainL;
            rightOut[i] += buf[i] * gainR;
        }
    }
}
