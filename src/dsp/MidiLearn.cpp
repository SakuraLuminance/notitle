#include "MidiLearn.h"
#include <algorithm>  // std::remove_if
#include <utility>    // std::move

namespace ana {

namespace {

/** Writes a scaled value into whichever target kind is connected.
    The atomic path is tried first so existing mappings keep their exact
    behaviour; the callback (effect-parameter) path is the fallback. */
void applyTargetValue(std::atomic<float>* target,
                      const std::function<void(float)>& setter,
                      float scaled)
{
    if (target != nullptr)
        target->store(scaled, std::memory_order_relaxed);
    else if (setter)
        setter(scaled);
}

} // namespace

//==============================================================================
void MidiLearn::addMappingInternal(int cc, const juce::String& paramId,
                                   std::atomic<float>* target,
                                   std::function<void(float)> setter,
                                   std::function<float()> getter,
                                   float min, float max)
{
    // Remove any existing mapping for this CC number first (lock_ is held, so this
    // cannot go through removeMapping()).
    mappings_.erase(std::remove_if(mappings_.begin(), mappings_.end(),
        [cc](const MidiMapping& m) { return m.ccNumber == cc; }),
        mappings_.end());

    MidiMapping mapping;
    mapping.ccNumber     = cc;
    mapping.parameterId  = paramId;
    mapping.targetParam  = target;
    mapping.targetSetter = std::move(setter);
    mapping.targetGetter = std::move(getter);
    mapping.minValue     = min;
    mapping.maxValue     = max;
    mappings_.push_back(std::move(mapping));
}

void MidiLearn::addMapping(int cc, const juce::String& paramId,
                           std::atomic<float>* target, float min, float max)
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    addMappingInternal(cc, paramId, target, {}, {}, min, max);
}

void MidiLearn::addMapping(int cc, const juce::String& paramId,
                           std::function<void(float)> setter,
                           std::function<float()> getter,
                           float min, float max)
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    addMappingInternal(cc, paramId, nullptr, std::move(setter), std::move(getter), min, max);
}

void MidiLearn::removeMapping(int cc)
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    mappings_.erase(std::remove_if(mappings_.begin(), mappings_.end(),
        [cc](const MidiMapping& m) { return m.ccNumber == cc; }),
        mappings_.end());
}

void MidiLearn::removeAllMappings()
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    mappings_.clear();
}

void MidiLearn::setMappingGlobal(const juce::String& paramId, bool isGlobal)
{
    const juce::SpinLock::ScopedLockType sl (lock_);

    for (auto& m : mappings_)
    {
        if (m.parameterId == paramId)
        {
            m.isGlobal = isGlobal;
            return;
        }
    }
}

//==============================================================================
void MidiLearn::processMidi(const juce::MidiMessage& msg)
{
    if (!msg.isController())
        return;

    // Audio thread: never block.  Dropping one CC while the user edits a mapping is
    // harmless; walking a vector that the message thread is reallocating is not -
    // every element owns std::function objects, so the old code could call through a
    // destroyed functor.
    const juce::SpinLock::ScopedTryLockType audioLock (lock_);
    if (!audioLock.isLocked())
        return;

    const int cc    = msg.getControllerNumber();
    const float value = msg.getControllerValue() / 127.0f;

    // --- Learn mode: capture the first CC we receive ---
    if (learning_.load())
    {
        // Only record the capture here.  Creating the mapping reallocates the table
        // and allocates the std::function it holds, neither of which belongs on the
        // audio thread, so applyPendingLearn() does it on the message thread.
        pendingLearnCc_.store(cc, std::memory_order_relaxed);

        // Apply the value immediately so the parameter snaps to the
        // current controller position
        applyTargetValue(learnTarget_, learnSetter_,
                         learnMin_ + value * (learnMax_ - learnMin_));

        learning_.store(false, std::memory_order_release);
        return;
    }

    // --- Normal mode: find a mapping for this CC ---
    for (auto& mapping : mappings_)
    {
        if (mapping.ccNumber == cc)
        {
            applyTargetValue(mapping.targetParam, mapping.targetSetter,
                             mapping.minValue + value * (mapping.maxValue - mapping.minValue));
            return; // first match wins (one CC → one mapping)
        }
    }
}

//==============================================================================
void MidiLearn::startLearn(const juce::String& paramId, std::atomic<float>* target,
                           float min, float max)
{
    const juce::SpinLock::ScopedLockType sl (lock_);

    // Fields first, flag last: the release store is what publishes the learn target
    // the audio thread is about to read.
    learnParamId_   = paramId;
    learnTarget_    = target;
    learnSetter_    = {};
    learnGetter_    = {};
    learnMin_       = min;
    learnMax_       = max;
    learning_.store(true, std::memory_order_release);
}

void MidiLearn::startLearn(const juce::String& paramId,
                           std::function<void(float)> setter,
                           std::function<float()> getter,
                           float min, float max)
{
    const juce::SpinLock::ScopedLockType sl (lock_);

    learnParamId_   = paramId;
    learnTarget_    = nullptr;
    learnSetter_    = std::move(setter);
    learnGetter_    = std::move(getter);
    learnMin_       = min;
    learnMax_       = max;
    learning_.store(true, std::memory_order_release);
}

void MidiLearn::stopLearn()
{
    const juce::SpinLock::ScopedLockType sl (lock_);

    // A capture may already have arrived from the audio thread: turn it into a
    // mapping before the learn fields are cleared, or the CC the user just moved is
    // silently dropped.
    const int cc = pendingLearnCc_.exchange(-1, std::memory_order_relaxed);
    if (cc >= 0 && !learnParamId_.isEmpty())
        addMappingInternal(cc, learnParamId_, learnTarget_,
                           learnSetter_, learnGetter_, learnMin_, learnMax_);

    learning_.store(false, std::memory_order_release);
    learnParamId_ = {};
    learnTarget_  = nullptr;
    learnSetter_  = {};
    learnGetter_  = {};
    learnMin_     = 0.0f;
    learnMax_     = 1.0f;
}

