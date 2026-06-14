#include "MidiLearn.h"
#include <algorithm>  // std::remove_if
#include <utility>    // std::move

namespace ana {

//==============================================================================
void MidiLearn::addMapping(int cc, const juce::String& paramId,
                           std::atomic<float>* target, float min, float max)
{
    // Remove any existing mapping for this CC number first
    removeMapping(cc);

    MidiMapping mapping;
    mapping.ccNumber    = cc;
    mapping.parameterId = paramId;
    mapping.targetParam = target;
    mapping.minValue    = min;
    mapping.maxValue    = max;
    mappings_.push_back(std::move(mapping));
}

void MidiLearn::removeMapping(int cc)
{
    mappings_.erase(std::remove_if(mappings_.begin(), mappings_.end(),
        [cc](const MidiMapping& m) { return m.ccNumber == cc; }),
        mappings_.end());
}

void MidiLearn::removeAllMappings()
{
    mappings_.clear();
}

//==============================================================================
void MidiLearn::processMidi(const juce::MidiMessage& msg)
{
    if (!msg.isController())
        return;

    const int cc    = msg.getControllerNumber();
    const float value = msg.getControllerValue() / 127.0f;

    // --- Learn mode: capture the first CC we receive ---
    if (learning_)
    {
        addMapping(cc, learnParamId_, learnTarget_, learnMin_, learnMax_);

        // Apply the value immediately so the parameter snaps to the
        // current controller position
        if (learnTarget_ != nullptr)
        {
            const float scaled = learnMin_ + value * (learnMax_ - learnMin_);
            learnTarget_->store(scaled);
        }

        stopLearn();
        return;
    }

    // --- Normal mode: find a mapping for this CC ---
    for (auto& mapping : mappings_)
    {
        if (mapping.ccNumber == cc)
        {
            if (mapping.targetParam != nullptr)
            {
                const float scaled = mapping.minValue
                                   + value * (mapping.maxValue - mapping.minValue);
                mapping.targetParam->store(scaled);
            }
            return; // first match wins (one CC → one mapping)
        }
    }
}

//==============================================================================
void MidiLearn::startLearn(const juce::String& paramId, std::atomic<float>* target,
                           float min, float max)
{
    learning_       = true;
    learnParamId_   = paramId;
    learnTarget_    = target;
    learnMin_       = min;
    learnMax_       = max;
}

void MidiLearn::stopLearn()
{
    learning_     = false;
    learnParamId_ = {};
    learnTarget_  = nullptr;
    learnMin_     = 0.0f;
    learnMax_     = 1.0f;
}

//==============================================================================
void MidiLearn::reconnectTarget(const juce::String& paramId, std::atomic<float>* target)
{
    for (auto& m : mappings_)
    {
        if (m.parameterId == paramId)
        {
            m.targetParam = target;
            return;
        }
    }
}

//==============================================================================
juce::ValueTree MidiLearn::saveState() const
{
    juce::ValueTree state("MidiLearn");

    for (const auto& m : mappings_)
    {
        auto child = juce::ValueTree("Mapping");
        child.setProperty("cc",      m.ccNumber,     nullptr);
        child.setProperty("paramId", m.parameterId,  nullptr);
        child.setProperty("min",     m.minValue,      nullptr);
        child.setProperty("max",     m.maxValue,      nullptr);
        // targetParam is NOT serialised – it is a runtime pointer
        state.addChild(child, -1, nullptr);
    }

    return state;
}

void MidiLearn::loadState(const juce::ValueTree& state)
{
    if (!state.isValid() || !state.hasType("MidiLearn"))
        return;

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
        mapping.targetParam = nullptr; // reconnected by the editor after load
        mappings_.push_back(std::move(mapping));
    }
}

} // namespace ana
