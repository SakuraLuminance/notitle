#include "GranularPage.h"
#include <cmath>

namespace ana
{

namespace
{
constexpr int kRowH   = CyberpunkTheme::kControlHeight;   // 20
constexpr int kLabelW = 64;
}

//==============================================================================
GranularPage::GranularPage(AnaPlugAudioProcessor& processor)
    : processor_(processor)
{
    // -- Enable toggle ------------------------------------------------------
    enableButton_.setClickingTogglesState(true);
    enableButton_.setTooltip("Enable the granular layer (right-click the knobs for MIDI Learn)");
    enableButton_.setColour(juce::TextButton::buttonColourId, CyberpunkTheme::bg_.brighter(0.08f));
    enableButton_.setColour(juce::TextButton::textColourOffId, CyberpunkTheme::fg_.withAlpha(0.55f));
    enableButton_.setColour(juce::TextButton::buttonOnColourId, CyberpunkTheme::cyan_.withAlpha(0.35f));
    enableButton_.setColour(juce::TextButton::textColourOnId, CyberpunkTheme::cyan_);
    enableButton_.onClick = [this]
    {
        processor_.setGranularEnabled(enableButton_.getToggleState());
    };
    addAndMakeVisible(enableButton_);

    // -- Mix ----------------------------------------------------------------
    mixSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    mixSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    mixSlider_.setRange(0.0, 100.0, 1.0);
    mixSlider_.setTooltip("Grain layer level mixed into the output");
    mixSlider_.setColour(juce::Slider::trackColourId, CyberpunkTheme::cyan_.withAlpha(0.5f));
    mixSlider_.setColour(juce::Slider::thumbColourId, CyberpunkTheme::cyan_);
    mixSlider_.onValueChange = [this]
    {
        processor_.setGrainMix(static_cast<float>(mixSlider_.getValue() / 100.0));
    };
    addAndMakeVisible(mixSlider_);
    addReadout(mixReadout_, "Grain layer level");

    // -- Grain size ---------------------------------------------------------
    sizeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    sizeSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sizeSlider_.setRange(1.0, 100.0, 0.5);
    sizeSlider_.setSkewFactorFromMidPoint(25.0);
    sizeSlider_.setTooltip("Grain duration in milliseconds (1-100)");
    sizeSlider_.setColour(juce::Slider::trackColourId, CyberpunkTheme::cyan_.withAlpha(0.5f));
    sizeSlider_.setColour(juce::Slider::thumbColourId, CyberpunkTheme::cyan_);
    sizeSlider_.onValueChange = [this]
    {
        processor_.setGrainSizeMs(static_cast<float>(sizeSlider_.getValue()));
    };
    addAndMakeVisible(sizeSlider_);
    addReadout(sizeReadout_, "Grain duration");

    // -- Density ------------------------------------------------------------
    densitySlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    densitySlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    densitySlider_.setRange(1.0, 1000.0, 1.0);
    densitySlider_.setSkewFactorFromMidPoint(60.0);
    densitySlider_.setTooltip("Grains spawned per second (1-1000)");
    densitySlider_.setColour(juce::Slider::trackColourId, CyberpunkTheme::cyan_.withAlpha(0.5f));
    densitySlider_.setColour(juce::Slider::thumbColourId, CyberpunkTheme::cyan_);
    densitySlider_.onValueChange = [this]
    {
        processor_.setGrainDensity(static_cast<float>(densitySlider_.getValue()));
    };
    addAndMakeVisible(densitySlider_);
    addReadout(densityReadout_, "Grains per second");

    // -- Position -----------------------------------------------------------
    positionSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    positionSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    positionSlider_.setRange(0.0, 100.0, 0.5);
    positionSlider_.setTooltip("Read position inside the sample (0% = start, 100% = end)");
    positionSlider_.setColour(juce::Slider::trackColourId, CyberpunkTheme::cyan_.withAlpha(0.5f));
    positionSlider_.setColour(juce::Slider::thumbColourId, CyberpunkTheme::cyan_);
    positionSlider_.onValueChange = [this]
    {
        processor_.setGrainPosition(static_cast<float>(positionSlider_.getValue() / 100.0));
    };
    addAndMakeVisible(positionSlider_);
    addReadout(positionReadout_, "Read position");

    // -- Pitch --------------------------------------------------------------
    pitchSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    pitchSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pitchSlider_.setRange(-24.0, 24.0, 1.0);
    pitchSlider_.setDoubleClickReturnValue(true, 0.0);
    pitchSlider_.setTooltip("Grain pitch shift in semitones (-24 to +24)");
    pitchSlider_.setColour(juce::Slider::trackColourId, CyberpunkTheme::cyan_.withAlpha(0.5f));
    pitchSlider_.setColour(juce::Slider::thumbColourId, CyberpunkTheme::cyan_);
    pitchSlider_.onValueChange = [this]
    {
        processor_.setGrainPitch(static_cast<float>(pitchSlider_.getValue()));
    };
    addAndMakeVisible(pitchSlider_);
    addReadout(pitchReadout_, "Pitch shift in semitones");

    // -- Window shape -------------------------------------------------------
    windowLabel_.setText("WINDOW", juce::dontSendNotification);
    windowLabel_.setFont(CyberpunkTheme::getCyberFont(9.0f, true));
    windowLabel_.setColour(juce::Label::textColourId, CyberpunkTheme::fg_.withAlpha(0.7f));
    addAndMakeVisible(windowLabel_);

    windowCombo_.addItem("HANN", 1);
    windowCombo_.addItem("TRIANGLE", 2);
    windowCombo_.addItem("GAUSSIAN", 3);
    windowCombo_.addItem("SINC", 4);
    windowCombo_.setSelectedId(1, juce::dontSendNotification);
    windowCombo_.setTooltip("Grain window shape");
    windowCombo_.onChange = [this]
    {
        processor_.setGrainWindow(windowCombo_.getSelectedId() - 1);
    };
    addAndMakeVisible(windowCombo_);

    // -- Position modulation ------------------------------------------------
    modLabel_.setText("MOD", juce::dontSendNotification);
    modLabel_.setFont(CyberpunkTheme::getCyberFont(9.0f, true));
    modLabel_.setColour(juce::Label::textColourId, CyberpunkTheme::fg_.withAlpha(0.7f));
    addAndMakeVisible(modLabel_);

    modCombo_.addItem("OFF", 1);
    modCombo_.addItem("LFO", 2);
    modCombo_.addItem("ENVELOPE", 3);
    modCombo_.addItem("RANDOM", 4);
    modCombo_.setSelectedId(1, juce::dontSendNotification);
    modCombo_.setTooltip("Position modulation source for the grain read head");
    modCombo_.onChange = [this]
    {
        processor_.setGrainModMode(modCombo_.getSelectedId() - 1);
    };
    addAndMakeVisible(modCombo_);

    modDepthSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    modDepthSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    modDepthSlider_.setRange(0.0, 100.0, 1.0);
    modDepthSlider_.setTooltip("Position modulation depth (fraction of the sample)");
    modDepthSlider_.setColour(juce::Slider::trackColourId, CyberpunkTheme::magenta_.withAlpha(0.5f));
    modDepthSlider_.setColour(juce::Slider::thumbColourId, CyberpunkTheme::magenta_);
    modDepthSlider_.onValueChange = [this]
    {
        processor_.setGrainModDepth(static_cast<float>(modDepthSlider_.getValue() / 100.0));
    };
    addAndMakeVisible(modDepthSlider_);
    addReadout(modDepthReadout_, "Position modulation depth");

    modRateSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    modRateSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    modRateSlider_.setRange(0.05, 10.0, 0.01);
    modRateSlider_.setSkewFactorFromMidPoint(1.0);
    modRateSlider_.setTooltip("Position modulation rate in Hz");
    modRateSlider_.setColour(juce::Slider::trackColourId, CyberpunkTheme::magenta_.withAlpha(0.5f));
    modRateSlider_.setColour(juce::Slider::thumbColourId, CyberpunkTheme::magenta_);
    modRateSlider_.onValueChange = [this]
    {
        processor_.setGrainModRate(static_cast<float>(modRateSlider_.getValue()));
    };
    addAndMakeVisible(modRateSlider_);
    addReadout(modRateReadout_, "Position modulation rate");

    modDepthLabel_.setText("DEPTH", juce::dontSendNotification);
    modDepthLabel_.setFont(CyberpunkTheme::getCyberFont(9.0f, true));
    modDepthLabel_.setColour(juce::Label::textColourId, CyberpunkTheme::fg_.withAlpha(0.7f));
    addAndMakeVisible(modDepthLabel_);

    modRateLabel_.setText("RATE", juce::dontSendNotification);
    modRateLabel_.setFont(CyberpunkTheme::getCyberFont(9.0f, true));
    modRateLabel_.setColour(juce::Label::textColourId, CyberpunkTheme::fg_.withAlpha(0.7f));
    addAndMakeVisible(modRateLabel_);

    // -- Status -------------------------------------------------------------
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setFont(CyberpunkTheme::getCyberFont(10.0f, true));
    statusLabel_.setColour(juce::Label::textColourId, CyberpunkTheme::fg_.withAlpha(0.6f));
    addAndMakeVisible(statusLabel_);

    syncFromProcessor();
}

//==============================================================================
void GranularPage::addReadout(juce::Label& readout, const juce::String& tooltip)
{
    readout.setText("-", juce::dontSendNotification);
    readout.setJustificationType(juce::Justification::centredRight);
    CyberpunkTheme::styleReadout(readout);
    readout.setTooltip(tooltip);
    addAndMakeVisible(readout);
}

void GranularPage::layoutSliderRow(juce::Rectangle<int>& area,
                                   juce::Label& label, juce::Slider& slider,
                                   juce::Label& readout)
{
    label.setBounds(area.removeFromLeft(kLabelW).reduced(2, 0));
    slider.setBounds(area.removeFromLeft(CyberpunkTheme::kSliderWidth));
    area.removeFromLeft(4);
    readout.setBounds(area.removeFromLeft(CyberpunkTheme::kReadoutWidth));
    area.removeFromLeft(12);
}

//==============================================================================
void GranularPage::resized()
{
    auto area = getLocalBounds().reduced(10);
    const int halfW = juce::jmax(1, area.getWidth() / 2);

    // Row 1: [GRAIN] [MIX ▬▬▬ readout]
    {
        auto row = area.removeFromTop(kRowH);
        enableButton_.setBounds(row.removeFromLeft(64).reduced(1));
        row.removeFromLeft(8);

        auto right = row.removeFromLeft(halfW);
        mixSlider_.setBounds(right.removeFromLeft(juce::jmax(60, right.getWidth()
                                                             - CyberpunkTheme::kReadoutWidth)));
        mixReadout_.setBounds(right);
        area.removeFromTop(4);
    }

    // Rows 2-3: two slider columns each
    for (int rowIndex = 0; rowIndex < 2; ++rowIndex)
    {
        auto row = area.removeFromTop(kRowH);

        auto left = row.removeFromLeft(halfW);
        auto right = row;

        if (rowIndex == 0)
        {
            layoutSliderRow(left, sizeLabel_, sizeSlider_, sizeReadout_);
            layoutSliderRow(right, densityLabel_, densitySlider_, densityReadout_);
        }
        else
        {
            layoutSliderRow(left, positionLabel_, positionSlider_, positionReadout_);
            layoutSliderRow(right, pitchLabel_, pitchSlider_, pitchReadout_);
        }

        area.removeFromTop(4);
    }

    // Row 4: window combo + modulation combo
    {
        auto row = area.removeFromTop(kRowH);
        auto left = row.removeFromLeft(halfW);
        windowLabel_.setBounds(left.removeFromLeft(52).reduced(2, 0));
        windowCombo_.setBounds(left.removeFromLeft(juce::jmin(110, left.getWidth())).reduced(1));

        auto right = row;
        modLabel_.setBounds(right.removeFromLeft(34).reduced(2, 0));
        modCombo_.setBounds(right.removeFromLeft(juce::jmin(110, right.getWidth())).reduced(1));
        area.removeFromTop(4);
    }

    // Row 5: modulation depth + rate
    {
        auto row = area.removeFromTop(kRowH);
        auto left = row.removeFromLeft(halfW);
        auto right = row;

        modDepthLabel_.setBounds(left.removeFromLeft(52).reduced(2, 0));
        modDepthSlider_.setBounds(left.removeFromLeft(juce::jmax(50, left.getWidth()
                                                                 - CyberpunkTheme::kReadoutWidth)));
        modDepthReadout_.setBounds(left);

        modRateLabel_.setBounds(right.removeFromLeft(40).reduced(2, 0));
        modRateSlider_.setBounds(right.removeFromLeft(juce::jmax(50, right.getWidth()
                                                                 - CyberpunkTheme::kReadoutWidth)));
        modRateReadout_.setBounds(right);
        area.removeFromTop(6);
    }

    // Grain cloud: whatever is left between the controls and the status line.
    cloudBounds_ = area.removeFromTop(juce::jmax(0, area.getHeight() - 18)).reduced(1);

    // Status line
    statusLabel_.setBounds(area.removeFromTop(16));
}

//==============================================================================
void GranularPage::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Canvas: theme background + token border, like the other pages' canvases
    g.setColour(CyberpunkTheme::bg_.darker(CyberpunkTheme::kCanvasBgDarken));
    g.fillRect(bounds);
    g.setColour(CyberpunkTheme::fg_.withAlpha(CyberpunkTheme::kCanvasBorderAlpha));
    g.drawRect(bounds, 1);

