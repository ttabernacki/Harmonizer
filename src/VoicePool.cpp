#include "VoicePool.h"
#include <cstring>
#include <algorithm>

VoicePool::VoicePool() = default;

void VoicePool::prepare(double sampleRate, int maxBlockSize)
{
    maxBlockSize_ = maxBlockSize;

    for (auto& voice : voices_)
        voice.prepare(sampleRate, maxBlockSize);
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

    // Step 2: Update input pitch for all active voices
    for (auto& voice : voices_)
    {
        if (voice.isActive())
            voice.updateInputPitch(detectedPitchHz);
    }

    // Step 3: Activate new voices for notes that don't have a voice yet
    for (int n = 0; n < numActiveNotes; ++n)
    {
        int note = activeNotes[n];

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
            // Find a free voice
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

            // If no free voice, steal a fading-out voice (it's nearly silent anyway)
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
