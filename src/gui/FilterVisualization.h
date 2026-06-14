#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <array>

namespace ana {

class FilterVisualization : public juce::Component,
                            public juce::Timer
{
public:
    FilterVisualization();
    ~FilterVisualization() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    // Set filter coefficients for display
    void setNumFilters(int num);
    void setFilterCoefficients(int filterIndex,
                                const std::array<double, 5>& coeffs,
                                double sampleRate);

    void clear();

private:
    struct FilterState
    {
        std::array<double, 5> coefficients = { 1, 0, 0, 1, 0 };
        double sampleRate = 44100.0;
        juce::Colour colour;
    };

    std::vector<FilterState> filterStates;

    float getMagnitudeAtFrequency(double frequency, const FilterState& state) const;
    static constexpr float minFreq = 20.0f;
    static constexpr float maxFreq = 20000.0f;
    static constexpr float minDb = -24.0f;
    static constexpr float maxDb = 24.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FilterVisualization)
};

} // namespace ana
