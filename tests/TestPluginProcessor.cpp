#include "TestFramework.h"
#include "PluginProcessor.h"
#include <cmath>
#include <cstring>

static constexpr double kSR = 44100.0;
static constexpr int    kBlock = 512;

// ============================================================================
// Parameter layout
// ============================================================================

TEST(Processor_HasAllParameters)
{
    HarmonizerProcessor proc;
    auto& apvts = proc.getAPVTS();

    // Verify all expected parameters exist
    EXPECT_TRUE(apvts.getParameter("dryWet") != nullptr);
    EXPECT_TRUE(apvts.getParameter("stereoWidth") != nullptr);
    EXPECT_TRUE(apvts.getParameter("outputGain") != nullptr);
    EXPECT_TRUE(apvts.getParameter("detune") != nullptr);
    EXPECT_TRUE(apvts.getParameter("pitchCorrect") != nullptr);
    EXPECT_TRUE(apvts.getParameter("formantShift") != nullptr);
    EXPECT_TRUE(apvts.getParameter("attack") != nullptr);
    EXPECT_TRUE(apvts.getParameter("release") != nullptr);
    EXPECT_TRUE(apvts.getParameter("midiChannel") != nullptr);
    EXPECT_TRUE(apvts.getParameter("bypass") != nullptr);
}

TEST(Processor_ParameterDefaults)
{
    HarmonizerProcessor proc;
    auto& apvts = proc.getAPVTS();

    // Check default values
    EXPECT_NEAR(apvts.getRawParameterValue("dryWet")->load(), 0.5f, 0.01f);
    EXPECT_NEAR(apvts.getRawParameterValue("stereoWidth")->load(), 1.0f, 0.01f);
    EXPECT_NEAR(apvts.getRawParameterValue("outputGain")->load(), 0.0f, 0.01f);
    EXPECT_NEAR(apvts.getRawParameterValue("detune")->load(), 0.0f, 0.01f);
    EXPECT_NEAR(apvts.getRawParameterValue("pitchCorrect")->load(), 0.0f, 0.01f);
    EXPECT_NEAR(apvts.getRawParameterValue("formantShift")->load(), 0.0f, 0.01f);
    EXPECT_NEAR(apvts.getRawParameterValue("attack")->load(), 0.0f, 0.01f);
    EXPECT_NEAR(apvts.getRawParameterValue("release")->load(), 10.0f, 0.01f);
    EXPECT_NEAR(apvts.getRawParameterValue("midiChannel")->load(), 0.0f, 0.01f);
}

TEST(Processor_ParameterRanges)
{
    HarmonizerProcessor proc;
    auto& apvts = proc.getAPVTS();

    // Dry/Wet range [0, 1]
    auto* dw = apvts.getParameter("dryWet");
    auto range = dw->getNormalisableRange();
    EXPECT_NEAR(range.start, 0.0f, 0.01f);
    EXPECT_NEAR(range.end, 1.0f, 0.01f);

    // Attack range [0, 500]
    auto* atk = apvts.getParameter("attack");
    auto atkRange = atk->getNormalisableRange();
    EXPECT_NEAR(atkRange.start, 0.0f, 0.01f);
    EXPECT_NEAR(atkRange.end, 500.0f, 0.01f);

    // Release range [10, 2000]
    auto* rel = apvts.getParameter("release");
    auto relRange = rel->getNormalisableRange();
    EXPECT_NEAR(relRange.start, 10.0f, 0.01f);
    EXPECT_NEAR(relRange.end, 2000.0f, 0.01f);
}

// ============================================================================
// Plugin properties
// ============================================================================

TEST(Processor_AcceptsMidi)
{
    HarmonizerProcessor proc;
    EXPECT_TRUE(proc.acceptsMidi());
}

TEST(Processor_DoesNotProduceMidi)
{
    HarmonizerProcessor proc;
    EXPECT_FALSE(proc.producesMidi());
}

TEST(Processor_IsNotMidiEffect)
{
    HarmonizerProcessor proc;
    EXPECT_FALSE(proc.isMidiEffect());
}

TEST(Processor_TailLength_MatchesMaxRelease)
{
    HarmonizerProcessor proc;
    EXPECT_NEAR(proc.getTailLengthSeconds(), 2.0, 0.01);
}

TEST(Processor_HasEditor)
{
    HarmonizerProcessor proc;
    EXPECT_TRUE(proc.hasEditor());
}

