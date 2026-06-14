#pragma once
#include <vector>
#include <array>
#include <juce_dsp/juce_dsp.h>

namespace ana {

struct FormantData
{
    float frequency = 800.0f;  // Hz
    float bandwidth = 80.0f;   // Hz
    float amplitude = 1.0f;    // 0.0 - 1.0
};

class FormantFilterBank
{
public:
    FormantFilterBank();
    ~FormantFilterBank();

    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset();

    void setFormant(int index, const FormantData& data);
    FormantData getFormant(int index) const;

    // Vowel presets
    void setVowel(char vowel); // 'A', 'E', 'I', 'O', 'U'
    void morphVowels(char from, char to, float amount);

    void process(juce::dsp::AudioBlock<float>& block);

    static constexpr int kNumFormants = 5;

private:
    void updateCoefficients();

    std::array<juce::dsp::IIR::Filter<float>, kNumFormants> filters;
    std::array<FormantData, kNumFormants> formants;
    double sampleRate = 44100.0;
    bool prepared = false;
};

} // namespace ana
