#include "VoicePool.h"
#include <cstring>
#include <cmath>
#include <algorithm>

VoicePool::VoicePool() = default;

// ============================================================================
// Prepare
// ============================================================================

void VoicePool::prepare(double sampleRate, int maxBlockSize)
{
    maxBlockSize_ = maxBlockSize;

    // Compute the shared pitch-smoothing coefficient.  All voices use the same
    // sample rate and the same kPitchSmoothTimeMs constant, so we compute it
    // once here and pass a per-block coefficient to each voice in renderVoices().
    float smoothSamples = static_cast<float>(sampleRate) * kPitchSmoothTimeMs / 1000.0f;
    pitchSmoothCoeff_ = 1.0f - std::exp(-1.0f / smoothSamples);

    for (auto& voice : voices_)
        voice.prepare(sampleRate, maxBlockSize);
}

// ============================================================================
// Formant shift (cached conversion from semitones → linear ratio)
// ============================================================================

void VoicePool::setFormantShiftSemitones(float semitones)
{
    // Only recompute std::pow when the value has meaningfully changed.
    // 0.001 semitones is far below the audible threshold (~1 cent = 0.01 st).
    if (std::abs(semitones - lastFormantSemitones_) > 0.001f)
    {
        lastFormantSemitones_ = semitones;
        formantScale_ = std::exp2f(semitones / 12.0f);
    }

    // Toggle formant preservation on the stretchers only when the formant
    // shift moves away from zero or back to zero.  5 cents (~0.05 st) is
    // well below audibility and avoids toggling on tiny automation noise.
    bool needFormant = std::abs(semitones) > 0.05f;
    if (needFormant != formantEnabled_)
    {
        formantEnabled_ = needFormant;
        for (auto& voice : voices_)
            voice.enableFormantPreservation(needFormant);
    }
}

// ============================================================================
// Envelope timing (forwarded to every voice)
// ============================================================================

void VoicePool::setAttackMs(float ms)
{
    for (auto& voice : voices_)
        voice.setAttackMs(ms);
}

void VoicePool::setReleaseMs(float ms)
{
    for (auto& voice : voices_)
        voice.setReleaseMs(ms);
}

// ============================================================================
// Voice allocation — 4-step process called once per audio block
// ============================================================================