// ============================================================================
// Prepare / process
// ============================================================================

TEST(Processor_PrepareToPlay_SetsLatency)
{
    HarmonizerProcessor proc;
    proc.prepareToPlay(kSR, kBlock);

    // Latency should be non-negative (RubberBand reports its delay)
    EXPECT_GE(proc.getLatencySamples(), 0);
}

TEST(Processor_ProcessBlock_SilenceIn_NoExplosion)
{
    HarmonizerProcessor proc;
    proc.prepareToPlay(kSR, kBlock);

    // Create a stereo buffer of silence
    juce::AudioBuffer<float> buffer(2, kBlock);
    buffer.clear();
    juce::MidiBuffer midi;

    // Should not crash
    proc.processBlock(buffer, midi);

    // Output should be finite
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < kBlock; ++i)
        {
            EXPECT_FALSE(std::isnan(data[i]));
            EXPECT_FALSE(std::isinf(data[i]));
        }
    }
}

TEST(Processor_ProcessBlock_DryOnlyMode)
{
    HarmonizerProcessor proc;
    proc.prepareToPlay(kSR, kBlock);

    // Set dry/wet to 0 (fully dry) — output should equal input
    proc.getAPVTS().getParameter("dryWet")->setValueNotifyingHost(0.0f);
    proc.getAPVTS().getParameter("outputGain")->setValueNotifyingHost(
        proc.getAPVTS().getParameter("outputGain")->getNormalisableRange().convertTo0to1(0.0f));

    // Create a buffer with known content
    juce::AudioBuffer<float> buffer(2, kBlock);
    for (int i = 0; i < kBlock; ++i)
    {
        buffer.setSample(0, i, 0.5f);
        buffer.setSample(1, i, 0.5f);
    }
    juce::MidiBuffer midi;

    // Process a few blocks for smoothing to converge
    for (int b = 0; b < 20; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            buffer.setSample(0, i, 0.5f);
            buffer.setSample(1, i, 0.5f);
        }
        proc.processBlock(buffer, midi);
    }

    // After smoothing, the output should be close to the dry input (0.5)
    // (mono downmix of 0.5+0.5 = 0.5, times dryGain=1.0, times outputGain=1.0)
    float lastL = buffer.getSample(0, kBlock - 1);
    EXPECT_NEAR(lastL, 0.5f, 0.05f);
}

TEST(Processor_ProcessBlock_FullyWet_NoNotes_Silence)
{
    HarmonizerProcessor proc;
    proc.prepareToPlay(kSR, kBlock);

    // Set fully wet, no MIDI notes → should be silent
    proc.getAPVTS().getParameter("dryWet")->setValueNotifyingHost(1.0f);

    juce::AudioBuffer<float> buffer(2, kBlock);
    for (int i = 0; i < kBlock; ++i)
    {
        buffer.setSample(0, i, 0.5f);
        buffer.setSample(1, i, 0.5f);
    }
    juce::MidiBuffer midi;

    // Process several blocks for smoothing
    for (int b = 0; b < 20; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            buffer.setSample(0, i, 0.5f);
            buffer.setSample(1, i, 0.5f);
        }
        proc.processBlock(buffer, midi);
    }

    // Fully wet with no voices = silence
    float maxAbs = 0.0f;
    for (int i = 0; i < kBlock; ++i)
        maxAbs = std::max(maxAbs, std::abs(buffer.getSample(0, i)));

    EXPECT_LT(maxAbs, 0.05f);
}

// ============================================================================
// Bypass
// ============================================================================

TEST(Processor_Bypass_PassesThrough)
{
    HarmonizerProcessor proc;
    proc.prepareToPlay(kSR, kBlock);

    // Enable bypass
    auto* bypassParam = proc.getAPVTS().getParameter("bypass");
    bypassParam->setValueNotifyingHost(1.0f);

    juce::AudioBuffer<float> buffer(2, kBlock);
    for (int i = 0; i < kBlock; ++i)
    {
        buffer.setSample(0, i, 0.25f);
        buffer.setSample(1, i, 0.75f);
    }
    juce::MidiBuffer midi;
    proc.processBlock(buffer, midi);

    // Output should be identical to input
    EXPECT_NEAR(buffer.getSample(0, 0), 0.25f, 1e-6f);
    EXPECT_NEAR(buffer.getSample(1, 0), 0.75f, 1e-6f);
    EXPECT_NEAR(buffer.getSample(0, kBlock - 1), 0.25f, 1e-6f);
    EXPECT_NEAR(buffer.getSample(1, kBlock - 1), 0.75f, 1e-6f);
}

