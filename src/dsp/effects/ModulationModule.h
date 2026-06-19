#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

enum class ModulationMode { Chorus, Flanger, Phaser };

class ModulationModule : public EffectBase {
public:
    ModulationModule();
    ~ModulationModule() override = default;

    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    void setMode(ModulationMode m);

    // Chorus
    void setChorusRate(float hz);
    void setChorusDepth(float v);
    void setChorusCentreDelay(float ms);

    // Flanger
    void setFlangerRate(float hz);
    void setFlangerDepth(float v);
    void setFlangerFeedback(float v);
    void setFlangerDelay(float ms);

    // Phaser
    void setPhaserRate(float hz);
    void setPhaserDepth(float v);
    void setPhaserFeedback(float v);
    void setPhaserStages(int stages);

    // Shared
    void setMix(float wet);
    void setWetHPF(float hz);
    void setWetLPF(float hz);
    void setBypass(bool b);

    juce::ValueTree getState() const override;
    void setState(const juce::ValueTree& tree) override;

private:
    void processChorus(juce::AudioBuffer<float>& buffer);
    void processFlanger(juce::AudioBuffer<float>& buffer);
    void processPhaser(juce::AudioBuffer<float>& buffer);
    void applyWetFilters(juce::AudioBuffer<float>& buffer);
    void applyDryWetMix(juce::AudioBuffer<float>& buffer);

    ModulationMode mode = ModulationMode::Chorus;

    // --- Chorus ---
    juce::dsp::Chorus<float> chorus;
    float chorusRate = 1.0f;
    float chorusDepth = 0.5f;
    float chorusCentreDelay = 10.0f;

    // --- Flanger ---
    float flangerRate = 0.5f;
    float flangerDepth = 0.5f;
    float flangerFeedback = 0.3f;
    float flangerDelay = 3.0f;
    std::vector<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>> flangerDelayLines;
    std::vector<float> flangerPhase;

    // --- Phaser ---
    static constexpr int phaserMaxStages = 12;
    float phaserRate = 1.0f;
    float phaserDepth = 0.5f;
    float phaserFeedback = 0.3f;
    int phaserStages = 6;
    int phaserUpdateCounter = 0;
    // TDF2 state: sv[ch * phaserMaxStages + stage]
    std::vector<float> phaserSV1;
    std::vector<float> phaserSV2;
    std::vector<float> phaserCoeffB0;
    std::vector<float> phaserCoeffB1;
    std::vector<float> phaserCoeffA1;
    std::vector<float> phaserCoeffA2;
    std::vector<float> phaserLfoPhase;
    std::vector<float> phaserFbOut;

    // --- Shared ---
    float mixVal = 1.0f;
    bool bypassed = false;
    double sampleRate = 44100.0;
    int numChannels = 2;

    // Wet filters
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> wetHPF;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> wetLPF;

    // Pre-allocated buffers
    juce::AudioBuffer<float> dryBuffer_;
    juce::AudioBuffer<float> wetBuffer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationModule)
};

} // namespace ana
