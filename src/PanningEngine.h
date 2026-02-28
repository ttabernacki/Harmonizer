#pragma once

#include "HarmonyVoice.h"
#include "Constants.h"
#include <array>
#include <cmath>

class PanningEngine
{
public:
    PanningEngine() = default;

    void prepare(double sampleRate, int maxBlockSize);

    // Recalculate pan positions based on which voices are active and their MIDI notes.
    void updatePanning(const std::array<HarmonyVoice, kMaxVoices>& voices);

    // Apply per-voice panning: takes per-voice mono buffers and writes to stereo output.
    // voiceBuffers[i] contains numSamples of mono audio for voice i.
    // leftOut and rightOut are the stereo output channels (accumulated, not overwritten).
    void applyPanning(const float* const* voiceBuffers,
                      const std::array<HarmonyVoice, kMaxVoices>& voices,
                      float* leftOut, float* rightOut, int numSamples);

private:
    struct VoiceInfo { int index; int note; };
    struct PanState
    {
        float targetPan = 0.0f;    // -1.0 (left) to +1.0 (right)
        float currentPan = 0.0f;
    };

    std::array<PanState, kMaxVoices> panStates_{};
    std::array<VoiceInfo, kMaxVoices> sortBuffer_{};  // Pre-allocated sort buffer (no heap alloc)
    float panSmoothCoeff_ = 0.0f;
};
