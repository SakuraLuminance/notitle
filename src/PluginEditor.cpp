#include "PluginEditor.h"
#include "dsp/PitchCorrector.h"
#include <cmath>

//==============================================================================
// StepSequencer UI — StepCell implementation
//==============================================================================
AnaPlugAudioProcessorEditor::StepCell::StepCell(int index, AnaPlugAudioProcessor& p)
    : index_(index), processor_(p)
{
    // Gate toggle button
    gateButton_.setToggleState(true, juce::dontSendNotification);
    gateButton_.setColour(juce::ToggleButton::tickColourId, ana::CyberpunkTheme::cyan_);
    gateButton_.setColour(juce::ToggleButton::tickDisabledColourId, ana::CyberpunkTheme::fg_.withAlpha(0.3f));
    gateButton_.onClick = [this]()
    {
        if (onGateChanged)
            onGateChanged(index_, gateButton_.getToggleState());
    };
    addAndMakeVisible(gateButton_);

    // Value slider
    valueSlider_.setRange(0.0, 1.0, 0.01);
    valueSlider_.setValue(static_cast<double>(index_) / 15.0);
    valueSlider_.setSliderStyle(juce::Slider::LinearVertical);
    valueSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    valueSlider_.setColour(juce::Slider::thumbColourId, ana::CyberpunkTheme::magenta_);
    valueSlider_.setColour(juce::Slider::trackColourId, ana::CyberpunkTheme::magenta_.withAlpha(0.5f));
    valueSlider_.setColour(juce::Slider::backgroundColourId, ana::CyberpunkTheme::bg_.brighter(0.15f));
    valueSlider_.onValueChange = [this]()
    {
        if (onValueChanged)
            onValueChanged(index_, static_cast<float>(valueSlider_.getValue()));
    };
    addAndMakeVisible(valueSlider_);
}

void AnaPlugAudioProcessorEditor::StepCell::resized()
{
    auto b = getLocalBounds();
    // Gate button at top 40%
    auto gateArea = b.removeFromTop(static_cast<int>(b.getHeight() * 0.4f));
    gateButton_.setBounds(gateArea.reduced(2, 0));
    // Value slider fills the rest
    valueSlider_.setBounds(b.reduced(2, 0));
}

void AnaPlugAudioProcessorEditor::StepCell::paint(juce::Graphics& g)
{
    // Highlight background if this step is the current one
    auto& seq = processor_.getStepSequencer();
    if (seq.getCurrentStep() == index_)
    {
        g.setColour(ana::CyberpunkTheme::cyan_.withAlpha(0.15f));
        g.fillRect(getLocalBounds().reduced(1));
    }

    // Border
    g.setColour(ana::CyberpunkTheme::fg_.withAlpha(0.1f));
    g.drawRect(getLocalBounds(), 1);
}

void AnaPlugAudioProcessorEditor::StepCell::setActive(bool active)
{
    gateButton_.setToggleState(active, juce::dontSendNotification);
}

void AnaPlugAudioProcessorEditor::StepCell::setValue(float val)
{
    valueSlider_.setValue(static_cast<double>(val), juce::dontSendNotification);
}

