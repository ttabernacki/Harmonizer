#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Constants.h"
#include "PitchDetector.h"
#include "MidiNoteTracker.h"
#include "VoicePool.h"
#include "PanningEngine.h"
#include <atomic>
#include <vector>

// ============================================================================
// HarmonizerProcessor — Main audio processor for the Harmonizer VST/Standalone.
//
// Signal flow (per processBlock call):
//
//   Audio in ──► mono downmix ──► pitch detection ──► pitch correction
//                                                          │
//   MIDI in ──► note tracker ──► voice pool ◄──────────────┘
//                                     │
//                              ┌──────┴──────┐
//                              │  N voices   │  (RubberBand pitch shift)
//                              └──────┬──────┘
//                                     │
//                              stereo panning ──► dry/wet mix ──► output
//
// The processor also exposes auxiliary output buses:
//   Bus 1:       Stereo harmony sum (wet signal with panning)
//   Bus 2..13:   Mono per-voice outputs (pre-panning, for DAW routing)
// ============================================================================
class HarmonizerProcessor : public juce::AudioProcessor
{
public:
    HarmonizerProcessor();
    ~HarmonizerProcessor() override;

    // --- juce::AudioProcessor overrides ---
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
    // Maximum release time is 2000 ms; report this so hosts don't truncate
    // the release tail when stopping playback or bouncing offline.
    double getTailLengthSeconds() const override { return 2.0; }

    // VST3 bypass support (required by the VST3 spec; hosts like Ableton expect this)
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam_; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- Thread-safe UI readouts ---
    // Written on the audio thread, read on the UI timer thread.
    // Atomics are sufficient here (no need for locks) because each value is
    // independent and the UI only needs an approximate snapshot.
    std::atomic<float> detectedPitchHz{ -1.0f };
    std::atomic<int>   activeVoiceCount{ 0 };
    std::atomic<bool>  midiActivity{ false };
    std::atomic<float> inputLevelDb{ kSilenceDb };

    // Bitmask of currently held MIDI notes, split into two 64-bit halves so
    // that each note maps to a single bit:
    //   notes  0–63  → heldNotesBitmaskLow,  bit = (1 << note)
    //   notes 64–127 → heldNotesBitmaskHigh, bit = (1 << (note - 64))
    // The editor reads these atomically to highlight keys on the mini keyboard.
    std::atomic<uint64_t> heldNotesBitmaskLow{ 0 };
    std::atomic<uint64_t> heldNotesBitmaskHigh{ 0 };

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts_; }

private:
    // --- Parameter state ---
    juce::AudioProcessorValueTreeState apvts_;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static BusesProperties makeDefaultBuses();

    // --- DSP components (order matches signal flow) ---
    PitchDetector    pitchDetector_;
    MidiNoteTracker  midiTracker_;
    VoicePool        voicePool_;
    PanningEngine    panningEngine_;

    // --- Pre-allocated work buffers ---
    // Sized once in prepareToPlay; never reallocated on the audio thread.
    std::vector<float> monoBuffer_;       // Mono downmix of input
    std::vector<float> wetLeftBuffer_;    // Panned wet mix (left channel)
    std::vector<float> wetRightBuffer_;   // Panned wet mix (right channel)
    std::array<std::vector<float>, kMaxVoices> voiceRenderStorage_;  // Per-voice output
    std::array<float*, kMaxVoices> voiceRenderPtrs_{};               // Raw pointers into above
    int allocatedBlockSize_ = 0;

    // --- Smoothed parameters (prevent zipper noise on automation changes) ---
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDryWet_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedVoiceGain_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutputGain_;

    juce::AudioParameterBool* bypassParam_ = nullptr;  // Owned by APVTS

    // Cached raw parameter pointers — fetched once in the constructor.
    // getRawParameterValue() returns a stable std::atomic<float>* for the
    // lifetime of the APVTS, so caching avoids a string hash-map lookup on
    // every processBlock call.
    std::atomic<float>* paramDryWet_        = nullptr;
    std::atomic<float>* paramStereoWidth_   = nullptr;
    std::atomic<float>* paramOutputGain_    = nullptr;
    std::atomic<float>* paramDetune_        = nullptr;
    std::atomic<float>* paramPitchCorrect_  = nullptr;
    std::atomic<float>* paramFormantShift_  = nullptr;
    std::atomic<float>* paramMidiChannel_   = nullptr;
    std::atomic<float>* paramAttack_        = nullptr;
    std::atomic<float>* paramRelease_       = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonizerProcessor)
};
