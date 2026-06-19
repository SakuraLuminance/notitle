#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

enum class DynamicsMode { Compressor, Limiter, Gate };

class DynamicsModule : public EffectBase {
public:
    DynamicsModule();
    ~DynamicsModule() override = default;

    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    void setMode(DynamicsMode m);

    // Compressor
    void setCompressorRatio(float r);
    void setCompressorThreshold(float db);
    void setCompressorAttack(float ms);
    void setCompressorRelease(float ms);

    // Limiter
    void setLimiterThreshold(float db);
    void setLimiterRelease(float ms);
    void setLimiterCeiling(float db);

    // Gate
    void setGateThreshold(float db);
    void setGateHold(float ms);
    void setGateRelease(float ms);

    // Shared
    void setMix(float wet);
    void setWetHPF(float hz);
    void setWetLPF(float hz);
    void setBypass(bool b);

    juce::ValueTree getState() const override;
    void setState(const juce::ValueTree& tree) override;

private:
    void processCompressor(juce::AudioBuffer<float>& buffer);
    void processLimiter(juce::AudioBuffer<float>& buffer);
    void processGate(juce::AudioBuffer<float>& buffer);
    void applyWetFilters(juce::AudioBuffer<float>& buffer);
    void applyDryWetMix(juce::AudioBuffer<float>& buffer);

    DynamicsMode mode = DynamicsMode::Compressor;

    // --- Compressor ---
    float compRatio = 4.0f;
    float compThreshold = -24.0f;
    float compAttack = 10.0f;
    float compRelease = 100.0f;
    float compEnvelope = 0.0f;
    float compAttackCoeff = 0.0f;
    float compReleaseCoeff = 0.0f;

    // --- Limiter ---
    float limThreshold = -6.0f;
    float limRelease = 20.0f;
    float limCeiling = -0.5f;
    float limEnvelope = 1.0f;
    float limReleaseCoeff = 0.0f;

    // --- Gate ---
    float gateThreshold = -60.0f;
    float gateHold = 20.0f;
    float gateRelease = 50.0f;
    float gateEnvelope = 1.0f;
    float gateHoldSamples = 0.0f;
    float gateHoldRemaining = 0.0f;
    float gateReleaseCoeff = 0.0f;

    // --- Shared ---
    float mixVal = 1.0f;
    bool bypassed = false;
    double sampleRate = 44100.0;

    // Wet filters
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> wetHPF;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> wetLPF;

    // Pre-allocated dry buffer
    juce::AudioBuffer<float> dryBuffer_;

    // Temporary gain reduction buffer for compressor
    std::vector<float> gainReduction_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamicsModule)
};

} // namespace ana
