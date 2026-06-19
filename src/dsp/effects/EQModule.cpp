#include "EQModule.h"
#include <cmath>

namespace ana {

EQModule::EQModule()
{
    // Default para bands
    for (int i = 0; i < 5; ++i)
    {
        paraBands_[i].frequency = 1000.0f;
        paraBands_[i].gain = 0.0f;
        paraBands_[i].q = 0.707f;
        paraBands_[i].type = EQBandType::Peaking;
    }
}

//==============================================================================
void EQModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;

    for (auto& f : filters_)
        f.prepare(spec);

    wetHPF_.prepare(spec);
    wetLPF_.prepare(spec);

    auto hpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(spec.sampleRate, wetLowCut_, 0.707);
    auto lpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(spec.sampleRate, wetHighCut_, 0.707);
    *wetHPF_.state = *hpfCoeffs;
    *wetLPF_.state = *lpfCoeffs;

    dryBuffer_.setSize(static_cast<int>(spec.numChannels),
                       static_cast<int>(spec.maximumBlockSize),
                       false, false, true);

    configureFilters();
}

void EQModule::reset()
{
    for (auto& f : filters_)
        f.reset();
    wetHPF_.reset();
    wetLPF_.reset();
}

void EQModule::process(juce::AudioBuffer<float>& buffer)
{
    const auto numSamples  = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    const bool needDry     = mix_ < 1.0f;
    const bool needFilter  = wetLowCut_ > 20.0f || wetHighCut_ < 20000.0f;

    // Capture dry signal
    if (needDry || needFilter)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            dryBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
    }

    // Update and apply EQ filters
    configureFilters();

    juce::dsp::AudioBlock<float> block(buffer);
    for (int i = 0; i < activeFilters_; ++i)
        filters_[i].process(juce::dsp::ProcessContextReplacing<float>(block));

    // Apply wet HPF/LPF and blend
    if (needDry || needFilter)
    {
        if (needFilter)
        {
            juce::dsp::AudioBlock<float> wetBlock(buffer);
            juce::dsp::ProcessContextReplacing<float> ctx(wetBlock);
            wetHPF_.process(ctx);
            wetLPF_.process(ctx);
        }

        if (needDry)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* dst = buffer.getWritePointer(ch);
                const auto* dry = dryBuffer_.getReadPointer(ch);
                for (int s = 0; s < numSamples; ++s)
                    dst[s] = dry[s] * (1.0f - mix_) + dst[s] * mix_;
            }
        }
    }
}

//==============================================================================
// Mode switching
void EQModule::setMode(EQMode mode)
{
    mode_ = mode;
    switch (mode)
    {
        case EQMode::Band3: activeFilters_ = 3; break;
        case EQMode::Band5: activeFilters_ = 5; break;
        case EQMode::Tilt:  activeFilters_ = 2; break;
        case EQMode::Para:  activeFilters_ = 5; break;
    }
    configureFilters();
}

//==============================================================================
// 3-Band setters
void EQModule::setLowGain(float db)  { lowGain_  = juce::jlimit(-24.0f, 24.0f, db); }
void EQModule::setMidGain(float db)  { midGain_  = juce::jlimit(-24.0f, 24.0f, db); }
void EQModule::setHighGain(float db) { highGain_ = juce::jlimit(-24.0f, 24.0f, db); }
void EQModule::setMidFreq(float hz)  { midFreq_  = juce::jlimit(20.0f, 20000.0f, hz); }

//==============================================================================
// 5-Band setters
void EQModule::setSubGain(float db)   { subGain_   = juce::jlimit(-24.0f, 24.0f, db); }
void EQModule::setSubFreq(float hz)   { subFreq_   = juce::jlimit(20.0f, 20000.0f, hz); }
void EQModule::setLowGain5(float db)  { lowGain5_  = juce::jlimit(-24.0f, 24.0f, db); }
void EQModule::setLowFreq5(float hz)  { lowFreq5_  = juce::jlimit(20.0f, 20000.0f, hz); }
void EQModule::setMidGain5(float db)  { midGain5_  = juce::jlimit(-24.0f, 24.0f, db); }
void EQModule::setMidFreq5(float hz)  { midFreq5_  = juce::jlimit(20.0f, 20000.0f, hz); }
void EQModule::setHighGain5(float db) { highGain5_ = juce::jlimit(-24.0f, 24.0f, db); }
void EQModule::setHighFreq5(float hz) { highFreq5_ = juce::jlimit(20.0f, 20000.0f, hz); }
void EQModule::setAirGain(float db)   { airGain_   = juce::jlimit(-24.0f, 24.0f, db); }
void EQModule::setAirFreq(float hz)   { airFreq_   = juce::jlimit(20.0f, 20000.0f, hz); }

