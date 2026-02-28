#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

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

    // Convert frequency to note name string (e.g. "A4 / 440.0 Hz")
    static juce::String frequencyToNoteName(float freqHz);

    HarmonizerProcessor& processor_;

    // Dry/Wet knob
    juce::Slider dryWetSlider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryWetAttachment_;

    // Labels
    juce::Label titleLabel_;
    juce::Label dryWetLabel_;
    juce::Label pitchLabel_;
    juce::Label pitchValueLabel_;
    juce::Label voicesLabel_;
    juce::Label voicesValueLabel_;
    juce::Label midiLabel_;

    // MIDI activity state
    bool midiLedOn_ = false;
    int midiLedCountdown_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonizerEditor)
};
