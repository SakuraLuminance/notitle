#include "FormantFilterBank.h"
#include <cmath>

namespace ana {

// Vowel formant data: F1, F2, F3, F4, F5 frequencies and bandwidths
struct VowelData
{
    float freq[5];
    float bw[5];
};

static const VowelData vowelPresets[5] = {
    // A (as in "father")
    { { 800.0f, 1200.0f, 2500.0f, 3500.0f, 4500.0f },
      { 80.0f,  90.0f,  120.0f,  150.0f,  200.0f } },
    // E (as in "bed")
    { { 400.0f, 2000.0f, 2800.0f, 3600.0f, 4200.0f },
      { 60.0f,  100.0f,  120.0f,  150.0f,  180.0f } },
    // I (as in "beet")
    { { 300.0f, 2500.0f, 3200.0f, 3800.0f, 4500.0f },
      { 50.0f,  80.0f,   100.0f,  130.0f,  150.0f } },
    // O (as in "boat")
    { { 400.0f, 800.0f,  2500.0f, 3000.0f, 4000.0f },
      { 70.0f,  80.0f,   100.0f,  130.0f,  180.0f } },
    // U (as in "boot")
    { { 350.0f, 600.0f,  2500.0f, 3200.0f, 3800.0f },
      { 60.0f,  70.0f,   100.0f,  120.0f,  150.0f } }
};

FormantFilterBank::FormantFilterBank() {}
FormantFilterBank::~FormantFilterBank() {}

void FormantFilterBank::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    for (auto& f : filters)
        f.prepare(spec);
    prepared = true;
    updateCoefficients();
}

void FormantFilterBank::reset()
{
    for (auto& f : filters)
        f.reset();
}

void FormantFilterBank::setFormant(int index, const FormantData& data)
{
    if (index >= 0 && index < kNumFormants)
    {
        formants[index] = data;
        if (prepared)
            updateCoefficients();
    }
}

FormantData FormantFilterBank::getFormant(int index) const
{
    if (index >= 0 && index < kNumFormants)
        return formants[index];
    return {};
}

void FormantFilterBank::setVowel(char vowel)
{
    int idx = -1;
    switch (vowel)
    {
        case 'A': case 'a': idx = 0; break;
        case 'E': case 'e': idx = 1; break;
        case 'I': case 'i': idx = 2; break;
        case 'O': case 'o': idx = 3; break;
        case 'U': case 'u': idx = 4; break;
    }

    if (idx >= 0)
    {
        for (int i = 0; i < kNumFormants; ++i)
        {
            formants[i].frequency = vowelPresets[idx].freq[i];
            formants[i].bandwidth = vowelPresets[idx].bw[i];
            formants[i].amplitude = 1.0f;
        }
        if (prepared)
            updateCoefficients();
    }
}

void FormantFilterBank::morphVowels(char from, char to, float amount)
{
    int fromIdx = -1, toIdx = -1;
    switch (from) { case 'A': fromIdx=0; break; case 'E': fromIdx=1; break;
                    case 'I': fromIdx=2; break; case 'O': fromIdx=3; break;
                    case 'U': fromIdx=4; break; }
    switch (to)   { case 'A': toIdx=0; break; case 'E': toIdx=1; break;
                    case 'I': toIdx=2; break; case 'O': toIdx=3; break;
                    case 'U': toIdx=4; break; }

    if (fromIdx >= 0 && toIdx >= 0)
    {
        float t = std::max(0.0f, std::min(1.0f, amount));
        for (int i = 0; i < kNumFormants; ++i)
        {
            formants[i].frequency = vowelPresets[fromIdx].freq[i] * (1.0f - t)
                                   + vowelPresets[toIdx].freq[i] * t;
            formants[i].bandwidth = vowelPresets[fromIdx].bw[i] * (1.0f - t)
                                   + vowelPresets[toIdx].bw[i] * t;
            formants[i].amplitude = 1.0f;
        }
        if (prepared)
            updateCoefficients();
    }
}

void FormantFilterBank::updateCoefficients()
{
    for (int i = 0; i < kNumFormants; ++i)
    {
        float freq = formants[i].frequency;
        float bw = formants[i].bandwidth;
        float q = freq / (bw > 0.0f ? bw : 1.0f);

        *filters[i].coefficients =
            *juce::dsp::IIR::Coefficients<float>::makeBandPass(sampleRate, freq, q);
    }
}

void FormantFilterBank::process(juce::dsp::AudioBlock<float>& block)
{
    // Create temp buffer for summing formants
    juce::AudioBuffer<float> temp(static_cast<int>(block.getNumChannels()),
                                  static_cast<int>(block.getNumSamples()));
    temp.clear();

    for (int i = 0; i < kNumFormants; ++i)
    {
        juce::AudioBuffer<float> formantBuf(static_cast<int>(block.getNumChannels()),
                                            static_cast<int>(block.getNumSamples()));
        for (int ch = 0; ch < formantBuf.getNumChannels(); ++ch)
            formantBuf.copyFrom(ch, 0, block.getChannelPointer(ch),
                                static_cast<int>(block.getNumSamples()));

        auto formantBlock = juce::dsp::AudioBlock<float>(formantBuf);
        filters[i].process(juce::dsp::ProcessContextReplacing<float>(formantBlock));

        // Scale by formant amplitude and add to temp
        for (int ch = 0; ch < temp.getNumChannels(); ++ch)
            temp.addFrom(ch, 0, formantBuf, ch, 0, temp.getNumSamples(),
                         formants[i].amplitude);
    }

    // Write back
    for (int ch = 0; ch < static_cast<int>(block.getNumChannels()); ++ch)
    {
        for (int s = 0; s < static_cast<int>(block.getNumSamples()); ++s)
            block.setSample(ch, s, temp.getSample(ch, s));
    }
}

} // namespace ana