//==============================================================================
AnaPlugAudioProcessorEditor::AnaPlugAudioProcessorEditor(AnaPlugAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), meteringPanel_(p), modPanel_(p), effectRack_(p)
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
    presetButton_.setTooltip("Open preset browser");
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
    aSubSlider_.setTooltip("Sub level (0-100%)");
    aBrightSlider_.setTooltip("Brightness (0-100%)");
    aBlurSlider_.setTooltip("Blur amount (0-100%)");
    aHpfSlider_.setTooltip("HPF cutoff (20-20000Hz)");
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
    bSubSlider_.setTooltip("Sub level (0-100%)");
    bBrightSlider_.setTooltip("Brightness (0-100%)");
    bBlurSlider_.setTooltip("Blur amount (0-100%)");
    bHpfSlider_.setTooltip("HPF cutoff (20-20000Hz)");
    bSubSlider_.onValueChange = [this]() {
        audioProcessor.getSubHarmonicGenerator().setSubLevel(1,
            static_cast<float>(bSubSlider_.getValue()));
    };

    // Timbre blend cross-fader
    addCyberKnob(timbreBlendSlider_, timbreBlendLabel_, "BLEND", 0.0, 1.0, 0.5, 0.01,
                 juce::Slider::LinearHorizontal);
    timbreBlendSlider_.setTooltip("A/B timbre blend (0-100%)");

    //==============================================================================
    // Center — Visual feedback + view selector
    addAndMakeVisible(feedbackPanel_);
    addAndMakeVisible(spectrumEditorCanvas_);
    spectrumEditorCanvas_.setVisible(false);
    viewModeCombo_.addItem("PARTIALS", 1);
    viewModeCombo_.addItem("WATERFALL", 2);
    viewModeCombo_.addItem("EDITOR", 3);
    viewModeCombo_.addItem("3D", 4);
    viewModeCombo_.setSelectedId(1);
    viewModeCombo_.onChange = [this] { onViewModeChanged(); };
    viewModeCombo_.setTooltip("View mode: PARTIALS/WATERFALL/EDITOR/3D");
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
    filterTypeCombo_.setTooltip("Filter type: LP/HP/BP/Notch/Comb");
    addAndMakeVisible(filterTypeCombo_);

    filterCutoffSlider_.setRange(20.0, 20000.0, 1.0);
    filterCutoffSlider_.setValue(1000.0);
    filterCutoffSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    filterCutoffSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    filterCutoffSlider_.setSkewFactor(0.3);
    filterCutoffSlider_.setDoubleClickReturnValue(true, 1000.0);
    filterCutoffSlider_.setTooltip("Filter cutoff frequency (20-20000Hz)");
    addAndMakeVisible(filterCutoffSlider_);

    filterResSlider_.setRange(0.0, 1.0, 0.01);
    filterResSlider_.setValue(0.3);
    filterResSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    filterResSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    filterResSlider_.setDoubleClickReturnValue(true, 0.3);
    filterResSlider_.setTooltip("Filter resonance (0-100%)");
    addAndMakeVisible(filterResSlider_);

    addAndMakeVisible(filterViz_);

    // Wire filter controls to MultiFilter
    filterTypeCombo_.onChange = [this]() {
        auto& slot = audioProcessor.getMultiFilter().getSlot(0);
        switch (filterTypeCombo_.getSelectedId())
        {
            case 1: slot.type = ana::FilterType::LowPass;   break;
            case 2: slot.type = ana::FilterType::HighPass;  break;
            case 3: slot.type = ana::FilterType::BandPass;  break;
            case 4: slot.type = ana::FilterType::Notch;     break;
            case 5: slot.type = ana::FilterType::Comb;      break;
            default: slot.type = ana::FilterType::LowPass;  break;
        }
        audioProcessor.getMultiFilter().markCoefficientsDirty();
    };
    filterCutoffSlider_.onValueChange = [this]() {
        audioProcessor.getMultiFilter().getSlot(0).params.cutoff
            = filterCutoffSlider_.getValue();
        audioProcessor.getMultiFilter().markCoefficientsDirty();
    };
    filterResSlider_.onValueChange = [this]() {
        audioProcessor.getMultiFilter().getSlot(0).params.resonance
            = static_cast<float>(filterResSlider_.getValue());
        audioProcessor.getMultiFilter().markCoefficientsDirty();
    };

    //==============================================================================
    // Macros — wired to MacroController with visual curve feedback
    static const char* macroNames[] = { "M1", "M2", "M3", "M4" };
    for (int i = 0; i < 4; ++i)
    {
        macroSliders_[i].setRange(0.0, 1.0, 0.01);
        macroSliders_[i].setValue(0.0);
        macroSliders_[i].setDoubleClickReturnValue(true, 0.0);
        macroSliders_[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        macroSliders_[i].setTooltip("Macro " + juce::String(i + 1) + " (0-100%)");
        addAndMakeVisible(macroSliders_[i]);

        macroLabels_[i].setText(macroNames[i], juce::dontSendNotification);
        macroLabels_[i].setFont(ana::CyberpunkTheme::getCyberFont(9.0f, false));
        macroLabels_[i].setColour(juce::Label::textColourId,
                                  ana::CyberpunkTheme::fg_.withAlpha(0.8f));
        macroLabels_[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(macroLabels_[i]);

        // Push slider changes to MacroController
        const int idx = i;
        macroSliders_[i].onValueChange = [this, idx]()
        {
            audioProcessor.getMacroController().setMacroValue(
                idx, static_cast<float>(macroSliders_[idx].getValue()));
        };
    }

    //==============================================================================
    // XY Pad — morph control with smooth interpolation + MIDI Learn
    xyPad_ = std::make_unique<ana::XYPad>(audioProcessor);
    xyPad_->setXParameter(&audioProcessor.getMorphAmountRef(), "MORPH");
    addAndMakeVisible(xyPad_.get());

    //==============================================================================
    // Effect preset combo box
    fxPresetLabel_.setText("FX PRESET", juce::dontSendNotification);
    fxPresetLabel_.setFont(ana::CyberpunkTheme::getCyberFont(10.0f, true));
    fxPresetLabel_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::cyan_);
    addAndMakeVisible(fxPresetLabel_);

    effectPresetCombo_.setTextWhenNothingSelected("Default");
    effectPresetCombo_.setTooltip("Effect preset: Save As... to store current FX state");
    populateEffectPresets();
    effectPresetCombo_.onChange = [this] { onEffectPresetSelected(); };
    effectPresetCombo_.addMouseListener(this, false);
    addAndMakeVisible(effectPresetCombo_);

    //==============================================================================
    // Spectral effects buttons (Prism / Blur / Harmonizer)
    // Each toggles its corresponding atomic flag on the processor and
    // updates its visual state (bright cyan when active, dim when off).
    auto setupSpectralButton = [this](juce::TextButton& btn, auto setEnabled, auto isEnabled)
    {
        addCyberButton(btn);
        btn.setClickingTogglesState(true);
        btn.onClick = [this, &btn, setEnabled, isEnabled]()
        {
            const bool active = !isEnabled();
            setEnabled(active);
            btn.setToggleState(active, juce::dontSendNotification);
            if (active)
            {
                btn.setColour(juce::TextButton::buttonOnColourId, ana::CyberpunkTheme::cyan_);
                btn.setColour(juce::TextButton::textColourOnId, ana::CyberpunkTheme::bg_);
            }
            else
            {
                btn.setColour(juce::TextButton::buttonOnColourId, ana::CyberpunkTheme::cyan_.darker(0.7f));
                btn.setColour(juce::TextButton::textColourOnId, ana::CyberpunkTheme::fg_);
            }
        };
        btn.setToggleState(false, juce::dontSendNotification);
    };

    setupSpectralButton(prismButton_,
        [this](bool e) { audioProcessor.setPrismEnabled(e); },
        [this]() { return audioProcessor.isPrismEnabled(); });
    prismButton_.setTooltip("Toggle Prism spectral effect");
    setupSpectralButton(blurButton_,
        [this](bool e) { audioProcessor.setBlurEnabled(e); },
        [this]() { return audioProcessor.isBlurEnabled(); });
    blurButton_.setTooltip("Toggle Blur spectral effect");
    setupSpectralButton(harmButton_,
        [this](bool e) { audioProcessor.setHarmEnabled(e); },
        [this]() { return audioProcessor.isHarmEnabled(); });
    harmButton_.setTooltip("Toggle Harmonizer spectral effect");

    //==============================================================================
    // Vocal Character mode selector
    vocalCharacterLabel_.setText("VOICE", juce::dontSendNotification);
    vocalCharacterLabel_.setFont(ana::CyberpunkTheme::getCyberFont(9.0f, true));
    vocalCharacterLabel_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::magenta_);
    addAndMakeVisible(vocalCharacterLabel_);

    vocalCharacterCombo_.setTextWhenNothingSelected("Chest");
    vocalCharacterCombo_.setTooltip("Vocal character mode preset");
    // Populate with all 7 modes
    for (int i = 0; i < ana::VocalProcessor::getNumModes(); ++i)
    {
        auto mode = static_cast<ana::VocalCharacter>(i);
        vocalCharacterCombo_.addItem(
            juce::String(ana::VocalProcessor::getModeName(mode)), i + 1);
    }
    vocalCharacterCombo_.setSelectedId(1);  // Chest
    vocalCharacterCombo_.onChange = [this]()
    {
        const int id = vocalCharacterCombo_.getSelectedId();
        if (id >= 1 && id <= ana::VocalProcessor::getNumModes())
        {
            auto mode = static_cast<ana::VocalCharacter>(id - 1);
            audioProcessor.getVocalProcessor().applyMode(mode);
        }
    };
    // Cyberpunk styling
    vocalCharacterCombo_.setColour(juce::ComboBox::backgroundColourId,
                                   ana::CyberpunkTheme::bg_.brighter(0.15f));
    vocalCharacterCombo_.setColour(juce::ComboBox::textColourId,
                                   ana::CyberpunkTheme::magenta_);
    vocalCharacterCombo_.setColour(juce::ComboBox::arrowColourId,
                                   ana::CyberpunkTheme::magenta_.withAlpha(0.7f));
    vocalCharacterCombo_.setColour(juce::ComboBox::outlineColourId,
                                   ana::CyberpunkTheme::magenta_.withAlpha(0.3f));
    vocalCharacterCombo_.setColour(juce::ComboBox::buttonColourId,
                                   ana::CyberpunkTheme::magenta_.darker(0.3f));
    addAndMakeVisible(vocalCharacterCombo_);

    //==============================================================================
    // Dynamic Effect Rack — replaces the old hardcoded effect slider stack
    addAndMakeVisible(effectRack_);

    //==============================================================================
    // Modulation assignment panel (replaces old LFO/Envelope + Vol ADSR + ModSrc)
    modPanel_.setSize(300, modPanel_.calcContentHeight());
    modViewport_.setViewedComponent(&modPanel_, false);
    modViewport_.setScrollBarsShown(true, false);
    modViewport_.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,
        ana::CyberpunkTheme::cyan_.withAlpha(0.5f));
    modViewport_.getVerticalScrollBar().setColour(juce::ScrollBar::trackColourId,
        ana::CyberpunkTheme::bg_.brighter(0.1f));
    addAndMakeVisible(modViewport_);

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
    unisonCountSlider_.setTooltip("Number of unison voices (1-8)");
    unisonDetuneSlider_.setTooltip("Detune amount (0-50 cents)");
    unisonSpreadSlider_.setTooltip("Stereo spread (0-100%)");
    unisonCountSlider_.onValueChange = [this]() {
        audioProcessor.getUnisonEngine().setVoiceCount(
            static_cast<int>(unisonCountSlider_.getValue()));
    };
    unisonDetuneSlider_.onValueChange = [this]() {
        audioProcessor.getUnisonEngine().setDetune(
            static_cast<float>(unisonDetuneSlider_.getValue()));
    };
    unisonSpreadSlider_.onValueChange = [this]() {
        audioProcessor.getUnisonEngine().setStereoSpread(
            static_cast<float>(unisonSpreadSlider_.getValue()));
    };

    //==============================================================================
    // Voice mode / Portamento
    voiceTitle_.setText("VOICE", juce::dontSendNotification);
    voiceTitle_.setFont(ana::CyberpunkTheme::getCyberFont(10.0f, true));
    voiceTitle_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::cyan_);
    addAndMakeVisible(voiceTitle_);

    voiceModeCombo_.addItem("POLY",    1);
    voiceModeCombo_.addItem("MONO",    2);
    voiceModeCombo_.addItem("LEGATO",  3);
    voiceModeCombo_.setSelectedId(1);
    voiceModeCombo_.onChange = [this]()
    {
        int id = voiceModeCombo_.getSelectedId();
        ana::VoiceMode mode = (id == 1) ? ana::VoiceMode::Poly
                            : (id == 2) ? ana::VoiceMode::Mono
                            :               ana::VoiceMode::Legato;
        audioProcessor.getVoiceManager().setVoiceMode(mode);
    };
    voiceModeCombo_.setTooltip("Voice mode: POLY/MONO/LEGATO");
    addAndMakeVisible(voiceModeCombo_);

    addCyberKnob(portamentoTimeSlider_, portamentoTimeLabel_, "PORT", 0.0, 2.0, 0.0, 0.01,
                 juce::Slider::RotaryVerticalDrag);
    portamentoTimeSlider_.setTooltip("Portamento time (0-2s)");
    portamentoTimeSlider_.onValueChange = [this]()
    {
        audioProcessor.getVoiceManager().setPortamentoTime(
            static_cast<float>(portamentoTimeSlider_.getValue()));
    };

    portamentoCurveCombo_.addItem("LIN",   1);
    portamentoCurveCombo_.addItem("EXP",   2);
    portamentoCurveCombo_.addItem("LOG",   3);
    portamentoCurveCombo_.setSelectedId(1);
    portamentoCurveCombo_.onChange = [this]()
    {
        int id = portamentoCurveCombo_.getSelectedId();
        ana::PortamentoCurve curve = (id == 1) ? ana::PortamentoCurve::Linear
                                   : (id == 2) ? ana::PortamentoCurve::Exponential
                                   :               ana::PortamentoCurve::Logarithmic;
        audioProcessor.getVoiceManager().setPortamentoCurve(curve);
    };
    portamentoCurveCombo_.setTooltip("Portamento curve: LIN/EXP/LOG");
    addAndMakeVisible(portamentoCurveCombo_);

    // Arpeggiator
    arpTitle_.setText("ARP", juce::dontSendNotification);
    arpTitle_.setFont(ana::CyberpunkTheme::getCyberFont(10.0f, true));
    arpTitle_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::magenta_);
    addAndMakeVisible(arpTitle_);
    arpPatternCombo_.addItem("OFF", 1); arpPatternCombo_.addItem("UP", 2);
    arpPatternCombo_.addItem("DOWN", 3); arpPatternCombo_.addItem("UP/DOWN", 4);
    arpPatternCombo_.addItem("RANDOM", 5);
    arpPatternCombo_.setSelectedId(1);
    arpPatternCombo_.setTooltip("Arpeggiator pattern: OFF/UP/DOWN/UP-DOWN/RANDOM");
    addAndMakeVisible(arpPatternCombo_);
    addCyberKnob(arpRateSlider_, arpRateLabel_, "RATE", 0.25, 4.0, 1.0, 0.25,
                 juce::Slider::RotaryVerticalDrag);
    arpRateSlider_.setTooltip("Arpeggiator rate (0.25-4.0x)");
    addCyberKnob(arpGateSlider_, arpGateLabel_, "GATE", 0.01, 1.0, 0.5, 0.01,
                 juce::Slider::RotaryVerticalDrag);
    arpGateSlider_.setTooltip("Gate length (0.01-1.0)");

    //==============================================================================
    // Step Sequencer
    seqTitle_.setText("SEQ", juce::dontSendNotification);
    seqTitle_.setFont(ana::CyberpunkTheme::getCyberFont(10.0f, true));
    seqTitle_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::yellow_);
    addAndMakeVisible(seqTitle_);

    seqPlayModeCombo_.addItem("FWD",  1);
    seqPlayModeCombo_.addItem("BWD",  2);
    seqPlayModeCombo_.addItem("P-P",  3);
    seqPlayModeCombo_.addItem("RND",  4);
    seqPlayModeCombo_.setSelectedId(1);
    seqPlayModeCombo_.onChange = [this]()
    {
        auto& seq = audioProcessor.getStepSequencer();
        switch (seqPlayModeCombo_.getSelectedId())
        {
            case 1: seq.setPlayMode(ana::SeqPlayMode::Forward);  break;
            case 2: seq.setPlayMode(ana::SeqPlayMode::Backward); break;
            case 3: seq.setPlayMode(ana::SeqPlayMode::PingPong); break;
            case 4: seq.setPlayMode(ana::SeqPlayMode::Random);   break;
        }
    };
    seqPlayModeCombo_.setTooltip("Sequencer play mode: FWD/BWD/PING-PONG/RANDOM");
    addAndMakeVisible(seqPlayModeCombo_);

    seqClockSourceCombo_.addItem("INT", 1);
    seqClockSourceCombo_.addItem("EXT", 2);
    seqClockSourceCombo_.setSelectedId(1);
    seqClockSourceCombo_.onChange = [this]()
    {
        auto& seq = audioProcessor.getStepSequencer();
        seq.setClockSource(seqClockSourceCombo_.getSelectedId() == 1
            ? ana::SeqClockSource::Internal
            : ana::SeqClockSource::External);
    };
    seqClockSourceCombo_.setTooltip("Sequencer clock: INT/EXT");
    addAndMakeVisible(seqClockSourceCombo_);

    addCyberKnob(seqBpmSlider_, seqBpmLabel_, "BPM", 20.0, 300.0, 120.0, 1.0,
                 juce::Slider::RotaryVerticalDrag);
    seqBpmSlider_.setTooltip("Sequencer BPM (20-300)");
    seqBpmSlider_.onValueChange = [this]()
    {
        audioProcessor.getStepSequencer().setBpm(seqBpmSlider_.getValue());
    };

    addCyberKnob(seqRateSlider_, seqRateLabel_, "RATE", 0.125, 4.0, 0.25, 0.125,
                 juce::Slider::RotaryVerticalDrag);
    seqRateSlider_.setTooltip("Sequencer rate (0.125-4.0 beats)");
    seqRateSlider_.onValueChange = [this]()
    {
        audioProcessor.getStepSequencer().setRateBeats(
            static_cast<float>(seqRateSlider_.getValue()));
    };

    // Current step indicator
    seqCurrentStepLabel_.setText("STEP: 0", juce::dontSendNotification);
    seqCurrentStepLabel_.setFont(ana::CyberpunkTheme::getCyberFont(9.0f, false));
    seqCurrentStepLabel_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::cyan_);
    seqCurrentStepLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(seqCurrentStepLabel_);

    // 16 step cells
    for (int i = 0; i < 16; ++i)
    {
        auto cell = std::make_unique<StepCell>(i, audioProcessor);
        const int idx = i;

        cell->onGateChanged = [this, idx](int index, bool active)
        {
            audioProcessor.getStepSequencer().setStep(
                index, active,
                audioProcessor.getStepSequencer().getStep(index).value);
        };

        cell->onValueChanged = [this, idx](int index, float value)
        {
            audioProcessor.getStepSequencer().setStep(
                index,
                audioProcessor.getStepSequencer().getStep(index).active,
                value);
        };

        addAndMakeVisible(cell.get());
        stepCells_[i] = std::move(cell);
    }

    //==============================================================================
    // Transport / Sample controls
    addCyberButton(loadButton_);
    loadButton_.setTooltip("Load sample (WAV file)");
    loadButton_.onClick = [this]() { loadButtonClicked(); };

    addCyberButton(playButton_);
    playButton_.setTooltip("Play/pause");
    playButton_.onClick = [this]() { playButtonClicked(); };
    playButton_.setEnabled(false);

    addCyberButton(stopButton_);
    stopButton_.setTooltip("Stop");
    stopButton_.onClick = [this]() { stopButtonClicked(); };
    stopButton_.setEnabled(false);

    addCyberButton(flattenButton_);
    flattenButton_.setTooltip("Flatten to audio");
    flattenButton_.onClick = [this]() { flattenButtonClicked(); };
    flattenButton_.setEnabled(false);

    addCyberKnob(rootNoteKnob_, rootNoteLabel_, "ROOT", 0.0, 127.0, 60.0, 1.0);
    rootNoteKnob_.setTooltip("Root note (0-127)");
    rootNoteKnob_.textFromValueFunction = [](double v) { return midiNoteToName(static_cast<int>(v)); };
    rootNoteKnob_.setDoubleClickReturnValue(true, 60.0);
    rootNoteKnob_.onValueChange = [this]() {
        audioProcessor.setRootNote(static_cast<int>(rootNoteKnob_.getValue()));
        updatePitchDisplay(midiNoteToName(static_cast<int>(rootNoteKnob_.getValue())));
    };

    addCyberKnob(rootFineTuneKnob_, rootFineTuneLabel_, "FINE", -50.0, 50.0, 0.0, 1.0);
    rootFineTuneKnob_.setTooltip("Fine tune (-50 to +50 cents)");
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
    masterVolSlider_.setTooltip("Master Volume (0-200%)");
    masterVolSlider_.onValueChange = [this]()
    {
        audioProcessor.setMasterVol(static_cast<float>(masterVolSlider_.getValue()));
    };

    addCyberKnob(masterPanSlider_, masterPanLabel_, "PAN", -1.0, 1.0, 0.0, 0.01,
                 juce::Slider::RotaryVerticalDrag);
    masterPanSlider_.setTooltip("Pan: stereo balance (-100% to +100%)");
    masterPanSlider_.onValueChange = [this]()
    {
        audioProcessor.setMasterPan(static_cast<float>(masterPanSlider_.getValue()));
    };

    //==============================================================================
    // Status
    statusLabel_.setText(">> READY <<", juce::dontSendNotification);
    statusLabel_.setFont(ana::CyberpunkTheme::getCyberFont(9.0f, false));
    statusLabel_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::fg_.withAlpha(0.7f));
    addAndMakeVisible(statusLabel_);

    // DNA Evolve button (launches EvolutionPanel in callout)
    addCyberButton(dnaButton_);
    dnaButton_.setButtonText("DNA EVOLVE");
    dnaButton_.setTooltip("Open DNA evolution panel");
    dnaButton_.onClick = [this] { dnaButtonClicked(); };

    // Credits / About button (launches CreditsPanel in callout)
    addCyberButton(creditsButton_);
    creditsButton_.setTooltip("About AnaPlug");
    creditsButton_.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    creditsButton_.setColour(juce::TextButton::textColourOffId, ana::CyberpunkTheme::fg_.withAlpha(0.5f));
    creditsButton_.onClick = [this] { creditsButtonClicked(); };

    // Randomizer — RANDOMIZE button + range selector
    {
        addCyberButton(randomizeButton_);
        randomizeButton_.setButtonText("RANDOM");
        randomizeButton_.setTooltip("Randomize all parameters");
        // Magenta accent to distinguish from other buttons
        randomizeButton_.setColour(juce::TextButton::buttonColourId,
                                   ana::CyberpunkTheme::magenta_.darker(0.6f));
        randomizeButton_.setColour(juce::TextButton::textColourOffId,
                                   ana::CyberpunkTheme::magenta_);
        randomizeButton_.onClick = [this]()
        {
            audioProcessor.getRandomizer().reseed();
            audioProcessor.randomizeAllParameters();
            statusLabel_.setText(">> PARAMETERS RANDOMIZED <<", juce::dontSendNotification);
        };

        rangeCombo_.addItem("\xC2\xB15%", 1);
        rangeCombo_.addItem("\xC2\xB110%", 2);
        rangeCombo_.addItem("\xC2\xB125%", 3);
        rangeCombo_.addItem("\xC2\xB150%", 4);
        rangeCombo_.setSelectedId(3);
        rangeCombo_.setTooltip("Randomize range: ±5/10/25/50%");
        rangeCombo_.setColour(juce::ComboBox::backgroundColourId,
                              ana::CyberpunkTheme::bg_.brighter(0.15f));
        rangeCombo_.setColour(juce::ComboBox::textColourId,
                              ana::CyberpunkTheme::fg_);
        rangeCombo_.setColour(juce::ComboBox::arrowColourId,
                              ana::CyberpunkTheme::magenta_.withAlpha(0.7f));
        rangeCombo_.setColour(juce::ComboBox::outlineColourId,
                              ana::CyberpunkTheme::magenta_.withAlpha(0.3f));
        rangeCombo_.onChange = [this]()
        {
            float pct = 25.0f;
            switch (rangeCombo_.getSelectedId())
            {
                case 1: pct = 5.0f;  break;
                case 2: pct = 10.0f; break;
                case 3: pct = 25.0f; break;
                case 4: pct = 50.0f; break;
            }
            audioProcessor.getRandomizer().setRangePercent(pct);
        };
        audioProcessor.getRandomizer().setRangePercent(25.0f);
        addAndMakeVisible(rangeCombo_);
    }

    // LUFS metering panel with EBU R128 compliant bars (lives on status bar)
    addAndMakeVisible(meteringPanel_);

    //==============================================================================
    // MIDI Learn indicator (hidden by default)
    midiLearnIndicator_.setText("MIDI LEARN", juce::dontSendNotification);
    midiLearnIndicator_.setFont(ana::CyberpunkTheme::getCyberFont(10.0f, true));
    midiLearnIndicator_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::yellow_);
    midiLearnIndicator_.setJustificationType(juce::Justification::centred);
    midiLearnIndicator_.setVisible(false);
    addAndMakeVisible(midiLearnIndicator_);

    //==============================================================================
    // Register sliders for MIDI Learn
    // Parameters with backing atomics in the processor
    setupMidiLearnForSlider(aSubSlider_, "sub_a", &audioProcessor.getSubHarmonicLevelRef());

    // Parameters without backing atomics yet — MIDI Learn will record the
    // mapping and the mappings persist across sessions. When a backing atomic
    // is added later, reconnect it via MidiLearn::reconnectTarget().
    setupMidiLearnForSlider(aBrightSlider_, "bright_a");
    setupMidiLearnForSlider(aBlurSlider_, "blur_a");
    setupMidiLearnForSlider(aHpfSlider_, "hpf_a");
    setupMidiLearnForSlider(bSubSlider_, "sub_b");
    setupMidiLearnForSlider(bBrightSlider_, "bright_b");
    setupMidiLearnForSlider(bBlurSlider_, "blur_b");
    setupMidiLearnForSlider(bHpfSlider_, "hpf_b");
    setupMidiLearnForSlider(timbreBlendSlider_, "timbre_blend");
    setupMidiLearnForSlider(filterCutoffSlider_, "filter_cutoff");
    setupMidiLearnForSlider(filterResSlider_, "filter_res");
    for (int i = 0; i < 4; ++i)
    {
        setupMidiLearnForSlider(macroSliders_[i],
                                "macro_" + juce::String(i + 1),
                                audioProcessor.getMacroController().getMacroValuePtr(i));
    }
    setupMidiLearnForSlider(unisonCountSlider_, "unison_count");
    setupMidiLearnForSlider(unisonDetuneSlider_, "unison_detune");
    setupMidiLearnForSlider(unisonSpreadSlider_, "unison_spread");
    setupMidiLearnForSlider(portamentoTimeSlider_, "portamento_time");
    setupMidiLearnForSlider(arpRateSlider_, "arp_rate");
    setupMidiLearnForSlider(arpGateSlider_, "arp_gate");
    setupMidiLearnForSlider(seqBpmSlider_, "seq_bpm");
    setupMidiLearnForSlider(seqRateSlider_, "seq_rate");
    setupMidiLearnForSlider(rootNoteKnob_, "root_note");
    setupMidiLearnForSlider(rootFineTuneKnob_, "root_fine");
    setupMidiLearnForSlider(masterVolSlider_, "master_vol");
    setupMidiLearnForSlider(masterPanSlider_, "master_pan");

    // Volume ADSR MIDI Learn

    // Effect rack — MIDI Learn for the rack controls is handled internally

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

    // Reserve status bar at bottom (compact 35px — was ~65px)
    r.statusBar = bounds.removeFromBottom(35);

    // Main 3-column area (42% of remaining height)
    r.mainArea = bounds.removeFromTop(static_cast<int>(bounds.getHeight() * 0.42f));

    // Process panel (filter + macros + effects) — increased from 32% to 46%
    // to give effect rack room for 3-4 visible modules
    r.processArea = bounds.removeFromTop(static_cast<int>(bounds.getHeight() * 0.46f));

    // Modulation panel (scrollable, can be compact)
    r.modArea = bounds.removeFromTop(static_cast<int>(bounds.getHeight() * 0.38f));

    // Bottom strip (unison, voice, arp, seq, sample, master)
    r.bottomArea = bounds;

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
    ana::CyberpunkTheme::drawPanelBorder(g, r.centerPanel, "SPECTRUM", ana::CyberpunkTheme::cyan_);
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

    // -- Center panel: view selector strip + feedback + XY pad --
    auto centerArea = r.centerPanel.reduced(4, 4);
    viewModeCombo_.setBounds(centerArea.removeFromTop(18).reduced(centerArea.getWidth() / 2 - 80, 0));
    auto fbArea = centerArea.removeFromTop(static_cast<int>(centerArea.getHeight() * 0.70f));
    feedbackPanel_.setBounds(fbArea.reduced(2));
    spectrumEditorCanvas_.setBounds(fbArea.reduced(2));
    xyPad_->setBounds(centerArea.reduced(2));

    // -- Process panel: FILTER (left) | MACROS (center) | EFFECTS (right) --
    auto pa = r.processArea.reduced(6, pad);

    // Filter section (22% — narrower to give effects more room)
    auto filterArea = pa.removeFromLeft(static_cast<int>(pa.getWidth() * 0.22f)).reduced(pad);
    filterTitle_.setBounds(filterArea.removeFromTop(14));
    filterTypeCombo_.setBounds(filterArea.removeFromTop(18).reduced(pad));
    filterCutoffSlider_.setBounds(filterArea.removeFromTop(16).reduced(pad));
    filterResSlider_.setBounds(filterArea.removeFromTop(16).reduced(pad));
    filterViz_.setBounds(filterArea.reduced(pad));

    // Macros section (40% of remaining — narrower for effects)
    auto macroArea = pa.removeFromLeft(static_cast<int>(pa.getWidth() * 0.40f)).reduced(pad);
    auto macroTitle = macroArea.removeFromTop(14);
    auto mkArea = macroArea.reduced(pad);
    int mkW = mkArea.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = mkArea.removeFromLeft(mkW).reduced(2);
        macroSliders_[i].setBounds(cell.removeFromTop(cell.getWidth()));
        macroLabels_[i].setBounds(cell);
    }

    // Effects section (right remainder) — dynamic effect rack
    auto fxArea = pa.reduced(pad);
    // Effect preset combo at top of effects section
    auto fxPresetRow = fxArea.removeFromTop(16).reduced(2, 0);
    fxPresetLabel_.setBounds(fxPresetRow.removeFromLeft(52));
    effectPresetCombo_.setBounds(fxPresetRow.reduced(0, 1));
    // Spectral effect toggles — single compact row (was 3×20px rows)
    auto specRow = fxArea.removeFromTop(18).reduced(pad);
    prismButton_.setBounds(specRow.removeFromLeft(specRow.getWidth() / 3).reduced(1));
    blurButton_.setBounds(specRow.removeFromLeft(specRow.getWidth() / 2).reduced(1));
    harmButton_.setBounds(specRow.reduced(1));
    // Vocal character selector row
    auto vocalRow = fxArea.removeFromTop(18).reduced(pad);
    vocalCharacterLabel_.setBounds(vocalRow.removeFromLeft(34));
    vocalCharacterCombo_.setBounds(vocalRow.reduced(0, 1));
    // Dynamic effect rack fills remaining — show 3-4 modules at a time
    effectRack_.setBounds(fxArea.reduced(1, pad));

    // -- Modulation assignment panel (scrollable Viewport) --
    modViewport_.setBounds(r.modArea.reduced(6, pad));

    // -- Bottom controls: UNISON | VOICE | ARP | SEQ | SAMPLE | MASTER --
    auto ba = r.bottomArea.reduced(6, pad);
    auto uniArea = ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.14f)).reduced(pad);
    auto voiceArea = ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.14f)).reduced(pad);
    auto arpArea = ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.14f)).reduced(pad);
    auto seqArea = ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.18f)).reduced(pad);
    auto smpArea = ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.26f)).reduced(pad);
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

    // Voice mode / Portamento
    voiceTitle_.setBounds(voiceArea.removeFromTop(14));
    voiceModeCombo_.setBounds(voiceArea.removeFromTop(18).reduced(pad));
    auto voiceKnobs = voiceArea.reduced(pad);
    int vkW = voiceKnobs.getWidth() / 2;

    auto voiceCell1 = voiceKnobs.removeFromLeft(vkW).reduced(2);
    portamentoTimeSlider_.setBounds(voiceCell1.removeFromTop(voiceCell1.getWidth()));
    portamentoTimeLabel_.setBounds(voiceCell1);

    auto voiceCell2 = voiceKnobs.reduced(2);
    portamentoCurveCombo_.setBounds(voiceCell2.removeFromTop(18).reduced(0, 2));
    portamentoCurveLabel_.setBounds(voiceCell2.removeFromTop(12).reduced(pad));

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

    // Step Sequencer
    seqTitle_.setBounds(seqArea.removeFromTop(14));
    auto seqControlRow = seqArea.removeFromTop(16).reduced(1, 0);
    seqPlayModeCombo_.setBounds(seqControlRow.removeFromLeft(seqControlRow.getWidth() / 3).reduced(1));
    seqClockSourceCombo_.setBounds(seqControlRow.removeFromLeft(seqControlRow.getWidth() / 2).reduced(1));
    seqCurrentStepLabel_.setBounds(seqControlRow.reduced(1));
    auto seqParamRow = seqArea.removeFromTop(18).reduced(1, 0);
    seqBpmSlider_.setBounds(seqParamRow.removeFromLeft(seqParamRow.getWidth() / 3).reduced(1));
    seqBpmLabel_.setBounds(seqBpmSlider_.getBounds().translated(0, -12));
    seqRateSlider_.setBounds(seqParamRow.removeFromLeft(seqParamRow.getWidth() / 2).reduced(1));
    seqRateLabel_.setBounds(seqRateSlider_.getBounds().translated(0, -12));
    // 16 step cells in 2 rows of 8
    auto seqGridArea = seqArea.reduced(1, 0);
    auto seqRow1 = seqGridArea.removeFromTop(seqGridArea.getHeight() / 2);
    auto seqRow2 = seqGridArea;
    int cellW = seqRow1.getWidth() / 8;
    for (int i = 0; i < 8; ++i)
    {
        stepCells_[i]->setBounds(seqRow1.removeFromLeft(cellW).reduced(1));
        stepCells_[i + 8]->setBounds(seqRow2.removeFromLeft(cellW).reduced(1));
    }

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

    // Master — compact single column (max 150w, Vol+Pan stacked with inline labels)
    mstArea = mstArea.withWidth(juce::jmin(mstArea.getWidth(), 150));
    mstArea.removeFromTop(4);
    {
        auto volRow = mstArea.removeFromTop(50).reduced(2, 0);
        masterVolSlider_.setBounds(volRow.removeFromLeft(volRow.getWidth() - 28));
        masterVolLabel_.setBounds(volRow);
        masterVolLabel_.setJustificationType(juce::Justification::centredRight);
    }
    {
        auto panRow = mstArea.removeFromTop(50).reduced(2, 0);
        masterPanSlider_.setBounds(panRow.removeFromLeft(panRow.getWidth() - 28));
        masterPanLabel_.setBounds(panRow);
        masterPanLabel_.setJustificationType(juce::Justification::centredRight);
    }
    // -- Status bar (compact 35px) --
    auto sb = r.statusBar.reduced(4, 1);
    meteringPanel_.setBounds(sb.removeFromRight(340).reduced(1));  // thinner metering
    midiLearnIndicator_.setBounds(sb.removeFromRight(70).reduced(1));
    dnaButton_.setBounds(sb.removeFromRight(90).reduced(1));
    creditsButton_.setBounds(sb.removeFromRight(20).reduced(1));
    auto randArea = sb.removeFromRight(120).reduced(1);
    randomizeButton_.setBounds(randArea.removeFromLeft(65));
    rangeCombo_.setBounds(randArea.reduced(1));
    statusLabel_.setBounds(sb.reduced(4, 0));
}

