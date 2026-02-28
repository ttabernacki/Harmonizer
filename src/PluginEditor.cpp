#include "PluginEditor.h"
#include <cmath>

static const juce::Colour kBackground   { 0xff1e1e2e };
static const juce::Colour kSurface      { 0xff2a2a3d };
static const juce::Colour kTextPrimary  { 0xffe0e0e0 };
static const juce::Colour kTextSecondary{ 0xff8888aa };
static const juce::Colour kAccent       { 0xff6c8cff };
static const juce::Colour kMidiGreen    { 0xff44dd66 };
static const juce::Colour kMidiOff      { 0xff444466 };
static const juce::Colour kMeterGreen  { 0xff44bb55 };
static const juce::Colour kMeterYellow { 0xffbbbb33 };
static const juce::Colour kMeterRed    { 0xffdd4444 };

static const char* const NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

HarmonizerEditor::HarmonizerEditor(HarmonizerProcessor& p)
    : AudioProcessorEditor(&p), processor_(p)
{
    setSize(400, 300);

    // Title
    titleLabel_.setText("HARMONIZER", juce::dontSendNotification);
    titleLabel_.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, kAccent);
    titleLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel_);

    // Dry/Wet knob
    dryWetSlider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    dryWetSlider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    dryWetSlider_.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    dryWetSlider_.setColour(juce::Slider::thumbColourId, kAccent);
    dryWetSlider_.setColour(juce::Slider::textBoxTextColourId, kTextPrimary);
    dryWetSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(dryWetSlider_);

    dryWetAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor_.getAPVTS(), "dryWet", dryWetSlider_);

    dryWetLabel_.setText("DRY / WET", juce::dontSendNotification);
    dryWetLabel_.setFont(juce::FontOptions(12.0f));
    dryWetLabel_.setColour(juce::Label::textColourId, kTextSecondary);
    dryWetLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(dryWetLabel_);

    // Pitch display
    pitchLabel_.setText("INPUT PITCH", juce::dontSendNotification);
    pitchLabel_.setFont(juce::FontOptions(11.0f));
    pitchLabel_.setColour(juce::Label::textColourId, kTextSecondary);
    pitchLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(pitchLabel_);

    pitchValueLabel_.setText("--", juce::dontSendNotification);
    pitchValueLabel_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    pitchValueLabel_.setColour(juce::Label::textColourId, kTextPrimary);
    pitchValueLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(pitchValueLabel_);

    // Voices display
    voicesLabel_.setText("VOICES", juce::dontSendNotification);
    voicesLabel_.setFont(juce::FontOptions(11.0f));
    voicesLabel_.setColour(juce::Label::textColourId, kTextSecondary);
    voicesLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(voicesLabel_);

    voicesValueLabel_.setText("0 / 12", juce::dontSendNotification);
    voicesValueLabel_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    voicesValueLabel_.setColour(juce::Label::textColourId, kTextPrimary);
    voicesValueLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(voicesValueLabel_);

    // MIDI label
    midiLabel_.setText("MIDI", juce::dontSendNotification);
    midiLabel_.setFont(juce::FontOptions(11.0f));
    midiLabel_.setColour(juce::Label::textColourId, kTextSecondary);
    midiLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(midiLabel_);

    // Timer at ~30Hz for UI updates
    startTimerHz(30);
}

HarmonizerEditor::~HarmonizerEditor()
{
    stopTimer();
}

void HarmonizerEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);

    // Info panel background
    auto infoBounds = getLocalBounds().reduced(15).removeFromBottom(100);
    g.setColour(kSurface);
    g.fillRoundedRectangle(infoBounds.toFloat(), 8.0f);

    // MIDI LED circle
    auto midiArea = infoBounds.removeFromRight(80);
    auto ledRect = midiArea.withSizeKeepingCentre(16, 16).translated(0, -10);
    g.setColour(midiLedOn_ ? kMidiGreen : kMidiOff);
    g.fillEllipse(ledRect.toFloat());

    // Input level meter (vertical bar on the left edge)
    auto meterBounds = getLocalBounds().removeFromLeft(8).reduced(0, 15);
    g.setColour(kSurface);
    g.fillRect(meterBounds);

    // Map dB to 0..1 range (-60dB to 0dB)
    float normLevel = (displayedLevelDb_ + 60.0f) / 60.0f;
    normLevel = std::clamp(normLevel, 0.0f, 1.0f);

    if (normLevel > 0.0f)
    {
        int meterHeight = static_cast<int>(static_cast<float>(meterBounds.getHeight()) * normLevel);
        auto filledRect = meterBounds.removeFromBottom(meterHeight);

        // Color based on level
        juce::Colour meterColour = kMeterGreen;
        if (displayedLevelDb_ > -3.0f)
            meterColour = kMeterRed;
        else if (displayedLevelDb_ > -12.0f)
            meterColour = kMeterYellow;

        g.setColour(meterColour);
        g.fillRect(filledRect);
    }
}

