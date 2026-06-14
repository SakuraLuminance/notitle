#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "../dsp/SpectralDNA.h"

namespace ana {

//==============================================================================
/**
    Evolution control panel for SpectralDNA genetic algorithm timbre breeding.

    Displays a population grid with per-individual fitness coloring,
    generation controls (Evolve 1/10, Randomize), sample import as a parent
    genome, and DNA save/load.

    Layout:
    ┌──────────────────────────────────────────────────┐
    │ [PopSize▾]  Gen: 42  │  Status text              │
    ├──────────────────────────────────────────────────┤
    │ [Evolve 1] [Evolve 10] [Randomize] [Load] [Save] │
    ├──────────────────────────────────────────────────┤
    │ ┌────┬────┬────┬────┐                            │
    │ │ 0  │ 1  │ 2  │ 3  │  fitness 0.87             │
    │ ├────┼────┼────┼────┤                            │
    │ │ 4  │ 5  │ 6  │ 7  │  ← selected parent A      │
    │ ├────┼────┼────┼────┤                            │
    │ │ 8  │ 9  │10  │11  │  ← selected parent B      │
    │ ├────┼────┼────┼────┤                            │
    │ │12  │13  │14  │15  │                            │
    │ └────┴────┴────┴────┘                            │
    └──────────────────────────────────────────────────┘
*/
class EvolutionPanel : public juce::Component,
                       public juce::Timer
{
public:
    EvolutionPanel(AnaPlugAudioProcessor& p);
    ~EvolutionPanel() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void timerCallback() override;

private:
    AnaPlugAudioProcessor& processor;

    //==============================================================================
    // Evolution controls
    juce::TextButton evolve1Btn_{"Evolve 1"};
    juce::TextButton evolve10Btn_{"Evolve 10"};
    juce::TextButton randomizeBtn_{"Randomize"};
    juce::TextButton loadSampleBtn_{"Load Sample"};
    juce::TextButton saveDNABtn_{"Save DNA"};
    juce::TextButton loadDNABtn_{"Load DNA"};

    //==============================================================================
    // Population selector
    juce::ComboBox popSizeCombo_;

    //==============================================================================
    // Status display
    juce::Label generationLabel_;
    juce::Label statusLabel_;

    //==============================================================================
    // Grid
    juce::Rectangle<int> gridArea_;
    int gridRows_ = 4;
    int gridCols_ = 4;
    int selectedIndex_ = -1;
    int selectedIndexB_ = -1;
    float cellFitness_[128]{};

    void mouseDown(const juce::MouseEvent& event) override;

    //==============================================================================
    // Internals
    void onEvolve1();
    void onEvolve10();
    void onRandomize();
    void onLoadSample();
    void onSaveDNA();
    void onLoadDNA();
    void onPopSizeChanged();
    void onCellClicked(int index);
    void updateGridDimensions(int popSize);
    void updateDisplay();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EvolutionPanel)
};

} // namespace ana
