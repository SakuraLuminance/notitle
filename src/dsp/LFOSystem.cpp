#include "LFOSystem.h"
#include <algorithm>

namespace ana {

//==============================================================================
LFOSystem::LFOSystem()
{
    randomValue = generateRandomValue();
}

//==============================================================================
void LFOSystem::prepare(double sr)
{
    sampleRate = sr;
    reset();
}

void LFOSystem::reset()
{
    phase = 0.0;
    currentValue = 0.0f;
    randomValue = generateRandomValue();
}

//==============================================================================
void LFOSystem::setWaveform(WaveformType type) noexcept
{
    waveform = type;
}

WaveformType LFOSystem::getWaveform() const noexcept
{
    return waveform;
}

//==============================================================================
void LFOSystem::setRate(float hz)
{
    if (audioRate)
    {
        const float maxHz = static_cast<float>(sampleRate * 0.5);
        rateHz = juce::jlimit(0.01f, maxHz, hz);
    }
    else
    {
        rateHz = juce::jlimit(0.01f, 100.0f, hz);
    }
    syncEnabled = false;
}

float LFOSystem::getRate() const noexcept
{
    return rateHz;
}

void LFOSystem::setRateBeats(float beats)
{
    rateBeats = juce::jmax(0.03125f, beats); // minimum 1/32 note
    syncEnabled = true;
}

float LFOSystem::getRateBeats() const noexcept
{
    return rateBeats;
}

//==============================================================================
void LFOSystem::setDepth(float percent)
{
    depthPercent = juce::jlimit(0.0f, 100.0f, percent);
}

float LFOSystem::getDepth() const noexcept
{
    return depthPercent;
}

//==============================================================================
void LFOSystem::setPhase(float degrees)
{
    phaseOffsetDeg = juce::jlimit(0.0f, 360.0f, degrees);
}

float LFOSystem::getPhase() const noexcept
{
    return phaseOffsetDeg;
}

//==============================================================================
void LFOSystem::setBipolar(bool b) noexcept
{
    bipolar = b;
}

bool LFOSystem::isBipolar() const noexcept
{
    return bipolar;
}

//==============================================================================
void LFOSystem::setTempo(double bpm)
{
    tempo = juce::jmax(1.0, bpm);
}

double LFOSystem::getTempo() const noexcept
{
    return tempo;
}

//==============================================================================
bool LFOSystem::isSyncEnabled() const noexcept
{
    return syncEnabled;
}

//==============================================================================
double LFOSystem::getEffectiveRate() const noexcept
{
    if (syncEnabled)
    {
        // Convert beat division to frequency:
        //   beats = note duration (1.0 = quarter, 0.5 = eighth…)
        //   cycles/sec = (bpm / 60) / beats
        return tempo / (60.0 * static_cast<double>(rateBeats));
    }
    return static_cast<double>(rateHz);
}

//==============================================================================
float LFOSystem::generateRandomValue()
{
    return dist(rng);
}

//==============================================================================
float LFOSystem::computeWaveform(float p) const
{
    // p is in [0, 1) — normalised phase
    switch (waveform)
    {
        case WaveformType::Sine:
        {
            // sin(2pi * p)
            return std::sin(juce::MathConstants<float>::twoPi * p);
        }

        case WaveformType::Triangle:
        {
            // 2 * |2 * (p - floor(p + 0.5))| - 1
            // Generates -1 at p=0, rising to +1 at p=0.5, falling back to -1 at p=1
            const float x = p - std::floor(p + 0.5f);
            return 2.0f * std::abs(2.0f * x) - 1.0f;
        }

        case WaveformType::Saw:
        {
            // 2 * (p - floor(p + 0.5))
            // Rising ramp: 0 at p=0, +1 at p→0.5, -1 at p=0.5, 0 at p=1
            const float x = p - std::floor(p + 0.5f);
            return 2.0f * x;
        }

        case WaveformType::Square:
        {
            // sign(sin(2pi * p)) — hard transition at phase 0 and 0.5
            return std::sin(juce::MathConstants<float>::twoPi * p) >= 0.0f ? 1.0f : -1.0f;
        }

        case WaveformType::Random:
        {
            // Return the held random sample
            return randomValue;
        }
    }

    return 0.0f;
}

//==============================================================================
float LFOSystem::process(int numSamples)
{
    if (numSamples > 0)
    {
        const double effectiveRate = getEffectiveRate();
        const double delta = effectiveRate * static_cast<double>(numSamples) / sampleRate;

        // Advance phase and detect wraps for sample & hold
        phase += delta;
        if (phase >= 1.0)
        {
            phase = std::fmod(phase, 1.0);
            if (waveform == WaveformType::Random)
                randomValue = generateRandomValue();
        }
    }

    // Apply phase offset to get the effective phase for waveform lookup
    double effectivePhase = phase + static_cast<double>(phaseOffsetDeg) / 360.0;
    effectivePhase = std::fmod(effectivePhase, 1.0);
    if (effectivePhase < 0.0)
        effectivePhase += 1.0;

    // Generate raw waveform in [-1, 1]
    float raw = computeWaveform(static_cast<float>(effectivePhase));

    // Apply depth scaling
    float scaled = raw * (depthPercent / 100.0f);

    // Convert to unipolar if needed
    if (!bipolar)
        scaled = scaled * 0.5f + 0.5f;

    currentValue = scaled;
    return currentValue;
}

//==============================================================================
float LFOSystem::getValue() const noexcept
{
    return currentValue;
}

//==============================================================================
float LFOSystem::getNextSample()
{
    return process(1);
}

//==============================================================================
void LFOSystem::setAudioRate(bool enabled) noexcept
{
    audioRate = enabled;
}

bool LFOSystem::isAudioRate() const noexcept
{
    return audioRate;
}

//==============================================================================
double LFOSystem::getCurrentPhase() const noexcept
{
    double effectivePhase = phase + static_cast<double>(phaseOffsetDeg) / 360.0;
    effectivePhase = std::fmod(effectivePhase, 1.0);
    if (effectivePhase < 0.0)
        effectivePhase += 1.0;
    return effectivePhase;
}

} // namespace ana
