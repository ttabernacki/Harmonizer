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

void HarmonizerProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    pitchDetector_.prepare(sampleRate, samplesPerBlock);
    midiTracker_.reset();
    voicePool_.prepare(sampleRate, samplesPerBlock);
    panningEngine_.prepare(sampleRate, samplesPerBlock);

    monoBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);
    wetBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);

    // Per-voice render buffers for panning
    voiceRenderStorage_.resize(MAX_VOICES);
    voiceRenderBuffers_.resize(MAX_VOICES);
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        voiceRenderStorage_[static_cast<size_t>(i)].resize(static_cast<size_t>(samplesPerBlock), 0.0f);
        voiceRenderBuffers_[static_cast<size_t>(i)] = voiceRenderStorage_[static_cast<size_t>(i)].data();
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
    int numInputChannels = getTotalNumInputChannels();
    int numOutputChannels = getTotalNumOutputChannels();

    // Clear any extra output channels
    for (int ch = numInputChannels; ch < numOutputChannels; ++ch)
        buffer.clear(ch, 0, numSamples);

    if (numSamples == 0)
        return;

    // Step 1: Process MIDI
    midiTracker_.processMidiBuffer(midiMessages);
    midiActivity.store(midiTracker_.hasActivity());

    // Step 2: Create mono input from first channel
    const float* inputData = buffer.getReadPointer(0);
    std::memcpy(monoBuffer_.data(), inputData, static_cast<size_t>(numSamples) * sizeof(float));

    // Step 3: Detect pitch
    float pitch = pitchDetector_.detectPitch(monoBuffer_.data(), numSamples);
    detectedPitchHz.store(pitch);

    // Step 4: Update voice allocation
    voicePool_.updateNotes(midiTracker_.getActiveNotes(), pitch);
    activeVoiceCount.store(voicePool_.getActiveVoiceCount());

    // Step 5: Render each voice individually (for per-voice panning)
    const auto& voices = voicePool_.getVoices();
    for (int v = 0; v < MAX_VOICES; ++v)
    {
        auto& storage = voiceRenderStorage_[static_cast<size_t>(v)];
        if (voices[static_cast<size_t>(v)].isActive())
        {
            // We need a mutable ref — use const_cast since we own the pool
            const_cast<HarmonyVoice&>(voices[static_cast<size_t>(v)])
                .process(monoBuffer_.data(), storage.data(), numSamples);
        }
        else
        {
            std::memset(storage.data(), 0, static_cast<size_t>(numSamples) * sizeof(float));
        }
        voiceRenderBuffers_[static_cast<size_t>(v)] = storage.data();
    }

    // Step 6: Update panning positions
    panningEngine_.updatePanning(voices);

    // Step 7: Get dry/wet mix parameter
    float dryWetMix = apvts_.getRawParameterValue("dryWet")->load();

    // Step 8: Prepare output — ensure stereo
    // Make sure we have at least 2 output channels
    if (numOutputChannels < 2)
    {
        // Mono output fallback: mix dry + wet without panning
        voicePool_.processBlock(monoBuffer_.data(), wetBuffer_.data(), numSamples);
        float* out = buffer.getWritePointer(0);
        for (int i = 0; i < numSamples; ++i)
            out[i] = monoBuffer_[static_cast<size_t>(i)] * (1.0f - dryWetMix) + wetBuffer_[static_cast<size_t>(i)] * dryWetMix;
        return;
    }

    float* leftOut  = buffer.getWritePointer(0);
    float* rightOut = buffer.getWritePointer(1);

    // Start with dry signal (centered) scaled by (1 - mix)
    float dryGain = 1.0f - dryWetMix;
    for (int i = 0; i < numSamples; ++i)
    {
        leftOut[i]  = monoBuffer_[static_cast<size_t>(i)] * dryGain;
        rightOut[i] = monoBuffer_[static_cast<size_t>(i)] * dryGain;
    }

    // Apply per-voice panning for wet signal, scaled by mix
    // First apply panning into temp buffers, then scale by wet gain
    std::vector<float> wetLeft(static_cast<size_t>(numSamples), 0.0f);
    std::vector<float> wetRight(static_cast<size_t>(numSamples), 0.0f);

    panningEngine_.applyPanning(voiceRenderBuffers_.data(), voices,
                                 wetLeft.data(), wetRight.data(), numSamples);

    float wetGain = dryWetMix;
    for (int i = 0; i < numSamples; ++i)
    {
        leftOut[i]  += wetLeft[static_cast<size_t>(i)]  * wetGain;
        rightOut[i] += wetRight[static_cast<size_t>(i)] * wetGain;
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
