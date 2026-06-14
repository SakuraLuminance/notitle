#include "PluginEditor.h"
#include "dsp/PitchCorrector.h"
#include <cmath>

//==============================================================================
AnaPlugAudioProcessorEditor::AnaPlugAudioProcessorEditor(AnaPlugAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&ana::CyberpunkTheme::getInstance());
    setSize(1100, 780);
    setResizable(true, true);
    setResizeLimits(900, 660, 1920, 1200);

    //==============================================================================
    // Title bar
    titleLabel_.setText("ANAPLUG :: CYBER SYNTH", juce::dontSendNotification);
    titleLabel_.setFont(ana::CyberpunkTheme::getCyberFont(15.0f, true));
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    titleLabel_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::cyan_);
    addAndMakeVisible(titleLabel_);

    presetButton_.setButtonText("PRESET: DEFAULT");
    presetButton_.onClick = [this] { presetButtonClicked(); };
    addCyberButton(presetButton_);
    presetButton_.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    presetButton_.setColour(juce::TextButton::textColourOffId, ana::CyberpunkTheme::cyan_);
    addAndMakeVisible(presetButton_);

    //==============================================================================
    // Timbre A panel knobs
    addCyberKnob(aSubSlider_, aSubLabel_,    "SUB",     0.0, 1.0, 0.0, 0.01);
    addCyberKnob(aBrightSlider_, aBrightLabel_, "BRIGHT", 0.0, 1.0, 0.5, 0.01);
    addCyberKnob(aBlurSlider_, aBlurLabel_,  "BLUR",    0.0, 1.0, 0.0, 0.01);
    addCyberKnob(aHpfSlider_, aHpfLabel_,    "HPF",     20.0, 20000.0, 20.0, 1.0,
                 juce::Slider::LinearHorizontal);
    aHpfSlider_.setSkewFactor(0.3);
    aSubSlider_.onValueChange = [this]() {
        audioProcessor.setSubHarmonicLevel(static_cast<float>(aSubSlider_.getValue()));
    };

    //==============================================================================
    // Timbre B panel knobs
    addCyberKnob(bSubSlider_, bSubLabel_,    "SUB",     0.0, 1.0, 0.0, 0.01);
    addCyberKnob(bBrightSlider_, bBrightLabel_, "BRIGHT", 0.0, 1.0, 0.5, 0.01);
    addCyberKnob(bBlurSlider_, bBlurLabel_,  "BLUR",    0.0, 1.0, 0.0, 0.01);
    addCyberKnob(bHpfSlider_, bHpfLabel_,    "HPF",     20.0, 20000.0, 20.0, 1.0,
                 juce::Slider::LinearHorizontal);
    bHpfSlider_.setSkewFactor(0.3);
    bSubSlider_.onValueChange = [this]() {
        audioProcessor.getSubHarmonicGenerator().setSubLevel(1,
            static_cast<float>(bSubSlider_.getValue()));
    };

    // Timbre blend cross-fader
    addCyberKnob(timbreBlendSlider_, timbreBlendLabel_, "BLEND", 0.0, 1.0, 0.5, 0.01,
                 juce::Slider::LinearHorizontal);

    //==============================================================================
    // Center — Visual feedback + view selector
    addAndMakeVisible(feedbackPanel_);
    viewModeCombo_.addItem("PARTIALS", 1);
    viewModeCombo_.addItem("WATERFALL", 2);
    viewModeCombo_.addItem("EDITOR", 3);
    viewModeCombo_.setSelectedId(1);
    addAndMakeVisible(viewModeCombo_);

    //==============================================================================
    // Filter panel
    filterTitle_.setText("FILTER", juce::dontSendNotification);
    filterTitle_.setFont(ana::CyberpunkTheme::getCyberFont(11.0f, true));
    filterTitle_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::cyan_);
    addAndMakeVisible(filterTitle_);

    filterTypeCombo_.addItem("LP", 1); filterTypeCombo_.addItem("HP", 2);
    filterTypeCombo_.addItem("BP", 3); filterTypeCombo_.addItem("Notch", 4);
    filterTypeCombo_.addItem("Comb", 5);
    filterTypeCombo_.setSelectedId(1);
    addAndMakeVisible(filterTypeCombo_);

    filterCutoffSlider_.setRange(20.0, 20000.0, 1.0);
    filterCutoffSlider_.setValue(1000.0);
    filterCutoffSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    filterCutoffSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    filterCutoffSlider_.setSkewFactor(0.3);
    filterCutoffSlider_.setDoubleClickReturnValue(true, 1000.0);
    addAndMakeVisible(filterCutoffSlider_);

    filterResSlider_.setRange(0.0, 1.0, 0.01);
    filterResSlider_.setValue(0.3);
    filterResSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    filterResSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    filterResSlider_.setDoubleClickReturnValue(true, 0.3);
    addAndMakeVisible(filterResSlider_);

    addAndMakeVisible(filterViz_);

    //==============================================================================
    // Macros
    static const char* macroNames[] = { "M1", "M2", "M3", "M4" };
    for (int i = 0; i < 4; ++i)
    {
        addCyberKnob(macroSliders_[i], macroLabels_[i], macroNames[i], 0.0, 1.0, 0.0, 0.01);
    }

    //==============================================================================
    // Effects buttons
    addCyberButton(prismButton_);
    addCyberButton(blurButton_);
    addCyberButton(harmButton_);

    //==============================================================================
    // LFO section
    lfoTitle_.setText("LFO 1", juce::dontSendNotification);
    lfoTitle_.setFont(ana::CyberpunkTheme::getCyberFont(10.0f, true));
    lfoTitle_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::yellow_);
    addAndMakeVisible(lfoTitle_);

    lfoWaveCombo_.addItem("SINE", 1); lfoWaveCombo_.addItem("TRI", 2);
    lfoWaveCombo_.addItem("SAW", 3); lfoWaveCombo_.addItem("SQR", 4);
    lfoWaveCombo_.addItem("S&H", 5);
    lfoWaveCombo_.setSelectedId(1);
    addAndMakeVisible(lfoWaveCombo_);

    lfoRateSlider_.setRange(0.01, 20.0, 0.01);
    lfoRateSlider_.setValue(2.0);
    lfoRateSlider_.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    lfoRateSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    lfoRateSlider_.setDoubleClickReturnValue(true, 2.0);
    addAndMakeVisible(lfoRateSlider_);

    lfoDepthSlider_.setRange(0.0, 1.0, 0.01);
    lfoDepthSlider_.setValue(0.5);
    lfoDepthSlider_.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    lfoDepthSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    lfoDepthSlider_.setDoubleClickReturnValue(true, 0.5);
    addAndMakeVisible(lfoDepthSlider_);
    lfoTargetCombo_.addItem("CUTOFF", 1); lfoTargetCombo_.addItem("VOLUME", 2);
    lfoTargetCombo_.addItem("PAN", 3);    lfoTargetCombo_.addItem("PITCH", 4);
    lfoTargetCombo_.setSelectedId(1);
    addAndMakeVisible(lfoTargetCombo_);

    // Envelope section
    envTitle_.setText("ENV 1", juce::dontSendNotification);
    envTitle_.setFont(ana::CyberpunkTheme::getCyberFont(10.0f, true));
    envTitle_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::magenta_);
    addAndMakeVisible(envTitle_);

    envAttackSlider_.setRange(0.0, 5.0, 0.01);
    envAttackSlider_.setValue(0.01);
    envAttackSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    envAttackSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    envAttackSlider_.setDoubleClickReturnValue(true, 0.01);
    addAndMakeVisible(envAttackSlider_);

    envDecaySlider_.setRange(0.0, 5.0, 0.01);
    envDecaySlider_.setValue(0.5);
    envDecaySlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    envDecaySlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    envDecaySlider_.setDoubleClickReturnValue(true, 0.5);
    addAndMakeVisible(envDecaySlider_);

    envSustainSlider_.setRange(0.0, 1.0, 0.01);
    envSustainSlider_.setValue(0.7);
    envSustainSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    envSustainSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    envSustainSlider_.setDoubleClickReturnValue(true, 0.7);
    addAndMakeVisible(envSustainSlider_);

    envReleaseSlider_.setRange(0.0, 10.0, 0.01);
    envReleaseSlider_.setValue(1.0);
    envReleaseSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    envReleaseSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    envReleaseSlider_.setDoubleClickReturnValue(true, 1.0);
    addAndMakeVisible(envReleaseSlider_);
    envTargetCombo_.addItem("VOLUME", 1); envTargetCombo_.addItem("CUTOFF", 2);
    envTargetCombo_.addItem("BLUR", 3);   envTargetCombo_.addItem("PITCH", 4);
    envTargetCombo_.setSelectedId(1);
    addAndMakeVisible(envTargetCombo_);

    //==============================================================================
    // Unison
    unisonTitle_.setText("UNISON", juce::dontSendNotification);
    unisonTitle_.setFont(ana::CyberpunkTheme::getCyberFont(10.0f, true));
    unisonTitle_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::cyan_);
    addAndMakeVisible(unisonTitle_);
    addCyberKnob(unisonCountSlider_, unisonCountLabel_, "VOICES", 1, 8, 1, 1,
                 juce::Slider::RotaryVerticalDrag);
    addCyberKnob(unisonDetuneSlider_, unisonDetuneLabel_, "DETUNE", 0.0, 50.0, 5.0, 1.0,
                 juce::Slider::RotaryVerticalDrag);
    addCyberKnob(unisonSpreadSlider_, unisonSpreadLabel_, "SPREAD", 0.0, 100.0, 50.0, 1.0,
                 juce::Slider::RotaryVerticalDrag);

    // Arpeggiator
    arpTitle_.setText("ARP", juce::dontSendNotification);
    arpTitle_.setFont(ana::CyberpunkTheme::getCyberFont(10.0f, true));
    arpTitle_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::magenta_);
    addAndMakeVisible(arpTitle_);
    arpPatternCombo_.addItem("OFF", 1); arpPatternCombo_.addItem("UP", 2);
    arpPatternCombo_.addItem("DOWN", 3); arpPatternCombo_.addItem("UP/DOWN", 4);
    arpPatternCombo_.addItem("RANDOM", 5);
    arpPatternCombo_.setSelectedId(1);
    addAndMakeVisible(arpPatternCombo_);
    addCyberKnob(arpRateSlider_, arpRateLabel_, "RATE", 0.25, 4.0, 1.0, 0.25,
                 juce::Slider::RotaryVerticalDrag);
    addCyberKnob(arpGateSlider_, arpGateLabel_, "GATE", 0.01, 1.0, 0.5, 0.01,
                 juce::Slider::RotaryVerticalDrag);

    //==============================================================================
    // Transport / Sample controls
    addCyberButton(loadButton_);
    loadButton_.onClick = [this]() { loadButtonClicked(); };

    addCyberButton(playButton_);
    playButton_.onClick = [this]() { playButtonClicked(); };
    playButton_.setEnabled(false);

    addCyberButton(stopButton_);
    stopButton_.onClick = [this]() { stopButtonClicked(); };
    stopButton_.setEnabled(false);

    addCyberButton(flattenButton_);
    flattenButton_.onClick = [this]() { flattenButtonClicked(); };
    flattenButton_.setEnabled(false);

    addCyberKnob(rootNoteKnob_, rootNoteLabel_, "ROOT", 0.0, 127.0, 60.0, 1.0);
    rootNoteKnob_.textFromValueFunction = [](double v) { return midiNoteToName(static_cast<int>(v)); };
    rootNoteKnob_.setDoubleClickReturnValue(true, 60.0);
    rootNoteKnob_.onValueChange = [this]() {
        audioProcessor.setRootNote(static_cast<int>(rootNoteKnob_.getValue()));
        updatePitchDisplay(midiNoteToName(static_cast<int>(rootNoteKnob_.getValue())));
    };

    addCyberKnob(rootFineTuneKnob_, rootFineTuneLabel_, "FINE", -50.0, 50.0, 0.0, 1.0);
    rootFineTuneKnob_.setDoubleClickReturnValue(true, 0.0);
    rootFineTuneKnob_.onValueChange = [this]() {
        audioProcessor.setRootFineTune(static_cast<float>(rootFineTuneKnob_.getValue()));
    };

    pitchDetectLabel_.setText("--", juce::dontSendNotification);
    pitchDetectLabel_.setFont(ana::CyberpunkTheme::getCyberFont(10.0f, false));
    pitchDetectLabel_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::yellow_);
    addAndMakeVisible(pitchDetectLabel_);

    //==============================================================================
    // Master
    addCyberKnob(masterVolSlider_, masterVolLabel_, "VOL", 0.0, 2.0, 0.8, 0.01,
                 juce::Slider::RotaryVerticalDrag);
    addCyberKnob(masterPanSlider_, masterPanLabel_, "PAN", -1.0, 1.0, 0.0, 0.01,
                 juce::Slider::RotaryVerticalDrag);

    //==============================================================================
    // Status
    statusLabel_.setText(">> READY <<", juce::dontSendNotification);
    statusLabel_.setFont(ana::CyberpunkTheme::getCyberFont(10.0f, false));
    statusLabel_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::fg_.withAlpha(0.7f));
    addAndMakeVisible(statusLabel_);

    updateStatus();
    startTimerHz(30);
}

