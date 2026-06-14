#pragma once
#include <juce_core/juce_core.h>

namespace ana {

class PresetFactory {
public:
    static juce::ValueTree createFactoryBass();
    static juce::ValueTree createFactoryLead();
    static juce::ValueTree createFactoryPad();
    static juce::ValueTree createFactoryPluck();
    static juce::ValueTree createFactoryFX();
};

} // namespace ana
