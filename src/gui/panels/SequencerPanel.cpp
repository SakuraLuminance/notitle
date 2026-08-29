#include "SequencerPanel.h"
#include "PanelWidgets.h"
#include "../CyberpunkTheme.h"

namespace ana
{

SequencerPanel::SequencerPanel(AnaPlugAudioProcessor& processor)
    : processor_(processor)
{
    title_.setText("SEQ", juce::dontSendNotification);
    title_.setFont(CyberpunkTheme::getCyberFont(10.0f, true));
    title_.setColour(juce::Label::textColourId, CyberpunkTheme::yellow_);
    addAndMakeVisible(title_);

    playModeCombo_.addItem("FWD",  1);
    playModeCombo_.addItem("BWD",  2);
    playModeCombo_.addItem("P-P",  3);
    playModeCombo_.addItem("RND",  4);
    playModeCombo_.setSelectedId(1);
    playModeCombo_.onChange = [this]()
    {
        auto& seq = processor_.getStepSequencer();
        switch (playModeCombo_.getSelectedId())
        {
            case 1: seq.setPlayMode(SeqPlayMode::Forward);  break;
            case 2: seq.setPlayMode(SeqPlayMode::Backward); break;
            case 3: seq.setPlayMode(SeqPlayMode::PingPong); break;
            case 4: seq.setPlayMode(SeqPlayMode::Random);   break;
        }
    };
    playModeCombo_.setTooltip("Sequencer play mode: FWD/BWD/PING-PONG/RANDOM");
    addAndMakeVisible(playModeCombo_);

    clockSourceCombo_.addItem("INT", 1);
    clockSourceCombo_.addItem("EXT", 2);
    clockSourceCombo_.setSelectedId(1);
    clockSourceCombo_.onChange = [this]()
    {
        auto& seq = processor_.getStepSequencer();
        seq.setClockSource(clockSourceCombo_.getSelectedId() == 1
            ? SeqClockSource::Internal
            : SeqClockSource::External);
    };
    clockSourceCombo_.setTooltip("Sequencer clock: INT/EXT");
    addAndMakeVisible(clockSourceCombo_);

    panelwidgets::cyberKnob(*this, bpmSlider_, bpmLabel_, "BPM", 20.0, 300.0, 120.0, 1.0,
                            juce::Slider::RotaryVerticalDrag);
    bpmSlider_.setTooltip("Sequencer BPM (20-300)");
    bpmSlider_.onValueChange = [this]()
    {
        processor_.getStepSequencer().setBpm(bpmSlider_.getValue());
    };

    panelwidgets::cyberKnob(*this, rateSlider_, rateLabel_, "RATE", 0.125, 4.0, 0.25, 0.125,
                            juce::Slider::RotaryVerticalDrag);
    rateSlider_.setTooltip("Sequencer rate (0.125-4.0 beats)");
    rateSlider_.onValueChange = [this]()
    {
        processor_.getStepSequencer().setRateBeats(
            static_cast<float>(rateSlider_.getValue()));
    };

    // Current step indicator
    currentStepLabel_.setText("STEP: 0", juce::dontSendNotification);
    currentStepLabel_.setFont(CyberpunkTheme::getCyberFont(9.0f, false));
    currentStepLabel_.setColour(juce::Label::textColourId, CyberpunkTheme::cyan_);
    currentStepLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(currentStepLabel_);

    // 16 step cells
    for (int i = 0; i < 16; ++i)
    {
        auto cell = std::make_unique<StepCell>(i, processor_);
        const int idx = i;

        cell->onGateChanged = [this, idx](int index, bool active)
        {
            processor_.getStepSequencer().setStep(
                index, active,
                processor_.getStepSequencer().getStep(index).value);
        };

        cell->onValueChanged = [this, idx](int index, float value)
        {
            processor_.getStepSequencer().setStep(
                index,
                processor_.getStepSequencer().getStep(index).active,
                value);
        };

        addAndMakeVisible(cell.get());
        stepCells_[i] = std::move(cell);
    }
}

void SequencerPanel::resized()
{
    const int pad = 3;
    auto seqArea = getLocalBounds().reduced(pad);
    title_.setBounds(seqArea.removeFromTop(14));
    auto seqControlRow = seqArea.removeFromTop(16).reduced(1, 0);
    playModeCombo_.setBounds(seqControlRow.removeFromLeft(seqControlRow.getWidth() / 3).reduced(1));
    clockSourceCombo_.setBounds(seqControlRow.removeFromLeft(seqControlRow.getWidth() / 2).reduced(1));
    currentStepLabel_.setBounds(seqControlRow.reduced(1));
    auto seqParamRow = seqArea.removeFromTop(18).reduced(1, 0);
    bpmSlider_.setBounds(seqParamRow.removeFromLeft(seqParamRow.getWidth() / 3).reduced(1));
    bpmLabel_.setBounds(bpmSlider_.getBounds().translated(0, -12));
    rateSlider_.setBounds(seqParamRow.removeFromLeft(seqParamRow.getWidth() / 2).reduced(1));
    rateLabel_.setBounds(rateSlider_.getBounds().translated(0, -12));
    // 16 step cells in 2 rows of 8
    auto seqGridArea = seqArea.reduced(1, 0);
    auto seqRow1 = seqGridArea.removeFromTop(seqGridArea.getHeight() / 2);
    auto seqRow2 = seqGridArea;
    const int cellW = seqRow1.getWidth() / 8;
    for (int i = 0; i < 8; ++i)
    {
        stepCells_[i]->setBounds(seqRow1.removeFromLeft(cellW).reduced(1));
        stepCells_[i + 8]->setBounds(seqRow2.removeFromLeft(cellW).reduced(1));
    }
}

void SequencerPanel::updateFromSequencer()
{
    auto& seq = processor_.getStepSequencer();
    // Update current step indicator
    currentStepLabel_.setText("STEP: " + juce::String(seq.getCurrentStep()),
                              juce::dontSendNotification);
    // Sync step cells from sequencer state
    for (int i = 0; i < 16; ++i)
    {
        const auto& step = seq.getStep(i);
        stepCells_[i]->setActive(step.active);
        stepCells_[i]->setValue(step.value);
    }
}

} // namespace ana
