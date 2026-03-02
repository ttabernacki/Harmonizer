#include "PluginEditor.h"
#include <cmath>

// ============================================================================
// Colour palette (dark theme)
// ============================================================================
static const juce::Colour kBackground   { 0xff1e1e2e };
static const juce::Colour kSurface      { 0xff2a2a3d };
static const juce::Colour kTextPrimary  { 0xffe0e0e0 };
static const juce::Colour kTextSecondary{ 0xff8888aa };
static const juce::Colour kAccent       { 0xff6c8cff };
static const juce::Colour kMidiGreen    { 0xff44dd66 };
static const juce::Colour kMidiOff      { 0xff444466 };
static const juce::Colour kMeterGreen   { 0xff44bb55 };
static const juce::Colour kMeterYellow  { 0xffbbbb33 };
static const juce::Colour kMeterRed     { 0xffdd4444 };
static const juce::Colour kKeyWhite     { 0xffe8e8e8 };
static const juce::Colour kKeyBlack     { 0xff222222 };
static const juce::Colour kKeyHeld      { 0xff6c8cff };  // Same as accent

static const char* const NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

// ============================================================================
// MiniKeyboard — paint 4 octaves (C2 = MIDI 36 through B5 = MIDI 83)
// ============================================================================

void MiniKeyboard::paint(juce::Graphics& g)
{
    const int startNote = 36;   // C2
    const int numNotes  = 48;   // 4 octaves (48 semitones)

    auto bounds = getLocalBounds().toFloat();

    // Count white keys to compute key width
    int whiteCount = 0;
    for (int i = 0; i < numNotes; ++i)
        if (!isBlackKey(startNote + i))
            ++whiteCount;

    float whiteWidth  = bounds.getWidth() / static_cast<float>(whiteCount);
    float whiteHeight = bounds.getHeight();
    float blackWidth  = whiteWidth * 0.6f;
    float blackHeight = whiteHeight * 0.6f;

    // Pass 1: draw white keys
    int wIdx = 0;
    for (int i = 0; i < numNotes; ++i)
    {
        int note = startNote + i;
        if (!isBlackKey(note))
        {
            float x = static_cast<float>(wIdx) * whiteWidth;
            bool held = isNoteHeld(note);
            g.setColour(held ? kKeyHeld : kKeyWhite);
            g.fillRect(x, 0.0f, whiteWidth - 1.0f, whiteHeight);
            g.setColour(juce::Colour(0xff888888));
            g.drawRect(x, 0.0f, whiteWidth - 1.0f, whiteHeight, 0.5f);
            ++wIdx;
        }
    }

    // Pass 2: draw black keys on top (positioned relative to the preceding white key)
    wIdx = 0;
    for (int i = 0; i < numNotes; ++i)
    {
        int note = startNote + i;
        if (!isBlackKey(note))
        {
            ++wIdx;
        }
        else
        {
            float x = static_cast<float>(wIdx - 1) * whiteWidth + whiteWidth * 0.65f;
            bool held = isNoteHeld(note);
            g.setColour(held ? kKeyHeld : kKeyBlack);
            g.fillRect(x, 0.0f, blackWidth, blackHeight);
        }
    }
}

// ============================================================================
// HarmonizerEditor — construction
// ============================================================================

void HarmonizerEditor::setupKnob(juce::Slider& slider, juce::Label& label, const juce::String& text,
                                  const juce::String& paramId,
                                  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
    slider.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    slider.setColour(juce::Slider::thumbColourId, kAccent);
    slider.setColour(juce::Slider::textBoxTextColourId, kTextPrimary);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(slider);

    // Bind the slider to the APVTS parameter — this handles two-way
    // sync between the knob and host automation automatically.
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor_.getAPVTS(), paramId, slider);

    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::FontOptions(10.0f));
    label.setColour(juce::Label::textColourId, kTextSecondary);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

