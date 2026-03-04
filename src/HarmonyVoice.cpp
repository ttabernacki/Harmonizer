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

// ============================================================================
// MIDI note → frequency table
// ============================================================================

const std::array<float, 128>& HarmonyVoice::getMidiFreqTable()
{
    // Built once on first call (thread-safe static initialisation).
    // Formula: freq = 440 * 2^((note - 69) / 12)
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
    // Fallback for out-of-range (should never happen with valid MIDI)
    return 440.0f * std::pow(2.0f, (static_cast<float>(noteNumber) - 69.0f) / 12.0f);
}

// ============================================================================
// Envelope timing setters
// ============================================================================

void HarmonyVoice::setAttackMs(float ms)
{
    if (ms <= 0.0f)
    {
        attackIncrement_ = 0.0f;  // 0 means instant attack (fadeGain_ jumps to 1.0)
    }
    else
    {
        float attackSamples = static_cast<float>(sampleRate_) * ms / 1000.0f;
        attackIncrement_ = 1.0f / attackSamples;
    }
}

void HarmonyVoice::setReleaseMs(float ms)
{
    // Enforce a minimum of ~0.5 ms to avoid division by zero; the parameter
    // range already enforces ≥10 ms so this is just a safety net.
    ms = std::max(ms, 0.5f);
    float releaseSamples = static_cast<float>(sampleRate_) * ms / 1000.0f;
    releaseDecrement_ = 1.0f / releaseSamples;
}

// ============================================================================
// Prepare — allocate RubberBand stretcher and work buffers
// ============================================================================

void HarmonyVoice::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    // Default envelope: instant attack, kFadeOutTimeMs release
    attackIncrement_ = 0.0f;
    float fadeSamples = static_cast<float>(sampleRate) * kFadeOutTimeMs / 1000.0f;
    releaseDecrement_ = 1.0f / fadeSamples;

    // --- RubberBand stretcher configuration ---
    //
    // OptionProcessRealTime  — Low-latency mode (as opposed to offline).
    // OptionEngineFaster     — R2 engine: significantly lower CPU than R3
    //                          (Finer), allowing more simultaneous voices.
    // OptionFormantPreserved — Separates formants from pitch so shifting
    //                          doesn't produce "chipmunk" artifacts.
    // OptionWindowShort      — Shorter analysis window for faster transient
    //                          response (slight quality trade-off).
    using RBS = RubberBand::RubberBandStretcher;
    int options = RBS::OptionProcessRealTime
                | RBS::OptionEngineFaster
                | RBS::OptionFormantPreserved
                | RBS::OptionWindowShort;

    stretcher_ = std::make_unique<RBS>(
        static_cast<size_t>(sampleRate),
        1,     // mono (each voice is a single channel)
        options,
        1.0,   // time ratio — 1.0 = no time-stretching, pitch shift only
        1.0    // initial pitch scale
    );

    stretcher_->setMaxProcessSize(static_cast<size_t>(maxBlockSize));

    // Output buffer is 2× block size to handle RubberBand returning more
    // samples than we fed in (can happen during ratio transitions)
    stretcherOutput_.resize(static_cast<size_t>(maxBlockSize * 2), 0.0f);

    // RubberBand may need a "start pad" of silence to fill its internal
    // buffers before it starts producing output.  Pre-allocate so that
    // activate() never hits the heap.
    size_t pad = stretcher_->getPreferredStartPad();
    startPadBuffer_.assign(pad, 0.0f);

    // Reset state
    active_ = false;
    fadingOut_ = false;
    attacking_ = false;
    assignedNote_ = -1;
    currentPitchRatio_ = 1.0f;
    targetPitchRatio_ = 1.0f;
    fadeGain_ = 0.0f;
    prepared_ = true;
}

// ============================================================================
// Activate / deactivate
// ============================================================================

void HarmonyVoice::activate(int midiNote, float inputPitchHz)
{
    if (!prepared_) return;

    assignedNote_ = midiNote;
    targetPitchHz_ = midiNoteToFrequency(midiNote);
    inputPitchHz_ = inputPitchHz;
    fadingOut_ = false;

    // Start the attack envelope: if attackIncrement_ > 0, ramp from 0 → 1;
    // otherwise jump straight to full gain.
    if (attackIncrement_ > 0.0f)
    {
        fadeGain_ = 0.0f;
        attacking_ = true;
    }
    else
    {
        fadeGain_ = 1.0f;
        attacking_ = false;
    }

    updatePitchRatio();

    // If pitch detection hasn't locked on yet, mute output until a valid
    // pitch arrives.  We still feed audio to the stretcher so its internal
    // state is ready when the pitch does come in (see process()).
    if (inputPitchHz <= 0.0f)
    {
        currentPitchRatio_ = 1.0f;
        waitingForPitch_ = true;
    }
    else
    {
        // Jump directly to the target ratio (no smoothing on activation)
        currentPitchRatio_ = targetPitchRatio_;
        waitingForPitch_ = false;
    }

    stretcher_->setPitchScale(static_cast<double>(currentPitchRatio_));

    // Reset stretcher for a clean start (flushes internal buffers)
    stretcher_->reset();

    // Feed the required start padding (silence) so RubberBand's internal
    // buffers are primed and it begins producing output immediately.
    if (!startPadBuffer_.empty())
    {
        std::memset(startPadBuffer_.data(), 0, startPadBuffer_.size() * sizeof(float));
        const float* padPtr = startPadBuffer_.data();
        stretcher_->process(&padPtr, startPadBuffer_.size(), false);
    }

    active_ = true;
}

