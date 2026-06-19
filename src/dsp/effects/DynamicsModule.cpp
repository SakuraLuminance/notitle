#include "DynamicsModule.h"
#include <cmath>

namespace ana {

DynamicsModule::DynamicsModule() {}

void DynamicsModule::prepare(const juce::dsp::ProcessSpec& spec) {
    sampleRate = spec.sampleRate;

    // Pre-calculate coefficients
    compAttackCoeff = std::exp(-1.0f / (compAttack * static_cast<float>(sampleRate) / 1000.0f));
    compReleaseCoeff = std::exp(-1.0f / (compRelease * static_cast<float>(sampleRate) / 1000.0f));
    limReleaseCoeff = std::exp(-1.0f / (limRelease * static_cast<float>(sampleRate) / 1000.0f));
    gateReleaseCoeff = std::exp(-1.0f / (gateRelease * static_cast<float>(sampleRate) / 1000.0f));
    gateHoldSamples = gateHold * static_cast<float>(sampleRate) / 1000.0f;

    // Reset envelope followers
    compEnvelope = 0.0f;
    limEnvelope = 1.0f;
    gateEnvelope = 1.0f;
    gateHoldRemaining = 0.0f;

    // Prepare wet filters (start with passthrough range)
    wetHPF.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 20.0f);
    wetLPF.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 20000.0f);
    wetHPF.prepare(spec);
    wetLPF.prepare(spec);

    // Pre-allocate dry buffer and gain reduction vector
    dryBuffer_.setSize(static_cast<int>(spec.numChannels), static_cast<int>(spec.maximumBlockSize), false, false, true);
    gainReduction_.resize(spec.maximumBlockSize);
}

void DynamicsModule::reset() {
    compEnvelope = 0.0f;
    limEnvelope = 1.0f;
    gateEnvelope = 1.0f;
    gateHoldRemaining = 0.0f;
    wetHPF.reset();
    wetLPF.reset();
}

void DynamicsModule::process(juce::AudioBuffer<float>& buffer) {
    if (bypassed) return;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;

    // Save dry signal
    dryBuffer_.makeCopyOf(buffer, true);

    // Process based on active mode
    switch (mode) {
        case DynamicsMode::Compressor: processCompressor(buffer); break;
        case DynamicsMode::Limiter:    processLimiter(buffer);    break;
        case DynamicsMode::Gate:       processGate(buffer);       break;
    }

    // Apply wet filters
    applyWetFilters(buffer);

    // Dry/wet mix
    applyDryWetMix(buffer);
}

void DynamicsModule::processCompressor(juce::AudioBuffer<float>& buffer) {
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    // Update coefficients (parameters may have changed)
    compAttackCoeff = std::exp(-1.0f / (compAttack * static_cast<float>(sampleRate) / 1000.0f));
    compReleaseCoeff = std::exp(-1.0f / (compRelease * static_cast<float>(sampleRate) / 1000.0f));

    // First pass: compute gain reduction per sample
    gainReduction_.assign(numSamples, 1.0f);

    for (int s = 0; s < numSamples; ++s) {
        // Stereo-linked: find max absolute level across all channels
        float absMax = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) {
            const float sample = std::abs(buffer.getSample(ch, s));
            if (sample > absMax) absMax = sample;
        }

        const float levelDb = 20.0f * std::log10(std::max(absMax, 1e-10f));
        float gainDb = 0.0f;

        if (levelDb > compThreshold) {
            const float compressed = compThreshold + (levelDb - compThreshold) / compRatio;
            gainDb = compressed - levelDb;
        }

        const float targetGain = juce::Decibels::decibelsToGain(gainDb);

        // Smooth envelope with attack/release
        if (targetGain < compEnvelope)
            compEnvelope = compEnvelope + (1.0f - compAttackCoeff) * (targetGain - compEnvelope);
        else
            compEnvelope = compEnvelope + (1.0f - compReleaseCoeff) * (targetGain - compEnvelope);

        gainReduction_[s] = compEnvelope;
    }

    // Second pass: apply gain reduction to all channels
    for (int ch = 0; ch < numCh; ++ch) {
        auto* data = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
            data[s] *= gainReduction_[s];
    }
}

