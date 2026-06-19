#pragma once

//==============================================================================
// Shared adapter classes for non-EffectBase standalone effects.
//
// These bridge the concrete effect API to the EffectBase interface so they
// can be created uniformly through ProcessorStore or wired directly into
// the effect chain.  Included by both ProcessorStore.cpp and
// PluginProcessor.cpp to eliminate duplicate class definitions.
//==============================================================================

#include "EffectsChain.h"
#include "effects/DelayEffect.h"
#include "effects/ReverbEffect.h"
#include "effects/ChorusEffect.h"
#include "effects/DistortionEffect.h"
#include "effects/EQEffect.h"
#include "effects/AutoTuneEffect.h"

namespace ana {
namespace {

//==============================================================================
// DelayEffectAdapter
//==============================================================================
class DelayEffectAdapter : public EffectBase
{
    DelayEffect effect;

public:
    DelayEffect* getEffect() { return &effect; }

    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
    juce::ValueTree getState() const override                 { return effect.getState(); }
    void setState(const juce::ValueTree& s) override          { effect.setState(s); }
};

//==============================================================================
// ReverbEffectAdapter
//==============================================================================
class ReverbEffectAdapter : public EffectBase
{
    ReverbEffect effect;

public:
    ReverbEffect* getEffect() { return &effect; }

    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
    juce::ValueTree getState() const override                 { return effect.getState(); }
    void setState(const juce::ValueTree& s) override          { effect.setState(s); }
};

//==============================================================================
// ChorusEffectAdapter
//==============================================================================
class ChorusEffectAdapter : public EffectBase
{
    ChorusEffect effect;

public:
    ChorusEffect* getEffect() { return &effect; }

    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
    juce::ValueTree getState() const override                 { return effect.getState(); }
    void setState(const juce::ValueTree& s) override          { effect.setState(s); }
};

//==============================================================================
// DistortionEffectAdapter
//==============================================================================
class DistortionEffectAdapter : public EffectBase
{
    DistortionEffect effect;

public:
    DistortionEffect* getEffect() { return &effect; }

    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
    juce::ValueTree getState() const override                 { return effect.getState(); }
    void setState(const juce::ValueTree& s) override          { effect.setState(s); }
};

//==============================================================================
// EQEffectAdapter
//==============================================================================
class EQEffectAdapter : public EffectBase
{
    EQEffect effect;

public:
    EQEffect* getEffect() { return &effect; }

    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
    juce::ValueTree getState() const override                 { return effect.getState(); }
    void setState(const juce::ValueTree& s) override          { effect.setState(s); }
};

//==============================================================================
// AutoTuneEffectAdapter
//
// Uses setSampleRate instead of deferred prepare, and processBlock instead of
// process, because AutoTuneEffect has a different internal architecture.
//==============================================================================
class AutoTuneEffectAdapter : public EffectBase
{
    AutoTuneEffect effect;

public:
    AutoTuneEffect* getEffect() { return &effect; }

    void prepare(const juce::dsp::ProcessSpec& spec) override
    {
        effect.prepare(spec);
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        effect.processBlock(buffer);
    }

    void reset() override                             { effect.reset(); }
    juce::ValueTree getState() const override         { return effect.getState(); }
    void setState(const juce::ValueTree& s) override  { effect.setState(s); }
};

} // namespace
} // namespace ana
