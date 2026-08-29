#include "PluginEditor.h"
#include "gui/panels/PanelWidgets.h"
#include "dsp/PitchCorrector.h"
#include "dsp/Crumb.h"
#include <cmath>


//==============================================================================
AnaPlugAudioProcessorEditor::AnaPlugAudioProcessorEditor(AnaPlugAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
      timbreAPanel_(p, true), timbreBPanel_(p, false),
      filterPanel_(p), macroPanel_(p), effectRack_(p),
      transportBar_(p), masterSection_(p), sequencerPanel_(p),
      meteringPanel_(p), modPanel_(p)
{
    setLookAndFeel(&ana::CyberpunkTheme::getInstance());
    ANA_CRUMB("ed:laf");
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
    // Timbre A/B panels
    addAndMakeVisible(timbreAPanel_);
    addAndMakeVisible(timbreBPanel_);

    // Timbre blend cross-fader
    addCyberKnob(timbreBlendSlider_, timbreBlendLabel_, "BLEND", 0.0, 1.0, 0.5, 0.01,
                 juce::Slider::LinearHorizontal);
    timbreBlendSlider_.setTooltip("A/B timbre blend (0-100%)");

    //==============================================================================
    // Center 鈥?Visual feedback + view selector
    ANA_CRUMB("ed:center-start");
    addAndMakeVisible(feedbackPanel_);
    addAndMakeVisible(waterfallDisplay_);
    waterfallDisplay_.setVisible(false);
    addAndMakeVisible(spectrumEditorCanvas_);
    spectrumEditorCanvas_.setVisible(false);
    viewModeCombo_.addItem("PARTIALS", 1);
    viewModeCombo_.addItem("WATERFALL", 2);
    viewModeCombo_.addItem("EDITOR", 3);
    viewModeCombo_.addItem("3D", 4);
    viewModeCombo_.addItem("SCOPE", 5);
    viewModeCombo_.setSelectedId(1);
    viewModeCombo_.onChange = [this] { onViewModeChanged(); };
    viewModeCombo_.setTooltip("View mode: PARTIALS/WATERFALL/EDITOR/3D/SCOPE");
    addAndMakeVisible(viewModeCombo_);

    // Oscilloscope view (hidden by default)
    waveformDisplay_ = std::make_unique<ana::WaveformDisplay>();
    addAndMakeVisible(waveformDisplay_.get());
    waveformDisplay_->setVisible(false);

    //==============================================================================
    // Filter panel
    addAndMakeVisible(filterPanel_);

    //==============================================================================
    // Macros 鈥?wired to MacroController with visual curve feedback
    addAndMakeVisible(macroPanel_);

    //==============================================================================
    // XY Pad 鈥?morph control with smooth interpolation + MIDI Learn
    xyPad_ = std::make_unique<ana::XYPad>(audioProcessor);
    xyPad_->setXParameter(&audioProcessor.getMorphAmountRef(), "MORPH");
    addAndMakeVisible(xyPad_.get());
    ANA_CRUMB("ed:xypad");

    //==============================================================================
    // Effect preset combo box
    ANA_CRUMB("ed:fx-start");
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
    // Dynamic Effect Rack 鈥?replaces the old hardcoded effect slider stack
    ANA_CRUMB("ed:rack");
    addAndMakeVisible(effectRack_);

    //==============================================================================
    // Modulation assignment panel (replaces old LFO/Envelope + Vol ADSR + ModSrc)
    ANA_CRUMB("ed:mod-start");
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
    // Step Sequencer panel
    addAndMakeVisible(sequencerPanel_);

    //==============================================================================
    // Transport / Sample controls (load + flatten clicks stay in the editor:
    // they touch the file chooser and the status label)
    transportBar_.getLoadButton().onClick = [this]() { loadButtonClicked(); };
    transportBar_.getFlattenButton().onClick = [this]() { flattenButtonClicked(); };

    //==============================================================================
    // Master
    addAndMakeVisible(masterSection_);

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

    // Randomizer 鈥?RANDOMIZE button + range selector
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

        rangeCombo_.addItem("\u00B15%", 1);
        rangeCombo_.addItem("\u00B110%", 2);
        rangeCombo_.addItem("\u00B125%", 3);
        rangeCombo_.addItem("\u00B150%", 4);
        rangeCombo_.setSelectedId(3);
        rangeCombo_.setTooltip("Randomize range: 卤5/10/25/50%");
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
    setupMidiLearnForSlider(timbreAPanel_.getSubSlider(), "sub_a", &audioProcessor.getSubHarmonicLevelRef());

    // Parameters without backing atomics yet 鈥?MIDI Learn will record the
    // mapping and the mappings persist across sessions. When a backing atomic
    // is added later, reconnect it via MidiLearn::reconnectTarget().
    setupMidiLearnForSlider(timbreAPanel_.getBrightSlider(), "bright_a");
    setupMidiLearnForSlider(timbreAPanel_.getBlurSlider(), "blur_a");
    setupMidiLearnForSlider(timbreAPanel_.getHpfSlider(), "hpf_a");
    setupMidiLearnForSlider(timbreBPanel_.getSubSlider(), "sub_b");
    setupMidiLearnForSlider(timbreBPanel_.getBrightSlider(), "bright_b");
    setupMidiLearnForSlider(timbreBPanel_.getBlurSlider(), "blur_b");
    setupMidiLearnForSlider(timbreBPanel_.getHpfSlider(), "hpf_b");
    setupMidiLearnForSlider(timbreBlendSlider_, "timbre_blend");
    setupMidiLearnForSlider(filterPanel_.getCutoffSlider(), "filter_cutoff");
    setupMidiLearnForSlider(filterPanel_.getResonanceSlider(), "filter_res");
    for (int i = 0; i < 4; ++i)
    {
        setupMidiLearnForSlider(macroPanel_.getMacroSlider(i),
                                "macro_" + juce::String(i + 1),
                                audioProcessor.getMacroController().getMacroValuePtr(i));
    }
    setupMidiLearnForSlider(unisonCountSlider_, "unison_count");
    setupMidiLearnForSlider(unisonDetuneSlider_, "unison_detune");
    setupMidiLearnForSlider(unisonSpreadSlider_, "unison_spread");
    setupMidiLearnForSlider(portamentoTimeSlider_, "portamento_time");
    setupMidiLearnForSlider(arpRateSlider_, "arp_rate");
    setupMidiLearnForSlider(arpGateSlider_, "arp_gate");
    setupMidiLearnForSlider(sequencerPanel_.getBpmSlider(), "seq_bpm");
    setupMidiLearnForSlider(sequencerPanel_.getRateSlider(), "seq_rate");
    setupMidiLearnForSlider(transportBar_.getRootNoteSlider(), "root_note");
    setupMidiLearnForSlider(transportBar_.getRootFineTuneSlider(), "root_fine");
    setupMidiLearnForSlider(masterSection_.getVolumeSlider(), "master_vol");
    setupMidiLearnForSlider(masterSection_.getPanSlider(), "master_pan");

    // Volume ADSR MIDI Learn

    // Effect rack 鈥?MIDI Learn for the rack controls is handled internally

    updateStatus();
    ANA_CRUMB("ed:exit");
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

    // Reserve status bar at bottom (compact 35px 鈥?was ~65px)
    r.statusBar = bounds.removeFromBottom(35);

    // Main 3-column area (42% of remaining height)
    r.mainArea = bounds.removeFromTop(static_cast<int>(bounds.getHeight() * 0.42f));

    // Process panel (filter + macros + effects) 鈥?increased from 32% to 46%
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

    // -- Timbre A/B panels --
    timbreAPanel_.setBounds(r.timbreAPanel);
    timbreBPanel_.setBounds(r.timbreBPanel);

    // Timbre blend at center-between
    auto blendArea = r.centerPanel.removeFromBottom(20).reduced(40, 0);
    timbreBlendSlider_.setBounds(blendArea);
    timbreBlendLabel_.setBounds(blendArea.translated(0, -16));

    // -- Center panel: view selector strip + feedback + XY pad --
    auto centerArea = r.centerPanel.reduced(4, 4);
    viewModeCombo_.setBounds(centerArea.removeFromTop(18).reduced(centerArea.getWidth() / 2 - 80, 0));
    auto fbArea = centerArea.removeFromTop(static_cast<int>(centerArea.getHeight() * 0.70f));
    feedbackPanel_.setBounds(fbArea.reduced(2));
    waterfallDisplay_.setBounds(fbArea.reduced(2));
    if (waveformDisplay_)
        waveformDisplay_->setBounds(fbArea.reduced(2));
    spectrumEditorCanvas_.setBounds(fbArea.reduced(2));
    xyPad_->setBounds(centerArea.reduced(2));

    // -- Process panel: FILTER (left) | MACROS (center) | EFFECTS (right) --
    auto pa = r.processArea.reduced(6, pad);

    // Filter section (22% 鈥?narrower to give effects more room)
    filterPanel_.setBounds(pa.removeFromLeft(static_cast<int>(pa.getWidth() * 0.22f)));

    // Macros section (40% of remaining 鈥?narrower for effects)
    macroPanel_.setBounds(pa.removeFromLeft(static_cast<int>(pa.getWidth() * 0.40f)));

    // Effects section (right remainder) 鈥?dynamic effect rack
    auto fxArea = pa.reduced(pad);
    // Effect preset combo at top of effects section
    auto fxPresetRow = fxArea.removeFromTop(16).reduced(2, 0);
    fxPresetLabel_.setBounds(fxPresetRow.removeFromLeft(52));
    effectPresetCombo_.setBounds(fxPresetRow.reduced(0, 1));
    // Spectral effect toggles 鈥?single compact row (was 3脳20px rows)
    auto specRow = fxArea.removeFromTop(18).reduced(pad);
    prismButton_.setBounds(specRow.removeFromLeft(specRow.getWidth() / 3).reduced(1));
    blurButton_.setBounds(specRow.removeFromLeft(specRow.getWidth() / 2).reduced(1));
    harmButton_.setBounds(specRow.reduced(1));
    // Vocal character selector row
    auto vocalRow = fxArea.removeFromTop(18).reduced(pad);
    vocalCharacterLabel_.setBounds(vocalRow.removeFromLeft(34));
    vocalCharacterCombo_.setBounds(vocalRow.reduced(0, 1));
    // Dynamic effect rack fills remaining 鈥?show 3-4 modules at a time
    effectRack_.setBounds(fxArea.reduced(1, pad));

    // -- Modulation assignment panel (scrollable Viewport) --
    modViewport_.setBounds(r.modArea.reduced(6, pad));

    // -- Bottom controls: UNISON | VOICE | ARP | SEQ | SAMPLE | MASTER --
    auto ba = r.bottomArea.reduced(6, pad);
    auto uniArea = ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.14f)).reduced(pad);
    auto voiceArea = ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.14f)).reduced(pad);
    auto arpArea = ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.14f)).reduced(pad);
    sequencerPanel_.setBounds(ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.18f)));
    transportBar_.setBounds(ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.26f)));
    masterSection_.setBounds(ba);

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
            waterfallDisplay_.updatePartials(simd);
            spectrumEditorCanvas_.setPartials(simd);
        }

        // Push scope buffer data to WaveformDisplay when SCOPE mode is active
        if (waveformDisplay_ && waveformDisplay_->isVisible())
        {
            std::vector<float> scopeData;
            if (audioProcessor.getScopeOutput(scopeData))
            {
                waveformDisplay_->setSamples(scopeData);
                waveformDisplay_->setPlaybackPosition(
                    static_cast<double>(audioProcessor.getPlaybackPosition()
                                        % audioProcessor.kScopeBufferSize));
            }
        }
    }
    if (audioProcessor.flattenPending())
        statusLabel_.setText(">> PITCH FLATTENING <<", juce::dontSendNotification);

    // --- Macro visual update: sync slider from controller, update ring colours ---
    macroPanel_.updateFromController();

    // XY Pad 鈫?processor parameter mapping
    // X axis is already written to morphAmount via setXParameter binding
    // Y axis 鈫?apply based on selected target
    if (xyPad_ != nullptr)
    {
        const float yVal = xyPad_->getY();
        switch (xyPad_->getYTarget())
        {
            case ana::XYPad::YTarget::Cutoff:
            {
                const float cutoff = 20.0f * std::pow(20000.0f / 20.0f, yVal);
                if (std::abs(static_cast<float>(filterPanel_.getCutoffSlider().getValue()) - cutoff) > 1.0f)
                    filterPanel_.getCutoffSlider().setValue(static_cast<double>(cutoff), juce::dontSendNotification);
                break;
            }
            case ana::XYPad::YTarget::Resonance:
                if (std::abs(static_cast<float>(filterPanel_.getResonanceSlider().getValue()) - yVal) > 0.005f)
                    filterPanel_.getResonanceSlider().setValue(static_cast<double>(yVal), juce::dontSendNotification);
                break;
            case ana::XYPad::YTarget::Volume:
                if (std::abs(static_cast<float>(masterSection_.getVolumeSlider().getValue()) - yVal) > 0.005f)
                    masterSection_.getVolumeSlider().setValue(static_cast<double>(yVal), juce::dontSendNotification);
                break;
            case ana::XYPad::YTarget::LFORate:
            case ana::XYPad::YTarget::LFODepth:
                break; // LFO controls moved to modulation panel
        }
    }

    // --- Sync modulation panel from processor state (preset reload, etc.) ---
    modPanel_.syncFromProcessor();

    // --- Step Sequencer: sync UI from processor state ---
    sequencerPanel_.updateFromSequencer();

    // --- Update filter visualization with live frequency response ---
    filterPanel_.updateFrequencyResponse();
}