void DynamicsModule::processLimiter(juce::AudioBuffer<float>& buffer) {
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    const float thresholdLinear = juce::Decibels::decibelsToGain(limThreshold);
    const float ceilingLinear   = juce::Decibels::decibelsToGain(limCeiling);

    limReleaseCoeff = std::exp(-1.0f / (limRelease * static_cast<float>(sampleRate) / 1000.0f));

    for (int s = 0; s < numSamples; ++s) {
        // Stereo-linked max level
        float absMax = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) {
            const float sample = std::abs(buffer.getSample(ch, s));
            if (sample > absMax) absMax = sample;
        }

        // Compute gain reduction (brickwall style)
        float targetGain = 1.0f;
        if (absMax > thresholdLinear)
            targetGain = thresholdLinear / (absMax + 1e-10f);

        // Instant attack, smooth release
        if (targetGain < limEnvelope)
            limEnvelope = targetGain;
        else
            limEnvelope = limEnvelope + (1.0f - limReleaseCoeff) * (targetGain - limEnvelope);

        // Apply gain reduction + hard ceiling
        for (int ch = 0; ch < numCh; ++ch) {
            float* data = buffer.getWritePointer(ch);
            data[s] *= limEnvelope;

            // Brickwall ceiling
            if (data[s] > ceilingLinear)
                data[s] = ceilingLinear;
            else if (data[s] < -ceilingLinear)
                data[s] = -ceilingLinear;
        }
    }
}

void DynamicsModule::processGate(juce::AudioBuffer<float>& buffer) {
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    const float thresholdLinear = juce::Decibels::decibelsToGain(gateThreshold);

    gateReleaseCoeff = std::exp(-1.0f / (gateRelease * static_cast<float>(sampleRate) / 1000.0f));

    for (int s = 0; s < numSamples; ++s) {
        // Stereo-linked max level
        float absMax = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) {
            const float sample = std::abs(buffer.getSample(ch, s));
            if (sample > absMax) absMax = sample;
        }

        if (absMax >= thresholdLinear) {
            // Signal above threshold: open gate
            gateEnvelope = 1.0f;
            gateHoldRemaining = gateHoldSamples;
        } else if (gateHoldRemaining > 0.0f) {
            // In hold period
            gateHoldRemaining -= 1.0f;
        } else {
            // Below threshold, no hold: close gate
            gateEnvelope += (1.0f - gateReleaseCoeff) * (0.0f - gateEnvelope);
        }

        // Apply gate envelope
        for (int ch = 0; ch < numCh; ++ch) {
            float* data = buffer.getWritePointer(ch);
            data[s] *= gateEnvelope;
        }
    }
}

void DynamicsModule::applyWetFilters(juce::AudioBuffer<float>& buffer) {
    juce::dsp::AudioBlock<float> block(buffer);
    const auto ctx = juce::dsp::ProcessContextReplacing<float>(block);
    wetHPF.process(ctx);
    wetLPF.process(ctx);
}

void DynamicsModule::applyDryWetMix(juce::AudioBuffer<float>& buffer) {
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    for (int ch = 0; ch < numCh; ++ch) {
        const auto* dryData = dryBuffer_.getReadPointer(ch);
        auto* outData = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
            outData[s] = dryData[s] * (1.0f - mixVal) + outData[s] * mixVal;
    }
}

// ============================================================================
// Setters — all clamped via jlimit
// ============================================================================

void DynamicsModule::setMode(DynamicsMode m) {
    if (mode != m) {
        mode = m;
        reset();
    }
}

