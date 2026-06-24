#include "MacroController.h"
#include "MidiLearn.h"
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

    const float clamped = juce::jlimit(0.0f, 1.0f, value);

    macros_[static_cast<size_t>(macroIndex)].value.store(
        clamped, std::memory_order_relaxed);

    // Push mapped value to direct (new-style) targets
    pushToDirectTargets(macroIndex);

    updateTargetCache();
}

void MacroController::pushToDirectTargets(int macroIndex)
{
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return;

    auto& macro = macros_[static_cast<size_t>(macroIndex)];
    const float rawVal = macro.value.load(std::memory_order_relaxed);
    const float mapped = std::pow(rawVal, macro.mappingCurve);

    for (auto& target : macro.targets)
    {
        if (target.target != nullptr)
        {
            target.target->store(juce::jlimit(0.0f, 1.0f, mapped),
                                 std::memory_order_relaxed);
        }
    }
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
// --- Mapping Curve ---

void MacroController::setMappingCurve(int macroIndex, float exponent)
{
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return;

    macros_[static_cast<size_t>(macroIndex)].mappingCurve =
        juce::jlimit(0.01f, 10.0f, exponent);

    // Re-apply curve to direct targets
    pushToDirectTargets(macroIndex);
}

float MacroController::getMappingCurve(int macroIndex) const
{
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return 1.0f;

    return macros_[static_cast<size_t>(macroIndex)].mappingCurve;
}

//==============================================================================
// --- Direct Target Binding ---

void MacroController::setMacroTarget(int macroIndex,
                                     const juce::String& paramId,
                                     std::atomic<float>* target)
{
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return;

    auto& macro = macros_[static_cast<size_t>(macroIndex)];

    // Check if target with this paramId already exists → update it
    for (auto& t : macro.targets)
    {
        if (t.paramId == paramId)
        {
            t.target = target;
            pushToDirectTargets(macroIndex);
            return;
        }
    }

    // Enforce max targets
    if (static_cast<int>(macro.targets.size()) >= maxDirectTargetsPerMacro)
        return;

    MacroTarget mt;
    mt.paramId = paramId;
    mt.target  = target;
    macro.targets.push_back(std::move(mt));

    pushToDirectTargets(macroIndex);
}

void MacroController::clearMacroTarget(int macroIndex, const juce::String& paramId)
{
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return;

    auto& macro = macros_[static_cast<size_t>(macroIndex)];
    auto it = std::remove_if(macro.targets.begin(), macro.targets.end(),
        [&paramId](const MacroTarget& t) { return t.paramId == paramId; });
    macro.targets.erase(it, macro.targets.end());
}

void MacroController::clearMacroTargets(int macroIndex)
{
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return;

    macros_[static_cast<size_t>(macroIndex)].targets.clear();
}

int MacroController::getNumMacroTargets(int macroIndex) const
{
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return 0;

    return static_cast<int>(macros_[static_cast<size_t>(macroIndex)].targets.size());
}

//==============================================================================
// --- MIDI Learn Integration ---

void MacroController::bindMidiLearn(int macroIndex, MidiLearn& midiLearn)
{
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return;

    auto& macro = macros_[static_cast<size_t>(macroIndex)];
    const juce::String paramId = "macro_" + juce::String(macroIndex + 1);

    // Remove any previous binding for this macro
    if (macro.midiLearnCC >= 0)
        midiLearn.removeMapping(macro.midiLearnCC);

    // Start learn mode so the next incoming CC binds to this macro
    midiLearn.startLearn(paramId, &macro.value, 0.0f, 1.0f);

    // We don't know the CC yet - it will be set when learn completes
    macro.midiLearnCC = -2; // -2 = currently learning
}

void MacroController::unbindMidiLearn(int macroIndex)
{
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return;

    auto& macro = macros_[static_cast<size_t>(macroIndex)];
    // We don't have a direct reference to MidiLearn here,
    // so the caller is responsible for calling midiLearn.removeMapping().
    // This just resets the local tracking state.
    macro.midiLearnCC = -1;
}

//==============================================================================
// --- Visualization Data ---

MacroVisualData MacroController::getVisualData(int macroIndex) const
{
    MacroVisualData data;
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return data;

    const auto& macro = macros_[static_cast<size_t>(macroIndex)];
    data.value         = macro.value.load(std::memory_order_relaxed);
    data.curveExponent = macro.mappingCurve;
    data.mappedValue   = std::pow(data.value, data.curveExponent);
    data.name          = macro.name;
    data.numTargets    = static_cast<int>(macro.targets.size());
    return data;
}

std::atomic<float>* MacroController::getMacroValuePtr(int macroIndex)
{
    if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
        return nullptr;

    return &macros_[static_cast<size_t>(macroIndex)].value;
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
std::unique_ptr<juce::XmlElement> MacroController::createXml() const
{
    auto xml = std::make_unique<juce::XmlElement>("macrocontroller");

    for (int m = 0; m < static_cast<int>(macros_.size()); ++m)
    {
        const auto& macro = macros_[static_cast<size_t>(m)];
        auto* macroXml = xml->createNewChildElement("macro");
        macroXml->setAttribute("index",   m);
        macroXml->setAttribute("name",    macro.name);
        macroXml->setAttribute("curve",   static_cast<double>(macro.mappingCurve));

        for (const auto& mapping : macro.mappings)
        {
            auto* mapXml = macroXml->createNewChildElement("mapping");
            mapXml->setAttribute("target", mapping.targetParamIndex);
            mapXml->setAttribute("min",    static_cast<double>(mapping.min));
            mapXml->setAttribute("max",    static_cast<double>(mapping.max));
            mapXml->setAttribute("curve",  static_cast<int>(mapping.curve));
            mapXml->setAttribute("bipolar", mapping.bipolar);
            mapXml->setAttribute("invert",  mapping.invert);
        }

        // Serialize direct targets (paramId only - the atomic pointer
        // is reconnected at runtime by the plugin)
        for (const auto& target : macro.targets)
        {
            auto* tgtXml = macroXml->createNewChildElement("directtarget");
            tgtXml->setAttribute("paramId", target.paramId);
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
    for (auto* macroXml = xml.getFirstChildElement(); macroXml != nullptr; macroXml = macroXml->getNextElement())
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
    for (auto* macroXml = xml.getFirstChildElement(); macroXml != nullptr; macroXml = macroXml->getNextElement())
    {
        if (! macroXml->hasTagName("macro"))
            continue;

        const int macroIndex = macroXml->getIntAttribute("index", -1);
        if (macroIndex < 0 || macroIndex >= static_cast<int>(macros_.size()))
            continue;

        auto& macro = macros_[static_cast<size_t>(macroIndex)];
        macro.name         = macroXml->getStringAttribute("name");
        macro.mappingCurve = static_cast<float>(
            macroXml->getDoubleAttribute("curve", 1.0));
        macro.mappings.clear();
        macro.targets.clear();

        for (auto* mapXml : *macroXml)
        {
            if (mapXml->hasTagName("mapping"))
            {
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
            else if (mapXml->hasTagName("directtarget"))
            {
                MacroTarget mt;
                mt.paramId = mapXml->getStringAttribute("paramId");
                mt.target  = nullptr; // reconnected at runtime
                macro.targets.push_back(std::move(mt));
            }
        }
    }

    // ---- Recompute target cache with the restored state ----
    updateTargetCache();
}

} // namespace ana
