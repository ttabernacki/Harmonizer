#include "PanningEngine.h"
#include <algorithm>
#include <vector>
#include <cstring>

void PanningEngine::prepare(double sampleRate, int /*maxBlockSize*/)
{
    float smoothSamples = static_cast<float>(sampleRate) * PAN_SMOOTH_TIME_MS / 1000.0f;
    panSmoothCoeff_ = 1.0f - std::exp(-1.0f / smoothSamples);

    for (auto& ps : panStates_)
    {
        ps.targetPan = 0.0f;
        ps.currentPan = 0.0f;
    }
}

void PanningEngine::updatePanning(const std::array<HarmonyVoice, MAX_PAN_VOICES>& voices)
{
    // Collect active voice indices sorted by MIDI note
    struct VoiceInfo { int index; int note; };
    std::vector<VoiceInfo> activeVoices;

    for (int i = 0; i < MAX_PAN_VOICES; ++i)
    {
        if (voices[static_cast<size_t>(i)].isActive())
        {
            activeVoices.push_back({ i, voices[static_cast<size_t>(i)].getAssignedNote() });
        }
    }

    // Sort by MIDI note (low to high)
    std::sort(activeVoices.begin(), activeVoices.end(),
              [](const VoiceInfo& a, const VoiceInfo& b) { return a.note < b.note; });

    int count = static_cast<int>(activeVoices.size());

    if (count == 0)
        return;

    if (count == 1)
    {
        // Single voice → center
        panStates_[static_cast<size_t>(activeVoices[0].index)].targetPan = 0.0f;
    }
    else
    {
        // Distribute evenly from -0.8 (left) to +0.8 (right)
        for (int i = 0; i < count; ++i)
        {
            float pan = -0.8f + 1.6f * static_cast<float>(i) / static_cast<float>(count - 1);
            panStates_[static_cast<size_t>(activeVoices[i].index)].targetPan = pan;
        }
    }
}

void PanningEngine::applyPanning(const float* const* voiceBuffers,
                                  const std::array<HarmonyVoice, MAX_PAN_VOICES>& voices,
                                  float* leftOut, float* rightOut, int numSamples)
{
    for (int v = 0; v < MAX_PAN_VOICES; ++v)
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
