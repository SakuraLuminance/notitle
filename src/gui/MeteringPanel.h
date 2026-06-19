#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "CyberpunkTheme.h"

class AnaPlugAudioProcessor;

namespace ana {

//==============================================================================
/**
    Compact LUFS metering panel with EBU R128 zone-coloured bar graphs.

    Displays:
      - Momentary LUFS   (400 ms window, horizontal bar)
      - Short-term LUFS  (3 s sliding window, horizontal bar)
      - Integrated LUFS  (gated entire session, horizontal bar)
      - Loudness Range   (LRA, numeric readout)
      - True-peak L/R    (dBTP, narrow bars with peak-hold)
      - Reset button     (clears integrated / LRA state)

    Refreshes at 20 Hz via a JUCE Timer; reads MeteringEngine atomics
    lock-free.  Never touches the audio thread.
*/
class MeteringPanel : public juce::Component,
                      public juce::Timer
{
public:
    explicit MeteringPanel(AnaPlugAudioProcessor& proc);
    ~MeteringPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    AnaPlugAudioProcessor& processor_;

    // Cached readings (updated in timerCallback)
    float momentaryLUFS_   = -70.0f;
    float shortTermLUFS_   = -70.0f;
    float integratedLUFS_  = -70.0f;
    float lra_             =  0.0f;
    float truePeakL_       = -70.0f;
    float truePeakR_       = -70.0f;

    juce::TextButton resetButton_;

    //==============================================================================
    // Layout regions (recomputed in resized)
    juce::Rectangle<int> titleArea_;
    juce::Rectangle<int> momentaryBarArea_;
    juce::Rectangle<int> shortTermBarArea_;
    juce::Rectangle<int> integratedBarArea_;
    juce::Rectangle<int> truePeakArea_;   // narrow L+R bars side by side
    juce::Rectangle<int> lraArea_;
    juce::Rectangle<int> resetArea_;

    //==============================================================================
    // Drawing helpers
    static void drawHorizontalLUFSBar(juce::Graphics& g,
                                      juce::Rectangle<int> bounds,
                                      float lufs,
                                      const juce::String& label,
                                      float rangeMin = -36.0f,
                                      float rangeMax = -6.0f);
    static void drawTruePeakBars(juce::Graphics& g,
                                 juce::Rectangle<int> bounds,
                                 float peakL,
                                 float peakR,
                                 float rangeMin = -36.0f,
                                 float rangeMax = 0.0f);
    static void drawLRA(juce::Graphics& g,
                        juce::Rectangle<int> bounds,
                        float lra);

    /** Pick a bar-fill colour for a given LUFS value (EBU R128 zones). */
    static juce::Colour zoneColour(float lufs);

    //==============================================================================
    // Bar scale helpers
    static float lufsToNormalised(float lufs, float min, float max);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeteringPanel)
};

} // namespace ana
