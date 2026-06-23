#pragma once
#include <vector>
#include <memory>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_data_structures/juce_data_structures.h>

namespace ana {

class EffectBase {
public:
    virtual ~EffectBase() = default;
    virtual void prepare(const juce::dsp::ProcessSpec& spec) = 0;
    virtual void process(juce::AudioBuffer<float>& buffer) = 0;
    virtual void reset() = 0;
    virtual juce::ValueTree getState() const = 0;
    virtual void setState(const juce::ValueTree& state) = 0;
};

struct EffectSlot {
    std::unique_ptr<EffectBase> effect;
    bool bypassed = false;
    float mix = 1.0f;
    float wetLowCut = 20.0f;
    float wetHighCut = 20000.0f;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> wetHPF;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> wetLPF;
    juce::String name;
};

class EffectsChain {
public:
    EffectsChain();
    ~EffectsChain() = default;
    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::AudioBuffer<float>& buffer);
    void reset();
    int addEffect(std::unique_ptr<EffectBase> effect, const juce::String& name = {});
    void removeEffect(int index);
    void reorderEffects(int from, int to);
    void bypassEffect(int index, bool bypass);
    void setMix(int index, float wetDry);
    void setWetLowCut(int slotIndex, float hz);
    void setWetHighCut(int slotIndex, float hz);
    int getNumEffects() const;
    EffectSlot& getEffect(int index);
    void clear();
private:
    std::vector<EffectSlot> slots;
    juce::AudioBuffer<float> dryBuffer;
    juce::dsp::ProcessSpec currentSpec;
};

} // namespace ana
