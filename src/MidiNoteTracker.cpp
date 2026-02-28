#include "MidiNoteTracker.h"

void MidiNoteTracker::processMidiBuffer(const juce::MidiBuffer& midiMessages)
{
    hadActivity_ = !midiMessages.isEmpty();

    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();

        if (msg.isNoteOn() && msg.getVelocity() > 0)
        {
            activeNotes_.insert(msg.getNoteNumber());
        }
        else if (msg.isNoteOff() || (msg.isNoteOn() && msg.getVelocity() == 0))
        {
            activeNotes_.erase(msg.getNoteNumber());
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            activeNotes_.clear();
        }
    }
}

void MidiNoteTracker::reset()
{
    activeNotes_.clear();
    hadActivity_ = false;
}
