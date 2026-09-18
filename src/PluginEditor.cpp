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
      meteringPanel_(p), modPanel_(p), envPage_(p)
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

    // Visual theme selector (ThemePalettes.h line-up)
    for (int i = 0; i < ana::ThemePalettes::count; ++i)
        themeCombo_.addItem(ana::ThemePalettes::name(i), i + 1);

    themeCombo_.setSelectedId(ana::CyberpunkTheme::getThemeIndex() + 1,
                              juce::dontSendNotification);
    themeCombo_.setTooltip("Visual theme");
    themeCombo_.onChange = [this]
    {
        const int idx = juce::jlimit(0, ana::ThemePalettes::count - 1,
                                     themeCombo_.getSelectedId() - 1);

        const auto oldPalette = ana::ThemePalettes::get(ana::CyberpunkTheme::getThemeIndex());

        ana::CyberpunkTheme::getInstance().applyPalette(idx);
        ana::CyberpunkTheme::remapComponentColours(*this, oldPalette);

        audioProcessor.setThemeIndex(idx);
        repaint();
    };
    addAndMakeVisible(themeCombo_);

    presetButton_.setButtonText("PRESET: DEFAULT");
    presetButton_.setTooltip("Open preset browser");
    presetButton_.onClick = [this] { presetButtonClicked(); };
    addCyberButton(presetButton_);
    presetButton_.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    presetButton_.setColour(juce::TextButton::textColourOffId, ana::CyberpunkTheme::cyan_);
    addAndMakeVisible(presetButton_);

    // Prominent sample-import entry (the rack LOAD-button corner was easy to miss)
    addCyberButton(importButton_);
    importButton_.setTooltip("Import a WAV sample to resynthesize (same as LOAD)");
    importButton_.onClick = [this] { loadButtonClicked(); };
    addAndMakeVisible(importButton_);

    // SYNTH mode toggle (P6): switch between sample playback and live additive synth.
    addCyberButton(synthModeButton_);
    synthModeButton_.setClickingTogglesState(true);
    synthModeButton_.setTooltip("Additive SYNTH mode: play the sample's partials live. Edit them in the spectrum EDITOR view.");
    synthModeButton_.onClick = [this]()
    {
        const bool on = synthModeButton_.getToggleState();
        audioProcessor.setSynthMode(on);
        if (on)
            spectrumEditorCanvas_.setPartials(audioProcessor.getEditedPartials());
    };
    addAndMakeVisible(synthModeButton_);

    //==============================================================================
    // Timbre A/B panels
    addAndMakeVisible(timbreAPanel_);
    addAndMakeVisible(timbreBPanel_);

    // Timbre blend cross-fader
    addCyberKnob(timbreBlendSlider_, timbreBlendLabel_, "BLEND", 0.0, 1.0, 0.5, 0.01,
                 juce::Slider::LinearHorizontal);
    timbreBlendSlider_.setTooltip("A/B timbre blend (0-100%)");
    timbreBlendSlider_.onValueChange = [this]()
    {
        audioProcessor.setTimbreBlend(static_cast<float>(timbreBlendSlider_.getValue()));
        timbreBlendReadout_.setText(ana::CyberpunkTheme::formatPercent(
                                        static_cast<float>(timbreBlendSlider_.getValue())),
                                    juce::dontSendNotification);
    };
    ana::CyberpunkTheme::styleReadout(timbreBlendReadout_);
    timbreBlendReadout_.setText("50%", juce::dontSendNotification);
    addAndMakeVisible(timbreBlendReadout_);

    // Generative timbre designer (P5): latent-driven harmonic set blended into the timbre
    genLabel_.setText("GENERATIVE", juce::dontSendNotification);
    genLabel_.setFont(ana::CyberpunkTheme::getCyberFont(10.0f));
    genLabel_.setJustificationType(juce::Justification::centredLeft);
    genLabel_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::fg_.withAlpha(0.75f));
    addAndMakeVisible(genLabel_);

    addCyberButton(genEnableButton_);
    genEnableButton_.setClickingTogglesState(true);
    genEnableButton_.setTooltip("Blend the generated timbre into the live harmonic set");
    genEnableButton_.onClick = [this]
    {
        audioProcessor.setGenerativeTimbreEnabled(genEnableButton_.getToggleState());
    };
    addAndMakeVisible(genEnableButton_);

    addCyberButton(genRandomButton_);
    genRandomButton_.setTooltip("Randomise the generative latent vector");
    genRandomButton_.onClick = [this] { audioProcessor.randomizeGeneratedTimbre(); };
    addAndMakeVisible(genRandomButton_);

    addCyberButton(genCaptureButton_);
    genCaptureButton_.setTooltip("Seed the latent vector from the current edited partials");
    genCaptureButton_.onClick = [this] { audioProcessor.captureGeneratedTimbreFromEdit(); };
    addAndMakeVisible(genCaptureButton_);

    static const char* genPresetNames[] = { "WARM", "BRIGHT", "DARK", "METALLIC",
                                            "GLASSY", "HOLLOW", "RICH", "THIN" };
    for (int i = 0; i < 8; ++i)
        genPresetCombo_.addItem(genPresetNames[i], i + 1);
    genPresetCombo_.setSelectedId(1, juce::dontSendNotification);
    genPresetCombo_.setTooltip("Generative timbre preset");
    genPresetCombo_.onChange = [this]
    {
        audioProcessor.setGenerativeTimbrePreset(genPresetCombo_.getSelectedId() - 1);
    };
    addAndMakeVisible(genPresetCombo_);

    genMixSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    genMixSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    genMixSlider_.setRange(0.0, 1.0, 0.01);
    genMixSlider_.setValue(0.5, juce::dontSendNotification);
    genMixSlider_.setTooltip("Generated timbre blend amount");
    genMixSlider_.onValueChange = [this]
    {
        audioProcessor.setGenerativeTimbreMix(static_cast<float>(genMixSlider_.getValue()));
        genMixReadout_.setText(ana::CyberpunkTheme::formatPercent(
                                   static_cast<float>(genMixSlider_.getValue())),
                               juce::dontSendNotification);
    };
    addAndMakeVisible(genMixSlider_);
    ana::CyberpunkTheme::styleReadout(genMixReadout_);
    genMixReadout_.setText("50%", juce::dontSendNotification);
    addAndMakeVisible(genMixReadout_);

    // Time-varying harmonic image (P6b): advances through analysed frames
    addCyberButton(imageEnableButton_);
    imageEnableButton_.setClickingTogglesState(true);
    imageEnableButton_.setTooltip("Play the analysed harmonic image over time (frame advance)");
    imageEnableButton_.onClick = [this]
    {
        audioProcessor.setImageEnabled(imageEnableButton_.getToggleState());
    };
    addAndMakeVisible(imageEnableButton_);

    imageRateSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    imageRateSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    imageRateSlider_.setRange(0.0, 20.0, 0.1);
    imageRateSlider_.setValue(2.0, juce::dontSendNotification);
    imageRateSlider_.setTooltip("Image advance rate (frames per second)");
    imageRateSlider_.onValueChange = [this]
    {
        audioProcessor.setImageRate(static_cast<float>(imageRateSlider_.getValue()));
        updateImageReadouts();
    };
    addAndMakeVisible(imageRateSlider_);

    addCyberButton(imageLoopButton_);
    imageLoopButton_.setClickingTogglesState(true);
    imageLoopButton_.setToggleState(true, juce::dontSendNotification);
    imageLoopButton_.setTooltip("Loop the image (off = hold the last frame)");
    imageLoopButton_.onClick = [this]
    {
        audioProcessor.setImageLoop(imageLoopButton_.getToggleState());
    };
    addAndMakeVisible(imageLoopButton_);

    imageFrameSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    imageFrameSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    imageFrameSlider_.setRange(0.0, 1.0, 1.0);   // widened once a sample is loaded
    imageFrameSlider_.setTooltip("Image frame being edited in the spectrum EDITOR view");
    imageFrameSlider_.onValueChange = [this]
    {
        audioProcessor.setImageEditFrame(
            static_cast<int>(std::lround(imageFrameSlider_.getValue())));
        spectrumEditorCanvas_.setPartials(audioProcessor.getEditedPartials());
        updateImageReadouts();
    };
    addAndMakeVisible(imageFrameSlider_);

    for (auto* l : { &imageRateReadout_, &imageFrameReadout_ })
    {
        ana::CyberpunkTheme::styleReadout(*l);
        addAndMakeVisible(*l);
    }

    updateImageReadouts();

    imageStatusLabel_.setFont(ana::CyberpunkTheme::getCyberFont(10.0f));
    imageStatusLabel_.setJustificationType(juce::Justification::centredLeft);
    imageStatusLabel_.setColour(juce::Label::textColourId, ana::CyberpunkTheme::fg_.withAlpha(0.75f));
    addAndMakeVisible(imageStatusLabel_);

    // Spectral freeze (P5): holds the output spectrum post-effects
    addCyberButton(freezeButton_);
    freezeButton_.setClickingTogglesState(true);
    freezeButton_.setTooltip("Freeze the current output spectrum");
    freezeButton_.onClick = [this]
    {
        audioProcessor.setSpectralFreezeEnabled(freezeButton_.getToggleState());
    };
    addAndMakeVisible(freezeButton_);

    addCyberButton(freezeTrigButton_);
    freezeTrigButton_.setTooltip("One-shot freeze: capture the spectrum now");
    freezeTrigButton_.onClick = [this]
    {
        audioProcessor.triggerSpectralFreeze();
        freezeButton_.setToggleState(true, juce::dontSendNotification);
    };
    addAndMakeVisible(freezeTrigButton_);

    static const char* freezeModeNames[] = { "SNAPSHOT", "ACCUMULATE", "MOTION", "REVERSE" };
    for (int i = 0; i < 4; ++i)
        freezeModeCombo_.addItem(freezeModeNames[i], i + 1);
    freezeModeCombo_.setSelectedId(1, juce::dontSendNotification);
    freezeModeCombo_.setTooltip("Freeze algorithm");
    freezeModeCombo_.onChange = [this]
    {
        audioProcessor.setSpectralFreezeMode(freezeModeCombo_.getSelectedId() - 1);
    };
    addAndMakeVisible(freezeModeCombo_);

    freezeMixSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    freezeMixSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    freezeMixSlider_.setRange(0.0, 1.0, 0.01);
    freezeMixSlider_.setValue(0.5, juce::dontSendNotification);
    freezeMixSlider_.setTooltip("Frozen / live blend");
    freezeMixSlider_.onValueChange = [this]
    {
        audioProcessor.setSpectralFreezeMix(static_cast<float>(freezeMixSlider_.getValue()));
        freezeMixReadout_.setText(ana::CyberpunkTheme::formatPercent(
                                      static_cast<float>(freezeMixSlider_.getValue())),
                                  juce::dontSendNotification);
    };
    addAndMakeVisible(freezeMixSlider_);
    ana::CyberpunkTheme::styleReadout(freezeMixReadout_);
    freezeMixReadout_.setText("50%", juce::dontSendNotification);
    addAndMakeVisible(freezeMixReadout_);

    //==============================================================================
    // Center 鈥?Visual feedback + view selector
    ANA_CRUMB("ed:center-start");
    addAndMakeVisible(liveSpectrumPanel_);
    addAndMakeVisible(feedbackPanel_);
    feedbackPanel_.setVisible(false);
    addAndMakeVisible(waterfallDisplay_);
    waterfallDisplay_.setVisible(false);
    addAndMakeVisible(spectrumEditorCanvas_);
    spectrumEditorCanvas_.setVisible(false);
    spectrumEditorCanvas_.onPartialEdited = [this](const ana::PartialDataSIMD& edited)
    {
        audioProcessor.setEditedPartials(edited);
    };
    addAndMakeVisible(particleDisplay_);
    particleDisplay_.setVisible(false);
    particleDisplay_.setParticleSystem(&audioProcessor.getParticleSystem());

    // Time x partial image editor (P6b): draw the harmonic image directly.
    addAndMakeVisible(partialEditorCanvas_);
    partialEditorCanvas_.setVisible(false);
    partialEditorCanvas_.onEdited = [this]
    {
        audioProcessor.applyImageFromPartialData(partialEditorCanvas_.getModifiedPartialData());
    };

    for (auto* b : { &imgUndoButton_, &imgRedoButton_, &imgClearButton_,
                     &imgNormButton_, &imgSmoothButton_ })
    {
        addCyberButton(*b);
        addAndMakeVisible(*b);
        b->setVisible(false);
    }
    imgUndoButton_.setTooltip("Undo the last image stroke");
    imgRedoButton_.setTooltip("Redo the last undone stroke");
    imgClearButton_.setTooltip("Clear the whole image");
    imgNormButton_.setTooltip("Normalise the image to full scale");
    imgSmoothButton_.setTooltip("Smooth the image (3x3)");
    ana::CyberpunkTheme::styleReadout(imgHintLabel_);
    imgHintLabel_.setJustificationType(juce::Justification::centredRight);
    imgHintLabel_.setText("DRAG = PAINT   RIGHT-DRAG = PAN   WHEEL = ZOOM",
                          juce::dontSendNotification);
    addAndMakeVisible(imgHintLabel_);
    imgHintLabel_.setVisible(false);

    imgUndoButton_.onClick   = [this] { partialEditorCanvas_.undo(); };
    imgRedoButton_.onClick   = [this] { partialEditorCanvas_.redo(); };
    imgClearButton_.onClick  = [this] { partialEditorCanvas_.clear(); };
    imgNormButton_.onClick   = [this] { partialEditorCanvas_.normalize(); };
    imgSmoothButton_.onClick = [this] { partialEditorCanvas_.smooth(); };

    viewModeCombo_.addItem("LIVE", 1);
    viewModeCombo_.addItem("PARTIALS", 2);
    viewModeCombo_.addItem("WATERFALL", 3);
    viewModeCombo_.addItem("EDITOR", 4);
    viewModeCombo_.addItem("3D", 5);
    viewModeCombo_.addItem("SCOPE", 6);
    viewModeCombo_.addItem("PARTICLES", 7);
    viewModeCombo_.addItem("IMAGE", 8);
    viewModeCombo_.setSelectedId(1);
    viewModeCombo_.onChange = [this] { onViewModeChanged(); };
    viewModeCombo_.setTooltip("View mode: LIVE/PARTIALS/WATERFALL/EDITOR/3D/SCOPE");
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

    addAndMakeVisible(envPage_);
    envPage_.onEnvelopeEdited = [this] { modPanel_.syncFromProcessor(); };
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
    // Page tabs (Serum-style pagination)
    pageTabs_.onTabChanged = [this](int page) { setActivePage(page); };
    addAndMakeVisible(pageTabs_);
    setActivePage(0);

    // Apply the persisted visual theme last, remapping every colour captured by
    // the components created above.
    {
        const int storedTheme = audioProcessor.getThemeIndex();
        if (storedTheme != ana::CyberpunkTheme::getThemeIndex())
        {
            const auto oldPalette = ana::ThemePalettes::get(ana::CyberpunkTheme::getThemeIndex());
            ana::CyberpunkTheme::getInstance().applyPalette(storedTheme);
            ana::CyberpunkTheme::remapComponentColours(*this, oldPalette);
            themeCombo_.setSelectedId(storedTheme + 1, juce::dontSendNotification);
        }
    }

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
    // Page strip
    r.titleBar = bounds.removeFromTop(28);
    r.statusBar = bounds.removeFromBottom(35);
    r.tabBar = bounds.removeFromBottom(22);

    // Pinned spectrum: 34% of the remainder, bounded so the content page
    // keeps a workable minimum height even at the 900x660 resize floor.
    const int minSpec = 120;
    const int minContent = 120;
    int specH = static_cast<int>(bounds.getHeight() * 0.34f);
    specH = juce::jlimit(minSpec,
                         juce::jmax(minSpec, bounds.getHeight() - minContent),
                         specH);
    r.spectrum = bounds.removeFromTop(specH);
    r.content = bounds;
}