HarmonizerEditor::HarmonizerEditor(HarmonizerProcessor& p)
    : AudioProcessorEditor(&p), processor_(p)
{
    setSize(620, 480);

    // Title
    titleLabel_.setText("HARMONIZER", juce::dontSendNotification);
    titleLabel_.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, kAccent);
    titleLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel_);

    // Knobs — row 1: core mix controls
    setupKnob(dryWetSlider_,      dryWetLabel_,      "DRY / WET",    "dryWet",      dryWetAttachment_);
    setupKnob(stereoWidthSlider_, stereoWidthLabel_, "STEREO WIDTH", "stereoWidth", stereoWidthAttachment_);
    setupKnob(outputGainSlider_,  outputGainLabel_,  "OUTPUT GAIN",  "outputGain",  outputGainAttachment_);
    outputGainSlider_.setTextValueSuffix(" dB");

    // Knobs — row 2: voice shaping + MIDI
    setupKnob(detuneSlider_,       detuneLabel_,       "DETUNE",     "detune",       detuneAttachment_);
    detuneSlider_.setTextValueSuffix(" ct");
    setupKnob(pitchCorrectSlider_, pitchCorrectLabel_, "PITCH CORR", "pitchCorrect", pitchCorrectAttachment_);
    setupKnob(formantShiftSlider_, formantShiftLabel_, "FORMANT",    "formantShift", formantShiftAttachment_);
    formantShiftSlider_.setTextValueSuffix(" st");
    setupKnob(midiChannelSlider_,  midiChannelLabel_,  "MIDI CH",    "midiChannel",  midiChannelAttachment_);

    // Info panel labels
    pitchLabel_.setText("INPUT PITCH", juce::dontSendNotification);
    pitchLabel_.setFont(juce::FontOptions(11.0f));
    pitchLabel_.setColour(juce::Label::textColourId, kTextSecondary);
    pitchLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(pitchLabel_);

    pitchValueLabel_.setText("--", juce::dontSendNotification);
    pitchValueLabel_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    pitchValueLabel_.setColour(juce::Label::textColourId, kTextPrimary);
    pitchValueLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(pitchValueLabel_);

    voicesLabel_.setText("VOICES", juce::dontSendNotification);
    voicesLabel_.setFont(juce::FontOptions(11.0f));
    voicesLabel_.setColour(juce::Label::textColourId, kTextSecondary);
    voicesLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(voicesLabel_);

    voicesValueLabel_.setText("0 / 12", juce::dontSendNotification);
    voicesValueLabel_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    voicesValueLabel_.setColour(juce::Label::textColourId, kTextPrimary);
    voicesValueLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(voicesValueLabel_);

    midiLabel_.setText("MIDI", juce::dontSendNotification);
    midiLabel_.setFont(juce::FontOptions(11.0f));
    midiLabel_.setColour(juce::Label::textColourId, kTextSecondary);
    midiLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(midiLabel_);

    addAndMakeVisible(miniKeyboard_);

    // 30 Hz refresh: fast enough for responsive metering, slow enough to be
    // negligible CPU.  At this rate kMidiLedFrames=4 gives ~133 ms LED hold.
    startTimerHz(30);
}

HarmonizerEditor::~HarmonizerEditor()
{
    stopTimer();
}

// ============================================================================
// Paint — background, MIDI LED, and input level meter
// ============================================================================

void HarmonizerEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);

    auto bounds = getLocalBounds();

    // --- Info panel background (rounded rectangle at the bottom) ---
    auto infoBounds = bounds.reduced(15).removeFromBottom(120);
    g.setColour(kSurface);
    g.fillRoundedRectangle(infoBounds.toFloat(), 8.0f);

    // --- MIDI activity LED (green circle in the info panel) ---
    {
        auto midiArea = infoBounds.removeFromRight(80);
        auto ledRect = midiArea.withSizeKeepingCentre(14, 14).translated(0, -30);
        g.setColour(midiLedOn_ ? kMidiGreen : kMidiOff);
        g.fillEllipse(ledRect.toFloat());
    }

    // --- Input level meter (thin vertical bar on the left edge) ---
    auto meterBounds = bounds.removeFromLeft(8).reduced(0, 15);
    g.setColour(kSurface);
    g.fillRect(meterBounds);

    // Map dBFS to 0..1: the meter shows a 60 dB range (-60 to 0 dBFS)
    float normLevel = (displayedLevelDb_ + 60.0f) / 60.0f;
    normLevel = std::clamp(normLevel, 0.0f, 1.0f);

    if (normLevel > 0.0f)
    {
        int meterHeight = static_cast<int>(static_cast<float>(meterBounds.getHeight()) * normLevel);
        auto filledRect = meterBounds.removeFromBottom(meterHeight);

        // Colour thresholds: green → yellow at -12 dB, yellow → red at -3 dB
        juce::Colour meterColour = kMeterGreen;
        if (displayedLevelDb_ > kMeterRedDb)
            meterColour = kMeterRed;
        else if (displayedLevelDb_ > kMeterYellowDb)
            meterColour = kMeterYellow;

        g.setColour(meterColour);
        g.fillRect(filledRect);
    }
}

// ============================================================================
// Layout
// ============================================================================

