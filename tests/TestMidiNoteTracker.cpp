#include "TestFramework.h"
#include "MidiNoteTracker.h"

// Helper: create a MidiBuffer with a single note-on message
static juce::MidiBuffer makeNoteOn(int note, int channel = 1)
{
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(channel, note, (juce::uint8)100), 0);
    return buf;
}

// Helper: create a MidiBuffer with a single note-off message
static juce::MidiBuffer makeNoteOff(int note, int channel = 1)
{
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOff(channel, note, (juce::uint8)0), 0);
    return buf;
}

// ============================================================================
// Basic add / remove
// ============================================================================

TEST(MidiTracker_InitiallyEmpty)
{
    MidiNoteTracker tracker;
    EXPECT_EQ(tracker.getActiveNoteCount(), 0);
    EXPECT_FALSE(tracker.hasActivity());
}

TEST(MidiTracker_AddSingleNote)
{
    MidiNoteTracker tracker;
    auto buf = makeNoteOn(60);
    tracker.processMidiBuffer(buf);

    EXPECT_EQ(tracker.getActiveNoteCount(), 1);

    int numNotes = 0;
    const int* notes = tracker.getActiveNotes(numNotes);
    EXPECT_EQ(numNotes, 1);
    EXPECT_EQ(notes[0], 60);
}

TEST(MidiTracker_AddMultipleNotes_PressOrderPreserved)
{
    MidiNoteTracker tracker;

    // Add notes in order: 64, 60, 67
    auto buf1 = makeNoteOn(64);
    tracker.processMidiBuffer(buf1);
    auto buf2 = makeNoteOn(60);
    tracker.processMidiBuffer(buf2);
    auto buf3 = makeNoteOn(67);
    tracker.processMidiBuffer(buf3);

    int numNotes = 0;
    const int* notes = tracker.getActiveNotes(numNotes);
    EXPECT_EQ(numNotes, 3);
    EXPECT_EQ(notes[0], 64);  // first pressed
    EXPECT_EQ(notes[1], 60);  // second pressed
    EXPECT_EQ(notes[2], 67);  // third pressed
}

TEST(MidiTracker_RemoveNote_ShiftsRemaining)
{
    MidiNoteTracker tracker;

    auto buf1 = makeNoteOn(60);
    tracker.processMidiBuffer(buf1);
    auto buf2 = makeNoteOn(64);
    tracker.processMidiBuffer(buf2);
    auto buf3 = makeNoteOn(67);
    tracker.processMidiBuffer(buf3);

    // Remove the middle note
    auto off = makeNoteOff(64);
    tracker.processMidiBuffer(off);

    int numNotes = 0;
    const int* notes = tracker.getActiveNotes(numNotes);
    EXPECT_EQ(numNotes, 2);
    EXPECT_EQ(notes[0], 60);
    EXPECT_EQ(notes[1], 67);
}

TEST(MidiTracker_DuplicateNoteOnIgnored)
{
    MidiNoteTracker tracker;

    auto buf1 = makeNoteOn(60);
    tracker.processMidiBuffer(buf1);
    auto buf2 = makeNoteOn(60);  // duplicate
    tracker.processMidiBuffer(buf2);

    EXPECT_EQ(tracker.getActiveNoteCount(), 1);
}

TEST(MidiTracker_NoteOffForUnheldNote_Harmless)
{
    MidiNoteTracker tracker;

    auto buf = makeNoteOff(60);
    tracker.processMidiBuffer(buf);

    EXPECT_EQ(tracker.getActiveNoteCount(), 0);
}

TEST(MidiTracker_RemoveAllNotes)
{
    MidiNoteTracker tracker;

    auto buf1 = makeNoteOn(60);
    tracker.processMidiBuffer(buf1);
    auto buf2 = makeNoteOn(64);
    tracker.processMidiBuffer(buf2);

    auto off1 = makeNoteOff(60);
    tracker.processMidiBuffer(off1);
    auto off2 = makeNoteOff(64);
    tracker.processMidiBuffer(off2);

    EXPECT_EQ(tracker.getActiveNoteCount(), 0);
}

// ============================================================================
// Channel filtering
// ============================================================================

TEST(MidiTracker_OmniMode_AcceptsAllChannels)
{
    MidiNoteTracker tracker;
    tracker.setChannelFilter(0);  // omni

    auto buf1 = makeNoteOn(60, 1);
    tracker.processMidiBuffer(buf1);
    auto buf2 = makeNoteOn(64, 5);
    tracker.processMidiBuffer(buf2);
    auto buf3 = makeNoteOn(67, 16);
    tracker.processMidiBuffer(buf3);

    EXPECT_EQ(tracker.getActiveNoteCount(), 3);
}

