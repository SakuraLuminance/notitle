#include "EffectsChain.h"
#include <algorithm>

namespace ana {

EffectsChain::EffectsChain() {}

void EffectsChain::prepare(const juce::dsp::ProcessSpec& spec) {
    currentSpec = spec;
    for (auto& s : slots)
        if (s.effect) s.effect->prepare(spec);
}

void EffectsChain::process(juce::AudioBuffer<float>& buffer) {
    for (auto& s : slots) {
        if (s.bypassed || !s.effect) continue;
        s.effect->process(buffer);
    }
}

void EffectsChain::reset() {
    for (auto& s : slots)
        if (s.effect) s.effect->reset();
}

int EffectsChain::addEffect(std::unique_ptr<EffectBase> effect, const juce::String& name) {
    EffectSlot slot;
    slot.effect = std::move(effect);
    slot.name = name;
    if (currentSpec.sampleRate > 0 && slot.effect)
        slot.effect->prepare(currentSpec);
    slots.push_back(std::move(slot));
    return static_cast<int>(slots.size()) - 1;
}

void EffectsChain::removeEffect(int index) {
    if (index >= 0 && index < static_cast<int>(slots.size()))
        slots.erase(slots.begin() + index);
}

void EffectsChain::reorderEffects(int from, int to) {
    if (from >= 0 && from < static_cast<int>(slots.size()) &&
        to >= 0 && to < static_cast<int>(slots.size())) {
        auto slot = std::move(slots[from]);
        slots.erase(slots.begin() + from);
        slots.insert(slots.begin() + to, std::move(slot));
    }
}

void EffectsChain::bypassEffect(int index, bool bypass) {
    if (index >= 0 && index < static_cast<int>(slots.size()))
        slots[index].bypassed = bypass;
}

void EffectsChain::setMix(int index, float wetDry) {
    if (index >= 0 && index < static_cast<int>(slots.size()))
        slots[index].mix = std::max(0.0f, std::min(1.0f, wetDry));
}

int EffectsChain::getNumEffects() const { return static_cast<int>(slots.size()); }
EffectSlot& EffectsChain::getEffect(int index) { return slots[index]; }
void EffectsChain::clear() { slots.clear(); }

} // namespace ana
