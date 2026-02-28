#pragma once

#include "HarmonyVoice.h"
#include <array>
#include <cmath>

static constexpr int MAX_PAN_VOICES = 12;
static constexpr float PAN_SMOOTH_TIME_MS = 50.0f;

class PanningEngine
{
public:
    PanningEngine() = default;

    void prepare(double sampleRate, int maxBlockSize);

    // Recalculate pan positions based on which voices are active and their MIDI notes.
    void updatePanning(const std::array<HarmonyVoice, MAX_PAN_VOICES>& voices);

    // Apply per-voice panning: takes per-voice mono buffers and writes to stereo output.
    // voiceBuffers[i] contains numSamples of mono audio for voice i.
    // leftOut and rightOut are the stereo output channels (accumulated, not overwritten).
    void applyPanning(const float* const* voiceBuffers,
                      const std::array<HarmonyVoice, MAX_PAN_VOICES>& voices,
                      float* leftOut, float* rightOut, int numSamples);

private:
    struct PanState
    {
        float targetPan = 0.0f;    // -1.0 (left) to +1.0 (right)
        float currentPan = 0.0f;
    };

    std::array<PanState, MAX_PAN_VOICES> panStates_{};
    float panSmoothCoeff_ = 0.0f;
};
