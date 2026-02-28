#include "VoicePool.h"
#include <cstring>
#include <algorithm>

VoicePool::VoicePool() = default;

void VoicePool::prepare(double sampleRate, int maxBlockSize)
{
    maxBlockSize_ = maxBlockSize;
    voiceBuffer_.resize(static_cast<size_t>(maxBlockSize), 0.0f);

    for (auto& voice : voices_)
        voice.prepare(sampleRate, maxBlockSize);
}

void VoicePool::updateNotes(const std::set<int>& activeNotes, float detectedPitchHz)
{
    // Step 1: Deactivate voices whose notes are no longer held
    for (auto& voice : voices_)
    {
        if (voice.isActive() && !voice.isFadingOut())
        {
            if (activeNotes.find(voice.getAssignedNote()) == activeNotes.end())
            {
                voice.deactivate();
            }
        }
    }

    // Step 2: Update input pitch for all active voices
    for (auto& voice : voices_)
    {
        if (voice.isActive())
        {
            voice.updateInputPitch(detectedPitchHz);
        }
    }

    // Step 3: Activate new voices for notes that don't have a voice yet
    for (int note : activeNotes)
    {
        // Check if this note already has a voice
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
            // Find a free voice (not active, or done fading)
            for (auto& voice : voices_)
            {
                if (!voice.isActive())
                {
                    voice.activate(note, detectedPitchHz);
                    break;
                }
            }
            // If no free voice, the note is ignored (first-held priority)
        }
    }
}

void VoicePool::processBlock(const float* input, float* wetOutput, int numSamples)
{
    std::memset(wetOutput, 0, static_cast<size_t>(numSamples) * sizeof(float));

    for (auto& voice : voices_)
    {
        if (voice.isActive())
        {
            voice.process(input, voiceBuffer_.data(), numSamples);

            // Sum into wet output
            for (int i = 0; i < numSamples; ++i)
                wetOutput[i] += voiceBuffer_[static_cast<size_t>(i)];
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