void HarmonizerEditor::resized()
{
    auto area = getLocalBounds().reduced(15);

    titleLabel_.setBounds(area.removeFromTop(30));
    area.removeFromTop(5);

    // Row 1: 3 knobs (Dry/Wet, Stereo Width, Output Gain)
    auto row1 = area.removeFromTop(100);
    int knobW = row1.getWidth() / 3;
    {
        auto knobArea = row1.removeFromLeft(knobW).reduced(5, 0);
        dryWetSlider_.setBounds(knobArea.removeFromTop(80));
        dryWetLabel_.setBounds(knobArea);
    }
    {
        auto knobArea = row1.removeFromLeft(knobW).reduced(5, 0);
        stereoWidthSlider_.setBounds(knobArea.removeFromTop(80));
        stereoWidthLabel_.setBounds(knobArea);
    }
    {
        auto knobArea = row1.reduced(5, 0);
        outputGainSlider_.setBounds(knobArea.removeFromTop(80));
        outputGainLabel_.setBounds(knobArea);
    }

    area.removeFromTop(5);

    // Row 2: 4 knobs (Detune, Pitch Correction, Formant, MIDI Channel)
    auto row2 = area.removeFromTop(100);
    int knobW2 = row2.getWidth() / 4;
    {
        auto knobArea = row2.removeFromLeft(knobW2).reduced(5, 0);
        detuneSlider_.setBounds(knobArea.removeFromTop(80));
        detuneLabel_.setBounds(knobArea);
    }
    {
        auto knobArea = row2.removeFromLeft(knobW2).reduced(5, 0);
        pitchCorrectSlider_.setBounds(knobArea.removeFromTop(80));
        pitchCorrectLabel_.setBounds(knobArea);
    }
    {
        auto knobArea = row2.removeFromLeft(knobW2).reduced(5, 0);
        formantShiftSlider_.setBounds(knobArea.removeFromTop(80));
        formantShiftLabel_.setBounds(knobArea);
    }
    {
        auto knobArea = row2.reduced(5, 0);
        midiChannelSlider_.setBounds(knobArea.removeFromTop(80));
        midiChannelLabel_.setBounds(knobArea);
    }

    area.removeFromTop(5);

    // Info panel (bottom 120 px)
    auto infoArea = area.removeFromBottom(120).reduced(10, 10);

    // Top row of info panel: three columns
    auto infoTopRow = infoArea.removeFromTop(50);
    int colWidth = infoTopRow.getWidth() / 3;

    auto pitchArea = infoTopRow.removeFromLeft(colWidth);
    pitchLabel_.setBounds(pitchArea.removeFromTop(16));
    pitchValueLabel_.setBounds(pitchArea);

    auto voicesArea = infoTopRow.removeFromLeft(colWidth);
    voicesLabel_.setBounds(voicesArea.removeFromTop(16));
    voicesValueLabel_.setBounds(voicesArea);

    auto midiArea = infoTopRow;
    midiLabel_.setBounds(midiArea.removeFromTop(16));
    // (LED is drawn directly in paint(), not a child component)

    // Mini keyboard fills the remaining info panel space
    infoArea.removeFromTop(5);
    miniKeyboard_.setBounds(infoArea);
}

// ============================================================================
// Timer — 30 Hz UI refresh
// ============================================================================

void HarmonizerEditor::timerCallback()
{
    bool needsRepaint = false;

    // --- Detected pitch display ---
    float pitch = processor_.detectedPitchHz.load();
    juce::String pitchText = (pitch > 0.0f) ? frequencyToNoteName(pitch) : "--";
    if (pitchText != lastPitchText_)
    {
        pitchValueLabel_.setText(pitchText, juce::dontSendNotification);
        lastPitchText_ = pitchText;
    }

    // --- Active voice count ---
    int voices = processor_.activeVoiceCount.load();
    juce::String voicesText = juce::String(voices) + " / 12";
    if (voicesText != lastVoicesText_)
    {
        voicesValueLabel_.setText(voicesText, juce::dontSendNotification);
        lastVoicesText_ = voicesText;
    }

    // --- MIDI activity LED ---
    // The LED turns on when MIDI arrives and stays lit for kMidiLedFrames
    // timer ticks (~133 ms at 30 Hz) after the last event.
    if (processor_.midiActivity.load())
    {
        midiLedOn_ = true;
        midiLedCountdown_ = kMidiLedFrames;
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

    // --- Input level meter ---
    // Attack is instant (jump to peak), decay is smoothed (~10 dB/sec at 30 Hz).
    // The 0.3 coefficient gives: 0.3 * 30 Hz ≈ 9 dB/sec decay.
    float targetDb = processor_.inputLevelDb.load();
    if (targetDb > displayedLevelDb_)
        displayedLevelDb_ = targetDb;   // instant attack
    else
        displayedLevelDb_ += 0.3f * (targetDb - displayedLevelDb_);  // smooth decay

    if (std::abs(displayedLevelDb_ - lastLevelDb_) > 0.5f)
    {
        lastLevelDb_ = displayedLevelDb_;
        needsRepaint = true;
    }

    // --- Mini keyboard ---
    miniKeyboard_.setHeldNotes(processor_.heldNotesBitmaskLow.load(),
                                processor_.heldNotesBitmaskHigh.load());

    if (needsRepaint)
        repaint();
}

// ============================================================================
// Helpers
// ============================================================================

juce::String HarmonizerEditor::frequencyToNoteName(float freqHz)
{
    if (freqHz <= 0.0f)
        return "--";

    // Convert Hz to MIDI note number: A4 (440 Hz) = note 69
    float noteFloat = 69.0f + 12.0f * std::log2(freqHz / 440.0f);
    int noteNum = static_cast<int>(std::round(noteFloat));

    if (noteNum < 0 || noteNum > 127)
        return juce::String(freqHz, 1) + " Hz";

    int octave    = (noteNum / 12) - 1;
    int noteIndex = noteNum % 12;

    // Format: "C4 / 261.6 Hz"
    return juce::String(NOTE_NAMES[noteIndex]) + juce::String(octave)
           + " / " + juce::String(freqHz, 1) + " Hz";
}
