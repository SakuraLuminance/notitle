#include "GranularSynthesizer.h"
#include <cmath>
#include <algorithm>
#include <juce_core/juce_core.h>

namespace ana {

//==============================================================================
GranularSynthesizer::GranularSynthesizer()
    : rng_(std::random_device{}())
{
    reset();
}

GranularSynthesizer::~GranularSynthesizer() = default;

//==============================================================================
void GranularSynthesizer::setSourceBuffer(const std::vector<float>& buffer, double sampleRate)
{
    sourceBuffer = buffer;
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void GranularSynthesizer::setGrainSize(float ms)
{
    grainSizeMs_ = std::clamp(ms, 1.0f, 100.0f);
}

void GranularSynthesizer::setDensity(float grainsPerSec)
{
    density_ = std::clamp(grainsPerSec, 1.0f, 1000.0f);
}

void GranularSynthesizer::setPosition(float normalizedPosition)
{
    position_ = std::clamp(normalizedPosition, 0.0f, 1.0f);
}

void GranularSynthesizer::setPitch(float semitones)
{
    pitchSemitones_ = std::clamp(semitones, -24.0f, 24.0f);
}

void GranularSynthesizer::setAmplitude(float amp)
{
    amplitude_ = std::clamp(amp, 0.0f, 1.0f);
}

void GranularSynthesizer::setPan(float pan)
{
    pan_ = std::clamp(pan, -1.0f, 1.0f);
}

void GranularSynthesizer::setWindowType(GrainWindowType type)
{
    windowType_ = type;
}

void GranularSynthesizer::setPositionModulation(PositionModulation mod, float depth, float rate)
{
    posMod_       = mod;
    posModDepth_  = std::clamp(depth, 0.0f, 1.0f);
    posModRate_   = std::max(rate, 0.01f);
}

//==============================================================================
void GranularSynthesizer::process(juce::AudioBuffer<float>& output)
{
    const int numSamples  = output.getNumSamples();
    const int numChannels = output.getNumChannels();

    if (sourceBuffer.empty() || numSamples <= 0)
        return;

    // Clear output before writing
    output.clear();

    //==========================================================================
    // 1. Schedule new grains based on density
    //==========================================================================
    const double grainsPerSample = static_cast<double>(density_) / sampleRate_;
    grainAccumulator_ += grainsPerSample * static_cast<double>(numSamples);

    while (grainAccumulator_ >= 1.0)
    {
        if (!spawnGrain())
            break; // no free slots available
        grainAccumulator_ -= 1.0;
    }

    //==========================================================================
    // 2. Render each sample by summing all active grains
    //==========================================================================
    float* channelData[2] = { nullptr, nullptr };
    if (numChannels >= 1) channelData[0] = output.getWritePointer(0);
    if (numChannels >= 2) channelData[1] = output.getWritePointer(1);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float left  = 0.0f;
        float right = 0.0f;

        for (int g = 0; g < maxGrains_; ++g)
        {
            auto& grain = grains_[g];
            if (!grain.active)
                continue;

            // Early exit for silent grains (avoids all computation)
            if (grain.amplitude == 0.0f)
            {
                grain.sourcePosition += grain.pitchRatio;
                grain.currentSample++;
                if (grain.currentSample >= grain.durationSamples)
                    grain.active = false;
                continue;
            }

            // Read source with linear interpolation
            const float srcSample = interpolateSource(grain.sourcePosition);

            // Apply window envelope (uses precomputed table for O(1) lookup)
            const float window = getCachedWindowValue(grain.currentSample,
                                                      grain.durationSamples,
                                                      grain.windowType);

            const float value = srcSample * window * grain.amplitude;

            left  += value * grain.panL;
            right += value * grain.panR;

            // Advance grain playhead
            grain.sourcePosition += grain.pitchRatio;
            grain.currentSample++;

            // Deactivate finished grains
            if (grain.currentSample >= grain.durationSamples)
                grain.active = false;
        }

        // Write interleaved output
        if (channelData[0] != nullptr)
            channelData[0][sample] = left;

        if (channelData[1] != nullptr)
            channelData[1][sample] = right;
    }

    //==========================================================================
    // 3. Advance LFO phase for position modulation
    //==========================================================================
    lfoPhase_ += static_cast<double>(posModRate_) * static_cast<double>(numSamples) / sampleRate_;
    if (lfoPhase_ >= 1.0)
        lfoPhase_ -= std::floor(lfoPhase_);
}

void GranularSynthesizer::reset()
{
    for (auto& grain : grains_)
        grain.active = false;

    grainAccumulator_   = 0.0;
    lfoPhase_           = 0.0;
    totalGrainsSpawned_ = 0;
}

//==============================================================================
int GranularSynthesizer::getActiveGrainCount() const
{
    int count = 0;
    for (const auto& grain : grains_)
    {
        if (grain.active)
            ++count;
    }
    return count;
}

int GranularSynthesizer::getTotalGrainsSpawned() const
{
    return totalGrainsSpawned_;
}

double GranularSynthesizer::getSampleRate() const
{
    return sampleRate_;
}

//==============================================================================
// Private
//==============================================================================

bool GranularSynthesizer::spawnGrain()
{
    // Find a free slot
    int slot = -1;
    for (int i = 0; i < maxGrains_; ++i)
    {
        if (!grains_[i].active)
        {
            slot = i;
            break;
        }
    }

    if (slot < 0)
        return false; // all 256 slots occupied

    //--------------------------------------------------------------------------
    // Compute modulated position
    //--------------------------------------------------------------------------
    double modPosition = static_cast<double>(position_);

    switch (posMod_)
    {
        case PositionModulation::LFO:
        {
            const double lfo = std::sin(2.0 * juce::MathConstants<double>::pi * lfoPhase_);
            modPosition += static_cast<double>(posModDepth_) * lfo;
            break;
        }

        case PositionModulation::Envelope:
        {
            // Bipolar triangle wave LFO mapped to [-depth, +depth]
            const double phase = lfoPhase_ - std::floor(lfoPhase_ + 0.5);
            const double tri   = 2.0 * std::abs(2.0 * phase);
            modPosition += static_cast<double>(posModDepth_) * (tri - 1.0);
            break;
        }

        case PositionModulation::Random:
        {
            std::uniform_real_distribution<float> dist(-posModDepth_, posModDepth_);
            modPosition += static_cast<double>(dist(rng_));
            break;
        }

        case PositionModulation::Off:
        default:
            break;
    }

    modPosition = std::clamp(modPosition, 0.0, 1.0);

    //--------------------------------------------------------------------------
    // Compute grain geometry
    //--------------------------------------------------------------------------
    const int sourceLen   = static_cast<int>(sourceBuffer.size());
    const int grainDur    = std::max(1, static_cast<int>(grainSizeMs_ * sampleRate_ / 1000.0));

    const double ratio    = std::pow(2.0, static_cast<double>(pitchSemitones_) / 12.0);

    // Centre the grain playback around the target position
    const double centreSample = modPosition * static_cast<double>(sourceLen - 1);
    const double halfSpan     = static_cast<double>(grainDur) * ratio * 0.5;
    const double startPos     = std::clamp(centreSample - halfSpan,
                                           0.0,
                                           static_cast<double>(sourceLen - 1));

    // Constant-power pan
    const float panNorm = (pan_ + 1.0f) * 0.5f; // [-1, 1] -> [0, 1]
    const float panL    = std::cos(panNorm * juce::MathConstants<float>::halfPi);
    const float panR    = std::sin(panNorm * juce::MathConstants<float>::halfPi);

    //--------------------------------------------------------------------------
    // Fill grain slot
    //--------------------------------------------------------------------------
    auto& g = grains_[slot];
    g.sourcePosition  = startPos;
    g.currentSample   = 0;
    g.durationSamples = grainDur;
    g.pitchRatio      = ratio;
    g.amplitude       = amplitude_;
    g.panL            = panL;
    g.panR            = panR;
    g.windowType      = windowType_;
    g.active          = true;

    ++totalGrainsSpawned_;
    return true;
}

float GranularSynthesizer::getWindowValue(int index, int duration, GrainWindowType type) const
{
    if (duration <= 1)
        return 1.0f;

    const double n = static_cast<double>(index);
    const double N = static_cast<double>(duration);

    switch (type)
    {
        case GrainWindowType::Hann:
        {
            // Raised cosine: 0.5 * (1 - cos(2pi * n / (N-1)))
            const double phase = 2.0 * juce::MathConstants<double>::pi * n / (N - 1.0);
            return static_cast<float>(0.5 * (1.0 - std::cos(phase)));
        }

        case GrainWindowType::Triangle:
        {
            const double mid = N / 2.0;
            return static_cast<float>(n < mid
                ? 2.0 * n / N
                : 2.0 * (1.0 - n / N));
        }

        case GrainWindowType::Gaussian:
        {
            // Gaussian with sigma=0.2, truncated to grain boundaries
            const double sigma = 0.2;
            const double x     = (n - N / 2.0) / (N / 2.0);
            return static_cast<float>(std::exp(-0.5 * (x * x) / (sigma * sigma)));
        }

        case GrainWindowType::Sinc:
        {
            // Truncated sinc with 4 side lobes
            const double a = 4.0;
            const double x = a * (2.0 * n / N - 1.0);
            if (std::abs(x) < 1.0e-8)
                return 1.0f;
            return static_cast<float>(std::sin(juce::MathConstants<double>::pi * x)
                                    / (juce::MathConstants<double>::pi * x));
        }
    }

    return 1.0f;
}

float GranularSynthesizer::getCachedWindowValue(int index, int duration, GrainWindowType type) const
{
    // Recompute cache when parameters change (common case: all active grains share same settings)
    if (duration != cachedWindowDuration_ || type != cachedWindowType_)
    {
        windowCache_.resize(static_cast<size_t>(duration));
        for (int i = 0; i < duration; ++i)
            windowCache_[static_cast<size_t>(i)] = getWindowValue(i, duration, type);
        cachedWindowDuration_ = duration;
        cachedWindowType_ = type;
    }

    return (index >= 0 && static_cast<size_t>(index) < windowCache_.size())
        ? windowCache_[static_cast<size_t>(index)]
        : 1.0f;
}

float GranularSynthesizer::interpolateSource(double position) const
{
    const int len = static_cast<int>(sourceBuffer.size());
    if (len < 1)
        return 0.0f;

    // Clamp to valid range
    const double clampedPos = std::clamp(position, 0.0, static_cast<double>(len - 1));

    const int i0    = static_cast<int>(clampedPos);
    const int i1    = std::min(i0 + 1, len - 1);
    const float frac = static_cast<float>(clampedPos - static_cast<double>(i0));

    // Float arithmetic only (sourceBuffer is float)
    return (1.0f - frac) * sourceBuffer[i0] + frac * sourceBuffer[i1];
}

} // namespace ana
