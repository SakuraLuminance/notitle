#include "SpaceModule.h"
#include <cmath>
#include <algorithm>

namespace ana {

SpaceModule::SpaceModule() {}

void SpaceModule::prepare(const juce::dsp::ProcessSpec& spec) {
    sampleRate = spec.sampleRate;

    // Prepare reverb
    reverb.reset();
    setReverbPreset(mode);

    // Prepare shimmer delay buffer: 2 seconds at sample rate
    shimmerBufSize = static_cast<int>(sampleRate * 2.0);
    shimmerBuf.assign(shimmerBufSize, 0.0f);
    shimmerWritePos = 0;
    shimmerReadPos = static_cast<float>(shimmerBufSize) * 0.25f;
    shimmerReadPos2 = static_cast<float>(shimmerBufSize) * 0.5f;
    shimmerCrossfadePos = 0.0f;
    shimmerCrossfadeLen = static_cast<float>(shimmerBufSize) * 0.125f; // 250ms crossfade
    shimmerUseSecondHead = false;
    shimmerPitchRatio = std::pow(2.0f, shimmerShift / 12.0f);

    // Prepare wet filters
    *wetHPF.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 20.0f);
    *wetLPF.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 20000.0f);
    wetHPF.prepare(spec);
    wetLPF.prepare(spec);

    // Pre-allocate dry and wet buffers
    dryBuffer_.setSize(static_cast<int>(spec.numChannels), static_cast<int>(spec.maximumBlockSize), false, false, true);
    wetBuffer_.setSize(static_cast<int>(spec.numChannels), static_cast<int>(spec.maximumBlockSize), false, false, true);
}

void SpaceModule::reset() {
    reverb.reset();
    std::fill(shimmerBuf.begin(), shimmerBuf.end(), 0.0f);
    shimmerWritePos = 0;
    shimmerReadPos = static_cast<float>(shimmerBufSize) * 0.25f;
    shimmerReadPos2 = static_cast<float>(shimmerBufSize) * 0.5f;
    shimmerCrossfadePos = 0.0f;
    shimmerUseSecondHead = false;
    wetHPF.reset();
    wetLPF.reset();
}

void SpaceModule::process(juce::AudioBuffer<float>& buffer) {
    if (bypassed) return;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;

    // Save dry signal
    dryBuffer_.makeCopyOf(buffer, true);

    // Process based on active mode
    switch (mode) {
        case SpaceMode::Room:
        case SpaceMode::Hall:
        case SpaceMode::Plate:
            processReverbMode(buffer);
            break;
        case SpaceMode::Shimmer:
            processShimmer(buffer);
            break;
        case SpaceMode::Widener:
            processWidener(buffer);
            break;
    }

    // Apply wet filters
    applyWetFilters(buffer);

    // Dry/wet mix
    applyDryWetMix(buffer);
}

void SpaceModule::processReverbMode(juce::AudioBuffer<float>& buffer) {
    // Ensure reverb has correct parameters for the current mode
    setReverbPreset(mode);

    juce::dsp::AudioBlock<float> block(buffer);
    reverb.process(juce::dsp::ProcessContextReplacing<float>(block));
}

