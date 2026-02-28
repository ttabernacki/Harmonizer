#include "MidiNoteTracker.h"

void MidiNoteTracker::processMidiBuffer(const juce::MidiBuffer& midiMessages)
{
    hadActivity_ = !midiMessages.isEmpty();

    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();

        if (msg.isNoteOn() && msg.getVelocity() > 0)
        {
            addNote(msg.getNoteNumber());
        }
        else if (msg.isNoteOff() || (msg.isNoteOn() && msg.getVelocity() == 0))
        {
            removeNote(msg.getNoteNumber());
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            numActive_ = 0;
        }
    }
}

void MidiNoteTracker::addNote(int note)
{
    // Don't add duplicates
    for (int i = 0; i < numActive_; ++i)
    {
        if (activeNotes_[static_cast<size_t>(i)] == note)
            return;
    }
    if (numActive_ < 128)
    {
        activeNotes_[static_cast<size_t>(numActive_)] = note;
        ++numActive_;
    }
}

void MidiNoteTracker::removeNote(int note)
{
    for (int i = 0; i < numActive_; ++i)
    {
        if (activeNotes_[static_cast<size_t>(i)] == note)
        {
            // Shift remaining notes down to fill gap (preserves insertion order)
            for (int j = i; j < numActive_ - 1; ++j)
                activeNotes_[static_cast<size_t>(j)] = activeNotes_[static_cast<size_t>(j + 1)];
            --numActive_;
            return;
        }
    }
    // Note not found — ignore (handles noteOff without preceding noteOn)
}

void MidiNoteTracker::reset()
{
    numActive_ = 0;
    hadActivity_ = false;
}
