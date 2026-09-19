#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "CyberpunkTheme.h"

namespace ana {

//==============================================================================
/**
    Scrollable modulation assignment panel that replaces the old LFO/Envelope
    area.  Shows a per-parameter modulation matrix:

        VOL ADSR: [A--] [D--] [S--] [R--]   ← pinned at top
        ▼ FILTER: cutoff, resonance
        ▼ TIMBRE: blur_a, blur_b
        ▼ EFFECTS: delay_time … bitcrush_bits
        ▼ MASTER: master_vol, master_pan

    Each row:  [Param Label] [Source Combo: OFF▼] [Depth Slider: --o--]
    Sections are collapsible by clicking the header.
*/
// juce::Component already IS a juce::MouseListener, so naming it again here
// created two MouseListener subobjects: passing 'this' to addMouseListener()
// is then ambiguous (clang: "ambiguous conversion from derived class"), even
// though MSVC's non-standard dominance rule lets it through.  mouseUp() below
// still overrides Component's virtual MouseListener hook.
class ModulationAssignPanel : public juce::Component
{
public:
    explicit ModulationAssignPanel(AnaPlugAudioProcessor& processor);
    ~ModulationAssignPanel() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    /** Call from the parent timer to sync slider/combo values after preset load. */
    void syncFromProcessor();

    /** Returns the total content height based on collapsed state (used by parent). */
    int calcContentHeight() const;

private:
    //==============================================================================
    struct ModRow
    {
        juce::Label label;
        juce::ComboBox sourceCombo;
        juce::Slider depthSlider;
        int slotIndex = 0;
    };

    struct SectionData
    {
        juce::Label headerLabel;
        bool collapsed = false;
        std::vector<std::unique_ptr<ModRow>> rows;
    };

    AnaPlugAudioProcessor& processor_;

    //==============================================================================
    // Volume ADSR (always visible, pinned at top)
    juce::Label volAdsrHeader_;
    juce::Slider volAttack_, volDecay_, volSustain_, volRelease_;
    juce::Label volAttackL_, volDecayL_, volSustainL_, volReleaseL_;

    // Collapsible sections
    SectionData filterSection_;
    SectionData timbreSection_;
    SectionData effectsSection_;
    SectionData masterSection_;

    //==============================================================================
    void toggleSection(SectionData& section);

    /** The depth slider follows the selected source: OFF disables it, LFOs are
        bipolar, envelopes unipolar.  Called both when the user picks a source
        and when a preset moves the combo behind the UI's back - otherwise a
        loaded assignment would show a greyed-out depth the user cannot drag. */
    static void applyDepthState(ModRow& row);

    //==============================================================================
    // MouseListener
    void mouseUp(const juce::MouseEvent& event) override;
};

} // namespace ana