//==============================================================================
// Tilt setters
void EQModule::setTiltAmount(float db) { tiltAmount_ = juce::jlimit(-24.0f, 24.0f, db); }
void EQModule::setCenterFreq(float hz) { centerFreq_ = juce::jlimit(20.0f, 20000.0f, hz); }

//==============================================================================
// Para setters
void EQModule::setParaBand(int index, float freq, float gain, float q, EQBandType type)
{
    if (index < 0 || index >= 5) return;
    paraBands_[index].frequency = juce::jlimit(20.0f, 20000.0f, freq);
    paraBands_[index].gain      = juce::jlimit(-24.0f, 24.0f, gain);
    paraBands_[index].q         = juce::jlimit(0.1f, 10.0f, q);
    paraBands_[index].type      = type;
}

//==============================================================================
// Shared setters
void EQModule::setMix(float v)     { mix_        = juce::jlimit(0.0f, 1.0f, v); }
void EQModule::setWetLowCut(float hz)  { wetLowCut_  = juce::jlimit(10.0f, 20000.0f, hz); }
void EQModule::setWetHighCut(float hz) { wetHighCut_ = juce::jlimit(10.0f, 20000.0f, hz); }

//==============================================================================
void EQModule::configureFilters()
{
    switch (mode_)
    {
        case EQMode::Band3:
        {
            // 3-Band: LowShelf at 200Hz, Peaking at midFreq, HighShelf at 5kHz
            *filters_[0].coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
                sampleRate_, 200.0f, 0.707f, juce::Decibels::decibelsToGain(lowGain_));
            *filters_[1].coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                sampleRate_, midFreq_, 0.707f, juce::Decibels::decibelsToGain(midGain_));
            *filters_[2].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                sampleRate_, 5000.0f, 0.707f, juce::Decibels::decibelsToGain(highGain_));
            break;
        }

        case EQMode::Band5:
        {
            // 5-Band: LowShelf(sub), Peaking(low), Peaking(mid), HighShelf(high), HighShelf(air)
            *filters_[0].coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
                sampleRate_, subFreq_, 0.707f, juce::Decibels::decibelsToGain(subGain_));
            *filters_[1].coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                sampleRate_, lowFreq5_, 0.707f, juce::Decibels::decibelsToGain(lowGain5_));
            *filters_[2].coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                sampleRate_, midFreq5_, 0.707f, juce::Decibels::decibelsToGain(midGain5_));
            *filters_[3].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                sampleRate_, highFreq5_, 0.707f, juce::Decibels::decibelsToGain(highGain5_));
            *filters_[4].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                sampleRate_, airFreq_, 0.707f, juce::Decibels::decibelsToGain(airGain_));
            break;
        }

        case EQMode::Tilt:
        {
            // Tilt: LowShelf at centerFreq with gain = -tilt/2, HighShelf with gain = tilt/2
            float halfGain = tiltAmount_ * 0.5f;
            *filters_[0].coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
                sampleRate_, centerFreq_, 0.707f, juce::Decibels::decibelsToGain(-halfGain));
            *filters_[1].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                sampleRate_, centerFreq_, 0.707f, juce::Decibels::decibelsToGain(halfGain));
            break;
        }

        case EQMode::Para:
        {
            for (int i = 0; i < 5; ++i)
            {
                const auto& b = paraBands_[i];
                switch (b.type)
                {
                    case EQBandType::LowShelf:
                        *filters_[i].coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
                            sampleRate_, b.frequency, b.q, juce::Decibels::decibelsToGain(b.gain));
                        break;
                    case EQBandType::Peaking:
                        *filters_[i].coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                            sampleRate_, b.frequency, b.q, juce::Decibels::decibelsToGain(b.gain));
                        break;
                    case EQBandType::HighShelf:
                        *filters_[i].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                            sampleRate_, b.frequency, b.q, juce::Decibels::decibelsToGain(b.gain));
                        break;
                }
            }
            break;
        }
    }
}

