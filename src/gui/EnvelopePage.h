#pragma once

#include "CyberpunkTheme.h"
#include "EnvelopeCanvas.h"
#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace ana
{

//==============================================================================
/**
    ENV page: slot list (VOL / ENV1 / ENV2), the editable envelope canvas and a
    parameter row (loop mode, tempo sync, beat division, reset, A/D/S/R readout).
*/
class EnvelopePage : public juce::Component
{
public:
    explicit EnvelopePage(AnaPlugAudioProcessor& processor);

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Called from the editor timer: playhead + derived readouts. */
    void syncFromProcessor();

    /** Fired after an edit so sibling UI (MOD page ADSR sliders) can refresh. */
    std::function<void()> onEnvelopeEdited;

private:
    void selectSlot(int slot);
    void refreshControls();
    void notifyEdited();

    AnaPlugAudioProcessor& processor_;

    juce::TextButton volSlot_  { "VOL" };
    juce::TextButton env1Slot_ { "ENV1" };
    juce::TextButton env2Slot_ { "ENV2" };
    juce::TextButton env3Slot_ { "ENV3" };

    EnvelopeCanvas canvas_;

    juce::ComboBox   loopModeCombo_;
    juce::TextButton syncButton_ { "SYNC" };
    juce::ComboBox   beatDivCombo_;
    juce::TextButton resetButton_ { "RESET ADSR" };
    juce::Label      readoutLabel_;

    int activeSlot_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnvelopePage)
};

} // namespace ana
