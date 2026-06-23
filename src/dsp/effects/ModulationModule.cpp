#include "ModulationModule.h"
#include <cmath>

namespace ana {

ModulationModule::ModulationModule() {}

void ModulationModule::prepare(const juce::dsp::ProcessSpec& spec) {
    sampleRate = spec.sampleRate;
    numChannels = static_cast<int>(spec.numChannels);

    // --- Chorus ---
    chorus.prepare(spec);

    // --- Flanger ---
    flangerDelayLines.resize(numChannels);
    for (auto& dl : flangerDelayLines)
        dl.prepare(spec);
    flangerPhase.resize(numChannels, 0.0f);
    for (int ch = 1; ch < numChannels; ++ch)
        flangerPhase[ch] = 0.25f; // 90-degree phase offset for stereo

    // --- Phaser ---
    phaserSV1.resize(numChannels * phaserMaxStages, 0.0f);
    phaserSV2.resize(numChannels * phaserMaxStages, 0.0f);
    phaserCoeffB0.resize(phaserMaxStages, 1.0f);
    phaserCoeffB1.resize(phaserMaxStages, 0.0f);
    phaserCoeffA1.resize(phaserMaxStages, 0.0f);
    phaserCoeffA2.resize(phaserMaxStages, 0.0f);
    phaserLfoPhase.resize(numChannels, 0.0f);
    phaserFbOut.resize(numChannels, 0.0f);
    phaserUpdateCounter = 0;

    // --- Wet filters ---
    *wetHPF.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 20.0f);
    *wetLPF.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 20000.0f);
    wetHPF.prepare(spec);
    wetLPF.prepare(spec);

    // --- Pre-allocated buffers ---
    dryBuffer_.setSize(numChannels, static_cast<int>(spec.maximumBlockSize), false, false, true);
    wetBuffer_.setSize(numChannels, static_cast<int>(spec.maximumBlockSize), false, false, true);
}

void ModulationModule::reset() {
    chorus.reset();

    for (auto& dl : flangerDelayLines)
        dl.reset();
    std::fill(flangerPhase.begin(), flangerPhase.end(), 0.0f);

    std::fill(phaserSV1.begin(), phaserSV1.end(), 0.0f);
    std::fill(phaserSV2.begin(), phaserSV2.end(), 0.0f);
    std::fill(phaserLfoPhase.begin(), phaserLfoPhase.end(), 0.0f);
    std::fill(phaserFbOut.begin(), phaserFbOut.end(), 0.0f);
    phaserUpdateCounter = 0;

    wetHPF.reset();
    wetLPF.reset();
}

void ModulationModule::process(juce::AudioBuffer<float>& buffer) {
    if (bypassed) return;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;

    // Save dry signal
    dryBuffer_.makeCopyOf(buffer, true);

    // Process based on active mode
    switch (mode) {
        case ModulationMode::Chorus:  processChorus(buffer);  break;
        case ModulationMode::Flanger: processFlanger(buffer); break;
        case ModulationMode::Phaser:  processPhaser(buffer);  break;
    }

    // Apply wet filters
    applyWetFilters(buffer);

    // Dry/wet mix
    applyDryWetMix(buffer);
}

void ModulationModule::processChorus(juce::AudioBuffer<float>& buffer) {
    chorus.setRate(chorusRate);
    chorus.setDepth(chorusDepth);
    chorus.setCentreDelay(chorusCentreDelay);
    chorus.setFeedback(0.0f);    // clean chorus — no internal feedback
    chorus.setMix(1.0f);          // full wet, module handles dry/wet

    juce::dsp::AudioBlock<float> block(buffer);
    chorus.process(juce::dsp::ProcessContextReplacing<float>(block));
}

void ModulationModule::processFlanger(juce::AudioBuffer<float>& buffer) {
    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin(numChannels, buffer.getNumChannels());

    const float rateNorm = flangerRate / static_cast<float>(sampleRate);
    const float baseDelaySamples = flangerDelay * static_cast<float>(sampleRate) / 1000.0f;
    // Maximum modulation depth in samples (~10ms max)
    const float maxModSamples = 10.0f * static_cast<float>(sampleRate) / 1000.0f;

    for (int ch = 0; ch < numCh; ++ch) {
        auto* data = buffer.getWritePointer(ch);
        float& phase = flangerPhase[ch];

        for (int s = 0; s < numSamples; ++s) {
            // Advance LFO
            phase += rateNorm;
            if (phase >= 1.0f) phase -= 1.0f;

            const float lfo = std::sin(2.0f * juce::MathConstants<float>::pi * phase);

            // Modulated delay in samples
            const float modDelay = baseDelaySamples + lfo * flangerDepth * maxModSamples;
            const float clampedDelay = std::max(1.0f, modDelay);

            // Comb filter: read delayed sample, push with feedback
            const float input = data[s];
            const float delayed = flangerDelayLines[ch].popSample(ch, clampedDelay);
            flangerDelayLines[ch].pushSample(ch, input + delayed * flangerFeedback);

            // Write flanger wet signal to buffer (100% wet — dry/wet mix handled by applyDryWetMix)
            data[s] = delayed;
        }
    }
}