//==============================================================================
void AnaPlugAudioProcessorEditor::timerCallback()
{
    // MIDI Learn: indicator blink, timeout, parameter polling
    updateMidiLearnState();

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
            spectrumEditorCanvas_.setPartials(simd);
        }
    }
    if (audioProcessor.flattenPending())
        statusLabel_.setText(">> PITCH FLATTENING <<", juce::dontSendNotification);

    // --- Macro visual update: sync slider from controller, update ring colours ---
    {
        auto& macroCtrl = audioProcessor.getMacroController();
        for (int i = 0; i < 4; ++i)
        {
            const auto data = macroCtrl.getVisualData(i);

            // Sync slider position if it differs from the controller value
            const double currentSlider = macroSliders_[i].getValue();
            if (std::abs(currentSlider - static_cast<double>(data.value)) > 0.001)
            {
                macroSliders_[i].setValue(static_cast<double>(data.value),
                                          juce::dontSendNotification);
            }

            // Update the curve exponent for ring colour
            macroSliders_[i].setCurveExponent(data.curveExponent);
            macroSliders_[i].repaint();
        }
    }

    // XY Pad → processor parameter mapping
    // X axis is already written to morphAmount via setXParameter binding
    // Y axis → apply based on selected target
    if (xyPad_ != nullptr)
    {
        const float yVal = xyPad_->getY();
        switch (xyPad_->getYTarget())
        {
            case ana::XYPad::YTarget::Cutoff:
            {
                const float cutoff = 20.0f * std::pow(20000.0f / 20.0f, yVal);
                if (std::abs(static_cast<float>(filterCutoffSlider_.getValue()) - cutoff) > 1.0f)
                    filterCutoffSlider_.setValue(static_cast<double>(cutoff), juce::dontSendNotification);
                break;
            }
            case ana::XYPad::YTarget::Resonance:
                if (std::abs(static_cast<float>(filterResSlider_.getValue()) - yVal) > 0.005f)
                    filterResSlider_.setValue(static_cast<double>(yVal), juce::dontSendNotification);
                break;
            case ana::XYPad::YTarget::Volume:
                if (std::abs(static_cast<float>(masterVolSlider_.getValue()) - yVal) > 0.005f)
                    masterVolSlider_.setValue(static_cast<double>(yVal), juce::dontSendNotification);
                break;
            case ana::XYPad::YTarget::LFORate:
            case ana::XYPad::YTarget::LFODepth:
                break; // LFO controls moved to modulation panel
        }
    }

    // --- Sync modulation panel from processor state (preset reload, etc.) ---
    modPanel_.syncFromProcessor();

    // --- Step Sequencer: sync UI from processor state ---
    {
        auto& seq = audioProcessor.getStepSequencer();
        // Update current step indicator
        seqCurrentStepLabel_.setText("STEP: " + juce::String(seq.getCurrentStep()),
                                     juce::dontSendNotification);
        // Sync step cells from sequencer state
        for (int i = 0; i < 16; ++i)
        {
            const auto& step = seq.getStep(i);
            stepCells_[i]->setActive(step.active);
            stepCells_[i]->setValue(step.value);
        }
    }

    // --- Update filter visualization with live frequency response ---
    {
        // Generate log-spaced frequencies once (100 points, 20 Hz - 20 kHz)
        static const std::vector<float> vizFrequencies = []()
        {
            std::vector<float> freqs;
            freqs.reserve(100);
            constexpr float minF = 20.0f;
            constexpr float maxF = 20000.0f;
            for (int i = 0; i < 100; ++i)
                freqs.push_back(minF * std::pow(maxF / minF, i / 99.0f));
            return freqs;
        }();

        auto magnitudes = audioProcessor.getMultiFilter().getFrequencyResponse(vizFrequencies);
        filterViz_.setFrequencyResponse(vizFrequencies, magnitudes);
    }
}