AnaPlugAudioProcessorEditor::~AnaPlugAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

//==============================================================================
void AnaPlugAudioProcessorEditor::computeRegions(juce::Rectangle<int> bounds, Regions& r) const
{
    int w = bounds.getWidth();
    (void)w;

    // Title bar at top
    r.titleBar = bounds.removeFromTop(28);

    // Main 3-column area (40% of remaining height)
    r.mainArea = bounds.removeFromTop(static_cast<int>(bounds.getHeight() * 0.42f));

    // Process panel (filter + macros + effects)
    r.processArea = bounds.removeFromTop(static_cast<int>(bounds.getHeight() * 0.32f));

    // Modulation panel
    r.modArea = bounds.removeFromTop(static_cast<int>(bounds.getHeight() * 0.38f));

    // Bottom strip (unison, arp, sample, master)
    r.bottomArea = bounds.removeFromTop(static_cast<int>(bounds.getHeight() * 0.65f));

    // Status bar
    r.statusBar = bounds;

    // Main area: A | center | B  (17% | 66% | 17%)
    auto mainW = r.mainArea.getWidth();
    r.timbreAPanel = r.mainArea.removeFromLeft(static_cast<int>(mainW * 0.17f));
    r.timbreBPanel = r.mainArea.removeFromRight(static_cast<int>(mainW * 0.17f));
    r.centerPanel = r.mainArea;
}