//==============================================================================
static const char* kPageNames[] = { "TIMBRE", "FILTER", "MOD", "SEQ", "FX", "MASTER", "ENV", "EVO" };

void AnaPlugAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    Regions r;
    computeRegions(bounds, r);

    // Background
    g.fillAll(ana::CyberpunkTheme::bg_);
    ana::CyberpunkTheme::drawGridBackground(g, getLocalBounds());

    // Region borders
    ana::CyberpunkTheme::drawPanelBorder(g, r.spectrum, "SPECTRUM", ana::CyberpunkTheme::cyan_);
    ana::CyberpunkTheme::drawPanelBorder(g, r.content,
        kPageNames[juce::jlimit(0, 7, activePage_)], ana::CyberpunkTheme::magenta_);
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
    synthModeButton_.setBounds(titleRect.removeFromRight(70).reduced(0, 3));
    importButton_.setBounds(titleRect.removeFromRight(84).reduced(0, 3));
    themeCombo_.setBounds(titleRect.removeFromRight(96).reduced(0, 3));
    presetButton_.setBounds(titleRect.removeFromRight(150));

    // -- Pinned spectrum (embedded view selector at top-right, clear of the border title) --
    auto spec = r.spectrum.reduced(6, 4);
    viewModeCombo_.setBounds(spec.removeFromTop(18).removeFromRight(120));
    auto fbArea = spec;
    liveSpectrumPanel_.setBounds(fbArea.reduced(2));
    feedbackPanel_.setBounds(fbArea.reduced(2));
    waterfallDisplay_.setBounds(fbArea.reduced(2));
    if (waveformDisplay_)
        waveformDisplay_->setBounds(fbArea.reduced(2));
    spectrumEditorCanvas_.setBounds(fbArea.reduced(2));
    particleDisplay_.setBounds(fbArea.reduced(2));

    {
        auto imgArea = fbArea.reduced(2);
        auto imgTools = imgArea.removeFromTop(18).reduced(1);
        imgUndoButton_.setBounds(imgTools.removeFromLeft(56).reduced(1));
        imgRedoButton_.setBounds(imgTools.removeFromLeft(56).reduced(1));
        imgClearButton_.setBounds(imgTools.removeFromLeft(58).reduced(1));
        imgNormButton_.setBounds(imgTools.removeFromLeft(54).reduced(1));
        imgSmoothButton_.setBounds(imgTools.removeFromLeft(66).reduced(1));
        imgHintLabel_.setBounds(imgTools.reduced(4, 0));
        partialEditorCanvas_.setBounds(imgArea);
    }

    // -- Page tab strip --
    pageTabs_.setBounds(r.tabBar);

    // -- Active page content --
    auto ca = r.content.reduced(6, 3);
    switch (activePage_)
    {
        case 0: // TIMBRE
        {
            auto imageStrip = ca.removeFromBottom(ana::CyberpunkTheme::kControlHeight).reduced(2, 0);
            imageEnableButton_.setBounds(imageStrip.removeFromLeft(56).reduced(2, 0));
            imageRateSlider_.setBounds(imageStrip.removeFromLeft(ana::CyberpunkTheme::kSliderWidth).reduced(4, 0));
            imageRateReadout_.setBounds(imageStrip.removeFromLeft(ana::CyberpunkTheme::kReadoutWidth));
            imageLoopButton_.setBounds(imageStrip.removeFromLeft(46).reduced(2, 0));
            imageFrameSlider_.setBounds(imageStrip.removeFromLeft(ana::CyberpunkTheme::kSliderWidth).reduced(4, 0));
            imageFrameReadout_.setBounds(imageStrip.removeFromLeft(ana::CyberpunkTheme::kReadoutWidth));
            imageStatusLabel_.setBounds(imageStrip);

            auto genStrip = ca.removeFromBottom(ana::CyberpunkTheme::kControlHeight).reduced(2, 0);
            genLabel_.setBounds(genStrip.removeFromLeft(74));
            genEnableButton_.setBounds(genStrip.removeFromLeft(42).reduced(2, 0));
            genRandomButton_.setBounds(genStrip.removeFromLeft(42).reduced(2, 0));
            genCaptureButton_.setBounds(genStrip.removeFromLeft(44).reduced(2, 0));
            genPresetCombo_.setBounds(genStrip.removeFromLeft(96).reduced(2, 0));
            genMixSlider_.setBounds(genStrip.removeFromLeft(
                juce::jmax(80, genStrip.getWidth() - ana::CyberpunkTheme::kReadoutWidth)).reduced(4, 0));
            genMixReadout_.setBounds(genStrip);

            auto blendStrip = ca.removeFromBottom(22).reduced(40, 0);
            timbreBlendReadout_.setBounds(blendStrip.removeFromRight(ana::CyberpunkTheme::kReadoutWidth));
            timbreBlendSlider_.setBounds(blendStrip);
            timbreBlendLabel_.setBounds(blendStrip.translated(0, -16));

            const int colW = juce::jmax(1, ca.getWidth() / 3);
            timbreAPanel_.setBounds(ca.removeFromLeft(colW));
            timbreBPanel_.setBounds(ca.removeFromLeft(colW));
            if (xyPad_)
                xyPad_->setBounds(ca.reduced(2));
            break;
        }

        case 1: // FILTER
            filterPanel_.setBounds(ca);
            break;

        case 2: // MOD
        {
            const int macroH = juce::jmin(70, juce::jmax(1, ca.getHeight() / 3));
            macroPanel_.setBounds(ca.removeFromTop(macroH));
            modViewport_.setBounds(ca.reduced(0, 2));
            break;
        }

        case 3: // SEQ
            sequencerPanel_.setBounds(ca);
            break;

        case 4: // FX
        {
            auto fxArea = ca;
            auto fxPresetRow = fxArea.removeFromTop(16).reduced(2, 0);
            fxPresetLabel_.setBounds(fxPresetRow.removeFromLeft(52));
            effectPresetCombo_.setBounds(fxPresetRow.reduced(0, 1));
            auto specRow = fxArea.removeFromTop(18).reduced(pad);
            prismButton_.setBounds(specRow.removeFromLeft(specRow.getWidth() / 3).reduced(1));
            blurButton_.setBounds(specRow.removeFromLeft(specRow.getWidth() / 2).reduced(1));
            harmButton_.setBounds(specRow.reduced(1));
            auto vocalRow = fxArea.removeFromTop(18).reduced(pad);
            vocalCharacterLabel_.setBounds(vocalRow.removeFromLeft(34));
            vocalCharacterCombo_.setBounds(vocalRow.reduced(0, 1));

            auto freezeRow = fxArea.removeFromTop(ana::CyberpunkTheme::kControlHeight).reduced(pad);
            freezeButton_.setBounds(freezeRow.removeFromLeft(66).reduced(1));
            freezeTrigButton_.setBounds(freezeRow.removeFromLeft(46).reduced(1));
            freezeModeCombo_.setBounds(freezeRow.removeFromLeft(104).reduced(1));
            freezeMixSlider_.setBounds(freezeRow.removeFromLeft(
                juce::jmax(80, freezeRow.getWidth() - ana::CyberpunkTheme::kReadoutWidth)).reduced(4, 0));
            freezeMixReadout_.setBounds(freezeRow);

            effectRack_.setBounds(fxArea.reduced(1, pad));
            break;
        }

        case 5: // MASTER
        {
            auto ba = ca;
            auto uniArea = ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.24f)).reduced(pad);
            auto voiceArea = ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.24f)).reduced(pad);
            auto arpArea = ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.22f)).reduced(pad);
            transportBar_.setBounds(ba.removeFromLeft(static_cast<int>(ba.getWidth() * 0.20f)).reduced(pad));
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
            break;
        }

        case 6: // ENV
            envPage_.setBounds(ca);
            break;

        case 7: // EVO (DNA)
            if (evolutionPanel != nullptr)
                evolutionPanel->setBounds(ca.reduced(4));
            break;
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
void AnaPlugAudioProcessorEditor::updateImageReadouts()
{
    imageRateReadout_.setText(ana::CyberpunkTheme::formatNumber(
                                  static_cast<float>(imageRateSlider_.getValue()), 1) + " f/s",
                              juce::dontSendNotification);

    const int frameCount = juce::jmax(1, audioProcessor.getImageFrameCount());
    imageFrameReadout_.setText(juce::String(audioProcessor.getImageEditFrame())
                               + "/" + juce::String(frameCount),
                               juce::dontSendNotification);
}

