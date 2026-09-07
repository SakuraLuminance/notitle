#pragma once

#include "../EffectsChain.h"
#include <juce_core/juce_core.h>

namespace ana
{

struct EffectParamRegistry
{
    static const juce::StringArray& getTypeNames();
    static std::unique_ptr<EffectBase> create(const juce::String& typeName);
};

} // namespace ana