//==============================================================================
void AnaPlugAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    Regions r;
    computeRegions(bounds, r);

    // Background
    g.fillAll(ana::CyberpunkTheme::bg_);
    ana::CyberpunkTheme::drawGridBackground(g, getLocalBounds());

    // Draw panel borders with cyberpunk accents
    ana::CyberpunkTheme::drawPanelBorder(g, r.timbreAPanel, "TIMBRE A", ana::CyberpunkTheme::cyan_);
    ana::CyberpunkTheme::drawPanelBorder(g, r.timbreBPanel, "TIMBRE B", ana::CyberpunkTheme::magenta_);
    ana::CyberpunkTheme::drawPanelBorder(g, r.centerPanel, "SPECTRUM", ana::CyberpunkTheme::cyan_.withAlpha(0.3f));
    ana::CyberpunkTheme::drawPanelBorder(g, r.processArea, "PROCESS", ana::CyberpunkTheme::cyan_);
    ana::CyberpunkTheme::drawPanelBorder(g, r.modArea, "MODULATION", ana::CyberpunkTheme::yellow_);
    ana::CyberpunkTheme::drawPanelBorder(g, r.bottomArea, "CONTROLS", ana::CyberpunkTheme::magenta_);
    ana::CyberpunkTheme::drawPanelBorder(g, r.statusBar, "", ana::CyberpunkTheme::fg_.withAlpha(0.15f));

    // Title bar
    auto tb = r.titleBar;
    g.setColour(ana::CyberpunkTheme::bg_.brighter(0.04f));
    g.fillRect(tb);
    g.setColour(ana::CyberpunkTheme::cyan_.withAlpha(0.4f));
    g.drawHorizontalLine(tb.getBottom(), tb.getX(), tb.getRight());

    // Corner accent at title left
    float cornerLen = 12.0f;
    g.setColour(ana::CyberpunkTheme::cyan_);
    g.drawLine(static_cast<float>(tb.getX()), static_cast<float>(tb.getBottom()),
               static_cast<float>(tb.getX()) + cornerLen, static_cast<float>(tb.getBottom()));
    g.drawLine(static_cast<float>(tb.getX()), static_cast<float>(tb.getBottom()),
               static_cast<float>(tb.getX()), static_cast<float>(tb.getBottom()) - cornerLen);

    // Version on status bar
    g.setFont(ana::CyberpunkTheme::getCyberFont(8.0f, false));
    g.setColour(ana::CyberpunkTheme::fg_.withAlpha(0.3f));
    g.drawText("v3.0 | " + juce::String(getWidth()) + "x" + juce::String(getHeight()),
               r.statusBar, juce::Justification::centredRight);
}