void AnaPlugAudioProcessorEditor::setActivePage(int page)
{
    if (page < 0 || page > 7)
        return;
    activePage_ = page;

    // Show only the active page's members
    timbreAPanel_.setVisible(page == 0);
    timbreBPanel_.setVisible(page == 0);
    timbreBlendSlider_.setVisible(page == 0);
    timbreBlendLabel_.setVisible(page == 0);
    timbreBlendReadout_.setVisible(page == 0);
    imageEnableButton_.setVisible(page == 0);
    imageRateSlider_.setVisible(page == 0);
    imageLoopButton_.setVisible(page == 0);
    imageFrameSlider_.setVisible(page == 0);
    imageRateReadout_.setVisible(page == 0);
    imageFrameReadout_.setVisible(page == 0);
    imageStatusLabel_.setVisible(page == 0);
    genLabel_.setVisible(page == 0);
    genEnableButton_.setVisible(page == 0);
    genRandomButton_.setVisible(page == 0);
    genCaptureButton_.setVisible(page == 0);
    genPresetCombo_.setVisible(page == 0);
    genMixSlider_.setVisible(page == 0);
    genMixReadout_.setVisible(page == 0);
    if (xyPad_)
        xyPad_->setVisible(page == 0);

    filterPanel_.setVisible(page == 1);

    macroPanel_.setVisible(page == 2);
    modViewport_.setVisible(page == 2);

    sequencerPanel_.setVisible(page == 3);

    fxPresetLabel_.setVisible(page == 4);
    effectPresetCombo_.setVisible(page == 4);
    prismButton_.setVisible(page == 4);
    blurButton_.setVisible(page == 4);
    harmButton_.setVisible(page == 4);
    vocalCharacterLabel_.setVisible(page == 4);
    vocalCharacterCombo_.setVisible(page == 4);
    freezeButton_.setVisible(page == 4);
    freezeTrigButton_.setVisible(page == 4);
    freezeModeCombo_.setVisible(page == 4);
    freezeMixSlider_.setVisible(page == 4);
    freezeMixReadout_.setVisible(page == 4);
    effectRack_.setVisible(page == 4);

    transportBar_.setVisible(page == 5);
    masterSection_.setVisible(page == 5);
    unisonTitle_.setVisible(page == 5);
    unisonCountSlider_.setVisible(page == 5);
    unisonCountLabel_.setVisible(page == 5);
    unisonDetuneSlider_.setVisible(page == 5);
    unisonDetuneLabel_.setVisible(page == 5);
    unisonSpreadSlider_.setVisible(page == 5);
    unisonSpreadLabel_.setVisible(page == 5);
    voiceTitle_.setVisible(page == 5);
    voiceModeCombo_.setVisible(page == 5);
    portamentoTimeSlider_.setVisible(page == 5);
    portamentoTimeLabel_.setVisible(page == 5);
    portamentoCurveCombo_.setVisible(page == 5);
    portamentoCurveLabel_.setVisible(page == 5);
    arpTitle_.setVisible(page == 5);
    arpPatternCombo_.setVisible(page == 5);
    arpRateSlider_.setVisible(page == 5);
    arpRateLabel_.setVisible(page == 5);
    arpGateSlider_.setVisible(page == 5);
    arpGateLabel_.setVisible(page == 5);

    envPage_.setVisible(page == 6);

    // DNA evolution panel is created on first use (DNA EVOLVE button / EVO tab).
    if (evolutionPanel != nullptr)
        evolutionPanel->setVisible(page == 7);

    resized();
}