void SpaceModule::processShimmer(juce::AudioBuffer<float>& buffer) {
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    // Update pitch ratio from shift parameter
    shimmerPitchRatio = std::pow(2.0f, shimmerShift / 12.0f);

    // --- 1. Reverb pass on wet buffer ---
    setReverbPreset(SpaceMode::Plate); // plate-like reverb for shimmer
    wetBuffer_.makeCopyOf(buffer, true);
    {
        juce::dsp::AudioBlock<float> reverbBlock(wetBuffer_);
        reverb.process(juce::dsp::ProcessContextReplacing<float>(reverbBlock));
    }

    // --- 2. Pitch-shifted feedback + combine output ---
    // Process mono-summed pitch shifter state, apply stereo reverb output
    for (int s = 0; s < numSamples; ++s) {
        // --- Pitch shifter on mono-summed reverb ---
        float monoRev = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            monoRev += wetBuffer_.getSample(ch, s);
        monoRev /= static_cast<float>(numCh);

        // Write reverb output into shimmer delay buffer
        shimmerBuf[shimmerWritePos] = monoRev;

        // Read from primary head with linear interpolation
        const int ri1 = static_cast<int>(shimmerReadPos) % shimmerBufSize;
        const float frac1 = shimmerReadPos - static_cast<float>(ri1);
        const int ri1b = (ri1 + 1) % shimmerBufSize;
        const float s1 = shimmerBuf[ri1] * (1.0f - frac1) + shimmerBuf[ri1b] * frac1;

        float s2Val = 0.0f;
        if (shimmerUseSecondHead) {
            const int ri2 = static_cast<int>(shimmerReadPos2) % shimmerBufSize;
            const float frac2 = shimmerReadPos2 - static_cast<float>(ri2);
            const int ri2b = (ri2 + 1) % shimmerBufSize;
            s2Val = shimmerBuf[ri2] * (1.0f - frac2) + shimmerBuf[ri2b] * frac2;
        }

        // Crossfade blend between two read heads
        const float fadePos = shimmerCrossfadePos / shimmerCrossfadeLen;
        const float pitched = shimmerUseSecondHead
            ? s1 * (1.0f - fadePos) + s2Val * fadePos
            : s1;

        // Advance shimmer state
        shimmerWritePos = (shimmerWritePos + 1) % shimmerBufSize;
        shimmerReadPos += shimmerPitchRatio;
        if (shimmerReadPos >= static_cast<float>(shimmerBufSize))
            shimmerReadPos -= static_cast<float>(shimmerBufSize);

        if (shimmerUseSecondHead) {
            shimmerReadPos2 += shimmerPitchRatio;
            if (shimmerReadPos2 >= static_cast<float>(shimmerBufSize))
                shimmerReadPos2 -= static_cast<float>(shimmerBufSize);
        }

        shimmerCrossfadePos += shimmerPitchRatio;
        if (shimmerCrossfadePos >= shimmerCrossfadeLen) {
            shimmerCrossfadePos = 0.0f;
            // Swap heads: second head becomes primary
            shimmerReadPos = shimmerReadPos2;
            // Place new second head safely behind write position
            shimmerReadPos2 = static_cast<float>(shimmerWritePos)
                - static_cast<float>(shimmerBufSize) * 0.125f;
            if (shimmerReadPos2 < 0.0f)
                shimmerReadPos2 += static_cast<float>(shimmerBufSize);
        }

        // Emergency reset when primary head catches write head
        {
            float dist = static_cast<float>(shimmerWritePos) - shimmerReadPos;
            if (dist < 0.0f) dist += static_cast<float>(shimmerBufSize);
            if (dist < shimmerCrossfadeLen && !shimmerUseSecondHead) {
                shimmerUseSecondHead = true;
                shimmerCrossfadePos = 0.0f;
                shimmerReadPos2 = shimmerReadPos;
                shimmerReadPos = static_cast<float>(shimmerWritePos)
                    - static_cast<float>(shimmerBufSize) * 0.125f;
                if (shimmerReadPos < 0.0f)
                    shimmerReadPos += static_cast<float>(shimmerBufSize);
            }
        }

        // Combine reverb + pitched feedback into each channel
        for (int ch = 0; ch < numCh; ++ch) {
            const float revSample = wetBuffer_.getSample(ch, s);
            buffer.setSample(ch, s, revSample + pitched * shimmerFeedback);
        }
    }
}

void SpaceModule::processWidener(juce::AudioBuffer<float>& buffer) {
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    // Mono passthrough
    if (numCh < 2) return;

    // Width factor: 0.0 = mono, 0.5 = original stereo, 1.0 = 2x width
    const float widthFactor = widenerWidth * 2.0f;
    if (widthFactor == 1.0f && mixVal >= 1.0f) return;

    for (int s = 0; s < numSamples; ++s) {
        const float L = buffer.getSample(0, s);
        const float R = buffer.getSample(1, s);

        // M/S encoding
        const float mid  = (L + R) * 0.5f;
        const float side = (L - R) * 0.5f;

        // Decode with width
        const float wetL = mid + side * widthFactor;
        const float wetR = mid - side * widthFactor;

        // Write result directly (dry/wet mix happens in applyDryWetMix)
        buffer.setSample(0, s, wetL);
        buffer.setSample(1, s, wetR);
    }
}

void SpaceModule::applyWetFilters(juce::AudioBuffer<float>& buffer) {
    juce::dsp::AudioBlock<float> block(buffer);
    const auto ctx = juce::dsp::ProcessContextReplacing<float>(block);
    wetHPF.process(ctx);
    wetLPF.process(ctx);
}

void SpaceModule::applyDryWetMix(juce::AudioBuffer<float>& buffer) {
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    for (int ch = 0; ch < numCh; ++ch) {
        const auto* dryData = dryBuffer_.getReadPointer(ch);
        auto* outData = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
            outData[s] = dryData[s] * (1.0f - mixVal) + outData[s] * mixVal;
    }
}

