#pragma once

#include <rubberband/RubberBandStretcher.h>
#include <memory>
#include <vector>

// Compile-time constants for tuning
static constexpr float PITCH_SMOOTH_TIME_MS = 100.0f;  // Pitch interpolation time (50–150ms range)
static constexpr float FADE_OUT_TIME_MS     = 10.0f;   // Click-free deactivation fade

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

    // Process one block: pitch-shift input into output.
    void process(const float* input, float* output, int numSamples);

    bool isActive() const { return active_; }
    bool isFadingOut() const { return fadingOut_; }
    int getAssignedNote() const { return assignedNote_; }

    // Convert MIDI note number to frequency in Hz.
    static float midiNoteToFrequency(int noteNumber);

private:
    void updatePitchRatio();

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

    // Buffers for RubberBand I/O
    std::vector<float> stretcherOutput_;
    bool prepared_ = false;
};