bool MidiLearn::applyPendingLearn()
{
    const int cc = pendingLearnCc_.exchange(-1, std::memory_order_relaxed);
    if (cc < 0)
        return false;

    const juce::SpinLock::ScopedLockType sl (lock_);

    if (learnParamId_.isEmpty())
        return false;

    addMappingInternal(cc, learnParamId_, learnTarget_,
                       learnSetter_, learnGetter_, learnMin_, learnMax_);
    return true;
}

//==============================================================================
void MidiLearn::reconnectTarget(const juce::String& paramId, std::atomic<float>* target)
{
    const juce::SpinLock::ScopedLockType sl (lock_);

    for (auto& m : mappings_)
    {
        if (m.parameterId == paramId)
        {
            m.targetParam  = target;
            m.targetSetter = {};
            m.targetGetter = {};
            return;
        }
    }
}

void MidiLearn::reconnectTarget(const juce::String& paramId,
                                std::function<void(float)> setter,
                                std::function<float()> getter)
{
    const juce::SpinLock::ScopedLockType sl (lock_);

    for (auto& m : mappings_)
    {
        if (m.parameterId == paramId)
        {
            m.targetParam  = nullptr;
            m.targetSetter = std::move(setter);
            m.targetGetter = std::move(getter);
            return;
        }
    }
}

//==============================================================================
bool MidiLearn::getMappingValue(const juce::String& paramId, float& outValue) const
{
    const juce::SpinLock::ScopedLockType sl (lock_);

    for (const auto& m : mappings_)
    {
        if (m.parameterId != paramId)
            continue;

        if (m.targetParam != nullptr)
        {
            outValue = m.targetParam->load(std::memory_order_relaxed);
            return true;
        }

        if (m.targetGetter)
        {
            outValue = m.targetGetter();
            return true;
        }

        return false;
    }

    return false;
}

//==============================================================================
// Processor state: save/load ALL mappings (global + per-preset)
juce::ValueTree MidiLearn::saveProcessorState() const
{
    const juce::SpinLock::ScopedLockType sl (lock_);

    juce::ValueTree state("MidiLearn");

    for (const auto& m : mappings_)
    {
        auto child = juce::ValueTree("Mapping");
        child.setProperty("cc",      m.ccNumber,     nullptr);
        child.setProperty("paramId", m.parameterId,  nullptr);
        child.setProperty("min",     m.minValue,      nullptr);
        child.setProperty("max",     m.maxValue,      nullptr);
        child.setProperty("global",  m.isGlobal,      nullptr);
        // targetParam is NOT serialised – it is a runtime pointer
        state.addChild(child, -1, nullptr);
    }

    return state;
}

void MidiLearn::loadProcessorState(const juce::ValueTree& state)
{
    if (!state.isValid() || !state.hasType("MidiLearn"))
        return;

    const juce::SpinLock::ScopedLockType sl (lock_);

    mappings_.clear();

    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto child = state.getChild(i);
        if (!child.hasType("Mapping"))
            continue;

        MidiMapping mapping;
        mapping.ccNumber    = child.getProperty("cc",     -1);
        mapping.parameterId = child.getProperty("paramId", {});
        mapping.minValue    = child.getProperty("min",     0.0f);
        mapping.maxValue    = child.getProperty("max",     1.0f);
        mapping.isGlobal    = child.getProperty("global",  false);
        mapping.targetParam = nullptr; // reconnected by the editor after load
        mappings_.push_back(std::move(mapping));
    }
}

//==============================================================================
// Preset state: save/load only per-preset (non-global) mappings
juce::ValueTree MidiLearn::savePresetState() const
{
    const juce::SpinLock::ScopedLockType sl (lock_);

    juce::ValueTree state("MidiLearnPreset");

    for (const auto& m : mappings_)
    {
        if (m.isGlobal)
            continue; // skip global mappings — they survive presets

        auto child = juce::ValueTree("Mapping");
        child.setProperty("cc",      m.ccNumber,     nullptr);
        child.setProperty("paramId", m.parameterId,  nullptr);
        child.setProperty("min",     m.minValue,      nullptr);
        child.setProperty("max",     m.maxValue,      nullptr);
        state.addChild(child, -1, nullptr);
    }

    return state;
}

void MidiLearn::loadPresetState(const juce::ValueTree& state)
{
    if (!state.isValid() || !state.hasType("MidiLearnPreset"))
        return;

    const juce::SpinLock::ScopedLockType sl (lock_);

    // Remove all non-global mappings (global ones survive)
    mappings_.erase(std::remove_if(mappings_.begin(), mappings_.end(),
        [](const MidiMapping& m) { return !m.isGlobal; }),
        mappings_.end());

    // Load the per-preset mappings
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto child = state.getChild(i);
        if (!child.hasType("Mapping"))
            continue;

        MidiMapping mapping;
        mapping.ccNumber    = child.getProperty("cc",     -1);
        mapping.parameterId = child.getProperty("paramId", {});
        mapping.minValue    = child.getProperty("min",     0.0f);
        mapping.maxValue    = child.getProperty("max",     1.0f);
        mapping.isGlobal    = false; // loaded mappings are always per-preset
        mapping.targetParam = nullptr; // reconnected by the editor after load
        mappings_.push_back(std::move(mapping));
    }
}

} // namespace ana
