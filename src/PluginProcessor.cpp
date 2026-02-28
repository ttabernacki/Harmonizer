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
    allocatedBlockSize_ = samplesPerBlock;
    monoBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);
    wetLeftBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);
    wetRightBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);

    for (int i = 0; i < kMaxVoices; ++i)
    {
        voiceRenderStorage_[static_cast<size_t>(i)].resize(static_cast<size_t>(samplesPerBlock), 0.0f);
        voiceRenderPtrs_[static_cast<size_t>(i)] = voiceRenderStorage_[static_cast<size_t>(i)].data();
    }

    // Dry/wet smoother: ~20ms ramp time to eliminate zipper noise
    smoothedDryWet_.reset(sampleRate, 0.02);
    smoothedDryWet_.setCurrentAndTargetValue(apvts_.getRawParameterValue("dryWet")->load());

    // Gain compensation smoother: ~30ms ramp to avoid volume jumps on voice count changes
    smoothedVoiceGain_.reset(sampleRate, 0.03);
    smoothedVoiceGain_.setCurrentAndTargetValue(1.0f);

    // Report RubberBand's internal processing delay to the host so the DAW
    // can time-align the dry signal with the pitch-shifted wet harmonies.
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

    // Guard: clamp to allocated size to prevent buffer overflows if host
    // sends a block larger than what was specified in prepareToPlay.
    numSamples = std::min(numSamples, allocatedBlockSize_);

    // Step 1: Process MIDI
    midiTracker_.processMidiBuffer(midiMessages);
    midiActivity.store(midiTracker_.hasActivity());

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
    float levelDb = (peak > 0.0f) ? 20.0f * std::log10(peak) : -100.0f;
    inputLevelDb.store(levelDb);

    // Step 3: Detect pitch
    float pitch = pitchDetector_.detectPitch(monoBuffer_.data(), numSamples);
    detectedPitchHz.store(pitch);

    // Step 4: Update voice allocation
    int numActiveNotes = 0;
    const int* activeNotes = midiTracker_.getActiveNotes(numActiveNotes);
    voicePool_.updateNotes(activeNotes, numActiveNotes, pitch);

    int activeCount = voicePool_.getActiveVoiceCount();
    activeVoiceCount.store(activeCount);

    // Step 5: Render each voice individually (for per-voice panning)
    for (int i = 0; i < kMaxVoices; ++i)
        voiceRenderPtrs_[static_cast<size_t>(i)] = voiceRenderStorage_[static_cast<size_t>(i)].data();

    voicePool_.renderVoices(monoBuffer_.data(), voiceRenderPtrs_.data(), numSamples);

    // Step 6: Update panning positions
    const auto& voices = voicePool_.getVoices();
    panningEngine_.updatePanning(voices);

    // Step 7: Smoothed dry/wet parameter (prevents zipper noise on knob movement)
    smoothedDryWet_.setTargetValue(apvts_.getRawParameterValue("dryWet")->load());

    // Step 8: Build stereo output
    float* leftOut  = buffer.getWritePointer(0);
    float* rightOut = (numOutputChannels >= 2) ? buffer.getWritePointer(1) : nullptr;

    // Apply per-voice panning into wet buffers
    std::memset(wetLeftBuffer_.data(), 0, static_cast<size_t>(numSamples) * sizeof(float));
    std::memset(wetRightBuffer_.data(), 0, static_cast<size_t>(numSamples) * sizeof(float));

    panningEngine_.applyPanning(voiceRenderPtrs_.data(), voices,
                                 wetLeftBuffer_.data(), wetRightBuffer_.data(), numSamples);

    // Gain compensation: attenuate wet signal when multiple voices are active to prevent
    // clipping. Scale by 1/sqrt(N) for N active voices (constant-power summation).
    // Smoothed to avoid volume jumps when voice count changes abruptly.
    float targetVoiceGain = 1.0f;
    if (activeCount > 1)
        targetVoiceGain = 1.0f / std::sqrt(static_cast<float>(activeCount));
    smoothedVoiceGain_.setTargetValue(targetVoiceGain);

    // Mix dry + wet per-sample with smoothed parameters
    for (int i = 0; i < numSamples; ++i)
    {
        float mix = smoothedDryWet_.getNextValue();
        float voiceGain = smoothedVoiceGain_.getNextValue();
        float dryGain = 1.0f - mix;
        float wetGain = mix * voiceGain;

        float dry = monoBuffer_[static_cast<size_t>(i)];
        float wetL = wetLeftBuffer_[static_cast<size_t>(i)];
        float wetR = wetRightBuffer_[static_cast<size_t>(i)];

        leftOut[i] = dry * dryGain + wetL * wetGain;
        if (rightOut != nullptr)
            rightOut[i] = dry * dryGain + wetR * wetGain;
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
