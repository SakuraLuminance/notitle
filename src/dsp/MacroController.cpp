#include "MacroController.h"
#include "SIMDSupport.h"

#include <algorithm>
#include <cmath>

namespace ana {

//==============================================================================
MacroController::MacroController()
{
}

//==============================================================================
void MacroController::setNumMacros(int count)
{
    count = std::clamp(count, 1, maxMacros);
    macros_.resize(count);
    updateTargetCache();
}

void MacroController::setMacroName(int index, const juce::String& name)
{
    if (index >= 0 && index < static_cast<int>(macros_.size()))
        macros_[static_cast<size_t>(index)].name = name;
}

//==============================================================================
void MacroController::addMapping(int macroIndex, const MacroMapping& mapping)
{
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return;

    macros_[static_cast<size_t>(macroIndex)].mappings.push_back(mapping);
    updateTargetCache();
}

void MacroController::clearMappings(int macroIndex)
{
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return;

    macros_[static_cast<size_t>(macroIndex)].mappings.clear();
    updateTargetCache();
}

void MacroController::clearAllMappings()
{
    for (auto& macro : macros_)
        macro.mappings.clear();

    updateTargetCache();
}

//==============================================================================
void MacroController::setMacroValue(int macroIndex, float value)
{
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return;

    macros_[static_cast<size_t>(macroIndex)].value.store(
        juce::jlimit(0.0f, 1.0f, value),
        std::memory_order_relaxed);

    updateTargetCache();
}

float MacroController::getMacroValue(int macroIndex) const
{
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return 0.0f;

    return macros_[static_cast<size_t>(macroIndex)].value.load(
        std::memory_order_relaxed);
}

//==============================================================================
int MacroController::getNumTargets() const
{
    return totalTargets_;
}

float MacroController::getTargetValue(int targetIndex) const
{
    if (targetIndex < 0 || targetIndex >= totalTargets_)
        return 0.0f;

    // Lock-free read from the active double-buffer.
    const int readIdx = readCacheIdx_.load(std::memory_order_acquire);
    const auto& readCache = readIdx == 0 ? targetCacheA_ : targetCacheB_;
    return readCache[static_cast<size_t>(targetIndex)];
}

//==============================================================================
void MacroController::reset()
{
    for (auto& macro : macros_)
        macro.value.store(0.0f, std::memory_order_relaxed);

    updateTargetCache();
}

//==============================================================================
float MacroController::applyCurve(float input, const MacroMapping& mapping) const noexcept
{
    // Clamp to valid input range
    float val = juce::jlimit(0.0f, 1.0f, input);

    // Apply the transfer curve shape
    switch (mapping.curve)
    {
        case MacroMapping::Curve::Linear:
            break;

        case MacroMapping::Curve::Exponential:
            val = val * val;
            break;

        case MacroMapping::Curve::Logarithmic:
            val = std::sqrt(val);
            break;

        case MacroMapping::Curve::SCurve:
            val = val < 0.5f
                      ? 2.0f * val * val
                      : 1.0f - 2.0f * (1.0f - val) * (1.0f - val);
            break;

        case MacroMapping::Curve::ReverseSCurve:
            val = val < 0.5f
                      ? 0.5f * std::sqrt(2.0f * val)
                      : 1.0f - 0.5f * std::sqrt(2.0f * (1.0f - val));
            break;

        case MacroMapping::Curve::Step:
            val = val > 0.5f ? 1.0f : 0.0f;
            break;
    }

    // Optional invert flips the curve output
    if (mapping.invert)
        val = 1.0f - val;

    return val; // Always in [0, 1]
}

//==============================================================================
void MacroController::updateTargetCache()
{
    // ---- Count total mappings across all macros ----
    int newTotal = 0;
    for (const auto& macro : macros_)
        newTotal += static_cast<int>(macro.mappings.size());

    totalTargets_ = newTotal;

    // ---- Resize all buffers ----
    targetCacheA_.resize(static_cast<size_t>(newTotal));
    targetCacheB_.resize(static_cast<size_t>(newTotal));
    simdCurve_.resize(static_cast<size_t>(newTotal));
    simdScale_.resize(static_cast<size_t>(newTotal));
    simdOffset_.resize(static_cast<size_t>(newTotal));

    if (newTotal == 0)
        return;

    // ---- Pass 1: compute curve values + per-mapping scale/offset ----
    size_t idx = 0;
    for (const auto& macro : macros_)
    {
        const float macroVal = macro.value.load(std::memory_order_relaxed);

        for (const auto& mapping : macro.mappings)
        {
            // Normalized curve output [0, 1]
            simdCurve_[idx] = applyCurve(macroVal, mapping);

            // Compute scale factor and offset for the final mapping:
            //
            //   Unipolar:  out = min + curveVal * (max - min)
            //   Bipolar:   out = curveVal * 2 - 1   (range [-1, 1])
            //
            // Both are expressed as:  out = curveVal * scale + offset
            // so they can be processed by the same SIMD kernel.
            if (mapping.bipolar)
            {
                simdScale_[idx] = 2.0f;
                simdOffset_[idx] = -1.0f;
            }
            else
            {
                simdScale_[idx] = mapping.max - mapping.min;
                simdOffset_[idx] = mapping.min;
            }

            ++idx;
        }
    }

    // ---- Pass 2: SIMD-accelerated scale + offset ----
    //
    // Write to the *inactive* buffer, then atomically flip readCacheIdx_.
    // The audio thread always reads from the active buffer, so it never
    // sees a partially-updated cache.

    const int writeIdx = 1 - readCacheIdx_.load(std::memory_order_relaxed);
    auto& writeCache = writeIdx == 0 ? targetCacheA_ : targetCacheB_;

    // writeCache[i] = simdCurve_[i] * simdScale_[i]  +  simdOffset_[i]
    SIMDKernels::vectorMul(
        writeCache.data(),
        simdCurve_.data(),
        simdScale_.data(),
        newTotal);

    SIMDKernels::vectorAdd(
        writeCache.data(),
        simdOffset_.data(),
        newTotal);

    // Ensure all cache writes are visible before flipping the read index.
    std::atomic_thread_fence(std::memory_order_release);
    readCacheIdx_.store(writeIdx, std::memory_order_release);
}

//==============================================================================
juce::XmlElement* MacroController::createXml() const
{
    auto* xml = new juce::XmlElement("macrocontroller");

    for (int m = 0; m < static_cast<int>(macros_.size()); ++m)
    {
        auto* macroXml = xml->createNewChildElement("macro");
        macroXml->setAttribute("index", m);
        macroXml->setAttribute("name", macros_[static_cast<size_t>(m)].name);

        for (const auto& mapping : macros_[static_cast<size_t>(m)].mappings)
        {
            auto* mapXml = macroXml->createNewChildElement("mapping");
            mapXml->setAttribute("target", mapping.targetParamIndex);
            mapXml->setAttribute("min",    static_cast<double>(mapping.min));
            mapXml->setAttribute("max",    static_cast<double>(mapping.max));
            mapXml->setAttribute("curve",  static_cast<int>(mapping.curve));
            mapXml->setAttribute("bipolar", mapping.bipolar);
            mapXml->setAttribute("invert",  mapping.invert);
        }
    }

    return xml;
}

void MacroController::loadFromXml(const juce::XmlElement& xml)
{
    if (xml.getTagName() != "macrocontroller")
        return;

    // ---- Count how many macros the XML describes ----
    int xmlMacroCount = 0;
    for (auto* macroXml : xml)
    {
        if (macroXml->hasTagName("macro"))
        {
            const int idx = macroXml->getIntAttribute("index", -1);
            if (idx >= 0)
                xmlMacroCount = std::max(xmlMacroCount, idx + 1);
        }
    }

    if (xmlMacroCount == 0)
    {
        clearAllMappings();
        return;
    }

    // ---- Resize macros_ to match the XML content ----
    setNumMacros(xmlMacroCount);

    // ---- Restore each macro ----
    for (auto* macroXml : xml)
    {
        if (! macroXml->hasTagName("macro"))
            continue;

        const int macroIndex = macroXml->getIntAttribute("index", -1);
        if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
            continue;

        auto& macro = macros_[static_cast<size_t>(macroIndex)];
        macro.name = macroXml->getStringAttribute("name");
        macro.mappings.clear();

        for (auto* mapXml : *macroXml)
        {
            if (! mapXml->hasTagName("mapping"))
                continue;

            MacroMapping mapping;
            mapping.targetParamIndex = mapXml->getIntAttribute("target", 0);
            mapping.min              = static_cast<float>(mapXml->getDoubleAttribute("min", 0.0));
            mapping.max              = static_cast<float>(mapXml->getDoubleAttribute("max", 1.0));
            mapping.curve            = static_cast<MacroMapping::Curve>(
                                           mapXml->getIntAttribute("curve", 0));
            mapping.bipolar          = mapXml->getBoolAttribute("bipolar", false);
            mapping.invert           = mapXml->getBoolAttribute("invert",  false);

            macro.mappings.push_back(mapping);
        }
    }

    // ---- Recompute target cache with the restored state ----
    updateTargetCache();
}

} // namespace ana