//==============================================================================
void AnaPlugAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    Regions r;
    computeRegions(bounds, r);
    const int pad = 3;

    // -- Title bar --
    auto titleRect = r.titleBar.reduced(8, 0);
    titleLabel_.setBounds(titleRect.removeFromLeft(260));
    presetButton_.setBounds(titleRect.removeFromRight(150));

    // -- Timbre A (left panel) --
    auto ta = r.timbreAPanel.reduced(6, pad * 2);
    int knobSize = (ta.getWidth() - pad * 2) / 2;
    auto taRow = [&](juce::Slider& s, juce::Label& l) {
        auto cell = ta.removeFromTop(knobSize + 14).reduced(pad);
        s.setBounds(cell.removeFromTop(knobSize));
        l.setBounds(cell);
    };
    taRow(aSubSlider_, aSubLabel_);
    taRow(aBrightSlider_, aBrightLabel_);
    aHpfSlider_.setBounds(ta.removeFromTop(16).reduced(pad));
    aHpfLabel_.setBounds(ta.removeFromTop(12).reduced(pad));

    // -- Timbre B (right panel) —
    auto tb = r.timbreBPanel.reduced(6, pad * 2);
    auto tbRow = [&](juce::Slider& s, juce::Label& l) {
        auto cell = tb.removeFromTop(knobSize + 14).reduced(pad);
        s.setBounds(cell.removeFromTop(knobSize));
        l.setBounds(cell);
    };
    tbRow(bSubSlider_, bSubLabel_);
    tbRow(bBrightSlider_, bBrightLabel_);
    bHpfSlider_.setBounds(tb.removeFromTop(16).reduced(pad));
    bHpfLabel_.setBounds(tb.removeFromTop(12).reduced(pad));

    // Timbre blend at center-between
    auto blendArea = r.centerPanel.removeFromBottom(20).reduced(40, 0);
    timbreBlendSlider_.setBounds(blendArea);
    timbreBlendLabel_.setBounds(blendArea.translated(0, -16));

    // -- Center panel: view selector strip + feedback --
    auto centerArea = r.centerPanel.reduced(4, 4);
    viewModeCombo_.setBounds(centerArea.removeFromTop(18).reduced(centerArea.getWidth() / 2 - 80, 0));
    feedbackPanel_.setBounds(centerArea.reduced(2));

    // -- Process panel: FILTER (left) | MACROS (center) | EFFECTS (right) --
    auto pa = r.processArea.reduced(6, pad);

    // Filter section (30%)
    auto filterArea = pa.removeFromLeft(static_cast<int>(pa.getWidth() * 0.30f)).reduced(pad);
    filterTitle_.setBounds(filterArea.removeFromTop(14));
    filterTypeCombo_.setBounds(filterArea.removeFromTop(18).reduced(pad));
    filterCutoffSlider_.setBounds(filterArea.removeFromTop(16).reduced(pad));
    filterResSlider_.setBounds(filterArea.removeFromTop(16).reduced(pad));
    filterViz_.setBounds(filterArea.reduced(pad));

    // Macros section (35%)
    auto macroArea = pa.removeFromLeft(static_cast<int>(pa.getWidth() * 0.55f)).reduced(pad);
    auto macroTitle = macroArea.removeFromTop(14);
    auto mkArea = macroArea.reduced(pad);
    int mkW = mkArea.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = mkArea.removeFromLeft(mkW).reduced(2);
        macroSliders_[i].setBounds(cell.removeFromTop(cell.getWidth()));
        macroLabels_[i].setBounds(cell);
    }

    // Effects section (right remainder)
    auto fxArea = pa.reduced(pad);
    auto fxTitle = fxArea.removeFromTop(14);
    prismButton_.setBounds(fxArea.removeFromTop(20).reduced(pad));
    blurButton_.setBounds(fxArea.removeFromTop(20).reduced(pad));
    harmButton_.setBounds(fxArea.removeFromTop(20).reduced(pad));

    // -- Modulation panel: LFO (left) | ENVELOPE (right) --
    auto ma = r.modArea.reduced(6, pad);
    auto lfoArea = ma.removeFromLeft(static_cast<int>(ma.getWidth() * 0.48f)).reduced(pad);
    auto envArea = ma.reduced(pad);

    // LFO layout
    lfoTitle_.setBounds(lfoArea.removeFromTop(14));
    lfoWaveCombo_.setBounds(lfoArea.removeFromTop(18).reduced(pad));
    auto lfoKnobs = lfoArea.removeFromTop(50).reduced(pad);
    int lfoKW = lfoKnobs.getWidth() / 2;
    lfoRateSlider_.setBounds(lfoKnobs.removeFromLeft(lfoKW).reduced(2));
    lfoDepthSlider_.setBounds(lfoKnobs.reduced(2));
    lfoTargetCombo_.setBounds(lfoArea.removeFromTop(18).reduced(pad));

    // Envelope layout
    envTitle_.setBounds(envArea.removeFromTop(14));
    envTargetCombo_.setBounds(envArea.removeFromTop(18).reduced(pad));
    auto envSliders = envArea.reduced(pad);
    int envSW = envSliders.getWidth() / 4;
    envAttackSlider_.setBounds(envSliders.removeFromLeft(envSW).reduced(2));
    envDecaySlider_.setBounds(envSliders.removeFromLeft(envSW).reduced(2));
    envSustainSlider_.setBounds(envSliders.removeFromLeft(envSW).reduced(2));
    envReleaseSlider_.setBounds(envSliders.reduced(2));

    // -- Bottom controls: UNISON | ARP | SAMPLE | MASTER --
    auto ba = r.bottomArea.reduced(6, pad);
    auto uniArea = ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.22f)).reduced(pad);
    auto arpArea = ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.22f)).reduced(pad);
    auto smpArea = ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.38f)).reduced(pad);
    auto mstArea = ba.reduced(pad);

    // Unison
    unisonTitle_.setBounds(uniArea.removeFromTop(14));
    auto uniKnobs = uniArea.reduced(pad);
    int ukW = uniKnobs.getWidth() / 3;
    
    auto uniCell1 = uniKnobs.removeFromLeft(ukW).reduced(2);
    unisonCountSlider_.setBounds(uniCell1.removeFromTop(uniCell1.getWidth()));
    unisonCountLabel_.setBounds(uniCell1);
    
    auto uniCell2 = uniKnobs.removeFromLeft(ukW).reduced(2);
    unisonDetuneSlider_.setBounds(uniCell2.removeFromTop(uniCell2.getWidth()));
    unisonDetuneLabel_.setBounds(uniCell2);
    
    auto uniCell3 = uniKnobs.reduced(2);
    unisonSpreadSlider_.setBounds(uniCell3.removeFromTop(uniCell3.getWidth()));
    unisonSpreadLabel_.setBounds(uniCell3);

    // Arp
    arpTitle_.setBounds(arpArea.removeFromTop(14));
    arpPatternCombo_.setBounds(arpArea.removeFromTop(18).reduced(pad));
    auto arpKnobs = arpArea.reduced(pad);
    int akW = arpKnobs.getWidth() / 2;
    
    auto arpCell1 = arpKnobs.removeFromLeft(akW).reduced(2);
    arpRateSlider_.setBounds(arpCell1.removeFromTop(arpCell1.getWidth()));
    arpRateLabel_.setBounds(arpCell1);
    
    auto arpCell2 = arpKnobs.reduced(2);
    arpGateSlider_.setBounds(arpCell2.removeFromTop(arpCell2.getWidth()));
    arpGateLabel_.setBounds(arpCell2);

    // Sample / transport
    auto smpTop = smpArea.removeFromTop(22).reduced(pad);
    int btnW = smpTop.getWidth() / 4;
    auto loadRect = smpTop.removeFromLeft(btnW).reduced(pad);
    loadButton_.setBounds(loadRect);
    auto playRect = smpTop.removeFromLeft(btnW).reduced(1, pad);
    playButton_.setBounds(playRect);
    auto stopRect = smpTop.removeFromLeft(btnW).reduced(1, pad);
    stopButton_.setBounds(stopRect);
    auto flattenRect = smpTop.reduced(pad);
    flattenButton_.setBounds(flattenRect);

    pitchDetectLabel_.setBounds(smpArea.removeFromTop(14).reduced(pad));
    auto rootArea = smpArea.reduced(pad);
    int rkW = rootArea.getWidth() / 2;
    
    auto rootCell1 = rootArea.removeFromLeft(rkW).reduced(2);
    rootNoteKnob_.setBounds(rootCell1.removeFromTop(rootCell1.getWidth()));
    rootNoteLabel_.setBounds(rootCell1);
    
    auto rootCell2 = rootArea.reduced(2);
    rootFineTuneKnob_.setBounds(rootCell2.removeFromTop(rootCell2.getWidth()));
    rootFineTuneLabel_.setBounds(rootCell2);

    // Master
    mstArea.removeFromTop(4);
    masterVolSlider_.setBounds(mstArea.removeFromTop(50).reduced(8, 0));
    masterPanSlider_.setBounds(mstArea.removeFromTop(50).reduced(8, 0));

    // -- Status bar --
    statusLabel_.setBounds(r.statusBar.reduced(10, 2));
}