//==============================================================================
juce::ValueTree EQModule::getState() const
{
    juce::ValueTree tree("EQModule");
    tree.setProperty("mode", static_cast<int>(mode_), nullptr);

    // 3-Band
    tree.setProperty("lowGain", lowGain_, nullptr);
    tree.setProperty("midGain", midGain_, nullptr);
    tree.setProperty("highGain", highGain_, nullptr);
    tree.setProperty("midFreq", midFreq_, nullptr);

    // 5-Band
    tree.setProperty("subGain", subGain_, nullptr);
    tree.setProperty("subFreq", subFreq_, nullptr);
    tree.setProperty("lowGain5", lowGain5_, nullptr);
    tree.setProperty("lowFreq5", lowFreq5_, nullptr);
    tree.setProperty("midGain5", midGain5_, nullptr);
    tree.setProperty("midFreq5", midFreq5_, nullptr);
    tree.setProperty("highGain5", highGain5_, nullptr);
    tree.setProperty("highFreq5", highFreq5_, nullptr);
    tree.setProperty("airGain", airGain_, nullptr);
    tree.setProperty("airFreq", airFreq_, nullptr);

    // Tilt
    tree.setProperty("tiltAmount", tiltAmount_, nullptr);
    tree.setProperty("centerFreq", centerFreq_, nullptr);

    // Para bands
    for (int i = 0; i < 5; ++i)
    {
        juce::ValueTree band("ParaBand");
        band.setProperty("index", i, nullptr);
        band.setProperty("frequency", paraBands_[i].frequency, nullptr);
        band.setProperty("gain", paraBands_[i].gain, nullptr);
        band.setProperty("q", paraBands_[i].q, nullptr);
        band.setProperty("type", static_cast<int>(paraBands_[i].type), nullptr);
        tree.addChild(band, -1, nullptr);
    }

    // Shared
    tree.setProperty("mix", mix_, nullptr);
    tree.setProperty("wetLowCut", wetLowCut_, nullptr);
    tree.setProperty("wetHighCut", wetHighCut_, nullptr);

    return tree;
}

void EQModule::setState(const juce::ValueTree& state)
{
    // Mode must be set first since it affects active filter count
    auto modeInt = static_cast<int>(state.getProperty("mode", static_cast<int>(EQMode::Band3)));
    setMode(static_cast<EQMode>(juce::jlimit(0, 3, modeInt)));

    // 3-Band
    setLowGain(state.getProperty("lowGain", 0.0f));
    setMidGain(state.getProperty("midGain", 0.0f));
    setHighGain(state.getProperty("highGain", 0.0f));
    setMidFreq(state.getProperty("midFreq", 1000.0f));

    // 5-Band
    setSubGain(state.getProperty("subGain", 0.0f));
    setSubFreq(state.getProperty("subFreq", 60.0f));
    setLowGain5(state.getProperty("lowGain5", 0.0f));
    setLowFreq5(state.getProperty("lowFreq5", 250.0f));
    setMidGain5(state.getProperty("midGain5", 0.0f));
    setMidFreq5(state.getProperty("midFreq5", 1000.0f));
    setHighGain5(state.getProperty("highGain5", 0.0f));
    setHighFreq5(state.getProperty("highFreq5", 5000.0f));
    setAirGain(state.getProperty("airGain", 0.0f));
    setAirFreq(state.getProperty("airFreq", 12000.0f));

    // Tilt
    setTiltAmount(state.getProperty("tiltAmount", 0.0f));
    setCenterFreq(state.getProperty("centerFreq", 1000.0f));

    // Para bands
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        auto band = state.getChild(i);
        int idx = band.getProperty("index", i);
        if (idx >= 0 && idx < 5)
        {
            paraBands_[idx].frequency = juce::jlimit(20.0f, 20000.0f,
                static_cast<float>(band.getProperty("frequency", 1000.0f)));
            paraBands_[idx].gain = juce::jlimit(-24.0f, 24.0f,
                static_cast<float>(band.getProperty("gain", 0.0f)));
            paraBands_[idx].q = juce::jlimit(0.1f, 10.0f,
                static_cast<float>(band.getProperty("q", 0.707f)));
            paraBands_[idx].type = static_cast<EQBandType>(juce::jlimit(0, 2,
                static_cast<int>(band.getProperty("type", static_cast<int>(EQBandType::Peaking)))));
        }
    }

    // Shared
    setMix(state.getProperty("mix", 1.0f));
    setWetLowCut(state.getProperty("wetLowCut", 20.0f));
    setWetHighCut(state.getProperty("wetHighCut", 20000.0f));

    configureFilters();
}

} // namespace ana
