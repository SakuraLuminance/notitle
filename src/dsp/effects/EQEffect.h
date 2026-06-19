#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_data_structures/juce_data_structures.h>
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

    juce::ValueTree getState() const
    {
        juce::ValueTree tree("EQEffect");
        for (int i = 0; i < 3; ++i)
        {
            juce::ValueTree band("Band");
            band.setProperty("index", i, nullptr);
            band.setProperty("frequency", bands[i].frequency, nullptr);
            band.setProperty("gain", bands[i].gain, nullptr);
            band.setProperty("q", bands[i].q, nullptr);
            band.setProperty("type", static_cast<int>(bands[i].type), nullptr);
            tree.addChild(band, -1, nullptr);
        }
        return tree;
    }

    void setState(const juce::ValueTree& state)
    {
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            auto band = state.getChild(i);
            int idx = band.getProperty("index", 0);
            if (idx >= 0 && idx < 3)
            {
                bands[idx].frequency = juce::jlimit(20.0f, 20000.0f, band.getProperty("frequency", 1000.0f));
                bands[idx].gain      = juce::jlimit(-24.0f, 24.0f, band.getProperty("gain", 0.0f));
                bands[idx].q         = juce::jlimit(0.1f, 10.0f, band.getProperty("q", 0.707f));
                bands[idx].type      = static_cast<EQBandType>(juce::jlimit(0, 2, static_cast<int>(band.getProperty("type", static_cast<int>(EQBandType::Peaking)))));
                updateCoeffs(idx);
            }
        }
    }

private:
    void updateCoeffs(int band);
    std::array<juce::dsp::IIR::Filter<float>, 3> filters;
    std::array<EQBand, 3> bands;
    double sampleRate = 44100.0;
    bool bypassed = false;
};

} // namespace ana
