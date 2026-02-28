#pragma once

#include "HarmonyVoice.h"
#include <array>
#include <set>
#include <vector>

static constexpr int MAX_VOICES = 12;

class VoicePool
{
public:
    VoicePool();

    void prepare(double sampleRate, int maxBlockSize);

    // Update voice allocation based on currently held MIDI notes and detected pitch.
    // Uses first-held priority: if >12 notes are held, excess notes are ignored.
    void updateNotes(const std::set<int>& activeNotes, float detectedPitchHz);

    // Render all active voices into the wet output buffer (mono, summed).
    void processBlock(const float* input, float* wetOutput, int numSamples);

    // Returns the number of currently active (non-fading) voices.
    int getActiveVoiceCount() const;

    // Access the voice array (for PanningEngine).
    const std::array<HarmonyVoice, MAX_VOICES>& getVoices() const { return voices_; }

private:
    std::array<HarmonyVoice, MAX_VOICES> voices_;
    std::vector<float> voiceBuffer_;  // Temp buffer for individual voice output
    int maxBlockSize_ = 512;
};