//==============================================================================
//==============================================================================
void AnaPlugAudioProcessorEditor::onViewModeChanged()
{
    const int mode = viewModeCombo_.getSelectedId();

    // 3D mode uses the SpectrumEditorCanvas; all others use feedbackPanel_
    if (mode == 4)
    {
        feedbackPanel_.setVisible(false);
        spectrumEditorCanvas_.setVisible(true);
        spectrumEditorCanvas_.set3DEnabled(true);
    }
    else
    {
        spectrumEditorCanvas_.setVisible(false);
        spectrumEditorCanvas_.set3DEnabled(false);
        feedbackPanel_.setVisible(true);
    }
}

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

//==============================================================================
void AnaPlugAudioProcessorEditor::dnaButtonClicked()
{
    evolutionPanel = std::make_unique<ana::EvolutionPanel>(audioProcessor);
    evolutionPanel->setSize(500, 440);
    juce::CallOutBox::launchAsynchronously(
        std::move(evolutionPanel),
        dnaButton_.getScreenBounds(),
        this);
}

//==============================================================================
void AnaPlugAudioProcessorEditor::creditsButtonClicked()
{
    auto* creditsPanel = new ana::CreditsPanel();
    creditsPanel->setSize(420, 460);
    juce::CallOutBox::launchAsynchronously(
        std::unique_ptr<juce::Component>(creditsPanel),
        creditsButton_.getScreenBounds(),
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
void AnaPlugAudioProcessorEditor::setupMidiLearnForSlider(juce::Slider& slider,
                                                          const juce::String& paramId,
                                                          std::atomic<float>* target)
{
    slider.addMouseListener(this, false);
    learnableSliders_[paramId] = &slider;
    midiLearnSliders_[&slider] = { paramId, target };
}

//==============================================================================
void AnaPlugAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    // Propagate to base class first (handles focus etc.)
    juce::AudioProcessorEditor::mouseDown(event);

    if (!event.mods.isRightButtonDown())
        return;

    // Check for effect preset combo right-click
    if (event.eventComponent == &effectPresetCombo_)
    {
        effectPresetRightClicked();
        return;
    }

    auto* slider = dynamic_cast<juce::Slider*>(event.eventComponent);
    if (slider == nullptr)
        return;

    auto it = midiLearnSliders_.find(slider);
    if (it == midiLearnSliders_.end())
        return;

    const auto& info = it->second;

    juce::PopupMenu menu;
    auto& midiLearn = audioProcessor.getMidiLearn();

    // --- Check if this is a macro slider → add curve submenu ---
    if (info.paramId.startsWith("macro_"))
    {
        const int macroIdx = info.paramId.getTrailingIntValue() - 1;
        auto& macroCtrl = audioProcessor.getMacroController();
        const float currentCurve = macroCtrl.getMappingCurve(macroIdx);

        juce::PopupMenu curveMenu;
        curveMenu.addItem("Linear (1.0)",  true, std::abs(currentCurve - 1.0f) < 0.01f,
                          [this, macroIdx]() { audioProcessor.getMacroController().setMappingCurve(macroIdx, 1.0f); });
        curveMenu.addItem("Exponential (2.0)", true, std::abs(currentCurve - 2.0f) < 0.01f,
                          [this, macroIdx]() { audioProcessor.getMacroController().setMappingCurve(macroIdx, 2.0f); });
        curveMenu.addItem("S-Curve (0.5)", true, std::abs(currentCurve - 0.5f) < 0.01f,
                          [this, macroIdx]() { audioProcessor.getMacroController().setMappingCurve(macroIdx, 0.5f); });
        menu.addSubMenu("Mapping Curve", curveMenu);
        menu.addSeparator();
    }

    // Check if this parameter already has a mapping
    bool alreadyMapped = false;
    bool mappingIsGlobal = false;
    int mappedCC = -1;
    for (const auto& mapping : midiLearn.getMappings())
    {
        if (mapping.parameterId == info.paramId)
        {
            alreadyMapped = true;
            mappingIsGlobal = mapping.isGlobal;
            mappedCC = mapping.ccNumber;
            break;
        }
    }

    if (midiLearn.isLearning())
    {
        menu.addItem("MIDI Learn (in progress…)", false, false, {});
    }
    else
    {
        // Copy info by value — the lambda fires asynchronously so the
        // original iterator may have been invalidated by then.
        const auto infoCopy = info;
        menu.addItem("MIDI Learn", [this, slider, infoCopy]()
        {
            auto& ml = audioProcessor.getMidiLearn();
            ml.startLearn(infoCopy.paramId, infoCopy.target,
                          static_cast<float>(slider->getMinimum()),
                          static_cast<float>(slider->getMaximum()));
            midiLearnStartTime_ = juce::Time::getMillisecondCounter();
        });

        if (alreadyMapped)
        {
            // Show which CC is mapped
            menu.addItem("Clear CC " + juce::String(mappedCC),
                         [this, cc = mappedCC]()
            {
                audioProcessor.getMidiLearn().removeMapping(cc);
            });

            // Global Mapping toggle (survives preset changes)
            menu.addItem("Global Mapping (survives presets)",
                         true, mappingIsGlobal,
                         [this, paramId = info.paramId, newGlobal = !mappingIsGlobal]()
            {
                audioProcessor.getMidiLearn().setMappingGlobal(paramId, newGlobal);
            });
        }
    }

    menu.showMenuAsync(juce::PopupMenu::Options());
}

//==============================================================================
void AnaPlugAudioProcessorEditor::updateMidiLearnState()
{
    auto& midiLearn = audioProcessor.getMidiLearn();

    // --- Timeout: auto-stop learn after 3 seconds ---
    if (midiLearn.isLearning())
    {
        if (juce::Time::getMillisecondCounter() - midiLearnStartTime_ > 3000)
            midiLearn.stopLearn();
    }

    // --- Indicator blink ---
    if (midiLearn.isLearning())
    {
        // Blink at ≈5 Hz (toggle every ~100 ms at 30 Hz timer)
        const bool on = ((juce::Time::getMillisecondCounter() / 100) % 2) == 0;
        midiLearnIndicator_.setVisible(on);
    }
    else
    {
        midiLearnIndicator_.setVisible(false);
    }

    // --- Poll mapping targets and sync matching sliders ---
    auto& macroCtrl = audioProcessor.getMacroController();
    for (const auto& mapping : midiLearn.getMappings())
    {
        if (mapping.targetParam != nullptr)
        {
            auto sit = learnableSliders_.find(mapping.parameterId);
            if (sit != learnableSliders_.end())
            {
                float currentAtomic = mapping.targetParam->load();
                double currentSlider = sit->second->getValue();
                // Use a small epsilon to avoid redundant setValue calls
                if (std::abs(static_cast<double>(currentAtomic) - currentSlider) > 0.001)
                {
                    sit->second->setValue(static_cast<double>(currentAtomic),
                                          juce::sendNotificationSync);
                }
            }

            // Also sync macro controller if this paramId is a macro
            if (mapping.parameterId.startsWith("macro_"))
            {
                const int macroIdx = mapping.parameterId.getTrailingIntValue() - 1;
                if (macroIdx >= 0 && macroIdx < 4)
                {
                    const float rawVal = macroCtrl.getMacroValue(macroIdx);
                    const float atomicVal = mapping.targetParam->load(std::memory_order_relaxed);
                    if (std::abs(rawVal - atomicVal) > 0.001f)
                        macroCtrl.setMacroValue(macroIdx, atomicVal);
                }
            }
        }
    }
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

//==============================================================================
// Effect preset helpers
//==============================================================================

void AnaPlugAudioProcessorEditor::populateEffectPresets()
{
    effectPresetCombo_.clear(juce::dontSendNotification);
    effectPresetCombo_.addItem("Save As...", -1);
    effectPresetCombo_.addSeparator();

    auto presets = audioProcessor.getPresetManager().getEffectPresetNames();
    for (int i = 0; i < presets.size(); ++i)
        effectPresetCombo_.addItem(presets[i], i + 1);
}

void AnaPlugAudioProcessorEditor::onEffectPresetSelected()
{
    const int id = effectPresetCombo_.getSelectedId();

    if (id == -1)
    {
        // "Save As..." — show text input dialog
        auto* alert = new juce::AlertWindow("Save Effect Preset",
                                            "Enter a name for the current effect state:",
                                            juce::MessageBoxIconType::QuestionIcon);
        alert->addTextEditor("name", "", "Preset name");
        alert->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey, 0, 0));
        alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey, 0, 0));
        alert->setColour(juce::AlertWindow::backgroundColourId, ana::CyberpunkTheme::bg_.brighter(0.1f));
        alert->setColour(juce::AlertWindow::textColourId, ana::CyberpunkTheme::fg_);
        alert->setColour(juce::TextEditor::backgroundColourId, ana::CyberpunkTheme::bg_.brighter(0.2f));
        alert->setColour(juce::TextEditor::textColourId, ana::CyberpunkTheme::fg_);
        alert->setColour(juce::TextEditor::outlineColourId, ana::CyberpunkTheme::cyan_.withAlpha(0.4f));

        alert->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, alert](int result)
            {
                if (result == 1)
                {
                    auto name = alert->getTextEditorContents("name").trim();
                    if (name.isNotEmpty())
                    {
                        if (audioProcessor.getPresetManager().saveEffectPreset(name))
                        {
                            populateEffectPresets();
                            effectPresetCombo_.setText(name, juce::dontSendNotification);
                        }
                    }
                }
                delete alert;
            }), true);
    }
    else if (id > 0)
    {
        // Load a named effect preset
        auto name = effectPresetCombo_.getText();
        if (name.isNotEmpty())
        {
            audioProcessor.getPresetManager().loadEffectPreset(name);
            effectPresetCombo_.setText(name, juce::dontSendNotification);
        }
    }

    // Reset selection to "no item" so the text stays as the current preset name
    if (id != -1)
        effectPresetCombo_.setSelectedId(0, juce::dontSendNotification);
}

void AnaPlugAudioProcessorEditor::effectPresetRightClicked()
{
    // Show delete option if a named preset is currently displayed
    auto currentName = effectPresetCombo_.getText();
    if (currentName.isEmpty() || currentName == "Default")
        return;

    // Check if it's actually a saved preset
    auto presets = audioProcessor.getPresetManager().getEffectPresetNames();
    if (!presets.contains(currentName))
        return;

    juce::PopupMenu menu;
    menu.addColour(juce::PopupMenu::backgroundColourId, ana::CyberpunkTheme::bg_.brighter(0.1f));
    menu.addColour(juce::PopupMenu::textColourId, ana::CyberpunkTheme::fg_);
    menu.addColour(juce::PopupMenu::highlightedBackgroundColourId, ana::CyberpunkTheme::magenta_.withAlpha(0.3f));
    menu.addItem("Delete \"" + currentName + "\"", [this, currentName]()
    {
        audioProcessor.getPresetManager().deleteEffectPreset(currentName);
        populateEffectPresets();
        effectPresetCombo_.setText("Default", juce::dontSendNotification);
    });
    menu.showMenuAsync(juce::PopupMenu::Options());
}
