#include "EffectsChain.h"
#include <algorithm>
#include <iterator>

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
        s.wetHPF2.prepare(spec);
        s.wetLPF2.prepare(spec);
        auto hpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(spec.sampleRate, s.wetLowCut, 0.707);
        auto lpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(spec.sampleRate, s.wetHighCut, 0.707);
        *s.wetHPF.state = *hpfCoeffs;
        *s.wetLPF.state = *lpfCoeffs;
        *s.wetHPF2.state = *hpfCoeffs;
        *s.wetLPF2.state = *lpfCoeffs;
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

        // Apply HPF + LPF to the wet signal (4th order: two cascaded stages)
        if (needFilter) {
            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> context(block);
            s.wetHPF.process(context);
            s.wetHPF2.process(context);
            s.wetLPF.process(context);
            s.wetLPF2.process(context);
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
        s.wetHPF2.reset();
        s.wetLPF2.reset();
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
        slot.wetHPF2.prepare(currentSpec);
        slot.wetLPF2.prepare(currentSpec);
        auto hpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(currentSpec.sampleRate, slot.wetLowCut, 0.707);
        auto lpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(currentSpec.sampleRate, slot.wetHighCut, 0.707);
        *slot.wetHPF.state = *hpfCoeffs;
        *slot.wetLPF.state = *lpfCoeffs;
        *slot.wetHPF2.state = *hpfCoeffs;
        *slot.wetLPF2.state = *lpfCoeffs;
    }
    slots.push_back(std::move(slot));
    return static_cast<int>(slots.size()) - 1;
}

void EffectsChain::removeEffect(int index) {
    if (index >= 0 && index < static_cast<int>(slots.size())) {
        auto it = slots.begin();
        std::advance(it, index);
        slots.erase(it);
    }
}

void EffectsChain::reorderEffects(int from, int to) {
    if (from == to) return;
    if (from >= 0 && from < static_cast<int>(slots.size()) &&
        to >= 0 && to < static_cast<int>(slots.size())) {
        auto fromIt = slots.begin();
        std::advance(fromIt, from);
        auto toIt = slots.begin();
        std::advance(toIt, to);
        auto slot = std::move(*fromIt);
        slots.erase(fromIt);
        slots.insert(toIt, std::move(slot));
    }
}

void EffectsChain::bypassEffect(int index, bool bypass) {
    if (index >= 0 && index < static_cast<int>(slots.size())) {
        auto it = slots.begin();
        std::advance(it, index);
        it->bypassed = bypass;
    }
}

void EffectsChain::setMix(int index, float wetDry) {
    if (index >= 0 && index < static_cast<int>(slots.size())) {
        auto it = slots.begin();
        std::advance(it, index);
        it->mix = std::max(0.0f, std::min(1.0f, wetDry));
    }
}

void EffectsChain::setWetLowCut(int index, float hz) {
    if (index >= 0 && index < static_cast<int>(slots.size())) {
        auto it = slots.begin();
        std::advance(it, index);
        it->wetLowCut = hz;
        if (currentSpec.sampleRate > 0) {
            auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(currentSpec.sampleRate, hz, 0.707);
            *it->wetHPF.state = *coeffs;
            *it->wetHPF2.state = *coeffs;
        }
    }
}

void EffectsChain::setWetHighCut(int index, float hz) {
    if (index >= 0 && index < static_cast<int>(slots.size())) {
        auto it = slots.begin();
        std::advance(it, index);
        it->wetHighCut = hz;
        if (currentSpec.sampleRate > 0) {
            auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(currentSpec.sampleRate, hz, 0.707);
            *it->wetLPF.state = *coeffs;
            *it->wetLPF2.state = *coeffs;
        }
    }
}

int EffectsChain::getNumEffects() const { return static_cast<int>(slots.size()); }
EffectSlot& EffectsChain::getEffect(int index) {
    auto it = slots.begin();
    std::advance(it, index);
    return *it;
}
void EffectsChain::clear() { slots.clear(); }

} // namespace ana
