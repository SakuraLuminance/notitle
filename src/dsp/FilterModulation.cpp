#include "FilterModulation.h"
#include <algorithm>

namespace ana {

//==============================================================================
FilterModulationSystem::FilterModulationSystem()
{
    // Initialise source values array to zero
    std::fill(std::begin(sourceValues), std::end(sourceValues), 0.0f);
}

//==============================================================================
int FilterModulationSystem::connect(ModulationSource source, ModulationTarget target,
                                    int filterIndex, float depth, bool bipolar)
{
    ModulationConnection conn;
    conn.source = source;
    conn.target = target;
    conn.filterIndex = filterIndex;
    conn.depth = std::clamp(depth, 0.0f, 1.0f);
    conn.bipolar = bipolar;
    conn.id = nextConnectionId++;

    connections.push_back(conn);
    return conn.id;
}

bool FilterModulationSystem::disconnect(int connectionId)
{
    auto it = std::find_if(connections.begin(), connections.end(),
        [connectionId](const ModulationConnection& c) { return c.id == connectionId; });

    if (it != connections.end())
    {
        connections.erase(it);
        return true;
    }
    return false;
}

void FilterModulationSystem::disconnectAll(int filterIndex)
{
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
            [filterIndex](const ModulationConnection& c)
            {
                return c.filterIndex == filterIndex;
            }),
        connections.end());
}

void FilterModulationSystem::clearAll()
{
    connections.clear();
}

//==============================================================================
void FilterModulationSystem::setSourceValue(ModulationSource source, float value)
{
    int index = static_cast<int>(source);
    if (index >= 0 && index < 7)
        sourceValues[index] = value;
}

float FilterModulationSystem::getSourceValue(ModulationSource source) const
{
    int index = static_cast<int>(source);
    if (index >= 0 && index < 7)
        return sourceValues[index];
    return 0.0f;
}

//==============================================================================
float FilterModulationSystem::getEffectiveSourceValue(const ModulationConnection& conn) const
{
    float rawValue = sourceValues[static_cast<int>(conn.source)];

    // LFO sources are inherently bipolar (-1..1), use as-is
    if (conn.source == ModulationSource::LFO1 || conn.source == ModulationSource::LFO2)
        return rawValue;

    // For unipolar sources (0..1):
    if (conn.bipolar)
    {
        // Remap 0..1 to -1..1 for bipolar modulation
        return rawValue * 2.0f - 1.0f;
    }
    else
    {
        // Unipolar: use value as-is (0..1), only positive modulation
        return rawValue;
    }
}

float FilterModulationSystem::getModulationValue(ModulationTarget target, int filterIndex) const
{
    float totalModulation = 0.0f;

    for (const auto& conn : connections)
    {
        if (conn.target == target && conn.filterIndex == filterIndex)
        {
            float effectiveSource = getEffectiveSourceValue(conn);
            totalModulation += effectiveSource * conn.depth;
        }
    }

    return std::clamp(totalModulation, -1.0f, 1.0f);
}

//==============================================================================
float FilterModulationSystem::getModulatedCutoff(float baseCutoff, int filterIndex) const
{
    float modulation = getModulationValue(ModulationTarget::Cutoff, filterIndex);
    // Musical 4-octave range: modulation -1..1 maps to multiplier 0.25..4.0
    float multiplier = std::pow(2.0f, modulation * 2.0f);
    return baseCutoff * multiplier;
}

float FilterModulationSystem::getModulatedResonance(float baseResonance, int filterIndex) const
{
    float modulation = getModulationValue(ModulationTarget::Resonance, filterIndex);
    return clamp01(baseResonance + modulation);
}

float FilterModulationSystem::getModulatedDrive(float baseDrive, int filterIndex) const
{
    float modulation = getModulationValue(ModulationTarget::Drive, filterIndex);
    return clamp01(baseDrive + modulation);
}

float FilterModulationSystem::getModulatedMix(float baseMix, int filterIndex) const
{
    float modulation = getModulationValue(ModulationTarget::Mix, filterIndex);
    return clamp01(baseMix + modulation);
}

//==============================================================================
const std::vector<ModulationConnection>& FilterModulationSystem::getConnections() const
{
    return connections;
}

int FilterModulationSystem::getNumConnections() const
{
    return static_cast<int>(connections.size());
}

void FilterModulationSystem::setNumFilters(int numFiltersInChain)
{
    numFilters = std::max(1, numFiltersInChain);
}

//==============================================================================
float FilterModulationSystem::clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace ana
