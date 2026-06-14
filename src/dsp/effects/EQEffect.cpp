#include "EQEffect.h"
#include <cmath>

namespace ana {

EQEffect::EQEffect() {
    bands[0].frequency = 200.0f;  bands[0].gain = 0.0f; bands[0].q = 0.707f; bands[0].type = EQBandType::LowShelf;
    bands[1].frequency = 1000.0f; bands[1].gain = 0.0f; bands[1].q = 0.707f; bands[1].type = EQBandType::Peaking;
    bands[2].frequency = 5000.0f; bands[2].gain = 0.0f; bands[2].q = 0.707f; bands[2].type = EQBandType::HighShelf;
}

void EQEffect::prepare(const juce::dsp::ProcessSpec& spec) {
    sampleRate = spec.sampleRate;
    for (auto& f : filters) f.prepare(spec);
    for (int i = 0; i < 3; ++i) updateCoeffs(i);
}

void EQEffect::reset() { for (auto& f : filters) f.reset(); }

void EQEffect::updateCoeffs(int band) {
    const auto& b = bands[band];
    double sr = sampleRate;
    switch (b.type) {
        case EQBandType::LowShelf:
            *filters[band].coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr, b.frequency, b.q, juce::Decibels::decibelsToGain(b.gain));
            break;
        case EQBandType::Peaking:
            *filters[band].coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, b.frequency, b.q, juce::Decibels::decibelsToGain(b.gain));
            break;
        case EQBandType::HighShelf:
            *filters[band].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr, b.frequency, b.q, juce::Decibels::decibelsToGain(b.gain));
            break;
    }
}

void EQEffect::process(juce::AudioBuffer<float>& buffer) {
    for (int i = 0; i < 3; ++i) {
        updateCoeffs(i);
        juce::dsp::AudioBlock<float> block(buffer);
        filters[i].process(juce::dsp::ProcessContextReplacing<float>(block));
    }
}

void EQEffect::setLowBand(float f, float g, float q) { bands[0] = {f, g, q, bands[0].type}; }
void EQEffect::setMidBand(float f, float g, float q) { bands[1] = {f, g, q, bands[1].type}; }
void EQEffect::setHighBand(float f, float g, float q) { bands[2] = {f, g, q, bands[2].type}; }
void EQEffect::setLowType(EQBandType t) { bands[0].type = t; }
void EQEffect::setMidType(EQBandType t) { bands[1].type = t; }
void EQEffect::setHighType(EQBandType t) { bands[2].type = t; }

} // namespace ana
