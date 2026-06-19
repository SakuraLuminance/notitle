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
        v.phasorRe = 1.0f;
        v.phasorIm = 0.0f;
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
        v.phasorRe = std::cos(v.initialPhase);
        v.phasorIm = std::sin(v.initialPhase);
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

            // Precompute expensive per-voice terms (pow, sqrt) — no longer done per-block
            voices_[i].detuneRatio = std::pow(2.0f, voices_[i].detuneCents / 1200.0f);
            voices_[i].leftGain = std::sqrt(0.5f * (1.0f - voices_[i].pan));
            voices_[i].rightGain = std::sqrt(0.5f * (1.0f + voices_[i].pan));
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

    // Hoist write pointers (Fix B: replaces per-sample buffer.addSample)
    float* const outL = buffer.getWritePointer(0);
    float* const outR = isStereo ? buffer.getWritePointer(1) : nullptr;

    for (int v = 0; v < voiceCount_; ++v)
    {
        auto& voice = voices_[v];

        // Frequency shift from precomputed detune ratio (Fix C: pow hoisted to updateVoices)
        const float voiceFreq = frequency_ * voice.detuneRatio;

        // Phase increment delta — compute cos/sin once per voice per block
        const float phaseDelta = juce::MathConstants<float>::twoPi * voiceFreq
                                 / static_cast<float>(sampleRate_);
        voice.cosDelta = std::cos(phaseDelta);
        voice.sinDelta = std::sin(phaseDelta);

        // Generate samples via recursive phasor rotation (Fix A: replaces std::sin)
        if (isStereo)
        {
            for (int s = 0; s < numSamples; ++s)
            {
                const float sample = voice.phasorIm;

                // z(n+1) = z(n) * e^(j*delta)
                const float re = voice.phasorRe * voice.cosDelta
                               - voice.phasorIm * voice.sinDelta;
                const float im = voice.phasorRe * voice.sinDelta
                               + voice.phasorIm * voice.cosDelta;
                voice.phasorRe = re;
                voice.phasorIm = im;

                outL[s] += sample * voice.leftGain;
                outR[s] += sample * voice.rightGain;
            }
        }
        else
        {
            for (int s = 0; s < numSamples; ++s)
            {
                const float sample = voice.phasorIm;

                const float re = voice.phasorRe * voice.cosDelta
                               - voice.phasorIm * voice.sinDelta;
                const float im = voice.phasorRe * voice.sinDelta
                               + voice.phasorIm * voice.cosDelta;
                voice.phasorRe = re;
                voice.phasorIm = im;

                outL[s] += sample;
            }
        }
    }

    // Normalise summed voices (constant-power scaling)
    const float norm = 1.0f / std::sqrt(static_cast<float>(voiceCount_));
    buffer.applyGain(norm);
}

} // namespace ana