void ModulationModule::processPhaser(juce::AudioBuffer<float>& buffer) {
    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin(numChannels, buffer.getNumChannels());

    const float rateNorm = phaserRate / static_cast<float>(sampleRate);
    const float twoPi = 2.0f * juce::MathConstants<float>::pi;
    const float baseFreq = 800.0f;
    const float sr = static_cast<float>(sampleRate);
    const float nyquist = sr * 0.45f;
    const float qInv = 0.707f;

    for (int ch = 0; ch < numCh; ++ch) {
        auto* data = buffer.getWritePointer(ch);
        const int chBase = ch * phaserMaxStages;
        float& lfoPh = phaserLfoPhase[ch];

        for (int s = 0; s < numSamples; ++s) {
            // Advance LFO
            lfoPh += rateNorm;
            if (lfoPh >= 1.0f) lfoPh -= 1.0f;

            const float lfo = std::sin(twoPi * lfoPh);
            const float halfSteps = phaserDepth * lfo * 2.0f;

            // Sub-sample coefficient update (every 8 samples)
            if (++phaserUpdateCounter >= 8) {
                phaserUpdateCounter = 0;

                const float baseModFreq = juce::jlimit(20.0f, nyquist,
                    baseFreq * std::pow(2.0f, halfSteps));

                for (int i = 0; i < phaserStages; ++i) {
                    const float stageFreqMult = 1.0f + static_cast<float>(i) * 0.05f;
                    const float freq = juce::jlimit(20.0f, nyquist, baseModFreq * stageFreqMult);

                    const float w0 = twoPi * freq / sr;
                    const float cosW0 = std::cos(w0);
                    const float sinW0 = std::sin(w0);
                    const float alpha = sinW0 * qInv;

                    const float a0 = 1.0f + alpha;
                    const float invA0 = 1.0f / a0;
                    const float a1Num = -2.0f * cosW0;
                    const float a2Num = 1.0f - alpha;

                    // All-pass TDF2 coefficients
                    phaserCoeffB0[i] = a2Num * invA0;
                    phaserCoeffB1[i] = a1Num * invA0;
                    phaserCoeffA1[i] = a1Num * invA0;
                    phaserCoeffA2[i] = a2Num * invA0;
                }
            }

            // All-pass chain (TDF2)
            float x = data[s] + phaserFbOut[ch] * phaserFeedback;
            float* sv1 = phaserSV1.data() + chBase;
            float* sv2 = phaserSV2.data() + chBase;

            for (int i = 0; i < phaserStages; ++i) {
                const float y = phaserCoeffB0[i] * x + sv1[i];
                sv1[i] = phaserCoeffB1[i] * x - phaserCoeffA1[i] * y + sv2[i];
                sv2[i] = x - phaserCoeffA2[i] * y; // b2 = 1.0 for all-pass
                x = y;
            }

            phaserFbOut[ch] = x + 1e-30f;
            data[s] = x;
        }
    }

    // Note: applyDryWetMix will handle mixing, so we keep the wet signal in buffer
}

void ModulationModule::applyWetFilters(juce::AudioBuffer<float>& buffer) {
    juce::dsp::AudioBlock<float> block(buffer);
    const auto ctx = juce::dsp::ProcessContextReplacing<float>(block);
    wetHPF.process(ctx);
    wetLPF.process(ctx);
}

void ModulationModule::applyDryWetMix(juce::AudioBuffer<float>& buffer) {
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    for (int ch = 0; ch < numCh; ++ch) {
        const auto* dryData = dryBuffer_.getReadPointer(ch);
        auto* outData = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
            outData[s] = dryData[s] * (1.0f - mixVal) + outData[s] * mixVal;
    }
}

// ============================================================================
// Setters — all clamped via jlimit
// ============================================================================

void ModulationModule::setMode(ModulationMode m) {
    if (mode != m) {
        mode = m;
        reset();
    }
}

