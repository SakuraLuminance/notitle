#include "LimiterEffect.h"
#include <cmath>

namespace ana {

LimiterEffect::LimiterEffect() {}

void LimiterEffect::prepare(const juce::dsp::ProcessSpec& spec) {
    sampleRate = spec.sampleRate;
    numChannels = static_cast<int>(spec.numChannels);

    // Prepare lookahead delay lines
    lookaheadLines.resize(numChannels);
    for (auto& dl : lookaheadLines)
        dl.prepare(spec);

    // Pre-allocate oversampling buffer (4x max)
    int maxOsBlock = static_cast<int>(spec.maximumBlockSize * 4);
    osBuffer.setSize(numChannels, maxOsBlock);
    osBuffer.clear();

    envelope = 1.0f;
    attackCoeff = std::exp(-1.0f / (attackMs * sampleRate / 1000.0f));
    releaseCoeff = std::exp(-1.0f / (releaseMs * sampleRate / 1000.0f));
}

void LimiterEffect::reset() {
    for (auto& dl : lookaheadLines)
        dl.reset();
    envelope = 1.0f;
    osBuffer.clear();
}

void LimiterEffect::process(juce::AudioBuffer<float>& buffer) {
    if (bypassed) return;

    int numSamples = buffer.getNumSamples();
    int numCh = juce::jmin(numChannels, buffer.getNumChannels());

    // Update attack/release coefficients
    attackCoeff = std::exp(-1.0f / (attackMs * static_cast<float>(sampleRate) / 1000.0f));
    releaseCoeff = std::exp(-1.0f / (releaseMs * static_cast<float>(sampleRate) / 1000.0f));

    if (oversamplingFactor > 1) {
        // Upsample: insert zeros and process at higher rate
        int osSamples = numSamples * oversamplingFactor;
        if (osBuffer.getNumSamples() < osSamples)
            osBuffer.setSize(numCh, osSamples, false, true, false);
        osBuffer.clear();

        // Simple copy + zero-stuff upsampling
        for (int ch = 0; ch < numCh; ++ch) {
            auto* osData = osBuffer.getWritePointer(ch);
            const auto* inData = buffer.getReadPointer(ch);
            for (int s = 0; s < numSamples; ++s)
                osData[s * oversamplingFactor] = inData[s] * static_cast<float>(oversamplingFactor);
        }

        // Process oversampled buffer through limiter
        processLimiter(osBuffer);

        // Downsample: decimate back
        for (int ch = 0; ch < numCh; ++ch) {
            auto* outData = buffer.getWritePointer(ch);
            const auto* osData = osBuffer.getReadPointer(ch);
            for (int s = 0; s < numSamples; ++s)
                outData[s] = osData[s * oversamplingFactor];
        }
    } else {
        processLimiter(buffer);
    }
}

void LimiterEffect::processLimiter(juce::AudioBuffer<float>& buffer) {
    int numSamples = buffer.getNumSamples();
    int numCh = juce::jmin(numChannels, buffer.getNumChannels());

    // Dry buffer for mixing
    juce::AudioBuffer<float> dry(numCh, numSamples);
    dry.makeCopyOf(buffer);

    // Lookahead delay in samples
    float lookaheadSamples = lookaheadMs * static_cast<float>(sampleRate) / 1000.0f;

    float thresholdLinear = juce::Decibels::decibelsToGain(thresholdDb);

    // Push all samples into lookahead delays first
    for (int ch = 0; ch < numCh; ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int s = 0; s < numSamples; ++s)
            lookaheadLines[ch].pushSample(ch, data[s]);
    }

    // Compute envelope per sample from max across channels
    std::vector<float> gainReduction(numSamples, 1.0f);

    for (int s = 0; s < numSamples; ++s) {
        // Find max absolute level across all channels (stereo-linked)
        float absMax = 0.0f;
        int readPos = juce::jmax(0, s - static_cast<int>(lookaheadSamples));
        for (int ch = 0; ch < numCh; ++ch) {
            float sample = std::abs(buffer.getSample(ch, readPos));
            if (sample > absMax) absMax = sample;
        }

        float targetGain = 1.0f;
        if (absMax > thresholdLinear)
            targetGain = thresholdLinear / (absMax + 1e-10f);

        if (targetGain < envelope)
            envelope = envelope + (1.0f - attackCoeff) * (targetGain - envelope);
        else
            envelope = envelope + (1.0f - releaseCoeff) * (targetGain - envelope);

        gainReduction[s] = envelope;
    }

    // Apply gain reduction + lookahead read to each channel
    for (int ch = 0; ch < numCh; ++ch) {
        auto* outData = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s) {
            float delayedSample = lookaheadLines[ch].popSample(ch, lookaheadSamples);
            outData[s] = delayedSample * gainReduction[s];
        }
    }

    // Dry/wet mix with gain
    for (int ch = 0; ch < numCh; ++ch) {
        const auto* dryData = dry.getReadPointer(ch);
        auto* outData = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s) {
            outData[s] = dryData[s] * (1.0f - mixVal) + outData[s] * mixVal * gainVal;
        }
    }
}

void LimiterEffect::setThreshold(float db) { thresholdDb = juce::jlimit(-30.0f, 0.0f, db); }
void LimiterEffect::setAttack(float ms) { attackMs = juce::jlimit(0.01f, 10.0f, ms); }
void LimiterEffect::setRelease(float ms) { releaseMs = juce::jlimit(1.0f, 100.0f, ms); }
void LimiterEffect::setLookahead(float ms) { lookaheadMs = juce::jlimit(0.0f, 10.0f, ms); }
void LimiterEffect::setMix(float wet) { mixVal = juce::jlimit(0.0f, 1.0f, wet); }
void LimiterEffect::setBypass(bool b) { bypassed = b; }
void LimiterEffect::setGain(float g) { gainVal = juce::jlimit(0.0f, 4.0f, g); }
void LimiterEffect::setOversampling(int factor) { oversamplingFactor = juce::jlimit(1, 4, factor); }

} // namespace ana
