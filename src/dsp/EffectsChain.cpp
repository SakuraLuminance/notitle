#include "EffectsChain.h"
#include <algorithm>

namespace ana {

EffectsChain::EffectsChain() {}

void EffectsChain::prepare(const juce::dsp::ProcessSpec& spec) {
    currentSpec = spec;
    dryBuffer.setSize(static_cast<int>(spec.numChannels),
                      static_cast<int>(spec.maximumBlockSize));
    for (auto& s : slots) {
        if (s.effect) s.effect->prepare(spec);
        s.wetHPF.prepare(spec);
        s.wetLPF.prepare(spec);
        auto hpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(spec.sampleRate, s.wetLowCut, 0.707);
        auto lpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(spec.sampleRate, s.wetHighCut, 0.707);
        *s.wetHPF.state = *hpfCoeffs;
        *s.wetLPF.state = *lpfCoeffs;
    }
}

void EffectsChain::process(juce::AudioBuffer<float>& buffer) {
    for (auto& s : slots) {
        if (s.bypassed || !s.effect) continue;
        if (s.mix == 0.0f) continue; // zero CPU

        const bool needDryWet = s.mix < 1.0f;
        const bool needFilter = s.wetLowCut > 20.0f || s.wetHighCut < 20000.0f;

        if (!needDryWet && !needFilter) {
            s.effect->process(buffer);
            continue;
        }

        const auto numChannels = buffer.getNumChannels();
        const auto numSamples  = buffer.getNumSamples();

        // Capture dry signal
        for (int ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        // Process wet signal through the effect
        s.effect->process(buffer);

        // Apply HPF + LPF to the wet signal
        if (needFilter) {
            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> context(block);
            s.wetHPF.process(context);
            s.wetLPF.process(context);
        }

        // Blend dry + filtered wet
        if (needDryWet) {
            for (int ch = 0; ch < numChannels; ++ch) {
                auto* dst = buffer.getWritePointer(ch);
                const auto* dry = dryBuffer.getReadPointer(ch);
                for (int samp = 0; samp < numSamples; ++samp)
                    dst[samp] = dry[samp] * (1.0f - s.mix) + dst[samp] * s.mix;
            }
        }
    }
}

void EffectsChain::reset() {
    for (auto& s : slots) {
        if (s.effect) s.effect->reset();
        s.wetHPF.reset();
        s.wetLPF.reset();
    }
}

int EffectsChain::addEffect(std::unique_ptr<EffectBase> effect, const juce::String& name) {
    EffectSlot slot;
    slot.effect = std::move(effect);
    slot.name = name;
    if (currentSpec.sampleRate > 0) {
        if (slot.effect) slot.effect->prepare(currentSpec);
        slot.wetHPF.prepare(currentSpec);
        slot.wetLPF.prepare(currentSpec);
        auto hpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(currentSpec.sampleRate, slot.wetLowCut, 0.707);
        auto lpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(currentSpec.sampleRate, slot.wetHighCut, 0.707);
        *slot.wetHPF.state = *hpfCoeffs;
        *slot.wetLPF.state = *lpfCoeffs;
    }
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

void EffectsChain::setWetLowCut(int index, float hz) {
    if (index >= 0 && index < static_cast<int>(slots.size())) {
        slots[index].wetLowCut = hz;
        if (currentSpec.sampleRate > 0) {
            auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(currentSpec.sampleRate, hz, 0.707);
            *slots[index].wetHPF.state = *coeffs;
        }
    }
}

void EffectsChain::setWetHighCut(int index, float hz) {
    if (index >= 0 && index < static_cast<int>(slots.size())) {
        slots[index].wetHighCut = hz;
        if (currentSpec.sampleRate > 0) {
            auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(currentSpec.sampleRate, hz, 0.707);
            *slots[index].wetLPF.state = *coeffs;
        }
    }
}

int EffectsChain::getNumEffects() const { return static_cast<int>(slots.size()); }
EffectSlot& EffectsChain::getEffect(int index) { return slots[index]; }
void EffectsChain::clear() { slots.clear(); }

} // namespace ana
