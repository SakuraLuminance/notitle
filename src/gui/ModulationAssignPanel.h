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
class ModulationAssignPanel : public juce::Component,
                              public juce::MouseListener
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

    //==============================================================================
    // MouseListener
    void mouseUp(const juce::MouseEvent& event) override;
};

} // namespace ana
