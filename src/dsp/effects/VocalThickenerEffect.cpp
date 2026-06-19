#include "VocalThickenerEffect.h"
#include <algorithm>
#include <cmath>

namespace ana {

//==============================================================================
// DelayLine implementation
//==============================================================================

void VocalThickenerEffect::DelayLine::resize(int numSamples)
{
    data.resize(static_cast<size_t>(numSamples), 0.0f);
    capacity = numSamples;
    writePos = 0;
}

void VocalThickenerEffect::DelayLine::reset()
{
    std::fill(data.begin(), data.end(), 0.0f);
    writePos = 0;
}

float VocalThickenerEffect::DelayLine::process(float input, int delaySamples) noexcept
{
    // Passthrough when no delay requested
    if (delaySamples <= 0 || capacity <= 0)
        return input;

    // Write current sample
    data[static_cast<size_t>(writePos)] = input;

    // Read delayed sample
    int readPos = writePos - delaySamples;
    if (readPos < 0)
        readPos += capacity;
    float output = data[static_cast<size_t>(readPos)];

    // Advance write position
    writePos = (writePos + 1) % capacity;

    return output;
}

//==============================================================================
// VocalThickenerEffect construction
//==============================================================================

VocalThickenerEffect::VocalThickenerEffect()
{
    pitchShifter.setAlgorithm(PitchAlgorithm::Simple);
    pitchShifter.setCorrectionAmount(1.0f); // fully wet — we handle dry/wet externally
}

//==============================================================================
// Prepare
//==============================================================================

void VocalThickenerEffect::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    numChannels = static_cast<int>(spec.numChannels);
    maxExpectedBlockSize = static_cast<int>(spec.maximumBlockSize);

    // --- Pitch shifter ---
    pitchShifter.setSampleRate(sampleRate);
    pitchShifter.prepare(1, maxExpectedBlockSize);

    // --- Haas delay lines ---
    // Worst-case delay: 30 ms at max sample rate (generous upper bound)
    const int maxDelaySamples = static_cast<int>(std::ceil(30.0 * sampleRate / 1000.0)) + 1;
    for (auto& dl : delayLines)
        dl.resize(maxDelaySamples);

    // --- Scratch buffers ---
    voiceScratch.setSize(1, maxExpectedBlockSize, false, false, true);
    monoInput.setSize(1, maxExpectedBlockSize, false, false, true);
    dryBuffer.setSize(numChannels, maxExpectedBlockSize, false, false, true);
}

//==============================================================================
// Reset
//==============================================================================

void VocalThickenerEffect::reset()
{
    for (auto& dl : delayLines)
        dl.reset();
    voiceScratch.clear();
    monoInput.clear();
    dryBuffer.clear();
}

//==============================================================================
// Process
//==============================================================================

void VocalThickenerEffect::process(juce::AudioBuffer<float>& buffer)
{
    // Snapshot parameters once per block (message thread vs audio thread safety)
    const bool     bypassed    = this->bypassed;
    const float    mix         = this->mix;
    const int      voiceCount  = this->voiceCount;
    const float    haasDelayMs = this->haasDelayMs;
    const float    detune      = this->detune;
    const float    spread      = this->spread;
    const double   sampleRate  = this->sampleRate;

    // --- Early outs ---
    if (bypassed)
        return;
    if (mix <= 0.0f)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin(numChannels, buffer.getNumChannels());
    if (numSamples <= 0)
        return;

    // --- Save dry signal ---
    dryBuffer.makeCopyOf(buffer, true);

    // --- Create mono mixdown ---
    monoInput.clear();
    if (numCh >= 2)
    {
        // Average L+R to mono
        const float* inL = buffer.getReadPointer(0);
        const float* inR = buffer.getReadPointer(1);
        float* mono = monoInput.getWritePointer(0);
        for (int s = 0; s < numSamples; ++s)
            mono[s] = (inL[s] + inR[s]) * 0.5f;
    }
    else
    {
        monoInput.copyFrom(0, 0, buffer, 0, 0, numSamples);
    }

    // --- Clear output for wet accumulation ---
    buffer.clear();

    // --- Voice processing ---
    const int activeVoices = juce::jlimit(2, kMaxVoices, voiceCount);
    const float invSqrtN = 1.0f / std::sqrt(static_cast<float>(activeVoices));
    const int haasSamples = static_cast<int>(std::round(haasDelayMs * sampleRate / 1000.0));

    const float* src = monoInput.getReadPointer(0);
    float* outL = buffer.getWritePointer(0);
    float* outR = (numCh > 1) ? buffer.getWritePointer(1) : nullptr;

    for (int v = 0; v < activeVoices; ++v)
    {
        // --- Pitch shift amount (cents → semitones) ---
        // Distribute evenly across [-detune, +detune] in cents
        const float voiceDetuneCents = detune * (2.0f * static_cast<float>(v)
                                                  / static_cast<float>(activeVoices - 1) - 1.0f);
        const float pitchShiftSemitones = voiceDetuneCents / 100.0f;

        // --- Haas delay: alternating voices ---
        const int voiceDelaySamples = (v % 2 == 0) ? 0 : haasSamples;

        // --- Pan: distribute evenly across stereo field ---
        const float panPos = spread * (2.0f * static_cast<float>(v)
                                        / static_cast<float>(activeVoices - 1) - 1.0f);
        // Constant-power pan: -1 → left, 0 → center, +1 → right
        const float angle = panPos * juce::MathConstants<float>::pi * 0.25f
                          + juce::MathConstants<float>::pi * 0.25f;
        const float leftGain  = std::cos(angle) * invSqrtN;
        const float rightGain = std::sin(angle) * invSqrtN;

        // --- Process this voice ---
        processVoice(src, outL, outR, numSamples,
                     pitchShiftSemitones, voiceDelaySamples,
                     leftGain, rightGain, v);
    }

    // --- Wet/dry mix ---
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* dry = dryBuffer.getReadPointer(ch);
        float* wet = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
            wet[s] = dry[s] * (1.0f - mix) + wet[s] * mix;
    }
}

