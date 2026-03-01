#pragma once

#include "Constants.h"
#include <rubberband/RubberBandStretcher.h>
#include <array>
#include <memory>
#include <vector>

class HarmonyVoice
{
public:
    HarmonyVoice();
    ~HarmonyVoice();

    void prepare(double sampleRate, int maxBlockSize);

    // Activate this voice for a given MIDI note.
    void activate(int midiNote, float inputPitchHz);

    // Begin fade-out deactivation.
    void deactivate();

    // Update the detected input pitch (recalculates pitch ratio).
    void updateInputPitch(float inputPitchHz);

    // Set per-voice detune offset in cents (applied to pitch ratio).
    void setDetuneOffset(float cents) { detuneOffsetCents_ = cents; }

    // Set formant scale in semitones (independent of pitch shift).
    void setFormantShift(float semitones);

    // Process one block: pitch-shift input into output.
    void process(const float* input, float* output, int numSamples);

    bool isActive() const { return active_; }
    bool isFadingOut() const { return fadingOut_; }
    int getAssignedNote() const { return assignedNote_; }

    // Returns RubberBand's internal processing delay in samples (for host latency reporting).
    int getStartDelay() const;

    // Convert MIDI note number to frequency in Hz (O(1) table lookup).
    static float midiNoteToFrequency(int noteNumber);

private:
    void updatePitchRatio();

    // Pre-computed MIDI note to frequency lookup table (128 entries)
    static const std::array<float, 128>& getMidiFreqTable();

    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher_;

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;

    bool active_ = false;
    bool fadingOut_ = false;
    int assignedNote_ = -1;

    float inputPitchHz_ = -1.0f;
    float targetPitchHz_ = 440.0f;

    float currentPitchRatio_ = 1.0f;
    float targetPitchRatio_ = 1.0f;
    float pitchSmoothCoeff_ = 0.0f;  // Exponential smoothing coefficient per sample

    float fadeGain_ = 0.0f;
    float fadeIncrement_ = 0.0f;     // Per-sample fade-out decrement

    float detuneOffsetCents_ = 0.0f; // Per-voice detune offset
    float formantScale_ = 1.0f;      // Formant shift ratio (1.0 = no shift)

    // Buffers for RubberBand I/O
    std::vector<float> stretcherOutput_;
    std::vector<float> startPadBuffer_;  // Pre-allocated to avoid heap alloc in activate()
    bool prepared_ = false;
    bool waitingForPitch_ = false;       // True when activated before pitch detection has locked on
};
