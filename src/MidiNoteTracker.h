#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

class MidiNoteTracker
{
public:
    MidiNoteTracker() = default;

    // Set MIDI channel filter: 0 = omni (all channels), 1-16 = specific channel.
    void setChannelFilter(int channel) { channelFilter_ = channel; }

    // Process all MIDI messages in the buffer, updating the active note set.
    void processMidiBuffer(const juce::MidiBuffer& midiMessages);

    // Returns a pointer to the active note array and the count via numNotes.
    // Notes are in the order they were pressed (oldest first).
    const int* getActiveNotes(int& numNotes) const
    {
        numNotes = numActive_;
        return activeNotes_.data();
    }

    int getActiveNoteCount() const { return numActive_; }

    // Returns true if any MIDI messages were received in the last processMidiBuffer call.
    bool hasActivity() const { return hadActivity_; }

    // Reset all state (e.g. on prepareToPlay).
    void reset();

private:
    void addNote(int note);
    void removeNote(int note);

    // Fixed-size storage — no heap allocation. 128 is the max number of MIDI notes.
    std::array<int, 128> activeNotes_{};
    int numActive_ = 0;
    bool hadActivity_ = false;
    int channelFilter_ = 0; // 0 = omni
};
