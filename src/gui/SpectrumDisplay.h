#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "../dsp/PartialData.h"

namespace ana {

class SpectrumDisplay : public juce::Component
{
public:
    SpectrumDisplay();
    ~SpectrumDisplay() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setPartials(const std::vector<Partial>& newPartials);
    void clear();

private:
    std::vector<Partial> partials;
    float maxFrequency = 20000.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumDisplay)
};

} // namespace ana
