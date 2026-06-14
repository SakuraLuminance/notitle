#pragma once

#include "PluginProcessor.h"
#include "gui/WaveformDisplay.h"
#include "gui/SpectrumDisplay.h"
#include "gui/FilterVisualization.h"
#include "gui/WaterfallDisplay.h"
#include "gui/VisualFeedbackPanel.h"
#include "gui/SpectrumEditorCanvas.h"
#include "gui/PresetBrowserPanel.h"
#include "gui/EvolutionPanel.h"
#include "gui/CyberpunkTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    Harmor-inspired cyberpunk GUI for AnaPlug.
    
    Layout (Harmor-style):
    ┌──────────────────────────────────────────────────────────────┐
    │  ANAPLUG :: SPECTRAL SYNTHESIZER              [PRESET INFO]  │  Title bar
    ├──────┬───────────────────────────────────┬──────────────────┤
    │      │                                   │                  │
    │  A   │     SPECTRAL DISPLAY              │   B              │
    │  f   │     (big visual canvas)           │   f               │
    │  x   │                                   │   x               │
    │      │                                   │                  │
    ├──────┴───────────────────────────────────┴──────────────────┤
    │  FILTER    │  MACROS (4 knobs)      │  EFFECTS              │
    ├─────────────────────────────────────┴───────────────────────┤
    │  MODULATION  LFO  ENV1  ENV2   →   ASSIGN                   │
    ├─────────────────────────────────────┬───────────────────────┤
    │  UNISON  |  ARP   |  SAMPLE         │  MASTER  VOL  PAN     │
    ├─────────────────────────────────────┴───────────────────────┤
    │  [LOAD] [>] [#] [FLATTEN]  ROOT:C4  FINE:0¢  STATUS...     │
    └──────────────────────────────────────────────────────────────┘
*/
class AnaPlugAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    public juce::Timer
{
public:
    AnaPlugAudioProcessorEditor(AnaPlugAudioProcessor&);
    ~AnaPlugAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    AnaPlugAudioProcessor& audioProcessor;

    //==============================================================================
    // Layout regions (computed in computeRegions, stored for paint)
    struct Regions {
        juce::Rectangle<int> titleBar;
        juce::Rectangle<int> mainArea;       // A | spectrum | B
        juce::Rectangle<int> timbreAPanel;     // left 17%
        juce::Rectangle<int> timbreBPanel;     // right 17%
        juce::Rectangle<int> centerPanel;      // spectrum canvas

        juce::Rectangle<int> processArea;      // filter + macros + effects
        juce::Rectangle<int> modArea;          // LFO + envelope
        juce::Rectangle<int> bottomArea;       // unison + arp + master
        juce::Rectangle<int> statusBar;        // transport + status
    };
    void computeRegions(juce::Rectangle<int> bounds, Regions& r) const;

    //==============================================================================
    // Title bar
    juce::Label titleLabel_;
    juce::TextButton presetButton_;

    //==============================================================================
    // Left panel — Timbre A (Harmor-style)
    juce::Slider aSubSlider_;        // Sub-harmonic amount
    juce::Slider aBrightSlider_;     // Brightness
    juce::Slider aBlurSlider_;       // Blur amount
    juce::Slider aHpfSlider_;        // High-pass filter
    juce::Label  aSubLabel_, aBrightLabel_, aBlurLabel_, aHpfLabel_;

    //==============================================================================
    // Right panel — Timbre B
    juce::Slider bSubSlider_, bBrightSlider_, bBlurSlider_, bHpfSlider_;
    juce::Label  bSubLabel_, bBrightLabel_, bBlurLabel_, bHpfLabel_;

    // Timbre blend
    juce::Slider timbreBlendSlider_;
    juce::Label  timbreBlendLabel_;

    //==============================================================================
    // Center — Spectrum / Partial display
    ana::VisualFeedbackPanel feedbackPanel_;
    juce::ComboBox viewModeCombo_;   // Bars / Waterfall / Editor

    //==============================================================================
    // Process panel — Filter
    juce::ComboBox filterTypeCombo_;
    juce::Slider filterCutoffSlider_;
    juce::Slider filterResSlider_;
    ana::FilterVisualization filterViz_;
    juce::Label filterTitle_;

    // Process panel — Macros (4 knobs)
    juce::Slider macroSliders_[4];
    juce::Label  macroLabels_[4];

    // Process panel — Effects
    juce::TextButton prismButton_{"PRISM"};
    juce::TextButton blurButton_{"BLUR"};
    juce::TextButton harmButton_{"HARMONIZER"};

    //==============================================================================
    // Modulation panel
    juce::ComboBox lfoWaveCombo_;
    juce::Slider lfoRateSlider_;
    juce::Slider lfoDepthSlider_;
    juce::ComboBox lfoTargetCombo_;
    juce::Label  lfoTitle_;

    juce::ComboBox envTargetCombo_;
    juce::Slider envAttackSlider_;
    juce::Slider envDecaySlider_;
    juce::Slider envSustainSlider_;
    juce::Slider envReleaseSlider_;
    juce::Label  envTitle_;

    //==============================================================================
    // Bottom area
    // Unison
    juce::Slider unisonCountSlider_;
    juce::Slider unisonDetuneSlider_;
    juce::Slider unisonSpreadSlider_;
    juce::Label  unisonCountLabel_;
    juce::Label  unisonDetuneLabel_;
    juce::Label  unisonSpreadLabel_;
    juce::Label  unisonTitle_;

    // Arpeggiator
    juce::ComboBox arpPatternCombo_;
    juce::Slider arpRateSlider_;
    juce::Slider arpGateSlider_;
    juce::Label  arpRateLabel_;
    juce::Label  arpGateLabel_;
    juce::Label  arpTitle_;

    // Sample controls
    juce::TextButton loadButton_{"LOAD"};
    juce::TextButton playButton_{">"};
    juce::TextButton stopButton_{"#"};
    juce::TextButton flattenButton_{"FLATTEN"};
    juce::Slider rootNoteKnob_;
    juce::Slider rootFineTuneKnob_;
    juce::Label  rootNoteLabel_;
    juce::Label  rootFineTuneLabel_;
    juce::Label  pitchDetectLabel_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    juce::String loadedFileName_;

    // Master
    juce::Slider masterVolSlider_;
    juce::Slider masterPanSlider_;
    juce::Label  masterVolLabel_;
    juce::Label  masterPanLabel_;

    // Status
    juce::Label statusLabel_;
    juce::TextButton dnaButton_{"DNA EVOLVE"};

    // Evolution panel (lazy-created in callout)
    std::unique_ptr<ana::EvolutionPanel> evolutionPanel;

    //==============================================================================
    // Helpers
    void loadButtonClicked();
    void playButtonClicked();
    void stopButtonClicked();
    void flattenButtonClicked();
    void presetButtonClicked();
    void dnaButtonClicked();
    void updateStatus();
    void stftParamChanged();
    void updatePitchDisplay(const juce::String& text);
    void setupTimbreControls();
    void setupUnisonArp();

    static juce::String midiNoteToName(int note);

    //==============================================================================
    // Knob builder helpers
    void addCyberKnob(juce::Slider& slider, juce::Label& label,
                      const juce::String& name, double min, double max,
                      double init, double step,
                      juce::Slider::SliderStyle style = juce::Slider::RotaryVerticalDrag);
    juce::TextButton& addCyberButton(juce::TextButton& btn);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnaPlugAudioProcessorEditor)
};
