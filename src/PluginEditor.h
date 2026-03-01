#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Minimal visual MIDI keyboard: draws 2 octaves (C3-B4) with held notes highlighted.
class MiniKeyboard : public juce::Component
{
public:
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
    uint64_t heldLow_ = 0;
    uint64_t heldHigh_ = 0;

    bool isNoteHeld(int note) const
    {
        if (note < 64)
            return (heldLow_ >> note) & 1;
        return (heldHigh_ >> (note - 64)) & 1;
    }

    static bool isBlackKey(int note)
    {
        int n = note % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }
};

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

    static juce::String frequencyToNoteName(float freqHz);

    // Helper to configure a rotary knob with consistent style
    void setupKnob(juce::Slider& slider, juce::Label& label, const juce::String& text,
                   const juce::String& paramId, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment);

    HarmonizerProcessor& processor_;

    // Knobs
    juce::Slider dryWetSlider_, stereoWidthSlider_, outputGainSlider_;
    juce::Slider detuneSlider_, pitchCorrectSlider_, formantShiftSlider_;
    juce::Slider midiChannelSlider_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        dryWetAttachment_, stereoWidthAttachment_, outputGainAttachment_,
        detuneAttachment_, pitchCorrectAttachment_, formantShiftAttachment_,
        midiChannelAttachment_;

    // Labels for knobs
    juce::Label dryWetLabel_, stereoWidthLabel_, outputGainLabel_;
    juce::Label detuneLabel_, pitchCorrectLabel_, formantShiftLabel_;
    juce::Label midiChannelLabel_;

    // Info / status labels
    juce::Label titleLabel_;
    juce::Label pitchLabel_, pitchValueLabel_;
    juce::Label voicesLabel_, voicesValueLabel_;
    juce::Label midiLabel_;

    MiniKeyboard miniKeyboard_;

    // MIDI activity state
    bool midiLedOn_ = false;
    int midiLedCountdown_ = 0;

    // Input level meter state
    float displayedLevelDb_ = -100.0f;

    // Cached display values for conditional repaint
    juce::String lastPitchText_;
    juce::String lastVoicesText_;
    bool lastMidiLedState_ = false;
    float lastLevelDb_ = -100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonizerEditor)
};
