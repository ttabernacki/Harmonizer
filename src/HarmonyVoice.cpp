#include "HarmonyVoice.h"
#include <cmath>
#include <algorithm>
#include <cstring>

HarmonyVoice::HarmonyVoice() = default;
HarmonyVoice::~HarmonyVoice() = default;

int HarmonyVoice::getStartDelay() const
{
    if (stretcher_)
        return static_cast<int>(stretcher_->getStartDelay());
    return 0;
}

const std::array<float, 128>& HarmonyVoice::getMidiFreqTable()
{
    static const auto table = []()
    {
        std::array<float, 128> t{};
        for (int i = 0; i < 128; ++i)
            t[static_cast<size_t>(i)] = 440.0f * std::pow(2.0f, (static_cast<float>(i) - 69.0f) / 12.0f);
        return t;
    }();
    return table;
}

float HarmonyVoice::midiNoteToFrequency(int noteNumber)
{
    if (noteNumber >= 0 && noteNumber < 128)
        return getMidiFreqTable()[static_cast<size_t>(noteNumber)];
    return 440.0f * std::pow(2.0f, (static_cast<float>(noteNumber) - 69.0f) / 12.0f);
}

void HarmonyVoice::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    // Calculate per-sample smoothing coefficient for exponential decay over kPitchSmoothTimeMs
    float smoothSamples = static_cast<float>(sampleRate) * kPitchSmoothTimeMs / 1000.0f;
    pitchSmoothCoeff_ = 1.0f - std::exp(-1.0f / smoothSamples);

    // Fade-out increment: full fade over kFadeOutTimeMs
    float fadeSamples = static_cast<float>(sampleRate) * kFadeOutTimeMs / 1000.0f;
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

    // Pre-allocate start padding buffer (avoid heap alloc in activate)
    size_t pad = stretcher_->getPreferredStartPad();
    startPadBuffer_.assign(pad, 0.0f);

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
    fadingOut_ = false;
    fadeGain_ = 1.0f;

    updatePitchRatio();

    // If no pitch detected yet, mute until we get a valid pitch to avoid unshifted leakage
    if (inputPitchHz <= 0.0f)
    {
        currentPitchRatio_ = 1.0f;
        waitingForPitch_ = true;
    }
    else
    {
        currentPitchRatio_ = targetPitchRatio_;
        waitingForPitch_ = false;
    }

    stretcher_->setPitchScale(static_cast<double>(currentPitchRatio_));

    // Reset the stretcher for a clean start
    stretcher_->reset();

    // Provide start padding using pre-allocated buffer (no heap allocation)
    if (!startPadBuffer_.empty())
    {
        // Ensure pad buffer is zeroed
        std::memset(startPadBuffer_.data(), 0, startPadBuffer_.size() * sizeof(float));
        const float* padPtr = startPadBuffer_.data();
        stretcher_->process(&padPtr, startPadBuffer_.size(), false);
    }

    active_ = true;
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

        // If we were waiting for a valid pitch, snap to it immediately
        if (waitingForPitch_)
        {
            currentPitchRatio_ = targetPitchRatio_;
            waitingForPitch_ = false;
        }
    }
}

void HarmonyVoice::updatePitchRatio()
{
    if (inputPitchHz_ > 0.0f && targetPitchHz_ > 0.0f)
    {
        targetPitchRatio_ = targetPitchHz_ / inputPitchHz_;

        // Apply per-voice detune offset (converts cents to ratio multiplier)
        if (std::abs(detuneOffsetCents_) > 0.01f)
            targetPitchRatio_ *= std::pow(2.0f, detuneOffsetCents_ / 1200.0f);

        // Clamp to reasonable range (Rubber Band handles ~0.25x to ~4x well)
        targetPitchRatio_ = std::clamp(targetPitchRatio_, 0.25f, 4.0f);
    }
}

void HarmonyVoice::process(const float* input, float* output, int numSamples)
{
    if (!active_ || !prepared_ || numSamples <= 0)
    {
        if (numSamples > 0)
            std::memset(output, 0, static_cast<size_t>(numSamples) * sizeof(float));
        return;
    }

    // If waiting for pitch detection, output silence (don't feed unshifted audio)
    if (waitingForPitch_)
    {
        std::memset(output, 0, static_cast<size_t>(numSamples) * sizeof(float));
        // Still feed input to keep stretcher primed
        const float* inputPtr = input;
        stretcher_->process(&inputPtr, static_cast<size_t>(numSamples), false);
        // Discard output
        int avail = stretcher_->available();
        if (avail > 0)
        {
            size_t toDrain = std::min(static_cast<size_t>(avail), stretcherOutput_.size());
            float* drainPtr = stretcherOutput_.data();
            stretcher_->retrieve(&drainPtr, toDrain);
        }
        return;
    }

    // Smooth the pitch ratio toward target using correct per-sample exponential filter
    // Compute per-block coefficient: 1 - (1 - perSampleCoeff)^numSamples
    float blockCoeff = 1.0f - std::pow(1.0f - pitchSmoothCoeff_, static_cast<float>(numSamples));
    currentPitchRatio_ += blockCoeff * (targetPitchRatio_ - currentPitchRatio_);
    stretcher_->setPitchScale(static_cast<double>(currentPitchRatio_));
    stretcher_->setFormantScale(static_cast<double>(formantScale_));

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

    // stretcherOutput_ is pre-allocated in prepare() to maxBlockSize*2 — no resize needed
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
                if (i + 1 < static_cast<size_t>(numSamples))
                {
                    std::memset(output + i + 1, 0,
                                (static_cast<size_t>(numSamples) - i - 1) * sizeof(float));
                }
                break;
            }
        }
    }
}
