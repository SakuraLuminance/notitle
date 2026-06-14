#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace ana {

enum class EQBandType { LowShelf, Peaking, HighShelf };

struct EQBand {
    float frequency = 1000.0f;
    float gain = 0.0f;
    float q = 0.707f;
    EQBandType type = EQBandType::Peaking;
};

class EQEffect {
public:
    EQEffect();
    ~EQEffect() = default;
    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::AudioBuffer<float>& buffer);
    void reset();
    void setLowBand(float freq, float gain, float q);
    void setMidBand(float freq, float gain, float q);
    void setHighBand(float freq, float gain, float q);
    void setLowType(EQBandType t);
    void setMidType(EQBandType t);
    void setHighType(EQBandType t);
private:
    void updateCoeffs(int band);
    std::array<juce::dsp::IIR::Filter<float>, 3> filters;
    std::array<EQBand, 3> bands;
    double sampleRate = 44100.0;
};

} // namespace ana
