#pragma once
#include <vector>
#include <memory>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace ana {

class EffectBase {
public:
    virtual ~EffectBase() = default;
    virtual void prepare(const juce::dsp::ProcessSpec& spec) = 0;
    virtual void process(juce::AudioBuffer<float>& buffer) = 0;
    virtual void reset() = 0;
};

struct EffectSlot {
    std::unique_ptr<EffectBase> effect;
    bool bypassed = false;
    float mix = 1.0f;
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
    int getNumEffects() const;
    EffectSlot& getEffect(int index);
    void clear();
private:
    std::vector<EffectSlot> slots;
    juce::dsp::ProcessSpec currentSpec;
};

} // namespace ana
