#pragma once

#include "Constants.h"
#include <rubberband/RubberBandStretcher.h>
#include <array>
#include <memory>
#include <vector>

// ============================================================================
// HarmonyVoice — A single polyphonic pitch-shifting voice.
//
// Each voice takes mono input and produces mono output at a different pitch
// using the RubberBand library (R3 Finer engine).
//
// Lifecycle (state machine):
//
//   INACTIVE ──activate()──► ATTACKING ──(attack done)──► SUSTAINING
//       ▲                       │                             │
//       │                       │ deactivate()                │ deactivate()
//       │                       ▼                             ▼
//       └────(fade done)────── RELEASING ◄────────────────────┘
//
// During ATTACKING, fadeGain_ ramps linearly from 0 → 1 over the attack time.
// During SUSTAINING, fadeGain_ stays at 1.0.
// During RELEASING, fadeGain_ ramps linearly from its current value → 0 over
// the release time.  The voice becomes INACTIVE once fadeGain_ reaches 0.
//
// When activated before pitch detection has locked on, the voice enters a
// "waiting for pitch" state: it feeds audio to the stretcher to keep it
// primed, but outputs silence to prevent unshifted leakage.
// ============================================================================
class HarmonyVoice
{
public:
    HarmonyVoice();
    ~HarmonyVoice();

    // Allocate RubberBand stretcher and internal buffers.  Must be called
    // before any other method (typically from VoicePool::prepare).
    void prepare(double sampleRate, int maxBlockSize);

    // Activate this voice for a given MIDI note.  If inputPitchHz <= 0 the
    // voice enters "waiting for pitch" mode until a valid pitch arrives.
    // The voice ramps in over the current attack time.
    void activate(int midiNote, float inputPitchHz);

    // Begin a linear fade-out over the current release time.  The voice
    // becomes INACTIVE once the fade completes (inside the next process() call).
    void deactivate();

    // Update the detected input pitch and recalculate the pitch ratio.
    // Called once per block by VoicePool with the latest pitch estimate.
    void updateInputPitch(float inputPitchHz);

    // Set the per-voice detune multiplier (precomputed by VoicePool from cents).
    // 1.0 = no detune.  Applied as a simple multiply on the pitch ratio.
    void setDetuneRatio(float ratio) { detuneRatio_ = ratio; }

    // Set the formant scale factor.  1.0 = no shift.  Values > 1 shift
    // formants up (brighter / smaller vocal tract), < 1 shift them down.
    void setFormantScale(float scale) { formantScale_ = scale; }

    // Set the attack time in milliseconds.  0 = instant on.
    // The per-sample increment is recomputed from the stored sample rate.
    void setAttackMs(float ms);

    // Set the release time in milliseconds.  Minimum ~10 ms for click-free.
    // The per-sample decrement is recomputed from the stored sample rate.
    void setReleaseMs(float ms);

    // Pitch-shift one block of audio.
    // pitchSmoothBlockCoeff is precomputed once per block by VoicePool so
    // that all voices share the same smoothing behaviour.
    void process(const float* input, float* output, int numSamples, float pitchSmoothBlockCoeff);

    bool isActive()    const { return active_; }
    bool isFadingOut() const { return fadingOut_; }
    int  getAssignedNote() const { return assignedNote_; }

    // Returns RubberBand's algorithmic delay in samples (reported to the host
    // for latency compensation).
    int getStartDelay() const;

    // O(1) lookup from a pre-computed 128-entry table.
    static float midiNoteToFrequency(int noteNumber);

private:
    // Recompute targetPitchRatio_ from current input/target Hz and detune.
    void updatePitchRatio();

    // Lazily-initialised MIDI → Hz lookup table (128 entries, computed once)
    static const std::array<float, 128>& getMidiFreqTable();

    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher_;

    double sampleRate_  = 44100.0;
    int    maxBlockSize_ = 512;

    // --- Voice state ---
    bool active_       = false;
    bool fadingOut_    = false;    // True during RELEASING phase
    bool attacking_    = false;    // True during ATTACKING phase
    int  assignedNote_ = -1;

    // --- Pitch tracking ---
    float inputPitchHz_  = -1.0f;   // Last detected input frequency (Hz)
    float targetPitchHz_ = 440.0f;  // Target frequency for assigned MIDI note (Hz)

    float currentPitchRatio_ = 1.0f;  // Smoothed ratio fed to RubberBand
    float targetPitchRatio_  = 1.0f;  // Ideal ratio = (target / input) * detune
    float pitchSmoothCoeff_  = 0.0f;  // Per-sample exponential smoothing coeff

    // --- Amplitude envelope (attack / release) ---
    float fadeGain_        = 0.0f;  // Current envelope gain (0.0 → 1.0 → 0.0)
    float attackIncrement_ = 0.0f;  // Per-sample gain ramp-up   (0 = instant)
    float releaseDecrement_ = 0.0f; // Per-sample gain ramp-down

    // --- Per-voice modifiers (set each block by VoicePool) ---
    float detuneRatio_  = 1.0f;  // Pitch detune multiplier (1.0 = none)
    float formantScale_ = 1.0f;  // Formant shift ratio   (1.0 = none)

    // --- Buffers ---
    std::vector<float> stretcherOutput_;  // Holds RubberBand's output samples
    std::vector<float> startPadBuffer_;   // Zero-padding fed to RubberBand on activate()
    bool  prepared_        = false;
    bool  waitingForPitch_ = false;       // True when activated before pitch lock
};
