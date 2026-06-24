#include "SaturationEffect.h"
#include <cmath>

namespace ana {

SaturationEffect::SaturationEffect() {}

void SaturationEffect::prepare(const juce::dsp::ProcessSpec& spec) {
    sampleRate = spec.sampleRate;
    numChannels = static_cast<int>(spec.numChannels);

    // Oversampling 4x — use half-band FIR for best anti-aliasing
    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        spec.numChannels, 4,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);

    oversampling->initProcessing(static_cast<double>(spec.maximumBlockSize));

    // Prepare WaveShaper with original spec — we manually process upsampled
    waveShaper.prepare(spec);

    // Set up waveshaper function — captures this to read live params (atomic-safe)
    waveShaper.setFunction([this](float x) -> float {
        const float g = preGainAtomic_.load(std::memory_order_relaxed);
        // Clamp input to prevent NaN from extreme values
        x = juce::jlimit(-100.0f, 100.0f, x);
        const auto m = static_cast<SaturationMode>(modeAtomic_.load(std::memory_order_relaxed));
        switch (m) {
            case SaturationMode::Soft:
                return std::tanh(x * g);
            case SaturationMode::Tube:
                if (x >= 0.0f)
                    return std::tanh(x * g * 1.2f);
                else
                    return std::tanh(x * g * 0.8f) * 1.1f;
            case SaturationMode::Tape:
                return std::atan(x * g) / juce::MathConstants<float>::halfPi;
        }
        return x;
    };

    // Tone filter (1-pole LP per channel)
    toneFilters.resize(numChannels);
    toneFilterDirty = true;
    updateToneFilter();

    // Prepare dry buffer
    dryBuffer_.setSize(numChannels, static_cast<int>(spec.maximumBlockSize), false, false, true);

    // Use integer latency so we can report an exact sample count to the DAW
    oversampling->setUsingIntegerLatency(true);
    latencySamples = static_cast<int>(oversampling->getLatencyInSamples());

    oversampling->reset();
    waveShaper.reset();
}

void SaturationEffect::process(juce::AudioBuffer<float>& buffer) {
    if (bypassed) return;

    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    if (numSamples == 0) return;

    // Save dry signal for wet/dry mix
    if (mixVal < 1.0f)
        dryBuffer_.makeCopyOf(buffer, true);

    // --- Oversampling + waveshaper pipeline ---
    {
        juce::dsp::AudioBlock<float> block(buffer);
        // Upsample to 4x — returns a mutable block at the oversampled rate
        auto upBlock = oversampling->processSamplesUp(block);
        // Apply waveshaper at oversampled rate (anti-aliasing)
        waveShaper.process(juce::dsp::ProcessContextReplacing<float>(upBlock));
        // Downsample back to original rate
        oversampling->processSamplesDown(block);
    }

    // --- Tone filter (1-pole LP at original rate) ---
    if (toneFilterDirty) {
        updateToneFilter();
        toneFilterDirty = false;
    }

    for (int ch = 0; ch < numCh; ++ch) {
        auto* const data = buffer.getWritePointer(ch);
        auto& f = toneFilters[static_cast<size_t>(ch)];
        const float a = f.a;
        const float b = 1.0f - a;
        float y1 = f.y1;

        for (int s = 0; s < numSamples; ++s) {
            const float in = data[s];
            y1 = a * in + b * y1;
            data[s] = y1;
        }

        f.y1 = y1;
    }

    // --- Dry/Wet mix ---
    if (mixVal < 1.0f) {
        for (int ch = 0; ch < numCh; ++ch) {
            const auto* const dry = dryBuffer_.getReadPointer(ch);
            auto* const wet = buffer.getWritePointer(ch);
            for (int s = 0; s < numSamples; ++s) {
                wet[s] = dry[s] * (1.0f - mixVal) + wet[s] * mixVal;
            }
        }
    }

    // Apply output gain
    if (gainVal != 1.0f) {
        buffer.applyGain(gainVal);
    }
}

void SaturationEffect::reset() {
    if (oversampling) oversampling->reset();
    waveShaper.reset();
    for (auto& f : toneFilters) {
        f.y1 = 0.0f;
    }
}

void SaturationEffect::setDrive(float percent) {
    drive = juce::jlimit(0.0f, 100.0f, percent);
    preGainAtomic_.store(driveToPreGain(drive), std::memory_order_relaxed);
}

void SaturationEffect::setTone(float freqHz) {
    toneFreq = juce::jlimit(20.0f, 20000.0f, freqHz);
    toneFilterDirty = true;
}

void SaturationEffect::setMode(SaturationMode m) {
    modeAtomic_.store(static_cast<int>(m), std::memory_order_relaxed);
}

void SaturationEffect::setMix(float percent) {
    mixVal = juce::jlimit(0.0f, 1.0f, percent / 100.0f);
}

void SaturationEffect::setBypass(bool b) {
    bypassed = b;
}

void SaturationEffect::setGain(float g) {
    gainVal = juce::jlimit(0.0f, 2.0f, g);
}

float SaturationEffect::driveToPreGain(float percent) {
    // Map 0..100% to 0..10 pre-gain
    return percent / 10.0f;
}

void SaturationEffect::updateToneFilter() {
    const double fc = static_cast<double>(toneFreq);
    const double fs = sampleRate;
    // 1-pole LP coefficient: a = exp(-2*pi*fc/fs)
    const double a = std::exp(-2.0 * juce::MathConstants<double>::pi * fc / fs);
    for (auto& f : toneFilters) {
        f.a = static_cast<float>(a);
    }
}

juce::ValueTree SaturationEffect::getState() const {
    juce::ValueTree tree("SaturationEffect");
    tree.setProperty("mode", modeAtomic_.load(std::memory_order_relaxed), nullptr);
    tree.setProperty("drive", static_cast<double>(drive), nullptr);
    tree.setProperty("tone", static_cast<double>(toneFreq), nullptr);
    tree.setProperty("mix", static_cast<double>(mixVal * 100.0), nullptr);
    tree.setProperty("bypass", bypassed, nullptr);
    tree.setProperty("gain", static_cast<double>(gainVal), nullptr);
    return tree;
}

void SaturationEffect::setState(const juce::ValueTree& tree) {
    setMode(static_cast<SaturationMode>(juce::jlimit(0, 2, static_cast<int>(tree.getProperty("mode", static_cast<int>(SaturationMode::Soft))))));
    setDrive(static_cast<float>(tree.getProperty("drive", 50.0)));
    setTone(static_cast<float>(tree.getProperty("tone", 20000.0)));
    setMix(static_cast<float>(tree.getProperty("mix", 100.0)));
    setBypass(tree.getProperty("bypass", false));
    setGain(static_cast<float>(tree.getProperty("gain", 1.0)));
}

} // namespace ana
