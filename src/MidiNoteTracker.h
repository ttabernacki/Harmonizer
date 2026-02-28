#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <set>

class MidiNoteTracker
{
public:
    MidiNoteTracker() = default;

    // Process all MIDI messages in the buffer, updating the active note set.
    void processMidiBuffer(const juce::MidiBuffer& midiMessages);

    // Returns the set of currently held MIDI note numbers.
    const std::set<int>& getActiveNotes() const { return activeNotes_; }

    // Returns true if any MIDI messages were received in the last processMidiBuffer call.
    bool hasActivity() const { return hadActivity_; }

    // Reset all state (e.g. on prepareToPlay).
    void reset();

private:
    std::set<int> activeNotes_;
    bool hadActivity_ = false;
};
