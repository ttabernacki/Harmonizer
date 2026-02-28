#pragma once

#include "HarmonyVoice.h"
#include "Constants.h"
#include <array>
#include <vector>

class MidiNoteTracker;  // Forward declare to avoid circular include

class VoicePool
{
public:
    VoicePool();

    void prepare(double sampleRate, int maxBlockSize);

    // Update voice allocation based on currently held MIDI notes and detected pitch.
    // Uses first-held priority: if >12 notes are held, excess notes are ignored.
    void updateNotes(const int* activeNotes, int numActiveNotes, float detectedPitchHz);

    // Render each active voice individually into the per-voice output buffers.
    // voiceOutputs[i] must point to a buffer of at least numSamples floats.
    // Inactive voices get zeroed.
    void renderVoices(const float* input, float* voiceOutputs[], int numSamples);

    // Returns the number of currently active (non-fading) voices.
    int getActiveVoiceCount() const;

    // Access the voice array (read-only, for PanningEngine).
    const std::array<HarmonyVoice, kMaxVoices>& getVoices() const { return voices_; }

    // Returns the RubberBand start delay from the first voice (all voices share the same config).
    int getStartDelay() const { return voices_[0].getStartDelay(); }

private:
    std::array<HarmonyVoice, kMaxVoices> voices_;
    int maxBlockSize_ = 512;
};