//==============================================================================
void AnaPlugAudioProcessorEditor::timerCallback()
{
    if (audioProcessor.isEngineLoaded())
    {
        int pos = audioProcessor.getPlaybackPosition();
        const auto& engine = audioProcessor.getEngine();
        const auto& partialData = engine.getPartialData();
        if (!partialData.frames.empty() && engine.getAudioData().sampleRate > 0)
        {
            double currentTime = static_cast<double>(pos) / engine.getAudioData().sampleRate;
            size_t bestFrame = 0;
            double bestDiff = std::abs(partialData.frames[0].timestamp - currentTime);
            for (size_t i = 0; i < partialData.frames.size(); ++i)
            {
                double diff = std::abs(partialData.frames[i].timestamp - currentTime);
                if (diff < bestDiff) { bestDiff = diff; bestFrame = i; }
            }
            // Update visual feedback panel
            ana::PartialDataSIMD simd = ana::PartialDataSIMD::fromPartialData(partialData);
            feedbackPanel_.updatePartials(simd);
        }
    }
    if (audioProcessor.flattenPending())
        statusLabel_.setText(">> PITCH FLATTENING <<", juce::dontSendNotification);
}

//==============================================================================
void AnaPlugAudioProcessorEditor::loadButtonClicked()
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Select a WAV file", juce::File{}, "*.wav");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                bool success = audioProcessor.loadFile(file);
                if (success)
                {
                    loadedFileName_ = file.getFileName();
                    auto& engine = audioProcessor.getEngine();
                    playButton_.setEnabled(true);
                    flattenButton_.setEnabled(true);

                    ana::PitchCorrector pitchDetector;
                    const auto& audioData = engine.getAudioData();
                    float detectedNote = pitchDetector.detectPitch(audioData.samples, audioData.sampleRate);
                    if (detectedNote >= 0.5f)
                    {
                        int midiNote = static_cast<int>(std::round(detectedNote));
                        audioProcessor.setRootNote(midiNote);
                        rootNoteKnob_.setValue(static_cast<double>(midiNote), juce::dontSendNotification);
                        updatePitchDisplay(midiNoteToName(midiNote));
                    }
                    else updatePitchDisplay(midiNoteToName(60));
                    updateStatus();
                }
                else statusLabel_.setText(">> FAILED TO LOAD <<", juce::dontSendNotification);
            }
        });
}