//==============================================================================
//==============================================================================
void AnaPlugAudioProcessorEditor::onViewModeChanged()
{
    const int mode = viewModeCombo_.getSelectedId();

    // Hide all view panels first
    feedbackPanel_.setVisible(false);
    waterfallDisplay_.setVisible(false);
    spectrumEditorCanvas_.setVisible(false);
    if (waveformDisplay_)
        waveformDisplay_->setVisible(false);

    switch (mode)
    {
        case 1: // PARTIALS 鈥?classic bar display
            feedbackPanel_.setVisible(true);
            break;

        case 2: // WATERFALL 鈥?3D waterfall spectral view
            waterfallDisplay_.setVisible(true);
            break;

        case 3: // EDITOR 鈥?2D spectrum editor canvas
            spectrumEditorCanvas_.setVisible(true);
            spectrumEditorCanvas_.set3DEnabled(false);
            break;

        case 4: // 3D 鈥?spectrum editor with OpenGL 3D waterfall
            spectrumEditorCanvas_.setVisible(true);
            spectrumEditorCanvas_.set3DEnabled(true);
            break;

        case 5: // SCOPE 鈥?real-time oscilloscope
            if (waveformDisplay_)
                waveformDisplay_->setVisible(true);
            break;

        default: // fallback to partials
            feedbackPanel_.setVisible(true);
            break;
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
                    transportBar_.getPlayButton().setEnabled(true);
                    transportBar_.getFlattenButton().setEnabled(true);

                    ana::PitchCorrector pitchDetector;
                    const auto& audioData = engine.getAudioData();
                    float detectedNote = pitchDetector.detectPitch(audioData.samples, audioData.sampleRate);
                    if (detectedNote >= 0.5f)
                    {
                        int midiNote = static_cast<int>(std::round(detectedNote));
                        audioProcessor.setRootNote(midiNote);
                        transportBar_.getRootNoteSlider().setValue(static_cast<double>(midiNote), juce::dontSendNotification);
                        transportBar_.updatePitchDisplay(ana::TransportBar::midiNoteToName(midiNote));
                    }
                    else transportBar_.updatePitchDisplay(ana::TransportBar::midiNoteToName(60));
                    updateStatus();
                }
                else statusLabel_.setText(">> FAILED TO LOAD <<", juce::dontSendNotification);
            }
        });
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
        transportBar_.getFlattenButton().setEnabled(false);
        return;
    }
    transportBar_.getFlattenButton().setEnabled(true);
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

    // --- Check if this is a macro slider 鈫?add curve submenu ---
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
        menu.addItem("MIDI Learn (in progress鈥?", false, false, {});
    }
    else
    {
        // Copy info by value 鈥?the lambda fires asynchronously so the
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
        // Blink at 鈮? Hz (toggle every ~100 ms at 30 Hz timer)
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
    ana::panelwidgets::cyberKnob(*this, slider, label, name, min, max, init, step, style);
}

juce::TextButton& AnaPlugAudioProcessorEditor::addCyberButton(juce::TextButton& btn)
{
    return ana::panelwidgets::cyberButton(*this, btn);
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
        // "Save As..." 鈥?show text input dialog
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
    menu.addItem("Delete \"" + currentName + "\"", [this, currentName]()
    {
        audioProcessor.getPresetManager().deleteEffectPreset(currentName);
        populateEffectPresets();
        effectPresetCombo_.setText("Default", juce::dontSendNotification);
    });
    menu.showMenuAsync(juce::PopupMenu::Options());
}
