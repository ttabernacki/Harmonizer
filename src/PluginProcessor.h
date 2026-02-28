#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Constants.h"
#include "PitchDetector.h"
#include "MidiNoteTracker.h"
#include "VoicePool.h"
#include "PanningEngine.h"
#include <atomic>
#include <vector>

class HarmonizerProcessor : public juce::AudioProcessor
{
public:
    HarmonizerProcessor();
    ~HarmonizerProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;  // Suppress -Woverloaded-virtual for double variant

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Thread-safe UI readouts (written on audio thread, read on UI thread)
    std::atomic<float> detectedPitchHz{ -1.0f };
    std::atomic<int>   activeVoiceCount{ 0 };
    std::atomic<bool>  midiActivity{ false };

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts_; }

private:
    juce::AudioProcessorValueTreeState apvts_;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    PitchDetector pitchDetector_;
    MidiNoteTracker midiTracker_;
    VoicePool voicePool_;
    PanningEngine panningEngine_;

    // Pre-allocated buffers (sized in prepareToPlay, no allocation in processBlock)
    std::vector<float> monoBuffer_;
    std::vector<float> wetLeftBuffer_;
    std::vector<float> wetRightBuffer_;
    std::array<std::vector<float>, kMaxVoices> voiceRenderStorage_;
    std::array<float*, kMaxVoices> voiceRenderPtrs_{};
    int allocatedBlockSize_ = 0;

    // Smoothed dry/wet parameter to prevent zipper noise
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDryWet_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonizerProcessor)
};
