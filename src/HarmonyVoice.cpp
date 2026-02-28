#include "HarmonyVoice.h"
#include <cmath>
#include <algorithm>
#include <cstring>

HarmonyVoice::HarmonyVoice() = default;
HarmonyVoice::~HarmonyVoice() = default;

float HarmonyVoice::midiNoteToFrequency(int noteNumber)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(noteNumber) - 69.0f) / 12.0f);
}

void HarmonyVoice::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    // Calculate smoothing coefficient: exponential decay over PITCH_SMOOTH_TIME_MS
    float smoothSamples = static_cast<float>(sampleRate) * PITCH_SMOOTH_TIME_MS / 1000.0f;
    pitchSmoothCoeff_ = 1.0f - std::exp(-1.0f / smoothSamples);

    // Fade-out increment: full fade over FADE_OUT_TIME_MS
    float fadeSamples = static_cast<float>(sampleRate) * FADE_OUT_TIME_MS / 1000.0f;
    fadeIncrement_ = 1.0f / fadeSamples;

    // Create RubberBand stretcher: real-time, R3 (Finer) engine, pitch consistency, formant preservation
    using RBS = RubberBand::RubberBandStretcher;
    int options = RBS::OptionProcessRealTime
                | RBS::OptionEngineFiner
                | RBS::OptionPitchHighConsistency
                | RBS::OptionFormantPreserved
                | RBS::OptionWindowShort;

    stretcher_ = std::make_unique<RBS>(
        static_cast<size_t>(sampleRate),
        1,  // mono
        options,
        1.0,  // time ratio (no stretching)
        1.0   // initial pitch scale
    );

    stretcher_->setMaxProcessSize(static_cast<size_t>(maxBlockSize));

    stretcherOutput_.resize(static_cast<size_t>(maxBlockSize * 2), 0.0f);

    active_ = false;
    fadingOut_ = false;
    assignedNote_ = -1;
    currentPitchRatio_ = 1.0f;
    targetPitchRatio_ = 1.0f;
    fadeGain_ = 0.0f;
    prepared_ = true;
}

void HarmonyVoice::activate(int midiNote, float inputPitchHz)
{
    if (!prepared_) return;

    assignedNote_ = midiNote;
    targetPitchHz_ = midiNoteToFrequency(midiNote);
    inputPitchHz_ = inputPitchHz;
    active_ = true;
    fadingOut_ = false;
    fadeGain_ = 1.0f;

    updatePitchRatio();

    // Snap immediately (no smoothing on activation)
    currentPitchRatio_ = targetPitchRatio_;
    stretcher_->setPitchScale(static_cast<double>(currentPitchRatio_));

    // Reset the stretcher for a clean start
    stretcher_->reset();

    // Provide start padding
    size_t pad = stretcher_->getPreferredStartPad();
    if (pad > 0)
    {
        std::vector<float> silence(pad, 0.0f);
        const float* silencePtr = silence.data();
        stretcher_->process(&silencePtr, pad, false);
    }
}

void HarmonyVoice::deactivate()
{
    if (active_)
        fadingOut_ = true;
}

void HarmonyVoice::updateInputPitch(float inputPitchHz)
{
    if (inputPitchHz > 0.0f)
    {
        inputPitchHz_ = inputPitchHz;
        updatePitchRatio();
    }
}

void HarmonyVoice::updatePitchRatio()
{
    if (inputPitchHz_ > 0.0f && targetPitchHz_ > 0.0f)
    {
        targetPitchRatio_ = targetPitchHz_ / inputPitchHz_;

        // Clamp to reasonable range (Rubber Band handles ~0.25x to ~4x well)
        targetPitchRatio_ = std::clamp(targetPitchRatio_, 0.25f, 4.0f);
    }
}

void HarmonyVoice::process(const float* input, float* output, int numSamples)
{
    if (!active_ || !prepared_ || numSamples <= 0)
    {
        std::memset(output, 0, static_cast<size_t>(numSamples) * sizeof(float));
        return;
    }

    // Smooth the pitch ratio toward target (per-block approximation)
    for (int i = 0; i < 4; ++i)  // A few smoothing steps per block
    {
        currentPitchRatio_ += pitchSmoothCoeff_ * (targetPitchRatio_ - currentPitchRatio_)
                              * static_cast<float>(numSamples) / 4.0f;
    }
    stretcher_->setPitchScale(static_cast<double>(currentPitchRatio_));

    // Feed input to RubberBand
    const float* inputPtr = input;
    stretcher_->process(&inputPtr, static_cast<size_t>(numSamples), false);

    // Retrieve available output
    int avail = stretcher_->available();
    if (avail <= 0)
    {
        std::memset(output, 0, static_cast<size_t>(numSamples) * sizeof(float));
        return;
    }

    size_t toRetrieve = std::min(static_cast<size_t>(avail), static_cast<size_t>(numSamples));

    // Ensure output buffer is large enough
    if (stretcherOutput_.size() < toRetrieve)
        stretcherOutput_.resize(toRetrieve);

    float* outPtr = stretcherOutput_.data();
    stretcher_->retrieve(&outPtr, toRetrieve);

    // Copy retrieved samples to output, applying fade
    size_t outSamples = std::min(toRetrieve, static_cast<size_t>(numSamples));
    for (size_t i = 0; i < static_cast<size_t>(numSamples); ++i)
    {
        float sample = (i < outSamples) ? stretcherOutput_[i] : 0.0f;

        // Apply fade gain
        output[i] = sample * fadeGain_;

        // Update fade for deactivation
        if (fadingOut_)
        {
            fadeGain_ -= fadeIncrement_;
            if (fadeGain_ <= 0.0f)
            {
                fadeGain_ = 0.0f;
                active_ = false;
                fadingOut_ = false;
                assignedNote_ = -1;
                // Zero remaining output
                std::memset(output + i + 1, 0,
                            (static_cast<size_t>(numSamples) - i - 1) * sizeof(float));
                break;
            }
        }
    }
}
