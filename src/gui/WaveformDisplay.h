#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace ana {

class WaveformDisplay : public juce::Component
{
public:
    WaveformDisplay();
    ~WaveformDisplay() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setSamples(const std::vector<float>& newSamples);
    void setPlaybackPosition(double position);
    void setSampleRate(double rate);

private:
    std::vector<float> samples;
    double playbackPosition = 0.0;
    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};

} // namespace ana
