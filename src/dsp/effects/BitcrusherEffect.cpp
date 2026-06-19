#include "BitcrusherEffect.h"
#include <cmath>

namespace ana {

BitcrusherEffect::BitcrusherEffect() {}

void BitcrusherEffect::prepare(const juce::dsp::ProcessSpec& spec)
{
    const auto numChannels = static_cast<size_t>(spec.numChannels);
    heldSample_.assign(numChannels, 0.0f);
    sampleCounter_.assign(numChannels, 0);
}

void BitcrusherEffect::reset()
{
    std::fill(heldSample_.begin(), heldSample_.end(), 0.0f);
    std::fill(sampleCounter_.begin(), sampleCounter_.end(), 0);
}

void BitcrusherEffect::setBitDepth(float depth)
{
    bitDepth_ = std::max(1.0f, std::min(16.0f, depth));
    updateQuantizeFactor();
}

void BitcrusherEffect::setDownsample(float factor)
{
    downsample_ = std::max(1.0f, std::min(32.0f, factor));
}

void BitcrusherEffect::setMix(float wet)
{
    mix_ = std::max(0.0f, std::min(1.0f, wet));
}

void BitcrusherEffect::updateQuantizeFactor()
{
    quantizeFactor_ = std::pow(2.0f, bitDepth_);
}

void BitcrusherEffect::process(juce::AudioBuffer<float>& buffer)
{
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples  = buffer.getNumSamples();
    const float mix        = mix_;
    const int   dsFactor   = static_cast<int>(downsample_);
    const float qf         = quantizeFactor_;
    const float denormGuard = 1.0e-30f;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* samples = buffer.getWritePointer(ch);
        float& held    = heldSample_[static_cast<size_t>(ch)];
        int&   counter = sampleCounter_[static_cast<size_t>(ch)];

        for (int s = 0; s < numSamples; ++s)
        {
            const float dry = samples[s] + denormGuard;

            // Sample-and-hold for rate reduction
            // Capture new sample when counter wraps to 0
            if (counter == 0)
                held = std::roundf(dry * qf) / qf;

            const float wet = held;
            samples[s] = dry * (1.0f - mix) + wet * mix;
            counter = (counter + 1) % dsFactor;
        }
    }
}

} // namespace ana