TEST(MidiTracker_ChannelFilter_BlocksWrongChannel)
{
    MidiNoteTracker tracker;
    tracker.setChannelFilter(1);

    // Channel 1 should work
    auto buf1 = makeNoteOn(60, 1);
    tracker.processMidiBuffer(buf1);
    EXPECT_EQ(tracker.getActiveNoteCount(), 1);

    // Channel 2 should be ignored
    auto buf2 = makeNoteOn(64, 2);
    tracker.processMidiBuffer(buf2);
    EXPECT_EQ(tracker.getActiveNoteCount(), 1);
}

TEST(MidiTracker_ChannelChange_ClearsNotes)
{
    MidiNoteTracker tracker;
    tracker.setChannelFilter(1);

    auto buf = makeNoteOn(60, 1);
    tracker.processMidiBuffer(buf);
    EXPECT_EQ(tracker.getActiveNoteCount(), 1);

    // Switching channels should clear everything
    tracker.setChannelFilter(2);
    EXPECT_EQ(tracker.getActiveNoteCount(), 0);
}

TEST(MidiTracker_SameChannelFilter_DoesNotClear)
{
    MidiNoteTracker tracker;
    tracker.setChannelFilter(1);

    auto buf = makeNoteOn(60, 1);
    tracker.processMidiBuffer(buf);

    // Setting the same channel again should NOT clear notes
    tracker.setChannelFilter(1);
    EXPECT_EQ(tracker.getActiveNoteCount(), 1);
}

// ============================================================================
// Activity flag
// ============================================================================

TEST(MidiTracker_ActivityFlag_SetOnMidi)
{
    MidiNoteTracker tracker;
    EXPECT_FALSE(tracker.hasActivity());

    auto buf = makeNoteOn(60);
    tracker.processMidiBuffer(buf);
    EXPECT_TRUE(tracker.hasActivity());
}

TEST(MidiTracker_ActivityFlag_ClearedOnEmptyBuffer)
{
    MidiNoteTracker tracker;

    auto buf = makeNoteOn(60);
    tracker.processMidiBuffer(buf);
    EXPECT_TRUE(tracker.hasActivity());

    // Process an empty buffer
    juce::MidiBuffer empty;
    tracker.processMidiBuffer(empty);
    EXPECT_FALSE(tracker.hasActivity());
}

// ============================================================================
// Reset
// ============================================================================

TEST(MidiTracker_Reset_ClearsEverything)
{
    MidiNoteTracker tracker;

    auto buf = makeNoteOn(60);
    tracker.processMidiBuffer(buf);
    EXPECT_EQ(tracker.getActiveNoteCount(), 1);

    tracker.reset();
    EXPECT_EQ(tracker.getActiveNoteCount(), 0);
    EXPECT_FALSE(tracker.hasActivity());
}

// ============================================================================
// Multiple messages in one buffer
// ============================================================================

TEST(MidiTracker_MultipleMessagesInOneBuffer)
{
    MidiNoteTracker tracker;

    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    buf.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 10);
    buf.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8)100), 20);
    tracker.processMidiBuffer(buf);

    EXPECT_EQ(tracker.getActiveNoteCount(), 3);
}

TEST(MidiTracker_NoteOnThenOffSameBuffer)
{
    MidiNoteTracker tracker;

    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    buf.addEvent(juce::MidiMessage::noteOff(1, 60, (juce::uint8)0), 50);
    tracker.processMidiBuffer(buf);

    // Note should have been added then removed
    EXPECT_EQ(tracker.getActiveNoteCount(), 0);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(MidiTracker_ZeroVelocityNoteOn_TreatedAsNoteOff)
{
    MidiNoteTracker tracker;

    auto on = makeNoteOn(60);
    tracker.processMidiBuffer(on);
    EXPECT_EQ(tracker.getActiveNoteCount(), 1);

    // Velocity-0 note-on is a note-off per MIDI spec
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)0), 0);
    tracker.processMidiBuffer(buf);

    EXPECT_EQ(tracker.getActiveNoteCount(), 0);
}

TEST(MidiTracker_BoundaryNotes_0And127)
{
    MidiNoteTracker tracker;

    auto buf1 = makeNoteOn(0);
    tracker.processMidiBuffer(buf1);
    auto buf2 = makeNoteOn(127);
    tracker.processMidiBuffer(buf2);

    EXPECT_EQ(tracker.getActiveNoteCount(), 2);

    int numNotes = 0;
    const int* notes = tracker.getActiveNotes(numNotes);
    EXPECT_EQ(notes[0], 0);
    EXPECT_EQ(notes[1], 127);
}
