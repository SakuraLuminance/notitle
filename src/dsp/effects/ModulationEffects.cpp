#include "ModulationEffects.h"
#include <cmath>

namespace ana {

//==============================================================================
ModulationEffects::ModulationEffects()
{
    // Default stereo offset for AutoPan
    stereoOffsetDeg_ = 90.0f;
}

//==============================================================================
void ModulationEffects::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate_   = spec.sampleRate;
    numChannels_  = static_cast<int>(spec.numChannels);

    // --- Per-channel LFO phase ---
    lfoPhase_.resize(numChannels_, 0.0f);

    // --- Wet filters (prepare BEFORE setting coefficients) ---
    wetHPF_.prepare(spec);
    wetLPF_.prepare(spec);
    *wetHPF_.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate_, 20.0f);
    *wetLPF_.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate_, 20000.0f);

    // --- Dry buffer ---
    dryBuffer_.setSize(numChannels_,
                       static_cast<int>(spec.maximumBlockSize),
                       false, false, true);
}

//==============================================================================
void ModulationEffects::reset()
{
    std::fill(lfoPhase_.begin(), lfoPhase_.end(), 0.0f);
    wetHPF_.reset();
    wetLPF_.reset();
}

//==============================================================================
void ModulationEffects::process(juce::AudioBuffer<float>& buffer)
{
    if (bypassed_)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numCh      = buffer.getNumChannels();
    if (numSamples == 0 || numCh == 0)
        return;

    // Save dry signal for wet/dry blend (only when mix < 1)
    const bool needDry = (mixVal_ < 1.0f);
    if (needDry)
        dryBuffer_.makeCopyOf(buffer, true);

    // Dispatch to active mode
    switch (mode_)
    {
        case TremoloMode::Tremolo: processTremolo(buffer); break;
        case TremoloMode::AutoPan:  processAutoPan(buffer);  break;
    }

    // Apply wet filters
    applyWetFilters(buffer);

    // Dry / Wet mix
    if (needDry)
        applyDryWetMix(buffer);
}

//==============================================================================
void ModulationEffects::processTremolo(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh      = juce::jmin(numChannels_, buffer.getNumChannels());

    const float phaseInc = rateHz_ / static_cast<float>(sampleRate_);
    const float depth    = depth01_;

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        float phase = lfoPhase_[static_cast<size_t>(ch)];

        for (int s = 0; s < numSamples; ++s)
        {
            // Advance LFO
            phase += phaseInc;
            if (phase >= 1.0f)
                phase -= 1.0f;

            // LFO value in [-1, 1]
            const float lfo = lfoGen(phase);

            // Tremolo gain: 1 - depth * (0.5 + 0.5 * lfo)
            //   lfo = -1 → gain = 1 - depth * 0 = 1  (no attenuation)
            //   lfo = +1 → gain = 1 - depth * 1 = 1 - depth
            const float gain = 1.0f - depth * (0.5f + 0.5f * lfo);
            data[s] *= gain;
        }

        lfoPhase_[static_cast<size_t>(ch)] = phase;
    }
}

//==============================================================================
void ModulationEffects::processAutoPan(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh      = juce::jmin(numChannels_, buffer.getNumChannels());

    const float phaseInc    = rateHz_ / static_cast<float>(sampleRate_);
    const float depth       = depth01_;
    const float offsetFrac  = stereoOffsetDeg_ / 360.0f;  // [0, 0.5]

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        float phase = lfoPhase_[static_cast<size_t>(ch)];

        // Stereo offset: ch 0 = left (no offset), ch 1 = right (+offset),
        // higher channels alternate
        const float panPhaseOffset = (ch % 2 == 0) ? 0.0f : offsetFrac;

        for (int s = 0; s < numSamples; ++s)
        {
            // Advance LFO
            phase += phaseInc;
            if (phase >= 1.0f)
                phase -= 1.0f;

            // Phase-shifted LFO for this channel
            float lfoPhase = phase + panPhaseOffset;
            if (lfoPhase >= 1.0f)
                lfoPhase -= 1.0f;

            const float lfo = lfoGen(lfoPhase);

            // AutoPan gain: 1 - depth * 0.5 * (1 + lfo)
            //   lfo = -1 → gain = 1 - depth * 0 = 1
            //   lfo = +1 → gain = 1 - depth * 1 = 1 - depth
            const float gain = 1.0f - depth * 0.5f * (1.0f + lfo);
            data[s] *= gain;
        }

        lfoPhase_[static_cast<size_t>(ch)] = phase;
    }
}

//==============================================================================
float ModulationEffects::lfoGen(float phase) const
{
    switch (shape_)
    {
        case TremoloShape::Sine:
            return std::sin(2.0f * juce::MathConstants<float>::pi * phase);

        case TremoloShape::Triangle:
            // 4*abs(phase-0.5) - 1  →  [-1, 1] triangle wave
            return 4.0f * std::abs(phase - 0.5f) - 1.0f;

        case TremoloShape::Square:
            return (phase > 0.5f) ? 1.0f : -1.0f;

        default:
            return 0.0f;
    }
}

//==============================================================================
void ModulationEffects::applyWetFilters(juce::AudioBuffer<float>& buffer)
{
    juce::dsp::AudioBlock<float> block(buffer);
    const auto ctx = juce::dsp::ProcessContextReplacing<float>(block);
    wetHPF_.process(ctx);
    wetLPF_.process(ctx);
}

//==============================================================================
void ModulationEffects::applyDryWetMix(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh      = buffer.getNumChannels();

    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto* dryData = dryBuffer_.getReadPointer(ch);
        auto*       outData = buffer.getWritePointer(ch);

        for (int s = 0; s < numSamples; ++s)
            outData[s] = dryData[s] * (1.0f - mixVal_) + outData[s] * mixVal_;
    }
}

