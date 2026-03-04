#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>
#include <cmath>
#include <algorithm>

// ============================================================================
// Bus configuration
// ============================================================================

HarmonizerProcessor::BusesProperties HarmonizerProcessor::makeDefaultBuses()
{
    // Main I/O: stereo in → stereo out
    auto props = BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Main", juce::AudioChannelSet::stereo(), true)
        // Auxiliary stereo bus for the mixed wet (harmony) signal.
        // Disabled by default; the host can enable it for separate routing.
        .withOutput("Harmony Sum", juce::AudioChannelSet::stereo(), false);

    // One mono bus per voice, so the host can route individual harmony voices.
    for (int i = 0; i < kMaxVoices; ++i)
        props = props.withOutput("Voice " + juce::String(i + 1),
                                  juce::AudioChannelSet::mono(), false);

    return props;
}

// ============================================================================
// Construction / destruction
// ============================================================================

HarmonizerProcessor::HarmonizerProcessor()
    : AudioProcessor(makeDefaultBuses()),
      apvts_(*this, nullptr, "Parameters", createParameterLayout())
{
    bypassParam_ = dynamic_cast<juce::AudioParameterBool*>(apvts_.getParameter("bypass"));

    // Cache raw parameter pointers once (stable for the lifetime of the APVTS)
    paramDryWet_       = apvts_.getRawParameterValue("dryWet");
    paramStereoWidth_  = apvts_.getRawParameterValue("stereoWidth");
    paramOutputGain_   = apvts_.getRawParameterValue("outputGain");
    paramDetune_       = apvts_.getRawParameterValue("detune");
    paramPitchCorrect_ = apvts_.getRawParameterValue("pitchCorrect");
    paramFormantShift_ = apvts_.getRawParameterValue("formantShift");
    paramMidiChannel_  = apvts_.getRawParameterValue("midiChannel");
    paramAttack_       = apvts_.getRawParameterValue("attack");
    paramRelease_      = apvts_.getRawParameterValue("release");
}

HarmonizerProcessor::~HarmonizerProcessor() = default;

// ============================================================================
// Parameter layout
// ============================================================================

juce::AudioProcessorValueTreeState::ParameterLayout HarmonizerProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Dry/Wet mix: 0 = fully dry (input only), 1 = fully wet (harmonies only)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("dryWet", 1), "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    // Stereo width: 0 = all voices panned center, 1 = voices spread to ±0.8 L/R
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("stereoWidth", 1), "Stereo Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    // Master output gain in dB: applied after dry/wet mixing
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("outputGain", 1), "Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    // Detune in cents: spread symmetrically across active voices to add
    // chorus-like width (e.g. 10 cents → voice 1 at -10ct, voice 2 at +10ct)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("detune", 1), "Detune",
        juce::NormalisableRange<float>(0.0f, 50.0f, 0.1f), 0.0f));

    // Pitch correction strength: 0 = off, 1 = snap fully to nearest semitone.
    // Intermediate values blend between raw detection and snapped pitch.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("pitchCorrect", 1), "Pitch Correction",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    // Formant shift in semitones: shifts vocal timbre independently of pitch.
    // Positive = brighter/smaller vocal tract, negative = darker/larger.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("formantShift", 1), "Formant Shift",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    // Voice attack time in ms: 0 = instant on, up to 500 ms for slow swells
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("attack", 1), "Attack",
        juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f), 0.0f));

    // Voice release time in ms: how long voices take to fade after note-off.
    // Minimum 10 ms keeps it click-free; up to 2000 ms for long tails.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("release", 1), "Release",
        juce::NormalisableRange<float>(10.0f, 2000.0f, 1.0f), 10.0f));

    // MIDI channel filter: 0 = omni (respond to all channels), 1-16 = specific
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID("midiChannel", 1), "MIDI Channel", 0, 16, 0));

    // DAW bypass (VST3 requires an explicit bypass parameter)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("bypass", 1), "Bypass", false));

    return { params.begin(), params.end() };
}

// ============================================================================
// Bus layout validation
// ============================================================================

bool HarmonizerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Input: mono or stereo (mono is summed to mono; stereo is averaged)
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

// ============================================================================
// Prepare / release
// ============================================================================

void HarmonizerProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Initialize all DSP components with the host's sample rate and block size
    pitchDetector_.prepare(sampleRate, samplesPerBlock);
    midiTracker_.reset();
    voicePool_.prepare(sampleRate, samplesPerBlock);
    panningEngine_.prepare(sampleRate, samplesPerBlock);

    // Pre-allocate all work buffers so processBlock never touches the heap
    allocatedBlockSize_ = samplesPerBlock;
    monoBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);
    wetLeftBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);
    wetRightBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);

    for (int i = 0; i < kMaxVoices; ++i)
    {
        voiceRenderStorage_[static_cast<size_t>(i)].resize(static_cast<size_t>(samplesPerBlock), 0.0f);
        voiceRenderPtrs_[static_cast<size_t>(i)] = voiceRenderStorage_[static_cast<size_t>(i)].data();
    }

    // Initialize smoothed parameters with current values so there's no ramp
    // from zero on the first block after a prepare
    smoothedDryWet_.reset(sampleRate, 0.02);   // 20 ms ramp
    smoothedDryWet_.setCurrentAndTargetValue(paramDryWet_->load());

    smoothedVoiceGain_.reset(sampleRate, 0.03); // 30 ms ramp
    smoothedVoiceGain_.setCurrentAndTargetValue(1.0f);

    smoothedOutputGain_.reset(sampleRate, 0.02); // 20 ms ramp
    float initGainDb = paramOutputGain_->load();
    smoothedOutputGain_.setCurrentAndTargetValue(std::pow(10.0f, initGainDb / 20.0f));

    // Report RubberBand's algorithmic delay so the host can compensate
    setLatencySamples(voicePool_.getStartDelay());
}

void HarmonizerProcessor::releaseResources()
{
    midiTracker_.reset();
}

// ============================================================================
// Audio processing — the real-time callback
// ============================================================================

void HarmonizerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    int numSamples = buffer.getNumSamples();
    int numInputChannels = getTotalNumInputChannels();
    int numOutputChannels = getTotalNumOutputChannels();

    if (numSamples == 0)
        return;

    // Guard against hosts passing more samples than we allocated for
    numSamples = std::min(numSamples, allocatedBlockSize_);

    // ------------------------------------------------------------------
    // Bypass: pass input through to output, clear auxiliary buses
    // ------------------------------------------------------------------
    if (bypassParam_ != nullptr && bypassParam_->get())
    {
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

        for (int busIdx = 1; busIdx < getBusCount(false); ++busIdx)
            getBusBuffer(buffer, false, busIdx).clear();

        midiMessages.clear();
        return;
    }

    // ------------------------------------------------------------------
    // Read automatable parameters (cached pointers, no hash lookups)
    // ------------------------------------------------------------------
    float detuneCents             = paramDetune_->load();
    float pitchCorrectionStrength = paramPitchCorrect_->load();
    float formantShiftSemitones   = paramFormantShift_->load();
    float stereoWidth             = paramStereoWidth_->load();
    int   midiChannel             = static_cast<int>(paramMidiChannel_->load());
    float outputGainDb            = paramOutputGain_->load();
    float attackMs                = paramAttack_->load();
    float releaseMs               = paramRelease_->load();

    // ------------------------------------------------------------------
    // Step 1: Process incoming MIDI (note on/off, channel filtering)
    // ------------------------------------------------------------------
    midiTracker_.setChannelFilter(midiChannel);
    midiTracker_.processMidiBuffer(midiMessages);
    midiActivity.store(midiTracker_.hasActivity());

    // Build a bitmask of held notes for the UI's mini keyboard.
    // Split across two 64-bit words because std::atomic<uint128_t> isn't
    // available on all platforms.
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

    // ------------------------------------------------------------------
    // Step 2: Downmix input to mono
    // ------------------------------------------------------------------
    const float* inputL = buffer.getReadPointer(0);
    if (numInputChannels >= 2)
    {
        // Average L+R to preserve level (0.5 * (L + R))
        const float* inputR = buffer.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i)
            monoBuffer_[static_cast<size_t>(i)] = 0.5f * (inputL[i] + inputR[i]);
    }
    else
    {
        std::memcpy(monoBuffer_.data(), inputL, static_cast<size_t>(numSamples) * sizeof(float));
    }

    // Peak level for the UI input meter
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float absVal = std::abs(monoBuffer_[static_cast<size_t>(i)]);
        if (absVal > peak)
            peak = absVal;
    }
    inputLevelDb.store((peak > 0.0f) ? 20.0f * std::log10(peak) : kSilenceDb);

    // ------------------------------------------------------------------
    // Step 3: Detect pitch (YIN algorithm, FFT-accelerated)
    // ------------------------------------------------------------------
    float rawPitch = pitchDetector_.detectPitch(monoBuffer_.data(), numSamples);
    detectedPitchHz.store(rawPitch);  // UI shows the raw detected pitch

    // Optional pitch correction: blend raw pitch toward the nearest semitone.
    // At strength=0 the pitch is used as-is; at strength=1 it snaps fully.
    float pitch = rawPitch;
    if (pitch > 0.0f && pitchCorrectionStrength > 0.0f)
    {
        // Convert Hz to fractional MIDI note number: A4 (440 Hz) = note 69
        float midiNote = 69.0f + 12.0f * std::log2(pitch / 440.0f);
        int snappedNote = static_cast<int>(std::round(midiNote));
        if (snappedNote >= 0 && snappedNote < 128)
        {
            float correctedPitch = HarmonyVoice::midiNoteToFrequency(snappedNote);
            // Linear blend: 0 = raw, 1 = fully corrected
            pitch = pitch + pitchCorrectionStrength * (correctedPitch - pitch);
        }
    }

    // ------------------------------------------------------------------
    // Step 4: Update voice allocation
    // ------------------------------------------------------------------
    voicePool_.setDetuneCents(detuneCents);
    voicePool_.setFormantShiftSemitones(formantShiftSemitones);
    voicePool_.setAttackMs(attackMs);
    voicePool_.setReleaseMs(releaseMs);

    int numActiveNotes = 0;
    const int* activeNotes = midiTracker_.getActiveNotes(numActiveNotes);
    voicePool_.updateNotes(activeNotes, numActiveNotes, pitch);

    int activeCount = voicePool_.getActiveVoiceCount();
    activeVoiceCount.store(activeCount);

    // ------------------------------------------------------------------
    // Step 5: Render each harmony voice (pitch-shifted copies of the input)
    // ------------------------------------------------------------------
    // Refresh raw pointers in case the vectors were resized in prepareToPlay
    for (int i = 0; i < kMaxVoices; ++i)
        voiceRenderPtrs_[static_cast<size_t>(i)] = voiceRenderStorage_[static_cast<size_t>(i)].data();

    voicePool_.renderVoices(monoBuffer_.data(), voiceRenderPtrs_.data(), numSamples);

    // ------------------------------------------------------------------
    // Step 6: Pan voices across the stereo field
    // ------------------------------------------------------------------
    const auto& voices = voicePool_.getVoices();
    panningEngine_.setWidth(stereoWidth);
    panningEngine_.updatePanning(voices);

    // ------------------------------------------------------------------
    // Step 7: Smooth parameter ramps for this block
    // ------------------------------------------------------------------
    smoothedDryWet_.setTargetValue(paramDryWet_->load());
    smoothedOutputGain_.setTargetValue(std::pow(10.0f, outputGainDb / 20.0f));

    // Gain compensation: divide by voice count so the summed output never
    // exceeds ±1.0.  Harmony voices are derived from the same input signal
    // (correlated), so they sum closer to N×amplitude — not sqrt(N)×amplitude
    // as uncorrelated signals would.  1/N is the only safe choice here.
    float targetVoiceGain = (activeCount > 0) ? 1.0f / static_cast<float>(activeCount) : 1.0f;
    smoothedVoiceGain_.setTargetValue(targetVoiceGain);

    // ------------------------------------------------------------------
    // Step 8: Mix dry + wet and write to main stereo output
    // ------------------------------------------------------------------
    float* leftOut  = buffer.getWritePointer(0);
    float* rightOut = (numOutputChannels >= 2) ? buffer.getWritePointer(1) : nullptr;

    std::memset(wetLeftBuffer_.data(), 0, static_cast<size_t>(numSamples) * sizeof(float));
    std::memset(wetRightBuffer_.data(), 0, static_cast<size_t>(numSamples) * sizeof(float));

    panningEngine_.applyPanning(voiceRenderPtrs_.data(), voices,
                                 wetLeftBuffer_.data(), wetRightBuffer_.data(), numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        float mix      = smoothedDryWet_.getNextValue();
        float voiceGain = smoothedVoiceGain_.getNextValue();
        float outGain  = smoothedOutputGain_.getNextValue();
        float dryGain  = 1.0f - mix;
        float wetGain  = mix * voiceGain;

        float dry  = monoBuffer_[static_cast<size_t>(i)];
        float wetL = wetLeftBuffer_[static_cast<size_t>(i)];
        float wetR = wetRightBuffer_[static_cast<size_t>(i)];

        leftOut[i] = juce::jlimit(-1.0f, 1.0f, (dry * dryGain + wetL * wetGain) * outGain);
        if (rightOut != nullptr)
            rightOut[i] = juce::jlimit(-1.0f, 1.0f, (dry * dryGain + wetR * wetGain) * outGain);
    }

    // ------------------------------------------------------------------
    // Step 9: Write to auxiliary output buses (if enabled by the host)
    // ------------------------------------------------------------------

    // Harmony Sum bus (index 1): the summed wet signal with panning applied
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

    // Individual voice buses (indices 2..13): mono, pre-panning signal per voice
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

    // MIDI is consumed — clear so the host doesn't forward it downstream
    midiMessages.clear();
}

// ============================================================================
// Editor factory
// ============================================================================

juce::AudioProcessorEditor* HarmonizerProcessor::createEditor()
{
    return new HarmonizerEditor(*this);
}

// ============================================================================
// State persistence (preset save/load)
// ============================================================================

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

// ============================================================================
// JUCE plugin entry point
// ============================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonizerProcessor();
}