void VoicePool::updateNotes(const int* activeNotes, int numActiveNotes, float detectedPitchHz)
{
    // ------------------------------------------------------------------
    // Step 1: Deactivate voices whose MIDI notes are no longer held
    // ------------------------------------------------------------------
    for (auto& voice : voices_)
    {
        if (voice.isActive() && !voice.isFadingOut())
        {
            bool stillHeld = false;
            for (int i = 0; i < numActiveNotes; ++i)
            {
                if (activeNotes[i] == voice.getAssignedNote())
                {
                    stillHeld = true;
                    break;
                }
            }
            if (!stillHeld)
                voice.deactivate();  // begins release fade-out
        }
    }

    // ------------------------------------------------------------------
    // Step 2: Distribute detune and formant shift to active voices
    // ------------------------------------------------------------------
    // Detune is spread symmetrically: with N active voices, voice i gets:
    //
    //   offset = detuneCents * (2*i/(N-1) - 1)
    //
    // Example with 3 voices and 10 cents:
    //   voice 0 → -10 ct,  voice 1 → 0 ct,  voice 2 → +10 ct
    //
    // The offset in cents is converted to a pitch ratio:
    //   ratio = 2^(offset / 1200)
    //
    // This is done here (not in HarmonyVoice) so the ratio is ready before
    // updateInputPitch triggers updatePitchRatio in step 3.
    {
        int activeIdx = 0;
        int activeTotal = getActiveVoiceCount();
        for (auto& voice : voices_)
        {
            if (voice.isActive() && !voice.isFadingOut())
            {
                float detuneRatio = 1.0f;
                if (activeTotal > 1 && detuneCents_ > 0.0f)
                {
                    float t = static_cast<float>(activeIdx) / static_cast<float>(activeTotal - 1);
                    float offsetCents = detuneCents_ * (2.0f * t - 1.0f);
                    detuneRatio = std::exp2f(offsetCents / 1200.0f);
                }
                voice.setDetuneRatio(detuneRatio);
                voice.setFormantScale(formantScale_);
                ++activeIdx;
            }
        }
    }

    // ------------------------------------------------------------------
    // Step 3: Feed the latest detected pitch to active (non-fading) voices
    // ------------------------------------------------------------------
    // This triggers updatePitchRatio() inside each voice, which uses the
    // detune ratio we just set in step 2.
    // Fading voices are skipped: updating their pitch during the release
    // fade would cause audible pitch wobble in the tail.
    for (auto& voice : voices_)
    {
        if (voice.isActive() && !voice.isFadingOut())
            voice.updateInputPitch(detectedPitchHz);
    }

    // ------------------------------------------------------------------
    // Step 4: Activate voices for newly pressed MIDI notes
    // ------------------------------------------------------------------
    for (int n = 0; n < numActiveNotes; ++n)
    {
        int note = activeNotes[n];

        // Check if this note already has a non-fading voice assigned
        bool found = false;
        for (const auto& voice : voices_)
        {
            if (voice.isActive() && !voice.isFadingOut() && voice.getAssignedNote() == note)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            // Priority 1: use an inactive (empty) voice slot
            HarmonyVoice* newVoice = nullptr;
            for (auto& voice : voices_)
            {
                if (!voice.isActive())
                {
                    newVoice = &voice;
                    break;
                }
            }

            // Priority 2: steal a fading voice (its fade-out is interrupted)
            if (newVoice == nullptr)
            {
                for (auto& voice : voices_)
                {
                    if (voice.isFadingOut())
                    {
                        newVoice = &voice;
                        break;
                    }
                }
            }

            if (newVoice != nullptr)
            {
                // Apply formant shift BEFORE activation so the voice starts
                // with the correct settings from its very first block.
                // (Detune will be distributed on the next updateNotes call,
                // once this voice is counted among the active voices.)
                newVoice->setFormantScale(formantScale_);
                newVoice->activate(note, detectedPitchHz);
            }
        }
    }
}

// ============================================================================
// Render all voices
// ============================================================================

void VoicePool::renderVoices(const float* input, float* voiceOutputs[], int numSamples)
{
    // Convert the per-sample smoothing coefficient into a per-block coefficient.
    // Cache it since block size rarely changes between calls.
    if (numSamples != cachedBlockSize_)
    {
        cachedBlockSize_ = numSamples;
        cachedPitchBlockCoeff_ = 1.0f - std::pow(1.0f - pitchSmoothCoeff_, static_cast<float>(numSamples));
    }
    float blockCoeff = cachedPitchBlockCoeff_;

    // --- Enforce voice processing budget ---
    // Count total active voices (sustaining + fading).  If over budget,
    // force-kill the oldest fading voices so we don't overload the CPU.
    int totalActive = 0;
    for (const auto& voice : voices_)
        if (voice.isActive()) ++totalActive;

    if (totalActive > kMaxProcessingVoices)
    {
        for (auto& voice : voices_)
        {
            if (totalActive <= kMaxProcessingVoices) break;
            if (voice.isFadingOut())
            {
                voice.forceKill();
                --totalActive;
            }
        }
    }

    for (int v = 0; v < kMaxVoices; ++v)
    {
        if (voices_[static_cast<size_t>(v)].isActive())
        {
            voices_[static_cast<size_t>(v)].process(input, voiceOutputs[v], numSamples, blockCoeff);
        }
        else
        {
            std::memset(voiceOutputs[v], 0, static_cast<size_t>(numSamples) * sizeof(float));
        }
    }
}

// ============================================================================
// Active voice count (excludes fading voices)
// ============================================================================

int VoicePool::getActiveVoiceCount() const
{
    int count = 0;
    for (const auto& voice : voices_)
    {
        if (voice.isActive() && !voice.isFadingOut())
            ++count;
    }
    return count;
}
