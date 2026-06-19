#include "CompressorEffect.h"
#include <cmath>

namespace ana {

CompressorEffect::CompressorEffect() {}

void CompressorEffect::prepare(const juce::dsp::ProcessSpec& spec) {
    sampleRate = spec.sampleRate;

    // RMS window of ~10ms
    int newWindow = static_cast<int>(sampleRate * 0.01);
    if (newWindow != rmsWindowSize) {
        rmsWindowSize = newWindow;
        rmsBuffer.resize(rmsWindowSize, 0.0f);
    }
    rmsSum = 0.0f;
    rmsIndex = 0;
    envelope = 0.0f;

    // Pre-calculate attack/release coefficients
    attackCoeff = std::exp(-1.0f / (attackMs * sampleRate / 1000.0f));
    releaseCoeff = std::exp(-1.0f / (releaseMs * sampleRate / 1000.0f));

    // Pre-allocate dry buffer and gain reduction vector
    dryBuffer_.setSize(static_cast<int>(spec.numChannels), static_cast<int>(spec.maximumBlockSize), false, false, true);
    gainReduction_.resize(spec.maximumBlockSize);
}

void CompressorEffect::reset() {
    rmsSum = 0.0f;
    rmsIndex = 0;
    std::fill(rmsBuffer.begin(), rmsBuffer.end(), 0.0f);
    envelope = 0.0f;
}

void CompressorEffect::process(juce::AudioBuffer<float>& buffer) {
    if (bypassed) return;

    int numSamples = buffer.getNumSamples();
    int numCh = buffer.getNumChannels();

    // Dry buffer for mixing (pre-allocated)
    dryBuffer_.makeCopyOf(buffer, true);

    // Update attack/release coefficients (in case parameters changed)
    attackCoeff = std::exp(-1.0f / (attackMs * static_cast<float>(sampleRate) / 1000.0f));
    releaseCoeff = std::exp(-1.0f / (releaseMs * static_cast<float>(sampleRate) / 1000.0f));

    // Stereo-linked processing: compute one envelope per sample from the max across all channels
    // First pass: compute gain reduction per sample (pre-allocated)
    gainReduction_.assign(numSamples, 1.0f);

    for (int s = 0; s < numSamples; ++s) {
        // Find max absolute level across all channels (stereo-linked)
        float absMax = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) {
            float sample = std::abs(buffer.getSample(ch, s));
            if (sample > absMax) absMax = sample;
        }

        float level;
        if (rmsMode) {
            // RMS detection with running sum (shared across channels)
            rmsSum -= rmsBuffer[rmsIndex];
            float squared = absMax * absMax;
            rmsBuffer[rmsIndex] = squared;
            rmsSum += squared;
            rmsIndex = (rmsIndex + 1) % rmsWindowSize;
            level = std::sqrt(rmsSum / static_cast<float>(rmsWindowSize));
            level = std::max(level, 1e-10f);
        } else {
            level = absMax;
        }

        float levelDb = 20.0f * std::log10(level);
        float gainReductionDb = computeGainReduction(levelDb);
        float targetGainLinear = juce::Decibels::decibelsToGain(gainReductionDb);

        // Smooth envelope with attack/release
        if (targetGainLinear < envelope)
            envelope = envelope + (1.0f - attackCoeff) * (targetGainLinear - envelope);
        else
            envelope = envelope + (1.0f - releaseCoeff) * (targetGainLinear - envelope);

        gainReduction_[s] = envelope;
    }

    // Second pass: apply gain reduction to each channel
    for (int ch = 0; ch < numCh; ++ch) {
        auto* outData = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
            outData[s] = outData[s] * gainReduction_[s];
    }

    // Compute auto makeup gain if enabled
    float finalMakeup = makeupGainDb;
    if (autoMakeup) {
        float autoMakeupDb = (1.0f - 1.0f / ratio) * -thresholdDb * 0.5f;
        finalMakeup = autoMakeupDb;
    }

    // Dry/wet mix with makeup and output gain
    for (int ch = 0; ch < numCh; ++ch) {
        const auto* dryData = dryBuffer_.getReadPointer(ch);
        auto* outData = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s) {
            float compressed = outData[s] * juce::Decibels::decibelsToGain(finalMakeup);
            outData[s] = juce::jlimit(-1.0f, 1.0f, dryData[s] * (1.0f - mixVal) + compressed * mixVal * gainVal);
        }
    }
}

float CompressorEffect::computeGainReduction(float levelDb) {
    if (kneeDb > 0.0f) {
        // Soft knee: smooth transition over kneeDb dB around threshold
        float kneeLow = thresholdDb - kneeDb * 0.5f;
        float kneeHigh = thresholdDb + kneeDb * 0.5f;

        if (levelDb < kneeLow) {
            // Below knee — no compression
            return 0.0f;
        } else if (levelDb < kneeHigh) {
            // In knee — gradual compression
            float x = (levelDb - kneeLow) / kneeDb;
            float compressed = levelDb + (1.0f / ratio - 1.0f) * (levelDb - thresholdDb + kneeDb * 0.5f) * x * x * 0.5f;
            return compressed - levelDb;
        } else {
            // Above knee — full compression
            float compressed = thresholdDb + (levelDb - thresholdDb) / ratio;
            return compressed - levelDb;
        }
    } else {
        // Hard knee
        if (levelDb <= thresholdDb)
            return 0.0f;
        float compressed = thresholdDb + (levelDb - thresholdDb) / ratio;
        return compressed - levelDb;
    }
}

void CompressorEffect::setThreshold(float db) { thresholdDb = juce::jlimit(-60.0f, 0.0f, db); }
void CompressorEffect::setRatio(float r) { ratio = juce::jlimit(1.0f, 20.0f, r); }
void CompressorEffect::setAttack(float ms) { attackMs = juce::jlimit(0.1f, 100.0f, ms); }
void CompressorEffect::setRelease(float ms) { releaseMs = juce::jlimit(10.0f, 1000.0f, ms); }
void CompressorEffect::setKnee(float db) { kneeDb = juce::jlimit(0.0f, 10.0f, db); }
void CompressorEffect::setMakeupGain(float db) { makeupGainDb = juce::jlimit(0.0f, 24.0f, db); }
void CompressorEffect::setMix(float wet) { mixVal = juce::jlimit(0.0f, 1.0f, wet); }
void CompressorEffect::setBypass(bool b) { bypassed = b; }
void CompressorEffect::setGain(float g) { gainVal = juce::jlimit(0.0f, 4.0f, g); }
void CompressorEffect::setRMSMode(bool rms) { rmsMode = rms; }
void CompressorEffect::setAutoMakeup(bool autoOn) { autoMakeup = autoOn; }

} // namespace ana