void AnaPlugAudioProcessorEditor::playButtonClicked()
{
    audioProcessor.startPlayback();
    stopButton_.setEnabled(true);
    playButton_.setEnabled(false);
}

void AnaPlugAudioProcessorEditor::stopButtonClicked()
{
    audioProcessor.stopPlayback();
    playButton_.setEnabled(true);
    stopButton_.setEnabled(false);
}

void AnaPlugAudioProcessorEditor::flattenButtonClicked()
{
    audioProcessor.triggerFlattenPitch();
    statusLabel_.setText(">> FLATTENING... <<", juce::dontSendNotification);
}

void AnaPlugAudioProcessorEditor::presetButtonClicked()
{
    auto* browser = new ana::PresetBrowserPanel(audioProcessor.getPresetManager());
    browser->setSize(400, 500);
    
    // When a preset is loaded, update the button text and dismiss the box
    browser->onPresetLoaded = [this, browser]()
    {
        auto presetName = audioProcessor.getPresetManager().getCurrentPresetName();
        presetButton_.setButtonText("PRESET: " + (presetName.isEmpty() ? "DEFAULT" : presetName));
        if (auto* callout = browser->findParentComponentOfClass<juce::CallOutBox>())
            callout->exitModalState(1);
    };

    juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(browser),
                                           presetButton_.getScreenBounds(),
                                           this);
}