    if (! processor_.isGrainSourceReady())
    {
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.35f));
        g.setFont(CyberpunkTheme::getCyberFont(12.0f, true));
        g.drawText("NO SAMPLE LOADED - IMPORT A SAMPLE TO GRAIN",
                   bounds.reduced(8), juce::Justification::centred);
        return;
    }

    // -- Grain cloud ---------------------------------------------------------
    const auto cloud = cloudBounds_;
    if (cloud.getWidth() <= 2 || cloud.getHeight() <= 2)
        return;

    g.setColour(CyberpunkTheme::bg_.darker(CyberpunkTheme::kCanvasBgDarken));
    g.fillRect(cloud);
    g.setColour(CyberpunkTheme::fg_.withAlpha(CyberpunkTheme::kCanvasBorderAlpha));
    g.drawRect(cloud, 1);

    // Position guides at 25 / 50 / 75 % of the source.
    g.setColour(CyberpunkTheme::fg_.withAlpha(CyberpunkTheme::kCanvasGridAlpha));
    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine(cloud.getX() + cloud.getWidth() * i / 4,
                           static_cast<float>(cloud.getY() + 1),
                           static_cast<float>(cloud.getBottom() - 1));

    // The base read position the grains are centred on.
    const float baseX = static_cast<float>(cloud.getX())
                      + juce::jlimit(0.0f, 1.0f, processor_.getGrainPosition())
                            * static_cast<float>(cloud.getWidth() - 1);
    g.setColour(CyberpunkTheme::yellow_.withAlpha(0.30f));
    g.drawVerticalLine(static_cast<int>(baseX),
                       static_cast<float>(cloud.getY() + 1),
                       static_cast<float>(cloud.getBottom() - 1));

    // One bar per grain: x = read position, y = age (fresh at the top),
    // width = grain length in source units, colour = cyan -> magenta as it ages.
    if (cloudCount_ <= 0)
    {
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.30f));
        g.setFont(CyberpunkTheme::getCyberFont(9.0f, true));
        g.drawText("NO ACTIVE GRAINS", cloud, juce::Justification::centred);
        return;
    }

    for (int i = 0; i < cloudCount_; ++i)
    {
        const auto& s = cloud_[i];
        const float progress = juce::jlimit(0.0f, 1.0f, s.progress);
        const float x = static_cast<float>(cloud.getX())
                      + juce::jlimit(0.0f, 1.0f, s.position)
                            * static_cast<float>(cloud.getWidth() - 1);
        const float y = static_cast<float>(cloud.getY())
                      + progress * static_cast<float>(cloud.getHeight() - 1);
        const float w = juce::jmax(2.0f, s.duration * static_cast<float>(cloud.getWidth()));

        g.setColour(CyberpunkTheme::cyan_
                        .interpolatedWith(CyberpunkTheme::magenta_, progress)
                        .withAlpha(juce::jlimit(0.15f, 1.0f, s.amplitude)));
        g.fillRect(juce::Rectangle<float>(x - w * 0.5f, y - 1.0f, w, 2.0f));
    }
}