//==============================================================================
void AnaPlugAudioProcessorEditor::timerCallback()
{
    // MIDI Learn: indicator blink, timeout, parameter polling
    updateMidiLearnState();

    // Live spectrum / scope views share one scratch buffer, and the whole block
    // is skipped when neither view is up (the FFT + repaint is not free).
    {
        const bool liveUp  = liveSpectrumPanel_.isVisible();
        const bool scopeUp = (waveformDisplay_ != nullptr && waveformDisplay_->isVisible());

        if ((liveUp || scopeUp) && audioProcessor.getScopeOutput(scopeScratch_))
        {
            if (liveUp)
                liveSpectrumPanel_.updateFromSamples(scopeScratch_.data(),
                                                     static_cast<int>(scopeScratch_.size()));

            if (scopeUp)
            {
                waveformDisplay_->setSamples(scopeScratch_);
                waveformDisplay_->setPlaybackPosition(
                    static_cast<double>(audioProcessor.getPlaybackPosition()
                                        % audioProcessor.kScopeBufferSize));
            }
        }
    }

    // Image editor: pull the analysis grid when a new sample appears (the frame
    // count changes); existing edits are never reloaded over.
    if (partialEditorCanvas_.isVisible() && audioProcessor.isEngineLoaded())
    {
        const auto& imagePd = audioProcessor.getEngine().getPartialData();
        if (partialEditorCanvas_.getNumFrames() != static_cast<int>(imagePd.frames.size()))
            partialEditorCanvas_.setPartialData(imagePd);
    }

    // Authoritative edited-set changes (sample load, image frame switch) must
    // reach the spectrum editor canvas even in SYNTH mode, where playback does
    // not feed it.  Version-gated so in-progress canvas edits are never clobbered.
    if (spectrumEditorCanvas_.isVisible())
    {
        const int editVersion = audioProcessor.getEditedPartialsVersion();
        if (editVersion != lastCanvasPartialsVersion_)
        {
            lastCanvasPartialsVersion_ = editVersion;
            spectrumEditorCanvas_.setPartials(audioProcessor.getEditedPartials());
        }
    }

    // Partial-derived views only need this conversion while one of them is
    // actually visible.  (The old linear timestamp scan was dead work: the SIMD
    // conversion always reads the most recent frame.)
    if ((feedbackPanel_.isVisible() || waterfallDisplay_.isVisible()
         || spectrumEditorCanvas_.isVisible())
        && audioProcessor.isEngineLoaded())
    {
        const auto& partialData = audioProcessor.getEngine().getPartialData();
        if (! partialData.frames.empty())
        {
            const ana::PartialDataSIMD simd = ana::PartialDataSIMD::fromPartialData(partialData);

            if (feedbackPanel_.isVisible())
                feedbackPanel_.updatePartials(simd);

            if (waterfallDisplay_.isVisible())
                waterfallDisplay_.updatePartials(simd);

            if (spectrumEditorCanvas_.isVisible() && ! audioProcessor.isSynthMode())
                spectrumEditorCanvas_.setPartials(simd);
        }
    }

    if (audioProcessor.flattenPending())
        statusLabel_.setText(">> PITCH FLATTENING <<", juce::dontSendNotification);

    // --- Macro visual update: sync slider from controller, update ring colours ---
    if (activePage_ == 2)
        macroPanel_.updateFromController();

    // XY Pad 鈫?processor parameter mapping
    // X axis is already written to morphAmount via setXParameter binding
    // Y axis 鈫?apply based on selected target
    if (xyPad_ != nullptr && activePage_ == 0)
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
    // Only the active page's panels are synced (hidden pages need no UI work).
    if (activePage_ == 2)
        modPanel_.syncFromProcessor();
    else if (activePage_ == 6)
        envPage_.syncFromProcessor();

    // Spectral particle view (P5): physics tick while the view is active
    if (viewModeCombo_.getSelectedId() == 7)
    {
        audioProcessor.advanceParticles(1.0 / 30.0);
        particleDisplay_.repaint();
    }

    // --- Step Sequencer: sync UI from processor state ---
    if (activePage_ == 3)
        sequencerPanel_.updateFromSequencer();

    // --- Update filter visualization with live frequency response ---
    if (activePage_ == 1)
        filterPanel_.updateFrequencyResponse();

    // --- Timbre shape / blend sync (preset reload, MIDI learn, etc.) ---
    if (activePage_ == 0)
    {
        auto sync = [](juce::Slider& s, float v)
        {
            if (std::abs(static_cast<float>(s.getValue()) - v) > 0.001f)
                s.setValue(static_cast<double>(v), juce::dontSendNotification);
        };
        sync(timbreAPanel_.getBrightSlider(), audioProcessor.getTimbreBright(true));
        sync(timbreAPanel_.getBlurSlider(),   audioProcessor.getTimbreBlur(true));
        sync(timbreAPanel_.getHpfSlider(),    audioProcessor.getTimbreHpf(true));
        sync(timbreBPanel_.getBrightSlider(), audioProcessor.getTimbreBright(false));
        sync(timbreBPanel_.getBlurSlider(),   audioProcessor.getTimbreBlur(false));
        sync(timbreBPanel_.getHpfSlider(),    audioProcessor.getTimbreHpf(false));
        sync(timbreBlendSlider_,              audioProcessor.getTimbreBlend());
        timbreBlendReadout_.setText(ana::CyberpunkTheme::formatPercent(
                                        static_cast<float>(timbreBlendSlider_.getValue())),
                                    juce::dontSendNotification);

        // Generative timbre designer (P5)
        if (genEnableButton_.getToggleState() != audioProcessor.isGenerativeTimbreEnabled())
            genEnableButton_.setToggleState(audioProcessor.isGenerativeTimbreEnabled(),
                                            juce::dontSendNotification);
        if (std::abs(static_cast<float>(genMixSlider_.getValue())
                     - audioProcessor.getGenerativeTimbreMix()) > 0.001f)
            genMixSlider_.setValue(static_cast<double>(audioProcessor.getGenerativeTimbreMix()),
                                   juce::dontSendNotification);
        genMixReadout_.setText(ana::CyberpunkTheme::formatPercent(
                                   static_cast<float>(genMixSlider_.getValue())),
                               juce::dontSendNotification);

        // Time-varying harmonic image (P6b)
        if (imageEnableButton_.getToggleState() != audioProcessor.isImageEnabled())
            imageEnableButton_.setToggleState(audioProcessor.isImageEnabled(),
                                              juce::dontSendNotification);
        if (std::abs(static_cast<float>(imageRateSlider_.getValue())
                     - audioProcessor.getImageRate()) > 0.001f)
            imageRateSlider_.setValue(static_cast<double>(audioProcessor.getImageRate()),
                                      juce::dontSendNotification);
        if (imageLoopButton_.getToggleState() != audioProcessor.isImageLoop())
            imageLoopButton_.setToggleState(audioProcessor.isImageLoop(), juce::dontSendNotification);
        {
            const int frameCount = audioProcessor.getImageFrameCount();
            const double maxFrame = juce::jmax(1.0, static_cast<double>(frameCount - 1));

            if (imageFrameSlider_.getMaximum() != maxFrame)
                imageFrameSlider_.setRange(0.0, maxFrame, 1.0);

            if (std::abs(static_cast<float>(imageFrameSlider_.getValue())
                         - static_cast<float>(audioProcessor.getImageEditFrame())) > 0.5f)
                imageFrameSlider_.setValue(static_cast<double>(audioProcessor.getImageEditFrame()),
                                           juce::dontSendNotification);

            updateImageReadouts();

            const juce::String imgText = juce::String(frameCount) + " FRAMES";
            if (imageStatusLabel_.getText() != imgText)
                imageStatusLabel_.setText(imgText, juce::dontSendNotification);
        }
    }

    // --- Spectral freeze sync (FX page) ---
    if (activePage_ == 4)
    {
        if (freezeButton_.getToggleState() != audioProcessor.isSpectralFreezeEnabled())
            freezeButton_.setToggleState(audioProcessor.isSpectralFreezeEnabled(),
                                         juce::dontSendNotification);
        if (freezeModeCombo_.getSelectedId() != audioProcessor.getSpectralFreezeMode() + 1)
            freezeModeCombo_.setSelectedId(audioProcessor.getSpectralFreezeMode() + 1,
                                           juce::dontSendNotification);
        if (std::abs(static_cast<float>(freezeMixSlider_.getValue())
                     - audioProcessor.getSpectralFreezeMix()) > 0.001f)
            freezeMixSlider_.setValue(static_cast<double>(audioProcessor.getSpectralFreezeMix()),
                                      juce::dontSendNotification);
        freezeMixReadout_.setText(ana::CyberpunkTheme::formatPercent(
                                      static_cast<float>(freezeMixSlider_.getValue())),
                                  juce::dontSendNotification);
    }

    // --- SYNTH mode status ---
    if (audioProcessor.isSynthMode())
        statusLabel_.setText(">> SYNTH: " + juce::String(audioProcessor.getActivePartialCount())
                             + " PARTIALS <<", juce::dontSendNotification);
}