void AnaPlugAudioProcessorEditor::updateStatus()
{
    if (!audioProcessor.isEngineLoaded())
    {
        statusLabel_.setText(">> NO FILE LOADED <<", juce::dontSendNotification);
        flattenButton_.setEnabled(false);
        return;
    }
    flattenButton_.setEnabled(true);
    const auto& engine = audioProcessor.getEngine();
    const auto& audioData = engine.getAudioData();
    double dur = static_cast<double>(audioData.samples.size()) / audioData.sampleRate;
    int partialCount = audioProcessor.getPartialCount();
    juce::String s = ">> FILE: " + loadedFileName_
        + "  |  " + juce::String(audioData.sampleRate, 0) + "Hz"
        + "  |  " + juce::String(dur, 2) + "s"
        + "  |  " + juce::String(partialCount) + " partials <<";
    statusLabel_.setText(s, juce::dontSendNotification);
}

void AnaPlugAudioProcessorEditor::updatePitchDisplay(const juce::String& text)
{
    float freq = 440.0f * std::pow(2.0f, (audioProcessor.getRootNote() - 69) / 12.0f);
    pitchDetectLabel_.setText(text + " " + juce::String(freq, 1) + "Hz",
                              juce::dontSendNotification);
}

//==============================================================================
void AnaPlugAudioProcessorEditor::addCyberKnob(juce::Slider& slider, juce::Label& label,
                                                const juce::String& name,
                                                double min, double max,
                                                double init, double step,
                                                juce::Slider::SliderStyle style)
{
    slider.setRange(min, max, step);
    slider.setValue(init);
    slider.setSliderStyle(style);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setDoubleClickReturnValue(true, init);
    addAndMakeVisible(slider);

    label.setText(name, juce::dontSendNotification);
    label.setFont(ana::CyberpunkTheme::getCyberFont(9.0f, false));
    label.setColour(juce::Label::textColourId, ana::CyberpunkTheme::fg_.withAlpha(0.8f));
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

juce::TextButton& AnaPlugAudioProcessorEditor::addCyberButton(juce::TextButton& btn)
{
    addAndMakeVisible(btn);
    btn.setColour(juce::TextButton::buttonColourId, ana::CyberpunkTheme::cyan_.darker(0.7f));
    btn.setColour(juce::TextButton::textColourOffId, ana::CyberpunkTheme::fg_);
    return btn;
}

//==============================================================================
juce::String AnaPlugAudioProcessorEditor::midiNoteToName(int note)
{
    note = juce::jlimit(0, 127, note);
    static const char* nn[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return juce::String(nn[note % 12]) + juce::String(note / 12 - 1);
}
