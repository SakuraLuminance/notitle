#include "BreathNoiseGenerator.h"
#include <cmath>

namespace ana {

BreathNoiseGenerator::BreathNoiseGenerator() {}

//==============================================================================
void BreathNoiseGenerator::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;

    const auto numCh = static_cast<size_t>(spec.numChannels);
    channels_.resize(numCh);

    for (auto& ch : channels_)
    {
        ch.bpf.prepare(spec);
        ch.envelope = 0.0f;
    }

    updateEnvCoeffs();
    filtersDirty_ = true;

    // Re-seed so each prepare gives a different noise pattern
    noiseGen_ = juce::Random();
}

//==============================================================================
void BreathNoiseGenerator::reset()
{
    for (auto& ch : channels_)
    {
        ch.bpf.reset();
        ch.envelope = 0.0f;
    }
}

//==============================================================================
void BreathNoiseGenerator::process(juce::AudioBuffer<float>& buffer)
{
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples  = buffer.getNumSamples();

    if (numChannels == 0 || numSamples == 0)
        return;

    // Snapshot parameters once per block (message thread vs audio thread safety)
    const float breathinessVal = breathinessVal_;
    const float mix            = mixVal_;
    const float attackAlpha    = attackAlpha_;
    const float releaseAlpha   = releaseAlpha_;
    const bool  filtersDirty   = filtersDirty_;

    const float breathNorm = breathinessVal / 100.0f; // 0-1

    // Recompute BPF coefficients if NoiseColor changed
    if (filtersDirty)
    {
        updateFilters();
        filtersDirty_ = false;
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* const data = buffer.getWritePointer(ch);
        auto& state = channels_[static_cast<size_t>(ch)];
        float env = state.envelope;

        for (int s = 0; s < numSamples; ++s)
        {
            const float dry = data[s];

            // --- Envelope follower on dry signal ---
            // Separate attack (~5ms) and release (~50ms) rates
            const float absDry = std::abs(dry);
            const float alpha  = (absDry > env) ? attackAlpha : releaseAlpha;
            env += alpha * (absDry - env);
            if (!std::isfinite(env))
                env = 0.0f;
            env += 1e-15f; // denormal protection

            // --- White noise → bandpass → envelope-shaped ---
            float noise = noiseGen_.nextFloat() * 2.0f - 1.0f;
            noise = state.bpf.processSample(noise);
            noise *= env;        // shape by vocal envelope
            noise *= breathNorm; // overall breathiness level

            // --- Wet/dry mix ---
            data[s] = dry * (1.0f - mix) + noise * mix;
        }

        state.envelope = juce::jlimit(0.0f, 1.0f, env);
    }
}

//==============================================================================
void BreathNoiseGenerator::updateFilters()
{
    // Map NoiseColor 0-100 → center frequency 2000-8000 Hz
    const float centreHz = 2000.0f + (noiseColorVal_ / 100.0f) * 6000.0f;
    constexpr float Q = 0.707f; // Butterworth Q

    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass(
        sampleRate_, centreHz, Q);

    for (auto& ch : channels_)
        *ch.bpf.coefficients = *coeffs;
}

//==============================================================================
void BreathNoiseGenerator::updateEnvCoeffs()
{
    // alpha = 1 - exp(-1 / (tau * fs))
    // tau_attack  = 5ms
    // tau_release = 50ms
    const float fs = static_cast<float>(sampleRate_);
    attackAlpha_  = 1.0f - std::exp(-1.0f / (0.005f * fs));
    releaseAlpha_ = 1.0f - std::exp(-1.0f / (0.050f * fs));
}

//==============================================================================
void BreathNoiseGenerator::setBreathiness(float percent)
{
    breathinessVal_ = juce::jlimit(0.0f, 100.0f, percent);
}

//==============================================================================
void BreathNoiseGenerator::setNoiseColor(float percent)
{
    noiseColorVal_ = juce::jlimit(0.0f, 100.0f, percent);
    filtersDirty_ = true;
}

//==============================================================================
void BreathNoiseGenerator::setMix(float percent)
{
    // Mix range is 0-20% (per spec), store as 0-1 normalized
    mixVal_ = juce::jlimit(0.0f, 20.0f, percent) / 100.0f;
}

//==============================================================================
juce::ValueTree BreathNoiseGenerator::getState() const
{
    juce::ValueTree tree("BreathNoiseGenerator");
    tree.setProperty("breathiness", static_cast<double>(breathinessVal_), nullptr);
    tree.setProperty("noiseColor",  static_cast<double>(noiseColorVal_), nullptr);
    tree.setProperty("mix",         static_cast<double>(mixVal_ * 100.0), nullptr);
    return tree;
}

//==============================================================================
void BreathNoiseGenerator::setState(const juce::ValueTree& state)
{
    setBreathiness(static_cast<float>(state.getProperty("breathiness", 30.0)));
    setNoiseColor(static_cast<float>(state.getProperty("noiseColor", 50.0)));
    setMix(static_cast<float>(state.getProperty("mix", 10.0)));
}

} // namespace ana
