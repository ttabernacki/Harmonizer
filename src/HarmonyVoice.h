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
//   INACTIVE ──activate()──► ACTIVE ──deactivate()──► FADING ──(fade done)──► INACTIVE
//       │                      │                        │
//       │                      │ updateInputPitch()     │ (10 ms linear fade)
//       │                      ▼                        │
//       │                   smoothly tracks             ▼
//       │                   target pitch ratio       zeroes output
//       │                                            and resets state
//       └──────────────────────────────────────────────────────────────────────┘
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
    void activate(int midiNote, float inputPitchHz);

    // Begin a 10 ms linear fade-out.  The voice becomes INACTIVE once the
    // fade completes (inside the next process() call).
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
    bool fadingOut_    = false;
    int  assignedNote_ = -1;

    // --- Pitch tracking ---
    float inputPitchHz_  = -1.0f;   // Last detected input frequency (Hz)
    float targetPitchHz_ = 440.0f;  // Target frequency for assigned MIDI note (Hz)

    float currentPitchRatio_ = 1.0f;  // Smoothed ratio fed to RubberBand
    float targetPitchRatio_  = 1.0f;  // Ideal ratio = (target / input) * detune
    float pitchSmoothCoeff_  = 0.0f;  // Per-sample exponential smoothing coeff

    // --- Fade-out ---
    float fadeGain_      = 0.0f;  // Current gain (1.0 → 0.0 during fade-out)
    float fadeIncrement_ = 0.0f;  // Per-sample linear decrement

    // --- Per-voice modifiers (set each block by VoicePool) ---
    float detuneRatio_  = 1.0f;  // Pitch detune multiplier (1.0 = none)
    float formantScale_ = 1.0f;  // Formant shift ratio   (1.0 = none)

    // --- Buffers ---
    std::vector<float> stretcherOutput_;  // Holds RubberBand's output samples
    std::vector<float> startPadBuffer_;   // Zero-padding fed to RubberBand on activate()
    bool  prepared_        = false;
    bool  waitingForPitch_ = false;       // True when activated before pitch lock
};
