#include "UnisonEngine.h"
#include <cmath>

namespace ana {

// ===========================================================================
// Construction
// ===========================================================================

UnisonEngine::UnisonEngine()
{
    voices_.resize(1);
}

// ===========================================================================
// Life-cycle
// ===========================================================================

void UnisonEngine::prepare(double sampleRate, int /*blockSize*/)
{
    sampleRate_ = sampleRate;
    reset();
}

void UnisonEngine::reset()
{
    for (auto& v : voices_)
    {
        v.phase = 0.0f;
        v.initialPhase = 0.0f;
    }
}

void UnisonEngine::noteOn()
{
    const float maxOffset = phaseOffset_ * juce::MathConstants<float>::twoPi;
    for (auto& v : voices_)
    {
        v.phase = 0.0f;
        v.initialPhase = maxOffset > 0.0f
                             ? random_.nextFloat() * maxOffset
                             : 0.0f;
    }
}

// ===========================================================================
// Parameter setters
// ===========================================================================

void UnisonEngine::setVoiceCount(int count)
{
    voiceCount_ = std::clamp(count, 1, 8);
    voicesValid_ = false;
}

void UnisonEngine::setDetune(float cents)
{
    detuneCents_ = std::clamp(cents, 0.0f, 100.0f);
    voicesValid_ = false;
}

void UnisonEngine::setStereoSpread(float percent)
{
    stereoSpread_ = std::clamp(percent, 0.0f, 100.0f);
    voicesValid_ = false;
}

void UnisonEngine::setPhaseOffset(float amount)
{
    phaseOffset_ = std::clamp(amount, 0.0f, 1.0f);
    voicesValid_ = false;
}

void UnisonEngine::setFrequency(float freqHz)
{
    frequency_ = freqHz;
}

// ===========================================================================
// Voice update
// ===========================================================================

void UnisonEngine::updateVoices()
{
    voices_.resize(voiceCount_);

    if (voiceCount_ == 1)
    {
        voices_[0].detuneCents = 0.0f;
        voices_[0].pan = 0.0f;
    }
    else
    {
        const float maxDetune = detuneCents_ * voiceCount_ * 0.5f;
        const float spreadNorm = stereoSpread_ / 100.0f;

        for (int i = 0; i < voiceCount_; ++i)
        {
            const float pos = static_cast<float>(i) / static_cast<float>(voiceCount_ - 1); // [0, 1]
            voices_[i].detuneCents = (pos * 2.0f - 1.0f) * maxDetune;
            voices_[i].pan = (pos * 2.0f - 1.0f) * spreadNorm;
        }
    }

    voicesValid_ = true;
}

// ===========================================================================
// Processing
// ===========================================================================

void UnisonEngine::process(juce::AudioBuffer<float>& buffer)
{
    const auto numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    if (!voicesValid_)
        updateVoices();

    // Clear output buffer
    buffer.clear();

    const bool isStereo = buffer.getNumChannels() >= 2;

    for (int v = 0; v < voiceCount_; ++v)
    {
        const auto& voice = voices_[v];

        // Frequency shift for this voice (cents -> ratio)
        const float detuneRatio = std::pow(2.0f, voice.detuneCents / 1200.0f);
        const float voiceFreq = frequency_ * detuneRatio;

        // Phase increment per sample
        const float phaseDelta = juce::MathConstants<float>::twoPi * voiceFreq
                                 / static_cast<float>(sampleRate_);

        // Pan gains (constant-power law)
        float leftGain = 1.0f;
        float rightGain = 1.0f;
        if (isStereo)
        {
            leftGain = std::sqrt(0.5f * (1.0f - voice.pan));
            rightGain = std::sqrt(0.5f * (1.0f + voice.pan));
        }

        // Retrieve per-voice mutable state
        auto& phase = const_cast<UnisonVoice&>(voice).phase;
        const float phaseInit = voice.initialPhase;

        // Generate samples for this voice across the entire buffer
        int s = 0;
        if (isStereo)
        {
            for (; s < numSamples; ++s)
            {
                const float sample = std::sin(phase + phaseInit);
                phase += phaseDelta;
                if (phase >= juce::MathConstants<float>::twoPi)
                    phase -= juce::MathConstants<float>::twoPi;

                buffer.addSample(0, s, sample * leftGain);
                buffer.addSample(1, s, sample * rightGain);
            }
        }
        else
        {
            for (; s < numSamples; ++s)
            {
                const float sample = std::sin(phase + phaseInit);
                phase += phaseDelta;
                if (phase >= juce::MathConstants<float>::twoPi)
                    phase -= juce::MathConstants<float>::twoPi;

                buffer.addSample(0, s, sample);
            }
        }
    }

    // Normalise summed voices (constant-power scaling)
    const float norm = 1.0f / std::sqrt(static_cast<float>(voiceCount_));
    buffer.applyGain(norm);
}

} // namespace ana
