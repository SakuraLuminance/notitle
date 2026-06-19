#include "RingModulatorEffect.h"
#include <cmath>

namespace ana {

RingModulatorEffect::RingModulatorEffect() {}

void RingModulatorEffect::prepare(const juce::dsp::ProcessSpec& spec) {
    sampleRate = spec.sampleRate;
    numChannels = static_cast<int>(spec.numChannels);

    // Per-channel state
    phase_.assign(numChannels, 0.0f);
    phasorCos_.assign(numChannels, 1.0f);
    phasorSin_.assign(numChannels, 0.0f);

    // Pre-allocate dry buffer
    dryBuffer_.setSize(numChannels,
                       static_cast<int>(spec.maximumBlockSize),
                       false, false, true);

    // Precompute rotation coefficients for recursive phasor
    const float delta = 2.0f * juce::MathConstants<float>::pi * frequency
                        / static_cast<float>(sampleRate);
    cosDelta_ = std::cos(delta);
    sinDelta_ = std::sin(delta);
}

void RingModulatorEffect::reset() {
    for (int ch = 0; ch < numChannels; ++ch) {
        phase_[ch] = 0.0f;
        phasorCos_[ch] = 1.0f;
        phasorSin_[ch] = 0.0f;
    }
}

void RingModulatorEffect::process(juce::AudioBuffer<float>& buffer) {
    if (bypassed)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin(numChannels, buffer.getNumChannels());

    // Save dry signal for wet/dry mix
    dryBuffer_.makeCopyOf(buffer, true);

    for (int ch = 0; ch < numCh; ++ch) {
        auto* data = buffer.getWritePointer(ch);
        float& phase = phase_[ch];
        float& cosVal = phasorCos_[ch];
        float& sinVal = phasorSin_[ch];

        const float phaseInc = frequency / static_cast<float>(sampleRate);

        for (int s = 0; s < numSamples; ++s) {
            // --- Recursive phasor rotation for sine carrier ---
            // Rotate (cos, sin) by delta using complex multiplication.
            // This avoids std::sin per sample.
            const float newCos = cosVal * cosDelta_ - sinVal * sinDelta_;
            const float newSin = sinVal * cosDelta_ + cosVal * sinDelta_;
            cosVal = newCos;
            sinVal = newSin;

            // --- Phase accumulator for triangle / square ---
            phase += phaseInc;
            if (phase >= 1.0f)
                phase -= 1.0f;

            // --- Generate carrier (pure AC, zero mean for all waveforms) ---
            float carrier;
            switch (waveform) {
                case 0:  // Sine
                    carrier = sinVal;
                    break;
                case 1: // Triangle — symmetric [-1, 1], zero mean
                    carrier = 2.0f * std::abs(2.0f * phase - 1.0f) - 1.0f;
                    break;
                case 2: // Square — symmetric [-1, 1], zero mean
                    carrier = (phase < 0.5f) ? 1.0f : -1.0f;
                    break;
                default:
                    carrier = sinVal;
                    break;
            }

            // Ring modulation: input * carrier
            data[s] = data[s] * carrier;
        }
    }

    // Dry/wet mix with gain
    for (int ch = 0; ch < numCh; ++ch) {
        const auto* dryData = dryBuffer_.getReadPointer(ch);
        auto* outData = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s) {
            outData[s] = juce::jlimit(-1.0f, 1.0f, dryData[s] * (1.0f - mixVal)
                       + outData[s] * mixVal * gainVal);
        }
    }
}

void RingModulatorEffect::setFrequency(float hz) {
    frequency = juce::jlimit(0.1f, 5000.0f, hz);
    // Precompute rotation coefficients for recursive phasor
    const float delta = 2.0f * juce::MathConstants<float>::pi * frequency
                        / static_cast<float>(sampleRate);
    cosDelta_ = std::cos(delta);
    sinDelta_ = std::sin(delta);
}

void RingModulatorEffect::setWaveform(int wf) {
    waveform = juce::jlimit(0, 2, wf);
}

void RingModulatorEffect::setMix(float wet) {
    mixVal = juce::jlimit(0.0f, 1.0f, wet);
}

void RingModulatorEffect::setBypass(bool b) {
    bypassed = b;
}

void RingModulatorEffect::setGain(float g) {
    gainVal = juce::jlimit(0.0f, 4.0f, g);
}

} // namespace ana
