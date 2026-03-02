#include "MidiNoteTracker.h"

void MidiNoteTracker::processMidiBuffer(const juce::MidiBuffer& midiMessages)
{
    hadActivity_ = !midiMessages.isEmpty();

    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();

        // Channel filter: skip messages not on the selected channel.
        // channelFilter_ == 0 means "omni" (accept all).
        if (channelFilter_ > 0 && msg.getChannel() != channelFilter_)
            continue;

        if (msg.isNoteOn() && msg.getVelocity() > 0)
        {
            addNote(msg.getNoteNumber());
        }
        else if (msg.isNoteOff() || (msg.isNoteOn() && msg.getVelocity() == 0))
        {
            // MIDI spec: noteOn with velocity 0 is equivalent to noteOff
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
    // Prevent duplicates — a note-on for an already-held note is ignored.
    // This avoids double-allocating voices for the same pitch.
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
            // Shift remaining notes down to fill the gap.  This preserves
            // the press-order so VoicePool's voice assignment stays stable.
            for (int j = i; j < numActive_ - 1; ++j)
                activeNotes_[static_cast<size_t>(j)] = activeNotes_[static_cast<size_t>(j + 1)];
            --numActive_;
            return;
        }
    }
    // Note wasn't in the list — harmless (handles orphaned note-offs)
}

void MidiNoteTracker::reset()
{
    numActive_ = 0;
    hadActivity_ = false;
}
