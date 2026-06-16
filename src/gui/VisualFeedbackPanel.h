#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "../dsp/PartialDataSIMD.h"
#include "CyberpunkTheme.h"

namespace ana {

class VisualFeedbackPanel : public juce::Component,
                            public juce::Timer
{
public:
    VisualFeedbackPanel();
    ~VisualFeedbackPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    // Update with partial data from audio thread (thread-safe via CriticalSection)
    void updatePartials(const PartialDataSIMD& partials);

    // Configuration
    void setShowPeakHold(bool show);
    void setPeakDecay(float decay);         // decay rate per frame (0.0-1.0)
    void setUseLogFreq(bool useLog);        // log vs linear frequency scale
    void setAmplitudeRange(float max);      // max amplitude for Y scale

    // Colours
    void setBarColour(juce::Colour colour);
    void setEnvelopeColour(juce::Colour colour);
    void setPeakColour(juce::Colour colour);
    void setBackgroundColour(juce::Colour colour);

private:
    // Drawing helpers (called from paint)
    void drawPartialBars(juce::Graphics& g, int w, int h);
    void drawEnvelope(juce::Graphics& g, int w, int h);
    void drawPeakHold(juce::Graphics& g, int w, int h);
    void drawStatusText(juce::Graphics& g, int w, int h);

    // Frequency-to-x coordinate mapping
    float freqToX(float freq, float width) const;

    // Thread-safe double buffer for partial data
    mutable juce::CriticalSection dataLock;
    PartialDataSIMD currentPartials_;   // written by audio thread
    PartialDataSIMD displayPartials_;   // read by paint thread (swapped in timer)

    // Peak hold state
    std::vector<float> peakLevels_;
    float peakDecay_ = 0.995f;
    bool showPeakHold_ = true;

    // Display settings
    bool useLogFreq_ = true;
    float ampRange_ = 1.0f;

    // Frequency axis bounds
    static constexpr float minFreq_ = 20.0f;
    static constexpr float maxFreq_ = 20000.0f;

    // Colours
    juce::Colour barColour_      { CyberpunkTheme::cyan_ };
    juce::Colour envelopeColour_ { CyberpunkTheme::fg_ };
    juce::Colour peakColour_     { CyberpunkTheme::magenta_ };
    juce::Colour bgColour_       { CyberpunkTheme::bg_ };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualFeedbackPanel)
};

} // namespace ana
