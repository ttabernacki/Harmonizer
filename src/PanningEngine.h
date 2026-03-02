#pragma once

#include "HarmonyVoice.h"
#include "Constants.h"
#include <array>
#include <cmath>

// ============================================================================
// PanningEngine — Distributes harmony voices across the stereo field.
//
// Active voices are sorted by MIDI note number (low → left, high → right)
// and spread evenly between ±kPanRangeLimit (default ±0.8).  The width
// parameter scales this range: 0 = all voices centred, 1 = full spread.
//
// Pan positions are exponentially smoothed (kPanSmoothTimeMs) so that
// adding/removing notes doesn't cause audible jumps.
//
// Gains use constant-power panning:
//   gainL = cos(angle),  gainR = sin(angle)
// where angle maps the pan position [-1, +1] to [0, π/2].  This preserves
// perceived loudness across the stereo field (unlike linear panning, which
// dips ~3 dB in the centre).
// ============================================================================
class PanningEngine
{
public:
    PanningEngine() = default;

    void prepare(double sampleRate, int maxBlockSize);

    // Set stereo width: 0.0 = mono centre, 1.0 = full spread to ±kPanRangeLimit.
    void setWidth(float width) { widthScale_ = width; }

    // Recalculate target pan positions based on active voices and their notes.
    void updatePanning(const std::array<HarmonyVoice, kMaxVoices>& voices);

    // Mix per-voice mono buffers into the stereo output using smoothed pan
    // positions.  leftOut/rightOut are accumulated (not overwritten).
    void applyPanning(const float* const* voiceBuffers,
                      const std::array<HarmonyVoice, kMaxVoices>& voices,
                      float* leftOut, float* rightOut, int numSamples);

private:
    // Scratch type for sorting active voices by note number
    struct VoiceInfo { int index; int note; };

    struct PanState
    {
        float targetPan  = 0.0f;  // Desired position: -1.0 (left) to +1.0 (right)
        float currentPan = 0.0f;  // Smoothed position (tracks targetPan)
    };

    std::array<PanState, kMaxVoices>  panStates_{};
    std::array<VoiceInfo, kMaxVoices> sortBuffer_{};  // No heap allocation for sort
    float panSmoothCoeff_ = 0.0f;
    float widthScale_     = 1.0f;
};
