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

    // Set detune amount in cents — distributed symmetrically across voices.
    void setDetuneCents(float cents) { detuneCents_ = cents; }

    // Set formant shift in semitones — converted to scale once, applied uniformly to all voices.
    void setFormantShiftSemitones(float semitones);

    // Update voice allocation based on currently held MIDI notes and detected pitch.
    void updateNotes(const int* activeNotes, int numActiveNotes, float detectedPitchHz);

    // Render each active voice individually into the per-voice output buffers.
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
    float detuneCents_ = 0.0f;
    float formantScale_ = 1.0f;       // Pre-computed from semitones
    float lastFormantSemitones_ = 0.0f; // Cache to avoid redundant std::pow
    float pitchSmoothCoeff_ = 0.0f;   // Shared across all voices (same sample rate + constant)
};
