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
#include "gui/CreditsPanel.h"
#include "gui/XYPad.h"
#include "gui/MeteringPanel.h"
#include "gui/CyberpunkTheme.h"
#include "gui/ModulationAssignPanel.h"
#include "gui/EffectRackComponent.h"
#include "gui/panels/TimbrePanel.h"
#include "gui/panels/FilterPanel.h"
#include "gui/panels/MacroPanel.h"
#include "gui/panels/SequencerPanel.h"
#include "gui/panels/TransportBar.h"
#include "gui/panels/MasterSection.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <map>
#include <unordered_map>

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
        juce::Rectangle<int> modArea;          // modulation assignment panel
        juce::Rectangle<int> bottomArea;       // unison + arp + master
        juce::Rectangle<int> statusBar;        // transport + status
    };
    void computeRegions(juce::Rectangle<int> bounds, Regions& r) const;

    //==============================================================================
    // Title bar
    juce::Label titleLabel_;
    juce::TextButton presetButton_;

    //==============================================================================
    // Left/right panels — Timbre A / B (Harmor-style)
    ana::TimbrePanel timbreAPanel_;
    ana::TimbrePanel timbreBPanel_;

    // Timbre blend
    juce::Slider timbreBlendSlider_;
    juce::Label  timbreBlendLabel_;

    //==============================================================================
    // Center — Spectrum / Partial display
    ana::VisualFeedbackPanel feedbackPanel_;
    ana::WaterfallDisplay waterfallDisplay_;
    ana::SpectrumEditorCanvas spectrumEditorCanvas_;
    std::unique_ptr<ana::WaveformDisplay> waveformDisplay_;
    juce::ComboBox viewModeCombo_;   // Bars / Waterfall / Editor / 3D / Scope

    //==============================================================================
    // Process panel — Filter
    ana::FilterPanel filterPanel_;

    // Process panel — Macros (4 knobs)
    ana::MacroPanel macroPanel_;

    // Process panel — Effects rack (dynamic, replaces hardcoded slider stack)
    juce::ComboBox effectPresetCombo_;
    juce::Label    fxPresetLabel_;
    juce::TextButton prismButton_{"PRISM"};
    juce::TextButton blurButton_{"BLUR"};
    juce::TextButton harmButton_{"HARMONIZER"};
    juce::ComboBox vocalCharacterCombo_;
    juce::Label    vocalCharacterLabel_;
    ana::EffectRackComponent effectRack_;

    //==============================================================================
    // Modulation assignment panel (replaces old LFO/Envelope area)
    juce::Viewport modViewport_;
    ana::ModulationAssignPanel modPanel_;

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

    // Voice mode / Portamento
    juce::ComboBox voiceModeCombo_;           // Poly / Mono / Legato
    juce::Slider   portamentoTimeSlider_;     // 0-2 seconds
    juce::ComboBox portamentoCurveCombo_;     // Linear / Exponential / Logarithmic
    juce::Label    voiceModeLabel_;
    juce::Label    portamentoTimeLabel_;
    juce::Label    portamentoCurveLabel_;
    juce::Label    voiceTitle_;

    // Arpeggiator
    juce::ComboBox arpPatternCombo_;
    juce::Slider arpRateSlider_;
    juce::Slider arpGateSlider_;
    juce::Label  arpRateLabel_;
    juce::Label  arpGateLabel_;
    juce::Label  arpTitle_;

    // Sample controls
    ana::TransportBar transportBar_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    juce::String loadedFileName_;

    // Master
    ana::MasterSection masterSection_;

    // Status
    juce::Label statusLabel_;
    juce::TextButton dnaButton_{"DNA EVOLVE"};
    juce::TextButton creditsButton_{"\u24D8"};
    juce::TextButton randomizeButton_{"RANDOM"};
    juce::ComboBox rangeCombo_;
    ana::MeteringPanel meteringPanel_;

    // Evolution panel (lazy-created in callout)
    std::unique_ptr<ana::EvolutionPanel> evolutionPanel;

    // XY Pad for 2D parameter control
    std::unique_ptr<ana::XYPad> xyPad_;

    //==============================================================================
    // Helpers
    void loadButtonClicked();
    void flattenButtonClicked();
    void presetButtonClicked();
    void dnaButtonClicked();
    void creditsButtonClicked();
    void updateStatus();
    void onViewModeChanged();

    // Effect preset helpers
    void populateEffectPresets();
    void onEffectPresetSelected();
    void effectPresetRightClicked();

    //==============================================================================
    // Knob builder helpers
    void addCyberKnob(juce::Slider& slider, juce::Label& label,
                      const juce::String& name, double min, double max,
                      double init, double step,
                      juce::Slider::SliderStyle style = juce::Slider::RotaryVerticalDrag);
    juce::TextButton& addCyberButton(juce::TextButton& btn);

    //==============================================================================
    // Step Sequencer panel (gui/panels/SequencerPanel)
    ana::SequencerPanel sequencerPanel_;

    //==============================================================================
    // MIDI Learn helpers
    struct MidiLearnSliderInfo {
        juce::String paramId;
        std::atomic<float>* target = nullptr;
    };

    /** Register a slider so that right-click → "MIDI Learn" works. */
    void setupMidiLearnForSlider(juce::Slider& slider,
                                 const juce::String& paramId,
                                 std::atomic<float>* target = nullptr);

    /** Process MIDI Learn timeout + indicator blink in timer. */
    void updateMidiLearnState();

    /** Inherited via Component — intercept right-clicks on registered sliders. */
    void mouseDown(const juce::MouseEvent& event) override;

    //==============================================================================
    // Members
    // MIDI Learn indicator (shows/blinks when learning)
    juce::Label midiLearnIndicator_;
    juce::uint32 midiLearnStartTime_ = 0;

    // Maps slider → info for right-click / polling
    std::map<juce::String, juce::Slider*> learnableSliders_;
    std::unordered_map<const juce::Slider*, MidiLearnSliderInfo> midiLearnSliders_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnaPlugAudioProcessorEditor)
};
