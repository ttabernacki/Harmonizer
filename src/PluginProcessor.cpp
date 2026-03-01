#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>
#include <cmath>
#include <algorithm>

HarmonizerProcessor::BusesProperties HarmonizerProcessor::makeDefaultBuses()
{
    auto props = BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Main", juce::AudioChannelSet::stereo(), true)
        .withOutput("Harmony Sum", juce::AudioChannelSet::stereo(), false);

    for (int i = 0; i < kMaxVoices; ++i)
        props = props.withOutput("Voice " + juce::String(i + 1),
                                  juce::AudioChannelSet::mono(), false);

    return props;
}

HarmonizerProcessor::HarmonizerProcessor()
    : AudioProcessor(makeDefaultBuses()),
      apvts_(*this, nullptr, "Parameters", createParameterLayout())
{
    bypassParam_ = dynamic_cast<juce::AudioParameterBool*>(apvts_.getParameter("bypass"));

    // Cache raw parameter pointers once (stable for lifetime of APVTS)
    paramDryWet_       = apvts_.getRawParameterValue("dryWet");
    paramStereoWidth_  = apvts_.getRawParameterValue("stereoWidth");
    paramOutputGain_   = apvts_.getRawParameterValue("outputGain");
    paramDetune_       = apvts_.getRawParameterValue("detune");
    paramPitchCorrect_ = apvts_.getRawParameterValue("pitchCorrect");
    paramFormantShift_ = apvts_.getRawParameterValue("formantShift");
    paramMidiChannel_  = apvts_.getRawParameterValue("midiChannel");
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

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("bypass", 1),
        "Bypass",
        false));

    return { params.begin(), params.end() };
}

bool HarmonizerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto inSet = layouts.getMainInputChannelSet();
    if (inSet != juce::AudioChannelSet::mono() && inSet != juce::AudioChannelSet::stereo())
        return false;

    // Main output (bus 0): must be stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Harmony Sum (bus 1): stereo or disabled
    if (static_cast<int>(layouts.outputBuses.size()) > 1)
    {
        auto set = layouts.outputBuses[1];
        if (!set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }

    // Individual voice buses (bus 2..13): mono or disabled
    for (int i = 2; i < static_cast<int>(layouts.outputBuses.size()); ++i)
    {
        auto set = layouts.outputBuses[i];
        if (!set.isDisabled() && set != juce::AudioChannelSet::mono())
            return false;
    }

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
    smoothedDryWet_.setCurrentAndTargetValue(paramDryWet_->load());

    smoothedVoiceGain_.reset(sampleRate, 0.03);
    smoothedVoiceGain_.setCurrentAndTargetValue(1.0f);

    smoothedOutputGain_.reset(sampleRate, 0.02);
    float initGainDb = paramOutputGain_->load();
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
    int numInputChannels = getTotalNumInputChannels();
    int numOutputChannels = getTotalNumOutputChannels();

    if (numSamples == 0)
        return;

    numSamples = std::min(numSamples, allocatedBlockSize_);

    // Bypass: pass input through to output unprocessed
    if (bypassParam_ != nullptr && bypassParam_->get())
    {
        // Copy input channel(s) to output — mono input gets copied to both L/R
        const float* inL = buffer.getReadPointer(0);
        float* outL = buffer.getWritePointer(0);
        float* outR = (numOutputChannels >= 2) ? buffer.getWritePointer(1) : nullptr;

        if (outL != inL)
            std::memcpy(outL, inL, static_cast<size_t>(numSamples) * sizeof(float));

        if (outR != nullptr)
        {
            const float* inR = (numInputChannels >= 2) ? buffer.getReadPointer(1) : inL;
            if (outR != inR)
                std::memcpy(outR, inR, static_cast<size_t>(numSamples) * sizeof(float));
        }

        // Clear auxiliary output buses when bypassed
        for (int busIdx = 1; busIdx < getBusCount(false); ++busIdx)
        {
            auto auxBus = getBusBuffer(buffer, false, busIdx);
            auxBus.clear();
        }

        midiMessages.clear();
        return;
    }

    // Read parameters (cached pointers — no string hash lookup)
    float detuneCents = paramDetune_->load();
    float pitchCorrectionStrength = paramPitchCorrect_->load();
    float formantShiftSemitones = paramFormantShift_->load();
    float stereoWidth = paramStereoWidth_->load();
    int midiChannel = static_cast<int>(paramMidiChannel_->load());
    float outputGainDb = paramOutputGain_->load();

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

    // Step 2: Extract mono input (sum to mono if stereo input)
    const float* inputL = buffer.getReadPointer(0);
    if (numInputChannels >= 2)
    {
        const float* inputR = buffer.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i)
            monoBuffer_[static_cast<size_t>(i)] = 0.5f * (inputL[i] + inputR[i]);
    }
    else
    {
        std::memcpy(monoBuffer_.data(), inputL, static_cast<size_t>(numSamples) * sizeof(float));
    }

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
    float rawPitch = pitchDetector_.detectPitch(monoBuffer_.data(), numSamples);
    detectedPitchHz.store(rawPitch);  // UI shows raw detected pitch

    // Step 3b: Apply pitch correction for voice allocation (snap toward nearest semitone)
    float pitch = rawPitch;
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
    smoothedDryWet_.setTargetValue(paramDryWet_->load());
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

    // Step 9: Write to auxiliary output buses (if enabled by host)
    // Harmony Sum bus (index 1): summed wet voices with panning applied
    {
        auto harmonySumBus = getBusBuffer(buffer, false, 1);
        if (harmonySumBus.getNumChannels() >= 2)
        {
            std::memcpy(harmonySumBus.getWritePointer(0), wetLeftBuffer_.data(),
                        static_cast<size_t>(numSamples) * sizeof(float));
            std::memcpy(harmonySumBus.getWritePointer(1), wetRightBuffer_.data(),
                        static_cast<size_t>(numSamples) * sizeof(float));
        }
    }

    // Individual voice buses (indices 2..13): mono pre-panning voice signal
    for (int v = 0; v < kMaxVoices; ++v)
    {
        auto voiceBus = getBusBuffer(buffer, false, 2 + v);
        if (voiceBus.getNumChannels() >= 1)
        {
            if (voices[static_cast<size_t>(v)].isActive())
                std::memcpy(voiceBus.getWritePointer(0),
                            voiceRenderPtrs_[static_cast<size_t>(v)],
                            static_cast<size_t>(numSamples) * sizeof(float));
            else
                std::memset(voiceBus.getWritePointer(0), 0,
                            static_cast<size_t>(numSamples) * sizeof(float));
        }
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
