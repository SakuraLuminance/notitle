#include "ModulationBus.h"
#include <algorithm>
#include <juce_core/juce_core.h>

namespace ana {

//==============================================================================
void ModulationBus::addRoute(Source src, int srcIndex, const std::string& targetId,
                              std::atomic<float>* target, const float* sourceValue, float depth)
{
    if (target == nullptr || sourceValue == nullptr)
        return;

    Route r;
    r.source        = src;
    r.sourceIndex   = srcIndex;
    r.targetParamId = targetId;
    r.targetParam   = target;
    r.sourceValue   = sourceValue;
    r.depth         = depth;
    routes_.push_back(r);
}

//==============================================================================
void ModulationBus::removeRoute(int index)
{
    if (index >= 0 && index < static_cast<int>(routes_.size()))
        routes_.erase(routes_.begin() + index);
}

//==============================================================================
void ModulationBus::clear()
{
    routes_.clear();
}

//==============================================================================
void ModulationBus::processBlock(int numSamples)
{
    juce::ignoreUnused(numSamples);

    for (auto& route : routes_)
    {
        if (route.targetParam == nullptr || route.sourceValue == nullptr)
            continue;

        // Read source value, scale by depth, write to target atomic
        const float modValue = (*route.sourceValue) * route.depth;
        route.targetParam->store(modValue, std::memory_order_relaxed);
    }
}

//==============================================================================
const ModulationBus::Route& ModulationBus::getRoute(int index) const
{
    jassert(index >= 0 && index < static_cast<int>(routes_.size()));
    return routes_[static_cast<size_t>(index)];
}

} // namespace ana
