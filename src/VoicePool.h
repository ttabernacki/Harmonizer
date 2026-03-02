#pragma once

#include "HarmonyVoice.h"
#include "Constants.h"
#include <array>
#include <vector>

class MidiNoteTracker;  // Forward declare to avoid circular include

// ============================================================================
// VoicePool — Manages an array of HarmonyVoice instances.
//
// Responsibilities:
//   1. Allocate / deallocate voices in response to MIDI note changes.
//   2. Distribute detune and formant settings across active voices.
//   3. Render all voices in a single pass (with a shared block coefficient).
//
// Voice allocation priority when all slots are full:
//   - First try an INACTIVE slot.
//   - If none, steal a FADING slot (the oldest fade-out is interrupted).
// ============================================================================
class VoicePool
{
public:
    VoicePool();

    // Prepare all voices and cache the pitch-smoothing coefficient.
    void prepare(double sampleRate, int maxBlockSize);

    // Set the total detune spread in cents.  The spread is distributed
    // symmetrically across active voices (see updateNotes step 2).
    void setDetuneCents(float cents) { detuneCents_ = cents; }

    // Set formant shift in semitones.  Converts to a linear scale factor
    // once, then applies uniformly to all voices.
    void setFormantShiftSemitones(float semitones);

    // Set attack time in ms (0 = instant).  Forwarded to every voice.
    void setAttackMs(float ms);

    // Set release time in ms (≥10 recommended for click-free).  Forwarded to every voice.
    void setReleaseMs(float ms);

    // Synchronise voice allocation with the current set of held MIDI notes
    // and the latest detected input pitch.
    void updateNotes(const int* activeNotes, int numActiveNotes, float detectedPitchHz);

    // Render each active voice into its own output buffer.  Inactive voice
    // buffers are zeroed.
    void renderVoices(const float* input, float* voiceOutputs[], int numSamples);

    // Count of voices that are fully active (not fading out).
    int getActiveVoiceCount() const;

    // Read-only access to the voice array (used by PanningEngine to read
    // assigned notes for pan-position sorting).
    const std::array<HarmonyVoice, kMaxVoices>& getVoices() const { return voices_; }

    // RubberBand's algorithmic delay in samples.  All voices share the same
    // stretcher configuration, so voice 0's delay is representative.
    int getStartDelay() const { return voices_[0].getStartDelay(); }

private:
    std::array<HarmonyVoice, kMaxVoices> voices_;
    int   maxBlockSize_ = 512;

    float detuneCents_ = 0.0f;

    // Formant scale is cached: std::pow is only recomputed when the semitone
    // value actually changes (threshold: 0.001 semitones ≈ inaudible).
    float formantScale_         = 1.0f;
    float lastFormantSemitones_ = 0.0f;

    // Pitch-smoothing per-sample coefficient, shared by every voice.  Derived
    // from kPitchSmoothTimeMs in prepare() and used in renderVoices() to
    // compute the per-block coefficient.
    float pitchSmoothCoeff_ = 0.0f;
};