void SpaceModule::setReverbPreset(SpaceMode m) {
    juce::dsp::Reverb::Parameters params;
    params.dryLevel = 0.0f;   // we handle dry/wet externally
    switch (m) {
        case SpaceMode::Room:
            params.roomSize = 0.5f;
            params.damping  = 0.6f;
            params.wetLevel = 1.0f;
            params.width    = 0.5f;
            break;
        case SpaceMode::Hall:
            params.roomSize = 0.9f;
            params.damping  = 0.3f;
            params.wetLevel = 1.0f;
            params.width    = 0.8f;
            break;
        case SpaceMode::Plate:
            params.roomSize = 0.7f;
            params.damping  = 0.2f;
            params.wetLevel = 1.0f;
            params.width    = 0.9f;
            break;
        default:
            params.roomSize = 0.7f;
            params.damping  = 0.3f;
            params.wetLevel = 1.0f;
            params.width    = 0.7f;
            break;
    }
    // Override with user params if set explicitly
    params.roomSize = reverbSize;
    params.damping  = reverbDamping;
    params.width    = reverbWidth;
    params.wetLevel = 1.0f;
    reverb.setParameters(params);
}

// ============================================================================
// Setters — all clamped via jlimit
// ============================================================================

void SpaceModule::setMode(SpaceMode m) {
    if (mode != m) {
        mode = m;
        reset();
        setReverbPreset(m);
    }
}

void SpaceModule::setReverbSize(float v)    { reverbSize    = juce::jlimit(0.0f, 1.0f, v); }
void SpaceModule::setReverbDamping(float v) { reverbDamping = juce::jlimit(0.0f, 1.0f, v); }
void SpaceModule::setReverbWidth(float v)   { reverbWidth   = juce::jlimit(0.0f, 1.0f, v); }

void SpaceModule::setShimmerShift(float semitones) {
    shimmerShift = juce::jlimit(-24.0f, 24.0f, semitones);
    shimmerPitchRatio = std::pow(2.0f, shimmerShift / 12.0f);
}

void SpaceModule::setShimmerFeedback(float v) { shimmerFeedback = juce::jlimit(0.0f, 0.95f, v); }
void SpaceModule::setWidenerWidth(float v)    { widenerWidth    = juce::jlimit(0.0f, 1.0f, v); }

void SpaceModule::setMix(float wet)           { mixVal  = juce::jlimit(0.0f, 1.0f, wet); }
void SpaceModule::setBypass(bool b)           { bypassed = b; }

void SpaceModule::setWetHPF(float hz) {
    hz = juce::jlimit(20.0f, 20000.0f, hz);
    *wetHPF.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, hz);
}

void SpaceModule::setWetLPF(float hz) {
    hz = juce::jlimit(20.0f, 20000.0f, hz);
    *wetLPF.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, hz);
}

// ============================================================================
// State serialization
// ============================================================================

juce::ValueTree SpaceModule::getState() const {
    juce::ValueTree tree("SpaceModule");
    tree.setProperty("mode",            static_cast<int>(mode), nullptr);
    tree.setProperty("reverbSize",      reverbSize,     nullptr);
    tree.setProperty("reverbDamping",   reverbDamping,  nullptr);
    tree.setProperty("reverbWidth",     reverbWidth,    nullptr);
    tree.setProperty("shimmerShift",    shimmerShift,   nullptr);
    tree.setProperty("shimmerFeedback", shimmerFeedback, nullptr);
    tree.setProperty("widenerWidth",    widenerWidth,   nullptr);
    tree.setProperty("mix",             mixVal,         nullptr);
    tree.setProperty("bypass",          bypassed,       nullptr);
    return tree;
}

void SpaceModule::setState(const juce::ValueTree& tree) {
    // All delegating to jlimit-guarded setters
    setMode(static_cast<SpaceMode>(juce::jlimit(0, 4, static_cast<int>(tree.getProperty("mode", 0)))));
    setReverbSize(tree.getProperty("reverbSize", 0.5f));
    setReverbDamping(tree.getProperty("reverbDamping", 0.5f));
    setReverbWidth(tree.getProperty("reverbWidth", 0.5f));
    setShimmerShift(tree.getProperty("shimmerShift", 12.0f));
    setShimmerFeedback(tree.getProperty("shimmerFeedback", 0.4f));
    setWidenerWidth(tree.getProperty("widenerWidth", 0.5f));
    setMix(tree.getProperty("mix", 1.0f));
    setBypass(tree.getProperty("bypass", false));
}

} // namespace ana