//==============================================================================
// Parameter setters
//==============================================================================

void ModulationEffects::setMode(TremoloMode m)
{
    if (mode_ != m)
    {
        mode_ = m;
        reset();
    }
}

void ModulationEffects::setRate(float hz)
{
    rateHz_ = juce::jlimit(0.1f, 20.0f, hz);
}

void ModulationEffects::setDepth(float d01)
{
    depth01_ = juce::jlimit(0.0f, 1.0f, d01);
}

void ModulationEffects::setShape(TremoloShape s)
{
    shape_ = s;
}

void ModulationEffects::setStereoOffset(float degrees)
{
    stereoOffsetDeg_ = juce::jlimit(0.0f, 180.0f, degrees);
}

void ModulationEffects::setMix(float wet)
{
    mixVal_ = juce::jlimit(0.0f, 1.0f, wet);
}

void ModulationEffects::setBypass(bool b)
{
    bypassed_ = b;
}

void ModulationEffects::setWetHPF(float hz)
{
    hz = juce::jlimit(20.0f, 20000.0f, hz);
    *wetHPF_.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate_, hz);
}

void ModulationEffects::setWetLPF(float hz)
{
    hz = juce::jlimit(20.0f, 20000.0f, hz);
    *wetLPF_.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate_, hz);
}

//==============================================================================
// State serialization
//==============================================================================

juce::ValueTree ModulationEffects::getState() const
{
    juce::ValueTree tree("ModulationEffects");
    tree.setProperty("mode",             static_cast<int>(mode_), nullptr);
    tree.setProperty("rate",             static_cast<double>(rateHz_), nullptr);
    tree.setProperty("depth",            static_cast<double>(depth01_), nullptr);
    tree.setProperty("shape",            static_cast<int>(shape_), nullptr);
    tree.setProperty("stereoOffset",     static_cast<double>(stereoOffsetDeg_), nullptr);
    tree.setProperty("mix",              static_cast<double>(mixVal_), nullptr);
    tree.setProperty("bypass",           bypassed_, nullptr);
    return tree;
}

void ModulationEffects::setState(const juce::ValueTree& tree)
{
    setMode(static_cast<TremoloMode>(
        juce::jlimit(0, 1, static_cast<int>(tree.getProperty("mode", 0)))));
    setRate(static_cast<float>(tree.getProperty("rate", 5.0)));
    setDepth(static_cast<float>(tree.getProperty("depth", 0.5)));
    setShape(static_cast<TremoloShape>(
        juce::jlimit(0, 2, static_cast<int>(tree.getProperty("shape", 0)))));
    setStereoOffset(static_cast<float>(tree.getProperty("stereoOffset", 90.0)));
    setMix(static_cast<float>(tree.getProperty("mix", 1.0)));
    setBypass(tree.getProperty("bypass", false));
}

//==============================================================================
// Factory presets
//==============================================================================

juce::StringArray ModulationEffects::getFactoryPresets()
{
    return juce::StringArray({ "Soft Tremolo", "Hard Tremolo", "Slow Pan", "Fast Pan" });
}

juce::ValueTree ModulationEffects::getFactoryPreset(const juce::String& name)
{
    juce::ValueTree tree("ModulationEffects");

    if (name == "Soft Tremolo")
    {
        tree.setProperty("mode",         static_cast<int>(TremoloMode::Tremolo), nullptr);
        tree.setProperty("rate",         4.0,  nullptr);
        tree.setProperty("depth",        0.3,  nullptr);
        tree.setProperty("shape",        static_cast<int>(TremoloShape::Sine), nullptr);
        tree.setProperty("stereoOffset", 0.0,  nullptr);
        tree.setProperty("mix",          1.0,  nullptr);
        tree.setProperty("bypass",       false, nullptr);
    }
    else if (name == "Hard Tremolo")
    {
        tree.setProperty("mode",         static_cast<int>(TremoloMode::Tremolo), nullptr);
        tree.setProperty("rate",         8.0,  nullptr);
        tree.setProperty("depth",        0.7,  nullptr);
        tree.setProperty("shape",        static_cast<int>(TremoloShape::Square), nullptr);
        tree.setProperty("stereoOffset", 0.0,  nullptr);
        tree.setProperty("mix",          1.0,  nullptr);
        tree.setProperty("bypass",       false, nullptr);
    }
    else if (name == "Slow Pan")
    {
        tree.setProperty("mode",         static_cast<int>(TremoloMode::AutoPan), nullptr);
        tree.setProperty("rate",         0.3,  nullptr);
        tree.setProperty("depth",        0.8,  nullptr);
        tree.setProperty("shape",        static_cast<int>(TremoloShape::Sine), nullptr);
        tree.setProperty("stereoOffset", 90.0, nullptr);
        tree.setProperty("mix",          1.0,  nullptr);
        tree.setProperty("bypass",       false, nullptr);
    }
    else if (name == "Fast Pan")
    {
        tree.setProperty("mode",         static_cast<int>(TremoloMode::AutoPan), nullptr);
        tree.setProperty("rate",         5.0,  nullptr);
        tree.setProperty("depth",        0.6,  nullptr);
        tree.setProperty("shape",        static_cast<int>(TremoloShape::Triangle), nullptr);
        tree.setProperty("stereoOffset", 90.0, nullptr);
        tree.setProperty("mix",          1.0,  nullptr);
        tree.setProperty("bypass",       false, nullptr);
    }

    return tree;
}

} // namespace ana