//==============================================================================
void GranularPage::syncFromProcessor()
{
    auto syncSlider = [](juce::Slider& s, double v)
    {
        if (std::abs(s.getValue() - v) > 0.001)
            s.setValue(v, juce::dontSendNotification);
    };

    if (enableButton_.getToggleState() != processor_.isGranularEnabled())
        enableButton_.setToggleState(processor_.isGranularEnabled(), juce::dontSendNotification);

    syncSlider(mixSlider_,      processor_.getGrainMix() * 100.0);
    syncSlider(sizeSlider_,     processor_.getGrainSizeMs());
    syncSlider(densitySlider_,  processor_.getGrainDensity());
    syncSlider(positionSlider_, processor_.getGrainPosition() * 100.0);
    syncSlider(pitchSlider_,    processor_.getGrainPitch());
    syncSlider(modDepthSlider_, processor_.getGrainModDepth() * 100.0);
    syncSlider(modRateSlider_,  processor_.getGrainModRate());

    if (windowCombo_.getSelectedId() != processor_.getGrainWindow() + 1)
        windowCombo_.setSelectedId(processor_.getGrainWindow() + 1, juce::dontSendNotification);
    if (modCombo_.getSelectedId() != processor_.getGrainModMode() + 1)
        modCombo_.setSelectedId(processor_.getGrainModMode() + 1, juce::dontSendNotification);

    // Value read-outs (every continuous control shows its number)
    mixReadout_.setText(CyberpunkTheme::formatPercent(static_cast<float>(mixSlider_.getValue())),
                        juce::dontSendNotification);
    sizeReadout_.setText(CyberpunkTheme::formatNumber(static_cast<float>(sizeSlider_.getValue()), 1) + " ms",
                         juce::dontSendNotification);
    densityReadout_.setText(CyberpunkTheme::formatNumber(static_cast<float>(densitySlider_.getValue()), 0) + " /s",
                            juce::dontSendNotification);
    positionReadout_.setText(CyberpunkTheme::formatPercent(static_cast<float>(positionSlider_.getValue())),
                             juce::dontSendNotification);
    pitchReadout_.setText(CyberpunkTheme::formatNumber(static_cast<float>(pitchSlider_.getValue()), 0) + " st",
                          juce::dontSendNotification);
    modDepthReadout_.setText(CyberpunkTheme::formatPercent(static_cast<float>(modDepthSlider_.getValue())),
                             juce::dontSendNotification);
    modRateReadout_.setText(CyberpunkTheme::formatNumber(static_cast<float>(modRateSlider_.getValue()), 2) + " Hz",
                            juce::dontSendNotification);

    // Grain cloud: the processor publishes a guarded copy once per audio block,
    // so this is a plain copy plus a repaint of the canvas only.
    cloudCount_ = processor_.getGrainVisualisation(cloud_, AnaPlugAudioProcessor::kGrainVisualMax);

    if (isShowing() && ! cloudBounds_.isEmpty())
        repaint(cloudBounds_);

    if (! processor_.isGrainSourceReady())
        statusLabel_.setText("STATUS :: WAITING FOR SAMPLE", juce::dontSendNotification);
    else
        statusLabel_.setText("STATUS :: " + juce::String(processor_.getActiveGrainCount())
                                 + " GRAINS ACTIVE  |  "
                                 + (processor_.isGranularEnabled() ? "GRAIN ON" : "GRAIN OFF"),
                             juce::dontSendNotification);
}

} // namespace ana
