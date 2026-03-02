#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// ============================================================================
// MiniKeyboard — A lightweight on-screen keyboard (4 octaves, C2-B5).
//
// Highlights held notes in the accent colour.  No interaction — purely visual.
// Reads a pair of 64-bit bitmasks from the processor (see PluginProcessor.h).
// ============================================================================
class MiniKeyboard : public juce::Component
{
public:
    // Update which notes are highlighted.  Only repaints if the bitmask changed.
    void setHeldNotes(uint64_t low, uint64_t high)
    {
        if (low != heldLow_ || high != heldHigh_)
        {
            heldLow_ = low;
            heldHigh_ = high;
            repaint();
        }
    }
    void paint(juce::Graphics& g) override;

private:
    uint64_t heldLow_  = 0;  // Bits for MIDI notes 0-63
    uint64_t heldHigh_ = 0;  // Bits for MIDI notes 64-127

    // Test a single bit in the appropriate bitmask half
    bool isNoteHeld(int note) const
    {
        if (note < 64)
            return (heldLow_ >> note) & 1;
        return (heldHigh_ >> (note - 64)) & 1;
    }

    // Returns true for MIDI notes that are sharps/flats (the black keys)
    static bool isBlackKey(int note)
    {
        int n = note % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }
};

// ============================================================================
// HarmonizerEditor — Plugin GUI.
//
// Layout (top to bottom):
//   1. Title
//   2. Knob row 1: Dry/Wet, Stereo Width, Output Gain
//   3. Knob row 2: Detune, Pitch Corr, Formant, Attack, Release, MIDI Ch
//   4. Info panel:  Detected pitch | Active voices | MIDI LED
//   5. Mini keyboard
//
// A vertical level meter is drawn along the left edge.
//
// The UI refreshes at 30 Hz via a juce::Timer.  Only components whose
// display values actually changed are repainted.
// ============================================================================
class HarmonizerEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit HarmonizerEditor(HarmonizerProcessor&);
    ~HarmonizerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Convert Hz → "C4 / 261.6 Hz" style string
    static juce::String frequencyToNoteName(float freqHz);

    // Configure a rotary knob + label with consistent styling and APVTS binding
    void setupKnob(juce::Slider& slider, juce::Label& label, const juce::String& text,
                   const juce::String& paramId,
                   std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment);

    HarmonizerProcessor& processor_;

    // --- Knobs ---
    juce::Slider dryWetSlider_, stereoWidthSlider_, outputGainSlider_;
    juce::Slider detuneSlider_, pitchCorrectSlider_, formantShiftSlider_;
    juce::Slider attackSlider_, releaseSlider_;
    juce::Slider midiChannelSlider_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        dryWetAttachment_, stereoWidthAttachment_, outputGainAttachment_,
        detuneAttachment_, pitchCorrectAttachment_, formantShiftAttachment_,
        attackAttachment_, releaseAttachment_,
        midiChannelAttachment_;

    // --- Knob labels ---
    juce::Label dryWetLabel_, stereoWidthLabel_, outputGainLabel_;
    juce::Label detuneLabel_, pitchCorrectLabel_, formantShiftLabel_;
    juce::Label attackLabel_, releaseLabel_;
    juce::Label midiChannelLabel_;

    // --- Info / status display ---
    juce::Label titleLabel_;
    juce::Label pitchLabel_, pitchValueLabel_;    // Detected input pitch
    juce::Label voicesLabel_, voicesValueLabel_;   // Active voice count
    juce::Label midiLabel_;                        // "MIDI" text above LED

    MiniKeyboard miniKeyboard_;

    // MIDI LED state: stays lit for kMidiLedFrames after the last event
    bool midiLedOn_       = false;
    int  midiLedCountdown_ = 0;

    // Level meter: smoothed dB value for the vertical bar
    float displayedLevelDb_ = kSilenceDb;

    // Cached display values — only repaint when something actually changes
    juce::String lastPitchText_;
    juce::String lastVoicesText_;
    bool  lastMidiLedState_ = false;
    float lastLevelDb_      = kSilenceDb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonizerEditor)
};
