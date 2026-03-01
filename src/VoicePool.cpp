#include "VoicePool.h"
#include <cstring>
#include <cmath>
#include <algorithm>

VoicePool::VoicePool() = default;

void VoicePool::prepare(double sampleRate, int maxBlockSize)
{
    maxBlockSize_ = maxBlockSize;

    for (auto& voice : voices_)
        voice.prepare(sampleRate, maxBlockSize);
}

void VoicePool::setFormantShiftSemitones(float semitones)
{
    // Only recompute std::pow when the value actually changes
    if (std::abs(semitones - lastFormantSemitones_) > 0.001f)
    {
        lastFormantSemitones_ = semitones;
        formantScale_ = std::pow(2.0f, semitones / 12.0f);
    }
}

void VoicePool::updateNotes(const int* activeNotes, int numActiveNotes, float detectedPitchHz)
{
    // Step 1: Deactivate voices whose notes are no longer held
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
                voice.deactivate();
        }
    }

    // Step 2: Distribute detune and formant shift BEFORE updating pitch,
    // so updatePitchRatio() uses the current detune offset (not the stale one).
    {
        int activeIdx = 0;
        int activeTotal = getActiveVoiceCount();
        for (auto& voice : voices_)
        {
            if (voice.isActive() && !voice.isFadingOut())
            {
                float detuneOffset = 0.0f;
                if (activeTotal > 1 && detuneCents_ > 0.0f)
                {
                    float t = static_cast<float>(activeIdx) / static_cast<float>(activeTotal - 1);
                    detuneOffset = detuneCents_ * (2.0f * t - 1.0f);
                }
                voice.setDetuneOffset(detuneOffset);
                voice.setFormantScale(formantScale_);
                ++activeIdx;
            }
        }
    }

    // Step 3: Update input pitch for all active voices (now uses current detune offset)
    for (auto& voice : voices_)
    {
        if (voice.isActive())
            voice.updateInputPitch(detectedPitchHz);
    }

    // Step 4: Activate new voices for notes that don't have a voice yet
    for (int n = 0; n < numActiveNotes; ++n)
    {
        int note = activeNotes[n];

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
            bool allocated = false;
            for (auto& voice : voices_)
            {
                if (!voice.isActive())
                {
                    voice.activate(note, detectedPitchHz);
                    allocated = true;
                    break;
                }
            }

            if (!allocated)
            {
                for (auto& voice : voices_)
                {
                    if (voice.isFadingOut())
                    {
                        voice.activate(note, detectedPitchHz);
                        break;
                    }
                }
            }
        }
    }
}

void VoicePool::renderVoices(const float* input, float* voiceOutputs[], int numSamples)
{
    for (int v = 0; v < kMaxVoices; ++v)
    {
        if (voices_[static_cast<size_t>(v)].isActive())
        {
            voices_[static_cast<size_t>(v)].process(input, voiceOutputs[v], numSamples);
        }
        else
        {
            std::memset(voiceOutputs[v], 0, static_cast<size_t>(numSamples) * sizeof(float));
        }
    }
}

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