TEST(Processor_Bypass_ClearsMidi)
{
    HarmonizerProcessor proc;
    proc.prepareToPlay(kSR, kBlock);

    auto* bypassParam = proc.getAPVTS().getParameter("bypass");
    bypassParam->setValueNotifyingHost(1.0f);

    juce::AudioBuffer<float> buffer(2, kBlock);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);

    proc.processBlock(buffer, midi);

    // MIDI buffer should be cleared in bypass
    EXPECT_TRUE(midi.isEmpty());
}

// ============================================================================
// State save/load
// ============================================================================

TEST(Processor_StateSaveLoad_RoundTrips)
{
    HarmonizerProcessor proc;
    proc.prepareToPlay(kSR, kBlock);

    // Set some non-default values
    proc.getAPVTS().getParameter("dryWet")->setValueNotifyingHost(0.75f);
    proc.getAPVTS().getParameter("detune")->setValueNotifyingHost(
        proc.getAPVTS().getParameter("detune")->getNormalisableRange().convertTo0to1(15.0f));

    // Save state
    juce::MemoryBlock state;
    proc.getStateInformation(state);

    // Create a new processor and load the state
    HarmonizerProcessor proc2;
    proc2.prepareToPlay(kSR, kBlock);
    proc2.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    // Verify the loaded values match
    float dw1 = proc.getAPVTS().getRawParameterValue("dryWet")->load();
    float dw2 = proc2.getAPVTS().getRawParameterValue("dryWet")->load();
    EXPECT_NEAR(dw1, dw2, 0.01f);

    float det1 = proc.getAPVTS().getRawParameterValue("detune")->load();
    float det2 = proc2.getAPVTS().getRawParameterValue("detune")->load();
    EXPECT_NEAR(det1, det2, 0.5f);
}

// ============================================================================
// Atomic readouts
// ============================================================================

TEST(Processor_AtomicReadouts_InitialValues)
{
    HarmonizerProcessor proc;
    proc.prepareToPlay(kSR, kBlock);

    // Before processing, detected pitch should be unset
    EXPECT_LE(proc.detectedPitchHz.load(), 0.0f);
    EXPECT_EQ(proc.activeVoiceCount.load(), 0);
}

TEST(Processor_ProcessBlock_WithMidi_ActivatesVoices)
{
    HarmonizerProcessor proc;
    proc.prepareToPlay(kSR, kBlock);

    // Feed a note-on with audio
    juce::AudioBuffer<float> buffer(2, kBlock);
    float pi = 3.14159265358979323846f;
    for (int i = 0; i < kBlock; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(kSR);
        float sample = 0.5f * std::sin(2.0f * pi * 440.0f * t);
        buffer.setSample(0, i, sample);
        buffer.setSample(1, i, sample);
    }

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, (juce::uint8)100), 0);
    proc.processBlock(buffer, midi);

    EXPECT_EQ(proc.activeVoiceCount.load(), 1);
}

// ============================================================================
// Zero-length block
// ============================================================================

TEST(Processor_ZeroLengthBlock_NoOp)
{
    HarmonizerProcessor proc;
    proc.prepareToPlay(kSR, kBlock);

    juce::AudioBuffer<float> buffer(2, 0);
    juce::MidiBuffer midi;

    // Should not crash
    proc.processBlock(buffer, midi);
}

// ============================================================================
// Mono input
// ============================================================================

TEST(Processor_MonoInput_DoesNotCrash)
{
    HarmonizerProcessor proc;

    // Check if mono layout is supported
    juce::AudioProcessor::BusesLayout monoLayout;
    monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
    monoLayout.outputBuses.add(juce::AudioChannelSet::stereo());

    if (proc.isBusesLayoutSupported(monoLayout))
    {
        proc.setBusesLayout(monoLayout);
        proc.prepareToPlay(kSR, kBlock);

        juce::AudioBuffer<float> buffer(2, kBlock);
        buffer.clear();
        for (int i = 0; i < kBlock; ++i)
            buffer.setSample(0, i, 0.5f);

        juce::MidiBuffer midi;
        proc.processBlock(buffer, midi);

        // Should not crash and output should be finite
        for (int i = 0; i < kBlock; ++i)
        {
            EXPECT_FALSE(std::isnan(buffer.getSample(0, i)));
        }
    }
}
