#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>
#include <cmath>
#include <algorithm>

HarmonizerProcessor::HarmonizerProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::mono(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "Parameters", createParameterLayout())
{
}

HarmonizerProcessor::~HarmonizerProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout HarmonizerProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("dryWet", 1),
        "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("stereoWidth", 1),
        "Stereo Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("outputGain", 1),
        "Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("detune", 1),
        "Detune",
        juce::NormalisableRange<float>(0.0f, 50.0f, 0.1f),
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("pitchCorrect", 1),
        "Pitch Correction",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("formantShift", 1),
        "Formant Shift",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID("midiChannel", 1),
        "MIDI Channel",
        0, 16, 0));

    return { params.begin(), params.end() };
}

bool HarmonizerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono())
        return false;
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

void HarmonizerProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    pitchDetector_.prepare(sampleRate, samplesPerBlock);
    midiTracker_.reset();
    voicePool_.prepare(sampleRate, samplesPerBlock);
    panningEngine_.prepare(sampleRate, samplesPerBlock);

    allocatedBlockSize_ = samplesPerBlock;
    monoBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);
    wetLeftBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);
    wetRightBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);

    for (int i = 0; i < kMaxVoices; ++i)
    {
        voiceRenderStorage_[static_cast<size_t>(i)].resize(static_cast<size_t>(samplesPerBlock), 0.0f);
        voiceRenderPtrs_[static_cast<size_t>(i)] = voiceRenderStorage_[static_cast<size_t>(i)].data();
    }

    smoothedDryWet_.reset(sampleRate, 0.02);
    smoothedDryWet_.setCurrentAndTargetValue(apvts_.getRawParameterValue("dryWet")->load());

    smoothedVoiceGain_.reset(sampleRate, 0.03);
    smoothedVoiceGain_.setCurrentAndTargetValue(1.0f);

    smoothedOutputGain_.reset(sampleRate, 0.02);
    float initGainDb = apvts_.getRawParameterValue("outputGain")->load();
    smoothedOutputGain_.setCurrentAndTargetValue(std::pow(10.0f, initGainDb / 20.0f));

    setLatencySamples(voicePool_.getStartDelay());
}

void HarmonizerProcessor::releaseResources()
{
    midiTracker_.reset();
}

void HarmonizerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    int numSamples = buffer.getNumSamples();
    int numOutputChannels = getTotalNumOutputChannels();

    if (numSamples == 0)
        return;

    numSamples = std::min(numSamples, allocatedBlockSize_);

    // Read parameters
    float detuneCents = apvts_.getRawParameterValue("detune")->load();
    float pitchCorrectionStrength = apvts_.getRawParameterValue("pitchCorrect")->load();
    float formantShiftSemitones = apvts_.getRawParameterValue("formantShift")->load();
    float stereoWidth = apvts_.getRawParameterValue("stereoWidth")->load();
    int midiChannel = static_cast<int>(apvts_.getRawParameterValue("midiChannel")->load());
    float outputGainDb = apvts_.getRawParameterValue("outputGain")->load();

    // Step 1: Process MIDI (with channel filter)
    midiTracker_.setChannelFilter(midiChannel);
    midiTracker_.processMidiBuffer(midiMessages);
    midiActivity.store(midiTracker_.hasActivity());

    // Update held-notes bitmask for visual keyboard
    {
        int numNotes = 0;
        const int* notes = midiTracker_.getActiveNotes(numNotes);
        uint64_t low = 0, high = 0;
        for (int i = 0; i < numNotes; ++i)
        {
            int n = notes[i];
            if (n >= 0 && n < 64)
                low |= (uint64_t(1) << n);
            else if (n >= 64 && n < 128)
                high |= (uint64_t(1) << (n - 64));
        }
        heldNotesBitmaskLow.store(low);
        heldNotesBitmaskHigh.store(high);
    }

    // Step 2: Copy mono input
    const float* inputData = buffer.getReadPointer(0);
    std::memcpy(monoBuffer_.data(), inputData, static_cast<size_t>(numSamples) * sizeof(float));

    // Compute input level (peak) for UI meter
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float absVal = std::abs(monoBuffer_[static_cast<size_t>(i)]);
        if (absVal > peak)
            peak = absVal;
    }
    inputLevelDb.store((peak > 0.0f) ? 20.0f * std::log10(peak) : -100.0f);

    // Step 3: Detect pitch
    float pitch = pitchDetector_.detectPitch(monoBuffer_.data(), numSamples);

    // Step 3b: Apply pitch correction (snap toward nearest semitone)
    if (pitch > 0.0f && pitchCorrectionStrength > 0.0f)
    {
        float midiNote = 69.0f + 12.0f * std::log2(pitch / 440.0f);
        int snappedNote = static_cast<int>(std::round(midiNote));
        if (snappedNote >= 0 && snappedNote < 128)
        {
            float correctedPitch = HarmonyVoice::midiNoteToFrequency(snappedNote);
            pitch = pitch + pitchCorrectionStrength * (correctedPitch - pitch);
        }
    }

    detectedPitchHz.store(pitch);

    // Step 4: Update voice allocation with detune and formant params
    voicePool_.setDetuneCents(detuneCents);
    voicePool_.setFormantShiftSemitones(formantShiftSemitones);

    int numActiveNotes = 0;
    const int* activeNotes = midiTracker_.getActiveNotes(numActiveNotes);
    voicePool_.updateNotes(activeNotes, numActiveNotes, pitch);

    int activeCount = voicePool_.getActiveVoiceCount();
    activeVoiceCount.store(activeCount);

    // Step 5: Render each voice individually
    for (int i = 0; i < kMaxVoices; ++i)
        voiceRenderPtrs_[static_cast<size_t>(i)] = voiceRenderStorage_[static_cast<size_t>(i)].data();

    voicePool_.renderVoices(monoBuffer_.data(), voiceRenderPtrs_.data(), numSamples);

    // Step 6: Update panning with stereo width
    const auto& voices = voicePool_.getVoices();
    panningEngine_.setWidth(stereoWidth);
    panningEngine_.updatePanning(voices);

    // Step 7: Update smoothed parameters
    smoothedDryWet_.setTargetValue(apvts_.getRawParameterValue("dryWet")->load());
    smoothedOutputGain_.setTargetValue(std::pow(10.0f, outputGainDb / 20.0f));

    // Step 8: Build stereo output
    float* leftOut  = buffer.getWritePointer(0);
    float* rightOut = (numOutputChannels >= 2) ? buffer.getWritePointer(1) : nullptr;

    std::memset(wetLeftBuffer_.data(), 0, static_cast<size_t>(numSamples) * sizeof(float));
    std::memset(wetRightBuffer_.data(), 0, static_cast<size_t>(numSamples) * sizeof(float));

    panningEngine_.applyPanning(voiceRenderPtrs_.data(), voices,
                                 wetLeftBuffer_.data(), wetRightBuffer_.data(), numSamples);

    // Smoothed gain compensation
    float targetVoiceGain = 1.0f;
    if (activeCount > 1)
        targetVoiceGain = 1.0f / std::sqrt(static_cast<float>(activeCount));
    smoothedVoiceGain_.setTargetValue(targetVoiceGain);

    // Mix dry + wet per-sample with smoothed parameters + output gain
    for (int i = 0; i < numSamples; ++i)
    {
        float mix = smoothedDryWet_.getNextValue();
        float voiceGain = smoothedVoiceGain_.getNextValue();
        float outGain = smoothedOutputGain_.getNextValue();
        float dryGain = 1.0f - mix;
        float wetGain = mix * voiceGain;

        float dry = monoBuffer_[static_cast<size_t>(i)];
        float wetL = wetLeftBuffer_[static_cast<size_t>(i)];
        float wetR = wetRightBuffer_[static_cast<size_t>(i)];

        leftOut[i] = (dry * dryGain + wetL * wetGain) * outGain;
        if (rightOut != nullptr)
            rightOut[i] = (dry * dryGain + wetR * wetGain) * outGain;
    }

    midiMessages.clear();
}

juce::AudioProcessorEditor* HarmonizerProcessor::createEditor()
{
    return new HarmonizerEditor(*this);
}

void HarmonizerProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void HarmonizerProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts_.state.getType()))
        apvts_.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonizerProcessor();
}