void DynamicsModule::setCompressorRatio(float r)       { compRatio     = juce::jlimit(1.0f, 20.0f, r); }
void DynamicsModule::setCompressorThreshold(float db)   { compThreshold = juce::jlimit(-60.0f, 0.0f, db); }
void DynamicsModule::setCompressorAttack(float ms)      { compAttack    = juce::jlimit(0.1f, 100.0f, ms); }
void DynamicsModule::setCompressorRelease(float ms)     { compRelease   = juce::jlimit(10.0f, 1000.0f, ms); }

void DynamicsModule::setLimiterThreshold(float db)      { limThreshold  = juce::jlimit(-30.0f, 0.0f, db); }
void DynamicsModule::setLimiterRelease(float ms)        { limRelease    = juce::jlimit(1.0f, 100.0f, ms); }
void DynamicsModule::setLimiterCeiling(float db)        { limCeiling    = juce::jlimit(-12.0f, 0.0f, db); }

void DynamicsModule::setGateThreshold(float db)         { gateThreshold = juce::jlimit(-80.0f, 0.0f, db); }
void DynamicsModule::setGateHold(float ms)              { gateHold      = juce::jlimit(0.0f, 500.0f, ms);
                                                          gateHoldSamples = gateHold * static_cast<float>(sampleRate) / 1000.0f; }
void DynamicsModule::setGateRelease(float ms)           { gateRelease   = juce::jlimit(1.0f, 500.0f, ms); }

void DynamicsModule::setMix(float wet)                  { mixVal  = juce::jlimit(0.0f, 1.0f, wet); }
void DynamicsModule::setBypass(bool b)                  { bypassed = b; }

void DynamicsModule::setWetHPF(float hz) {
    hz = juce::jlimit(20.0f, 20000.0f, hz);
    *wetHPF.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, hz);
}

void DynamicsModule::setWetLPF(float hz) {
    hz = juce::jlimit(20.0f, 20000.0f, hz);
    *wetLPF.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, hz);
}

// ============================================================================
// State serialization
// ============================================================================

juce::ValueTree DynamicsModule::getState() const {
    juce::ValueTree tree("DynamicsModule");
    tree.setProperty("mode",                static_cast<int>(mode), nullptr);
    tree.setProperty("compRatio",           compRatio,     nullptr);
    tree.setProperty("compThreshold",       compThreshold, nullptr);
    tree.setProperty("compAttack",          compAttack,    nullptr);
    tree.setProperty("compRelease",         compRelease,   nullptr);
    tree.setProperty("limThreshold",        limThreshold,  nullptr);
    tree.setProperty("limRelease",          limRelease,    nullptr);
    tree.setProperty("limCeiling",          limCeiling,    nullptr);
    tree.setProperty("gateThreshold",       gateThreshold, nullptr);
    tree.setProperty("gateHold",            gateHold,      nullptr);
    tree.setProperty("gateRelease",         gateRelease,   nullptr);
    tree.setProperty("mix",                 mixVal,        nullptr);
    tree.setProperty("bypass",              bypassed,      nullptr);
    return tree;
}

void DynamicsModule::setState(const juce::ValueTree& tree) {
    // All setters delegate to jlimit-guarded setters
    setMode(static_cast<DynamicsMode>(juce::jlimit(0, 2, static_cast<int>(tree.getProperty("mode", 0)))));
    setCompressorRatio(tree.getProperty("compRatio", 4.0f));
    setCompressorThreshold(tree.getProperty("compThreshold", -24.0f));
    setCompressorAttack(tree.getProperty("compAttack", 10.0f));
    setCompressorRelease(tree.getProperty("compRelease", 100.0f));
    setLimiterThreshold(tree.getProperty("limThreshold", -6.0f));
    setLimiterRelease(tree.getProperty("limRelease", 20.0f));
    setLimiterCeiling(tree.getProperty("limCeiling", -0.5f));
    setGateThreshold(tree.getProperty("gateThreshold", -60.0f));
    setGateHold(tree.getProperty("gateHold", 20.0f));
    setGateRelease(tree.getProperty("gateRelease", 50.0f));
    setMix(tree.getProperty("mix", 1.0f));
    setBypass(tree.getProperty("bypass", false));
}

} // namespace ana
