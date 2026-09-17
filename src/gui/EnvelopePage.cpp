#include "EnvelopePage.h"
#include "../dsp/EnvelopeEditOps.h"

#include <cmath>

namespace ana
{

namespace
{
int loopModeToItemId(LoopMode mode)
{
    switch (mode)
    {
        case LoopMode::Forward:  return 2;
        case LoopMode::PingPong: return 3;
        case LoopMode::Sustain:  return 4;
        default:                 return 1;
    }
}

LoopMode itemIdToLoopMode(int id)
{
    switch (id)
    {
        case 2: return LoopMode::Forward;
        case 3: return LoopMode::PingPong;
        case 4: return LoopMode::Sustain;
        default: return LoopMode::None;
    }
}

const double kBeatDivisions[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };

int beatDivToItemId(double beats)
{
    int best = 3;
    double bestDist = 1.0e9;
    for (int i = 0; i < 6; ++i)
    {
        const double d = std::abs(kBeatDivisions[i] - beats);
        if (d < bestDist)
        {
            bestDist = d;
            best = i + 1;
        }
    }
    return best;
}
}

//==============================================================================
EnvelopePage::EnvelopePage(AnaPlugAudioProcessor& processor)
    : processor_(processor)
{
    for (auto* b : { &volSlot_, &env1Slot_, &env2Slot_, &env3Slot_ })
    {
        b->setClickingTogglesState(true);
        b->setRadioGroupId(2501);
        b->setColour(juce::TextButton::buttonColourId, CyberpunkTheme::bg_.brighter(0.08f));
        b->setColour(juce::TextButton::textColourOffId, CyberpunkTheme::fg_.withAlpha(0.55f));
        b->setColour(juce::TextButton::buttonOnColourId, CyberpunkTheme::cyan_.withAlpha(0.35f));
        b->setColour(juce::TextButton::textColourOnId, CyberpunkTheme::cyan_);
        addAndMakeVisible(*b);
    }

    volSlot_.onClick  = [this] { selectSlot(0); };
    env1Slot_.onClick = [this] { selectSlot(1); };
    env2Slot_.onClick = [this] { selectSlot(2); };
    env3Slot_.onClick = [this] { selectSlot(3); };
    volSlot_.setToggleState(true, juce::dontSendNotification);

    addAndMakeVisible(canvas_);
    canvas_.setEnvelope(&processor_.getEnvelopeSlot(0));
    canvas_.setEditLock(&processor_.getEnvelopeLock());
    canvas_.onEdited = [this] { notifyEdited(); };

    loopModeCombo_.addItem("ONCE", 1);
    loopModeCombo_.addItem("LOOP FWD", 2);
    loopModeCombo_.addItem("LOOP PP", 3);
    loopModeCombo_.addItem("SUSTAIN", 4);
    loopModeCombo_.setTooltip("Envelope loop mode");
    loopModeCombo_.onChange = [this]
    {
        const auto mode = itemIdToLoopMode(loopModeCombo_.getSelectedId());
        {
            const juce::SpinLock::ScopedLockType lock(processor_.getEnvelopeLock());
            auto& env = processor_.getEnvelopeSlot(activeSlot_);
            env.setLoopMode(mode);

            const bool needsRange = (mode == LoopMode::Forward
                                     || mode == LoopMode::PingPong
                                     || mode == LoopMode::Sustain);
            if (needsRange && env.getLoopEnd() < 0 && env.getNumBreakpoints() >= 2)
                env.setLoopEnd(env.getNumBreakpoints() - 1);
        }
        notifyEdited();
    };
    addAndMakeVisible(loopModeCombo_);

    syncButton_.setClickingTogglesState(true);
    syncButton_.setTooltip("Sync envelope time to host tempo");
    syncButton_.onClick = [this]
    {
        const bool on = syncButton_.getToggleState();
        {
            const juce::SpinLock::ScopedLockType lock(processor_.getEnvelopeLock());
            processor_.getEnvelopeSlot(activeSlot_).setSyncMode(on);
        }
        notifyEdited();
    };
    addAndMakeVisible(syncButton_);

    beatDivCombo_.addItem("1/1", 1);
    beatDivCombo_.addItem("1/2", 2);
    beatDivCombo_.addItem("1/4", 3);
    beatDivCombo_.addItem("1/8", 4);
    beatDivCombo_.addItem("1/16", 5);
    beatDivCombo_.addItem("1/32", 6);
    beatDivCombo_.setTooltip("Beat division used when sync is on");
    beatDivCombo_.onChange = [this]
    {
        const int id = juce::jlimit(1, 6, beatDivCombo_.getSelectedId());
        {
            const juce::SpinLock::ScopedLockType lock(processor_.getEnvelopeLock());
            processor_.getEnvelopeSlot(activeSlot_).setBeatDivision(kBeatDivisions[id - 1]);
        }
        notifyEdited();
    };
    addAndMakeVisible(beatDivCombo_);

    resetButton_.setTooltip("Rebuild a standard A/D/S/R shape from the current values");
    resetButton_.onClick = [this]
    {
        {
            const juce::SpinLock::ScopedLockType lock(processor_.getEnvelopeLock());
            processor_.getEnvelopeSlot(activeSlot_).rebuildADSR();
        }
        notifyEdited();
    };
    addAndMakeVisible(resetButton_);

    readoutLabel_.setJustificationType(juce::Justification::centredRight);
    readoutLabel_.setColour(juce::Label::textColourId, CyberpunkTheme::fg_.withAlpha(0.85f));
    addAndMakeVisible(readoutLabel_);

    refreshControls();
}

//==============================================================================
void EnvelopePage::selectSlot(int slot)
{
    activeSlot_ = juce::jlimit(0, 3, slot);
    canvas_.setEnvelope(&processor_.getEnvelopeSlot(activeSlot_));
    refreshControls();
    canvas_.repaint();
}

void EnvelopePage::notifyEdited()
{
    refreshControls();
    canvas_.repaint();

    if (onEnvelopeEdited)
        onEnvelopeEdited();
}

void EnvelopePage::refreshControls()
{
    EnvelopeEditOps::DerivedADSR derived;

    {
        const juce::SpinLock::ScopedLockType lock(processor_.getEnvelopeLock());
        auto& env = processor_.getEnvelopeSlot(activeSlot_);

        const int loopId = loopModeToItemId(env.getLoopMode());
        if (loopModeCombo_.getSelectedId() != loopId)
            loopModeCombo_.setSelectedId(loopId, juce::dontSendNotification);

        if (syncButton_.getToggleState() != env.getSyncMode())
            syncButton_.setToggleState(env.getSyncMode(), juce::dontSendNotification);

        const int beatId = beatDivToItemId(env.getBeatDivision());
        if (beatDivCombo_.getSelectedId() != beatId)
            beatDivCombo_.setSelectedId(beatId, juce::dontSendNotification);

        derived = EnvelopeEditOps::deriveADSR(env);
    }

    beatDivCombo_.setEnabled(syncButton_.getToggleState());

    readoutLabel_.setText(juce::String::formatted("A %.3f  D %.3f  S %.2f  R %.3f",
                                                  derived.attack, derived.decay,
                                                  derived.sustain, derived.release),
                          juce::dontSendNotification);
}

void EnvelopePage::syncFromProcessor()
{
    canvas_.setPlayhead(processor_.getEnvUITime(activeSlot_),
                        processor_.getEnvUIValue(activeSlot_));
    refreshControls();
}

//==============================================================================
void EnvelopePage::paint(juce::Graphics& g)
{
    g.setColour(CyberpunkTheme::bg_);
    g.fillRect(getLocalBounds());

    CyberpunkTheme::drawPanelBorder(g, getLocalBounds(), "ENVELOPES");
}

void EnvelopePage::resized()
{
    auto area = getLocalBounds().reduced(8, 14);

    auto slotCol = area.removeFromLeft(58);
    volSlot_.setBounds(slotCol.removeFromTop(22).reduced(0, 2));
    env1Slot_.setBounds(slotCol.removeFromTop(22).reduced(0, 2));
    env2Slot_.setBounds(slotCol.removeFromTop(22).reduced(0, 2));
    env3Slot_.setBounds(slotCol.removeFromTop(22).reduced(0, 2));

    auto paramRow = area.removeFromBottom(22).reduced(2, 1);
    loopModeCombo_.setBounds(paramRow.removeFromLeft(96));
    syncButton_.setBounds(paramRow.removeFromLeft(52).reduced(3, 0));
    beatDivCombo_.setBounds(paramRow.removeFromLeft(66).reduced(2, 0));
    resetButton_.setBounds(paramRow.removeFromLeft(94).reduced(3, 0));
    readoutLabel_.setBounds(paramRow);

    canvas_.setBounds(area.reduced(2));
}

} // namespace ana