void HarmonizerEditor::resized()
{
    auto area = getLocalBounds().reduced(15);

    // Title at top
    titleLabel_.setBounds(area.removeFromTop(35));

    area.removeFromTop(5);

    // Knob in center area
    auto knobArea = area.removeFromTop(150);
    auto knobCenter = knobArea.withSizeKeepingCentre(120, 130);
    dryWetSlider_.setBounds(knobCenter.removeFromTop(110));
    dryWetLabel_.setBounds(knobCenter);

    // Info panel at bottom
    auto infoArea = area.removeFromBottom(100).reduced(10, 15);

    // Three columns: Pitch | Voices | MIDI
    int colWidth = infoArea.getWidth() / 3;

    auto pitchArea = infoArea.removeFromLeft(colWidth);
    pitchLabel_.setBounds(pitchArea.removeFromTop(18));
    pitchValueLabel_.setBounds(pitchArea);

    auto voicesArea = infoArea.removeFromLeft(colWidth);
    voicesLabel_.setBounds(voicesArea.removeFromTop(18));
    voicesValueLabel_.setBounds(voicesArea);

    auto midiArea = infoArea;
    midiLabel_.setBounds(midiArea.removeFromTop(18));
    // LED is drawn in paint()
}

void HarmonizerEditor::timerCallback()
{
    bool needsRepaint = false;

    // Update pitch display
    float pitch = processor_.detectedPitchHz.load();
    juce::String pitchText = (pitch > 0.0f) ? frequencyToNoteName(pitch) : "--";
    if (pitchText != lastPitchText_)
    {
        pitchValueLabel_.setText(pitchText, juce::dontSendNotification);
        lastPitchText_ = pitchText;
    }

    // Update voice count
    int voices = processor_.activeVoiceCount.load();
    juce::String voicesText = juce::String(voices) + " / 12";
    if (voicesText != lastVoicesText_)
    {
        voicesValueLabel_.setText(voicesText, juce::dontSendNotification);
        lastVoicesText_ = voicesText;
    }

    // Update MIDI LED (stays on for a few frames after activity)
    if (processor_.midiActivity.load())
    {
        midiLedOn_ = true;
        midiLedCountdown_ = 4;  // ~130ms at 30Hz
    }
    else if (midiLedCountdown_ > 0)
    {
        --midiLedCountdown_;
        if (midiLedCountdown_ == 0)
            midiLedOn_ = false;
    }
    if (midiLedOn_ != lastMidiLedState_)
    {
        lastMidiLedState_ = midiLedOn_;
        needsRepaint = true;
    }

    // Update input level meter (smoothed decay)
    float targetDb = processor_.inputLevelDb.load();
    // Fast attack, slow decay for visual smoothness
    if (targetDb > displayedLevelDb_)
        displayedLevelDb_ = targetDb;
    else
        displayedLevelDb_ += 0.3f * (targetDb - displayedLevelDb_);

    if (std::abs(displayedLevelDb_ - lastLevelDb_) > 0.5f)
    {
        lastLevelDb_ = displayedLevelDb_;
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

juce::String HarmonizerEditor::frequencyToNoteName(float freqHz)
{
    if (freqHz <= 0.0f)
        return "--";

    // MIDI note number from frequency: n = 69 + 12 * log2(f / 440)
    float noteFloat = 69.0f + 12.0f * std::log2(freqHz / 440.0f);
    int noteNum = static_cast<int>(std::round(noteFloat));

    if (noteNum < 0 || noteNum > 127)
        return juce::String(freqHz, 1) + " Hz";

    int octave = (noteNum / 12) - 1;
    int noteIndex = noteNum % 12;

    return juce::String(NOTE_NAMES[noteIndex]) + juce::String(octave)
           + " / " + juce::String(freqHz, 1) + " Hz";
}
