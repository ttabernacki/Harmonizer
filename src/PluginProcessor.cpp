#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>

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

    return { params.begin(), params.end() };
}

bool HarmonizerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Must have mono input
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono())
        return false;

    // Output must be stereo
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

    // Pre-allocate all buffers used in processBlock
    monoBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);
    wetLeftBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);
    wetRightBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);

    for (int i = 0; i < kMaxVoices; ++i)
    {
        voiceRenderStorage_[static_cast<size_t>(i)].resize(static_cast<size_t>(samplesPerBlock), 0.0f);
        voiceRenderPtrs_[static_cast<size_t>(i)] = voiceRenderStorage_[static_cast<size_t>(i)].data();
    }
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

    // Step 1: Process MIDI
    midiTracker_.processMidiBuffer(midiMessages);
    midiActivity.store(midiTracker_.hasActivity());

    // Step 2: Copy mono input
    const float* inputData = buffer.getReadPointer(0);
    std::memcpy(monoBuffer_.data(), inputData, static_cast<size_t>(numSamples) * sizeof(float));

    // Step 3: Detect pitch
    float pitch = pitchDetector_.detectPitch(monoBuffer_.data(), numSamples);
    detectedPitchHz.store(pitch);

    // Step 4: Update voice allocation
    int numActiveNotes = 0;
    const int* activeNotes = midiTracker_.getActiveNotes(numActiveNotes);
    voicePool_.updateNotes(activeNotes, numActiveNotes, pitch);
    activeVoiceCount.store(voicePool_.getActiveVoiceCount());

    // Step 5: Render each voice individually (for per-voice panning)
    // Update pointers in case resize moved the data (only happens on first call after prepare)
    for (int i = 0; i < kMaxVoices; ++i)
        voiceRenderPtrs_[static_cast<size_t>(i)] = voiceRenderStorage_[static_cast<size_t>(i)].data();

    voicePool_.renderVoices(monoBuffer_.data(), voiceRenderPtrs_.data(), numSamples);

    // Step 6: Update panning positions
    const auto& voices = voicePool_.getVoices();
    panningEngine_.updatePanning(voices);

    // Step 7: Get dry/wet mix parameter
    float dryWetMix = apvts_.getRawParameterValue("dryWet")->load();

    // Step 8: Build stereo output
    float* leftOut  = buffer.getWritePointer(0);
    float* rightOut = (numOutputChannels >= 2) ? buffer.getWritePointer(1) : nullptr;

    // Start with dry signal (centered) scaled by (1 - mix)
    float dryGain = 1.0f - dryWetMix;
    for (int i = 0; i < numSamples; ++i)
    {
        leftOut[i] = monoBuffer_[static_cast<size_t>(i)] * dryGain;
    }
    if (rightOut != nullptr)
        std::memcpy(rightOut, leftOut, static_cast<size_t>(numSamples) * sizeof(float));

    // Apply per-voice panning for wet signal using pre-allocated buffers
    std::memset(wetLeftBuffer_.data(), 0, static_cast<size_t>(numSamples) * sizeof(float));
    std::memset(wetRightBuffer_.data(), 0, static_cast<size_t>(numSamples) * sizeof(float));

    panningEngine_.applyPanning(voiceRenderPtrs_.data(), voices,
                                 wetLeftBuffer_.data(), wetRightBuffer_.data(), numSamples);

    float wetGain = dryWetMix;
    for (int i = 0; i < numSamples; ++i)
    {
        leftOut[i] += wetLeftBuffer_[static_cast<size_t>(i)] * wetGain;
    }
    if (rightOut != nullptr)
    {
        for (int i = 0; i < numSamples; ++i)
            rightOut[i] += wetRightBuffer_[static_cast<size_t>(i)] * wetGain;
    }

    // Clear MIDI buffer so downstream plugins don't re-process
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

// JUCE plugin instantiation
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonizerProcessor();
}
