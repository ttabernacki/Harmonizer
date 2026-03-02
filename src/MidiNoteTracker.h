#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

// ============================================================================
// MidiNoteTracker — Maintains the set of currently held MIDI notes.
//
// Designed for the audio thread: fixed-size storage, zero allocation.
//
// Notes are stored in press-order (oldest first).  This order is preserved
// through additions and removals so that VoicePool can use it for consistent
// voice assignment (the first note in the list always maps to voice 0, etc.).
//
// Channel filtering is supported: set to 0 for omni (all channels) or 1-16
// for a specific channel.  Changing the filter clears all held notes to
// prevent notes from the old channel getting "stuck".
// ============================================================================
class MidiNoteTracker
{
public:
    MidiNoteTracker() = default;

    // Set MIDI channel filter.  0 = omni (all channels), 1-16 = specific.
    // Changing the channel immediately clears held notes.
    void setChannelFilter(int channel)
    {
        if (channel != channelFilter_)
        {
            channelFilter_ = channel;
            numActive_ = 0;
        }
    }

    // Walk through every MIDI message in the buffer and update the active
    // note set (note-on adds, note-off removes, all-notes-off clears).
    void processMidiBuffer(const juce::MidiBuffer& midiMessages);

    // Returns a pointer to the active note array (MIDI note numbers, 0-127)
    // and writes the count to numNotes.  Notes are in press order.
    const int* getActiveNotes(int& numNotes) const
    {
        numNotes = numActive_;
        return activeNotes_.data();
    }

    int getActiveNoteCount() const { return numActive_; }

    // True if any MIDI messages were received in the most recent
    // processMidiBuffer call (used to flash the MIDI LED in the UI).
    bool hasActivity() const { return hadActivity_; }

    // Clear all held notes and activity flag (called from prepareToPlay).
    void reset();

private:
    void addNote(int note);
    void removeNote(int note);

    // Fixed-size array — 128 entries covers every possible MIDI note.
    // Only the first numActive_ entries are valid.
    std::array<int, 128> activeNotes_{};
    int  numActive_    = 0;
    bool hadActivity_  = false;
    int  channelFilter_ = 0;  // 0 = omni, 1-16 = specific channel
};