void ModulationModule::setChorusRate(float hz)        { chorusRate        = juce::jlimit(0.001f, 20.0f, hz); }
void ModulationModule::setChorusDepth(float v)         { chorusDepth       = juce::jlimit(0.0f, 1.0f, v); }
void ModulationModule::setChorusCentreDelay(float ms)  { chorusCentreDelay = juce::jlimit(0.1f, 100.0f, ms); }

void ModulationModule::setFlangerRate(float hz)        { flangerRate     = juce::jlimit(0.1f, 10.0f, hz); }
void ModulationModule::setFlangerDepth(float v)        { flangerDepth    = juce::jlimit(0.0f, 1.0f, v); }
void ModulationModule::setFlangerFeedback(float v)     { flangerFeedback = juce::jlimit(0.0f, 1.0f, v); }
void ModulationModule::setFlangerDelay(float ms)       { flangerDelay    = juce::jlimit(0.1f, 10.0f, ms); }

void ModulationModule::setPhaserRate(float hz)         { phaserRate     = juce::jlimit(0.1f, 20.0f, hz); }
void ModulationModule::setPhaserDepth(float v)         { phaserDepth    = juce::jlimit(0.0f, 1.0f, v); }
void ModulationModule::setPhaserFeedback(float v)      { phaserFeedback = juce::jlimit(0.0f, 1.0f, v); }
void ModulationModule::setPhaserStages(int stages)     { phaserStages   = juce::jlimit(2, phaserMaxStages, stages); }

void ModulationModule::setMix(float wet)               { mixVal  = juce::jlimit(0.0f, 1.0f, wet); }
void ModulationModule::setBypass(bool b)               { bypassed = b; }

void ModulationModule::setWetHPF(float hz) {
    hz = juce::jlimit(20.0f, 20000.0f, hz);
    *wetHPF.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, hz);
}

void ModulationModule::setWetLPF(float hz) {
    hz = juce::jlimit(20.0f, 20000.0f, hz);
    *wetLPF.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, hz);
}

// ============================================================================
// State serialization
// ============================================================================

juce::ValueTree ModulationModule::getState() const {
    juce::ValueTree tree("ModulationModule");
    tree.setProperty("mode",               static_cast<int>(mode), nullptr);
    tree.setProperty("chorusRate",         chorusRate,        nullptr);
    tree.setProperty("chorusDepth",        chorusDepth,       nullptr);
    tree.setProperty("chorusCentreDelay",  chorusCentreDelay, nullptr);
    tree.setProperty("flangerRate",        flangerRate,       nullptr);
    tree.setProperty("flangerDepth",       flangerDepth,      nullptr);
    tree.setProperty("flangerFeedback",    flangerFeedback,   nullptr);
    tree.setProperty("flangerDelay",       flangerDelay,      nullptr);
    tree.setProperty("phaserRate",         phaserRate,        nullptr);
    tree.setProperty("phaserDepth",        phaserDepth,       nullptr);
    tree.setProperty("phaserFeedback",     phaserFeedback,    nullptr);
    tree.setProperty("phaserStages",       phaserStages,      nullptr);
    tree.setProperty("mix",                mixVal,            nullptr);
    tree.setProperty("bypass",             bypassed,          nullptr);
    return tree;
}

void ModulationModule::setState(const juce::ValueTree& tree) {
    // All delegating to jlimit-guarded setters
    setMode(static_cast<ModulationMode>(juce::jlimit(0, 2, static_cast<int>(tree.getProperty("mode", 0)))));
    setChorusRate(tree.getProperty("chorusRate", 1.0f));
    setChorusDepth(tree.getProperty("chorusDepth", 0.5f));
    setChorusCentreDelay(tree.getProperty("chorusCentreDelay", 10.0f));
    setFlangerRate(tree.getProperty("flangerRate", 0.5f));
    setFlangerDepth(tree.getProperty("flangerDepth", 0.5f));
    setFlangerFeedback(tree.getProperty("flangerFeedback", 0.3f));
    setFlangerDelay(tree.getProperty("flangerDelay", 3.0f));
    setPhaserRate(tree.getProperty("phaserRate", 1.0f));
    setPhaserDepth(tree.getProperty("phaserDepth", 0.5f));
    setPhaserFeedback(tree.getProperty("phaserFeedback", 0.3f));
    setPhaserStages(tree.getProperty("phaserStages", 6));
    setMix(tree.getProperty("mix", 1.0f));
    setBypass(tree.getProperty("bypass", false));
}

} // namespace ana