void HarmonyVoice::deactivate()
{
    // Don't immediately kill the voice — set fadingOut_ so that process()
    // ramps the gain to zero over the release time, preventing clicks.
    // If we were still in the attack phase, the release starts from wherever
    // fadeGain_ currently is (no jump).
    if (active_)
    {
        fadingOut_ = true;
        attacking_ = false;
    }
}

// ============================================================================
// Pitch tracking
// ============================================================================

void HarmonyVoice::updateInputPitch(float inputPitchHz)
{
    if (inputPitchHz > 0.0f)
    {
        inputPitchHz_ = inputPitchHz;
        updatePitchRatio();

        // If we were waiting for a valid pitch, snap to it immediately
        // so the voice doesn't ramp up from an arbitrary ratio.
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
        // ratio = (desired Hz / detected Hz) * detune multiplier
        // detuneRatio_ is precomputed by VoicePool from cents, avoiding
        // a per-voice std::pow call here.
        targetPitchRatio_ = (targetPitchHz_ / inputPitchHz_) * detuneRatio_;

        // Clamp to RubberBand's comfortable range.  Beyond ~4× the R3 engine
        // produces increasing artifacts; below ~0.25× it becomes unstable.
        targetPitchRatio_ = std::clamp(targetPitchRatio_, 0.25f, 4.0f);
    }
}

// ============================================================================
// Process — pitch-shift one block of audio
// ============================================================================

void HarmonyVoice::process(const float* input, float* output, int numSamples, float pitchSmoothBlockCoeff)
{
    if (!active_ || !prepared_ || numSamples <= 0)
    {
        if (numSamples > 0)
            std::memset(output, 0, static_cast<size_t>(numSamples) * sizeof(float));
        return;
    }

    // --- Waiting for pitch: output silence but keep stretcher fed ---
    if (waitingForPitch_)
    {
        std::memset(output, 0, static_cast<size_t>(numSamples) * sizeof(float));
        const float* inputPtr = input;
        stretcher_->process(&inputPtr, static_cast<size_t>(numSamples), false);
        // Drain any output to prevent the stretcher's internal buffer from growing
        int avail = stretcher_->available();
        if (avail > 0)
        {
            size_t toDrain = std::min(static_cast<size_t>(avail), stretcherOutput_.size());
            float* drainPtr = stretcherOutput_.data();
            stretcher_->retrieve(&drainPtr, toDrain);
        }
        return;
    }

    // --- Smooth the pitch ratio toward its target ---
    currentPitchRatio_ += pitchSmoothBlockCoeff * (targetPitchRatio_ - currentPitchRatio_);
    stretcher_->setPitchScale(static_cast<double>(currentPitchRatio_));
    stretcher_->setFormantScale(static_cast<double>(formantScale_));

    // --- Feed input to RubberBand ---
    const float* inputPtr = input;
    stretcher_->process(&inputPtr, static_cast<size_t>(numSamples), false);

    // --- Retrieve pitch-shifted output ---
    int avail = stretcher_->available();
    if (avail <= 0)
    {
        std::memset(output, 0, static_cast<size_t>(numSamples) * sizeof(float));
        return;
    }

    size_t toRetrieve = std::min(static_cast<size_t>(avail), static_cast<size_t>(numSamples));
    float* outPtr = stretcherOutput_.data();
    stretcher_->retrieve(&outPtr, toRetrieve);

    // --- Copy to output with attack/release envelope ---
    //
    // Split into phase-specific loops to avoid per-sample branching.
    // At most one phase transition can happen per block.
    size_t outSamples = std::min(toRetrieve, static_cast<size_t>(numSamples));
    size_t i = 0;
    size_t n = static_cast<size_t>(numSamples);

    // Attack phase: ramp gain from current → 1.0
    if (attacking_)
    {
        for (; i < n; ++i)
        {
            float sample = (i < outSamples) ? stretcherOutput_[i] : 0.0f;
            output[i] = sample * fadeGain_;
            fadeGain_ += attackIncrement_;
            if (fadeGain_ >= 1.0f)
            {
                fadeGain_ = 1.0f;
                attacking_ = false;
                ++i;
                break;
            }
        }
    }

    // Sustain phase: constant gain, no branching per sample
    if (!attacking_ && !fadingOut_)
    {
        float gain = fadeGain_;
        for (; i < n; ++i)
        {
            float sample = (i < outSamples) ? stretcherOutput_[i] : 0.0f;
            output[i] = sample * gain;
        }
    }

    // Release phase: ramp gain from current → 0.0
    if (fadingOut_)
    {
        for (; i < n; ++i)
        {
            float sample = (i < outSamples) ? stretcherOutput_[i] : 0.0f;
            output[i] = sample * fadeGain_;
            fadeGain_ -= releaseDecrement_;
            if (fadeGain_ <= 0.0f)
            {
                fadeGain_ = 0.0f;
                active_ = false;
                fadingOut_ = false;
                assignedNote_ = -1;
                if (i + 1 < n)
                    std::memset(output + i + 1, 0, (n - i - 1) * sizeof(float));
                break;
            }
        }
    }
}