//==============================================================================
//==============================================================================
void AnaPlugAudioProcessorEditor::onViewModeChanged()
{
    const int mode = viewModeCombo_.getSelectedId();

    // Hide all view panels first
    liveSpectrumPanel_.setVisible(false);
    feedbackPanel_.setVisible(false);
    waterfallDisplay_.setVisible(false);
    spectrumEditorCanvas_.setVisible(false);
    if (waveformDisplay_)
        waveformDisplay_->setVisible(false);
    particleDisplay_.setVisible(false);
    audioProcessor.setParticlesEnabled(false);
    partialEditorCanvas_.setVisible(false);
    imgHintLabel_.setVisible(false);
    for (auto* b : { &imgUndoButton_, &imgRedoButton_, &imgClearButton_,
                     &imgNormButton_, &imgSmoothButton_ })
        b->setVisible(false);

    switch (mode)
    {
        case 1: // LIVE 鈥?real-time output spectrum (always available)
            liveSpectrumPanel_.setVisible(true);
            break;

        case 2: // PARTIALS 鈥?classic bar display
            feedbackPanel_.setVisible(true);
            break;

        case 3: // WATERFALL 鈥?3D waterfall spectral view
            waterfallDisplay_.setVisible(true);
            break;

        case 4: // EDITOR 鈥?2D spectrum editor canvas
            spectrumEditorCanvas_.setVisible(true);
            spectrumEditorCanvas_.set3DEnabled(false);
            break;

        case 5: // 3D 鈥?spectrum editor with OpenGL 3D waterfall
            spectrumEditorCanvas_.setVisible(true);
            spectrumEditorCanvas_.set3DEnabled(true);
            break;

        case 6: // SCOPE - real-time oscilloscope
            if (waveformDisplay_)
                waveformDisplay_->setVisible(true);
            break;

        case 7: // PARTICLES - spectral particle visualisation
            particleDisplay_.setVisible(true);
            audioProcessor.setParticlesEnabled(true);
            audioProcessor.syncParticlesFromEdit();
            break;

        case 8: // IMAGE - draw the time x partial harmonic image
            partialEditorCanvas_.setVisible(true);
            imgHintLabel_.setVisible(true);
            for (auto* b : { &imgUndoButton_, &imgRedoButton_, &imgClearButton_,
                             &imgNormButton_, &imgSmoothButton_ })
                b->setVisible(true);

            if (audioProcessor.isEngineLoaded())
            {
                const auto& pd = audioProcessor.getEngine().getPartialData();
                if (partialEditorCanvas_.getNumFrames() != static_cast<int>(pd.frames.size()))
                    partialEditorCanvas_.setPartialData(pd);
            }
            break;

        default: // fallback to live
            liveSpectrumPanel_.setVisible(true);
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
    // The panel lives on its own page now (instead of a transient call-out).
    if (evolutionPanel == nullptr)
    {
        evolutionPanel = std::make_unique<ana::EvolutionPanel>(audioProcessor);
        addAndMakeVisible(*evolutionPanel);
    }

    // Route through the tab strip so the highlighted tab follows along.
    pageTabs_.setActive(7);
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
    MidiLearnSliderInfo info;
    info.paramId = paramId;
    info.target  = target;
    midiLearnSliders_[&slider] = std::move(info);
}

//==============================================================================
void AnaPlugAudioProcessorEditor::setupMidiLearnForEffectKnob(juce::Slider& knob,
                                                              const juce::String& paramId,
                                                              int slotIndex, int paramIndex)
{
    knob.addMouseListener(this, false);

    auto* processor = &audioProcessor;

    // Slot indices are resolved lazily: the rack rebuilds its widgets (and the
    // effects behind them move) on every add/remove/reorder, so capturing an
    // EffectBase* here would dangle.  Out-of-range slots are simply ignored.
    auto resolve = [processor, slotIndex, paramIndex]() -> ana::EffectBase*
    {
        auto& chain = processor->getEffectsChain();
        if (slotIndex < 0 || slotIndex >= chain.getNumEffects())
            return nullptr;
        return chain.getEffect(slotIndex).effect.get();
    };

    MidiLearnSliderInfo info;
    info.paramId = paramId;
    info.target  = nullptr;
    info.targetSetter = [resolve](float v)
    {
        if (auto* effect = resolve())
            effect->setParamValue(paramIndex, v);
    };
    info.targetGetter = [resolve]() -> float
    {
        if (auto* effect = resolve())
            return effect->getParamValue(paramIndex);
        return 0.0f;
    };

    midiLearnSliders_[&knob] = std::move(info);
    effectKnobSliders_[paramId] = &knob;

    // If this parameter was already learned (e.g. after a state load, or the
    // slot was rebuilt), point the existing mapping at the new knob.
    auto& stored = midiLearnSliders_[&knob];
    audioProcessor.getMidiLearn().reconnectTarget(paramId, stored.targetSetter,
                                                  stored.targetGetter);
}

//==============================================================================
void AnaPlugAudioProcessorEditor::refreshEffectKnobMidiLearn()
{
    // 1. Collect the knobs that exist right now (expanded slots only).
    effectKnobScratch_.clear();
    effectRack_.visitSlots([this](int, ana::EffectSlotWidget& slot)
    {
        auto* panel = slot.getParamPanel();
        if (panel == nullptr)
            return;

        const int slotIndex = slot.getSlotIndex();
        panel->visitKnobs([this, slotIndex](int paramIndex, juce::Slider& knob)
        {
            effectKnobScratch_.push_back({ &knob, slotIndex, paramIndex });
        });
    });

    auto isLive = [this](const juce::Slider* knob)
    {
        for (const auto& ref : effectKnobScratch_)
            if (ref.knob == knob)
                return true;
        return false;
    };

    // 2. Forget registrations whose knob was destroyed.  The pointers are no
    //    longer dereferenced anywhere after this point (no listener removal on
    //    a dead component).
    for (auto it = effectKnobSliders_.begin(); it != effectKnobSliders_.end(); )
    {
        if (isLive(it->second))
        {
            ++it;
            continue;
        }

        midiLearnSliders_.erase(it->second);
        it = effectKnobSliders_.erase(it);
    }

    // 3. Register the knobs that are new (or whose slot index changed).
    for (const auto& ref : effectKnobScratch_)
    {
        const juce::String paramId = "fx" + juce::String(ref.slot)
                                  + "_p" + juce::String(ref.param);

        auto registered = effectKnobSliders_.find(paramId);
        if (registered != effectKnobSliders_.end() && registered->second == ref.knob)
            continue;

        // The address may be reused by a different knob after a rebuild: drop
        // the stale registration for that slider first.
        auto existing = midiLearnSliders_.find(ref.knob);
        if (existing != midiLearnSliders_.end())
        {
            auto owner = effectKnobSliders_.find(existing->second.paramId);
            if (owner != effectKnobSliders_.end() && owner->second == ref.knob)
                effectKnobSliders_.erase(owner);

            ref.knob->removeMouseListener(this);
            midiLearnSliders_.erase(existing);
        }

        setupMidiLearnForEffectKnob(*ref.knob, paramId, ref.slot, ref.param);
    }
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
            const float lo = static_cast<float>(slider->getMinimum());
            const float hi = static_cast<float>(slider->getMaximum());

            if (infoCopy.target != nullptr)
                ml.startLearn(infoCopy.paramId, infoCopy.target, lo, hi);
            else
                ml.startLearn(infoCopy.paramId, infoCopy.targetSetter,
                              infoCopy.targetGetter, lo, hi);

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
    // Effect knobs are created/destroyed by the rack (expand, rebuild, remove),
    // so the registry is re-scanned before anything dereferences it.
    refreshEffectKnobMidiLearn();

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
        // Read through whichever target kind is connected (atomic or the
        // callback used by effect parameters).
        float currentValue = 0.0f;
        if (! midiLearn.getMappingValue(mapping.parameterId, currentValue))
            continue;

        // Find the UI control: static sliders first, then effect knobs.
        juce::Slider* slider = nullptr;
        auto sit = learnableSliders_.find(mapping.parameterId);
        if (sit != learnableSliders_.end())
            slider = sit->second;
        else
        {
            auto kit = effectKnobSliders_.find(mapping.parameterId);
            if (kit != effectKnobSliders_.end())
                slider = kit->second;
        }

        if (slider != nullptr)
        {
            const double currentSlider = slider->getValue();
            // Use a small epsilon to avoid redundant setValue calls
            if (std::abs(static_cast<double>(currentValue) - currentSlider) > 0.001)
                slider->setValue(static_cast<double>(currentValue), juce::sendNotificationSync);
        }

        // Also sync macro controller if this paramId is a macro
        if (mapping.targetParam != nullptr && mapping.parameterId.startsWith("macro_"))
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