//==============================================================================
// processVoice
//==============================================================================

void VocalThickenerEffect::processVoice(
    const float* monoInput,
    float* outL, float* outR,
    int numSamples,
    float pitchShiftSemitones,
    int haasDelaySamples,
    float leftGain, float rightGain,
    int voiceIndex)
{
    (void)voiceIndex; // reserved for future per-voice LFO etc.

    // --- 1. Copy mono input to scratch buffer ---
    voiceScratch.copyFrom(0, 0, monoInput, numSamples);

    // --- 2. Pitch shift via sinc interpolation (Simple mode) ---
    pitchShifter.setPitchShift(pitchShiftSemitones);
    pitchShifter.process(voiceScratch);

    // --- 3. Apply Haas delay + pan, accumulate to output ---
    const float* shifted = voiceScratch.getReadPointer(0);
    auto& delayLine = delayLines[voiceIndex];

    if (outR != nullptr)
    {
        // Stereo output
        for (int s = 0; s < numSamples; ++s)
        {
            const float sample = delayLine.process(shifted[s], haasDelaySamples);
            outL[s] += sample * leftGain;
            outR[s] += sample * rightGain;
        }
    }
    else
    {
        // Mono output — just sum with equal gain
        for (int s = 0; s < numSamples; ++s)
        {
            const float sample = delayLine.process(shifted[s], haasDelaySamples);
            outL[s] += sample * 0.5f; // mono fold: average L+R gains
        }
    }
}

//==============================================================================
// Parameter setters
//==============================================================================

void VocalThickenerEffect::setDetune(float cents)
{
    detune = std::max(0.0f, std::min(25.0f, cents));
}

void VocalThickenerEffect::setVoiceCount(int voices)
{
    voiceCount = juce::jlimit(2, kMaxVoices, voices);
}

void VocalThickenerEffect::setSpread(float percent)
{
    spread = std::max(0.0f, std::min(1.0f, percent / 100.0f));
}

void VocalThickenerEffect::setHaasDelay(float ms)
{
    haasDelayMs = std::max(0.0f, std::min(30.0f, ms));
}

void VocalThickenerEffect::setMix(float percent)
{
    mix = std::max(0.0f, std::min(1.0f, percent / 100.0f));
}

void VocalThickenerEffect::setBypass(bool b)
{
    bypassed = b;
}

//==============================================================================
// State persistence
//==============================================================================

juce::ValueTree VocalThickenerEffect::getState() const
{
    juce::ValueTree tree("VocalThickenerEffect");
    tree.setProperty("detune",      detune, nullptr);
    tree.setProperty("voiceCount",  voiceCount, nullptr);
    tree.setProperty("spread",      spread, nullptr);
    tree.setProperty("haasDelayMs", haasDelayMs, nullptr);
    tree.setProperty("mix",         mix, nullptr);
    tree.setProperty("bypass",      bypassed, nullptr);
    return tree;
}

void VocalThickenerEffect::setState(const juce::ValueTree& state)
{
    setDetune(static_cast<float>(state.getProperty("detune", 10.0f)));
    setVoiceCount(static_cast<int>(state.getProperty("voiceCount", 2)));
    setSpread(static_cast<float>(state.getProperty("spread", 0.5f)) * 100.0f);
    setHaasDelay(static_cast<float>(state.getProperty("haasDelayMs", 10.0f)));
    setMix(static_cast<float>(state.getProperty("mix", 0.4f)) * 100.0f);
    setBypass(state.getProperty("bypass", false));
}

} // namespace ana
