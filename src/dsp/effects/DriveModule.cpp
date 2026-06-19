#include "DriveModule.h"
#include <cmath>
#include <algorithm>

namespace ana {

//==============================================================================
// Construction
DriveModule::DriveModule() {}

//==============================================================================
// Preparation
void DriveModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate_   = spec.sampleRate;
    numChannels_  = static_cast<int>(spec.numChannels);
    blockSize_    = static_cast<int>(spec.maximumBlockSize);

    // --- Oversampling 4x (half-band FIR for best anti-aliasing) ---
    oversampling_ = std::make_unique<juce::dsp::Oversampling<float>>(
        spec.numChannels, 4,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        juce::dsp::Oversampling<float>::standard);
    oversampling_->initProcessing(static_cast<double>(spec.maximumBlockSize));
    oversampling_->setUsingIntegerLatency(true);
    latencySamples_ = static_cast<int>(oversampling_->getLatencyInSamples());

    // --- Tone filter ---
    toneFilters_.resize(numChannels_);
    toneFilterDirty_ = true;
    updateToneFilter();

    // --- Wet HPF / LPF (prepare BEFORE setting coefficients) ---
    wetHPFFilter_.prepare(spec);
    wetLPFFilter_.prepare(spec);
    wetFiltersDirty_ = true;
    updateWetFilters();

    // --- Ring-mod state ---
    phase_.assign(numChannels_, 0.0f);
    phasorCos_.assign(numChannels_, 1.0f);
    phasorSin_.assign(numChannels_, 0.0f);

    // --- Crush state ---
    heldSample_.assign(numChannels_, 0.0f);
    sampleCounter_.assign(numChannels_, 0);

    // --- Dry buffer ---
    dryBuffer_.setSize(numChannels_, blockSize_, false, false, true);

    // Pre-compute rotation coeffs for default ring freq
    const float delta = 2.0f * juce::MathConstants<float>::pi * ringFreq_
                        / static_cast<float>(sampleRate_);
    cosDelta_ = std::cos(delta);
    sinDelta_ = std::sin(delta);

    // Initial quantize factor
    quantizeFactor_ = std::pow(2.0f, 8.0f);

    oversampling_->reset();
}

//==============================================================================
// Main process entry point
void DriveModule::process(juce::AudioBuffer<float>& buffer)
{
    if (bypassed_)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numCh      = buffer.getNumChannels();
    if (numSamples == 0 || numCh == 0)
        return;

    const auto mode = static_cast<DriveMode>(
        modeAtomic_.load(std::memory_order_relaxed));

    // Determine whether oversampling is needed
    const bool useOS = (mode == DriveMode::Tape || mode == DriveMode::Crush);

    // Save dry signal for wet/dry blend (only needed when mix < 1)
    const bool needDry = (mix_ < 1.0f);
    if (needDry)
        dryBuffer_.makeCopyOf(buffer, true);

    // ---- Processing ----
    if (useOS)
    {
        // Oversampled path (Tape or Crush)
        juce::dsp::AudioBlock<float> block(buffer);
        processOversampled(block, mode);
    }
    else
    {
        // Native-rate path
        const float preGain = drive_ * 10.0f + 0.001f;  // 0.001…10.001

        juce::dsp::AudioBlock<float> block(buffer);
        processNative(block, mode, preGain);
    }

    // ---- Tone filter (1-pole LP) ----
    if (toneFilterDirty_)
    {
        updateToneFilter();
        toneFilterDirty_ = false;
    }

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        auto& f    = toneFilters_[static_cast<size_t>(ch)];
        const float a  = f.a;
        const float b  = 1.0f - a;
        float y1 = f.y1;

        for (int s = 0; s < numSamples; ++s)
        {
            const float in = data[s];
            y1 = a * in + b * y1;
            data[s] = y1;
        }

        f.y1 = y1;
    }

    // ---- Wet signal HPF + LPF (IIR) ----
    if (wetFiltersDirty_)
    {
        updateWetFilters();
        wetFiltersDirty_ = false;
    }

    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        wetHPFFilter_.process(ctx);
        wetLPFFilter_.process(ctx);
    }

    // ---- Dry / Wet mix ----
    if (mix_ < 1.0f)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto* dry = dryBuffer_.getReadPointer(ch);
            auto*       wet = buffer.getWritePointer(ch);
            for (int s = 0; s < numSamples; ++s)
            {
                wet[s] = dry[s] * (1.0f - mix_) + wet[s] * mix_;
            }
        }
    }

    // ---- Output gain ----
    if (gain_ != 1.0f)
        buffer.applyGain(gain_);
}

//==============================================================================
// Oversampled processing (Tape / Crush)
void DriveModule::processOversampled(juce::dsp::AudioBlock<float>& block,
                                     DriveMode mode)
{
    // Upsample
    auto upBlock = oversampling_->processSamplesUp(block);

    const int upNumCh = static_cast<int>(upBlock.getNumChannels());
    const int upNumS  = static_cast<int>(upBlock.getNumSamples());

    if (mode == DriveMode::Tape)
    {
        const float preGain = drive_ * 10.0f + 0.001f;

        for (int ch = 0; ch < upNumCh; ++ch)
        {
            auto* data = upBlock.getChannelPointer(ch);
            for (int s = 0; s < upNumS; ++s)
                data[s] = tapeClip(data[s], preGain);
        }
    }
    else // Crush
    {
        // Crush: quantise + downsample at oversampled rate
        // quantizeFactor_ is precomputed in setDrive() from drive_ to avoid
        // per-block std::pow in the audio thread.
        const float qf   = quantizeFactor_;
        // Scale hold factor by oversampling ratio (4x) to maintain timing
        const int   hold = static_cast<int>((1.0f + drive_ * 15.0f) * 4.0f + 0.5f);

        for (int ch = 0; ch < upNumCh; ++ch)
        {
            auto* data = upBlock.getChannelPointer(ch);
            auto& held = heldSample_[static_cast<size_t>(ch)];
            int&  cnt  = sampleCounter_[static_cast<size_t>(ch)];

            for (int s = 0; s < upNumS; ++s)
            {
                const float x = data[s] + 1.0e-30f;  // denormal guard
                if (cnt == 0)
                    held = std::roundf(x * qf) / qf;
                data[s] = held;
                cnt = (cnt + 1) % hold;
            }
        }
    }

    // Downsample back to original rate
    oversampling_->processSamplesDown(block);
}

//==============================================================================
// Native-rate processing (Soft, Tube, Hard, Fold, Ring)
void DriveModule::processNative(juce::dsp::AudioBlock<float>& block,
                                DriveMode mode,
                                float preGain)
{
    const int numCh = static_cast<int>(block.getNumChannels());
    const int numS  = static_cast<int>(block.getNumSamples());

    switch (mode)
    {
        case DriveMode::Soft:
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* data = block.getChannelPointer(ch);
                for (int s = 0; s < numS; ++s)
                    data[s] = softClip(data[s], preGain);
            }
            break;
        }

        case DriveMode::Tube:
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* data = block.getChannelPointer(ch);
                for (int s = 0; s < numS; ++s)
                    data[s] = tubeClip(data[s], preGain);
            }
            break;
        }

        case DriveMode::Hard:
        {
            // threshold = 1 / preGain  (preGain >= 0.001 so no div-by-zero)
            const float threshold = 1.0f / preGain;
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* data = block.getChannelPointer(ch);
                for (int s = 0; s < numS; ++s)
                    data[s] = hardClip(data[s], threshold);
            }
            break;
        }

        case DriveMode::Fold:
        {
            const float foldFactor = 1.0f + drive_ * 5.0f;  // 1 … 6
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* data = block.getChannelPointer(ch);
                for (int s = 0; s < numS; ++s)
                    data[s] = waveFold(data[s], foldFactor);
            }
            break;
        }

        case DriveMode::Ring:
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* data = block.getChannelPointer(ch);
                processRing(ch, data, numS, ringFreq_);
            }
            break;
        }

        default:
            break; // Shouldn't reach here (Soft/Tube already handled)
    }
}

//==============================================================================
// Stateless waveshaping functions
float DriveModule::softClip(float x, float g)
{
    // tanh(preGain * x) — clamp input to avoid NaN from extreme values
    x = juce::jlimit(-100.0f, 100.0f, x * g);
    return std::tanh(x);
}

float DriveModule::tubeClip(float x, float g)
{
    // Asymmetric tanh: higher gain on positive half
    x = juce::jlimit(-100.0f, 100.0f, x * g);
    if (x >= 0.0f)
        return std::tanh(x * 1.2f);
    else
        return std::tanh(x * 0.8f) * 1.1f;
}

float DriveModule::tapeClip(float x, float g)
{
    // atan(preGain * x) / (pi/2) — clamp input
    x = juce::jlimit(-100.0f, 100.0f, x * g);
    constexpr float invHalfPi = 1.0f / juce::MathConstants<float>::halfPi;
    return std::atan(x) * invHalfPi;
}

float DriveModule::hardClip(float x, float threshold)
{
    return std::max(-threshold, std::min(threshold, x));
}

float DriveModule::waveFold(float x, float foldFactor)
{
    // v *= foldFactor; then fold v into [-1, 1] by reflecting at ±1
    //   f(v) = v - 2 * round(v / 2)
    // This is a classic triangle-wave folder.
    x = juce::jlimit(-100.0f, 100.0f, x * foldFactor);
    return x - std::round(x * 0.5f) * 2.0f;
}

//==============================================================================
// Stateful: Ring modulator (per-channel recursive phasor + phase accumulator)
void DriveModule::processRing(int channel,
                              float* data,
                              int numSamples,
                              float freq)
{
    const float phaseInc = freq / static_cast<float>(sampleRate_);

    float& phase  = phase_[static_cast<size_t>(channel)];
    float& cosVal = phasorCos_[static_cast<size_t>(channel)];
    float& sinVal = phasorSin_[static_cast<size_t>(channel)];

    for (int s = 0; s < numSamples; ++s)
    {
        // Recursive phasor rotation (avoids per-sample sin/cos)
        const float newCos = cosVal * cosDelta_ - sinVal * sinDelta_;
        const float newSin = sinVal * cosDelta_ + cosVal * sinDelta_;
        cosVal = newCos;
        sinVal = newSin;

        // Phase accumulator (for potential triangle/square)
        phase += phaseInc;
        if (phase >= 1.0f)
            phase -= 1.0f;

        // Sine carrier (pure AC, zero mean) — from recursive phasor
        const float carrier = sinVal;

        // Ring modulation: input * carrier
        data[s] = data[s] * carrier;
    }
}

//==============================================================================
// Reset
void DriveModule::reset()
{
    if (oversampling_)
        oversampling_->reset();

    for (auto& f : toneFilters_)
        f.y1 = 0.0f;

    wetHPFFilter_.reset();
    wetLPFFilter_.reset();

    // Ring state
    for (int ch = 0; ch < numChannels_; ++ch)
    {
        phase_[ch] = 0.0f;
        phasorCos_[ch] = 1.0f;
        phasorSin_[ch] = 0.0f;
    }

    // Crush state
    std::fill(heldSample_.begin(), heldSample_.end(), 0.0f);
    std::fill(sampleCounter_.begin(), sampleCounter_.end(), 0);
}

//==============================================================================
// Parameter setters
void DriveModule::setMode(DriveMode m)
{
    modeAtomic_.store(static_cast<int>(m), std::memory_order_relaxed);
}

void DriveModule::setDrive(float d01)
{
    drive_ = juce::jlimit(0.0f, 1.0f, d01);

    // Pre-compute derived values that depend on drive

    // Ring carrier frequency: map drive 0…1 → 20…2000 Hz (log-ish)
    ringFreq_ = 20.0f + drive_ * drive_ * 1980.0f;

    // Recompute recursive phasor rotation coefficients for ring modulator
    const float delta = 2.0f * juce::MathConstants<float>::pi * ringFreq_
                        / static_cast<float>(sampleRate_);
    cosDelta_ = std::cos(delta);
    sinDelta_ = std::sin(delta);

    // Crush quantize factor
    const float bitDepth = 16.0f - drive_ * 14.0f;
    quantizeFactor_ = std::pow(2.0f, bitDepth);
}

void DriveModule::setTone(float t01)
{
    tone_ = juce::jlimit(0.0f, 1.0f, t01);
    // Map tone 0…1 → 200…20000 Hz
    toneCutoff_ = 200.0f + tone_ * 19800.0f;
    toneFilterDirty_ = true;
}

void DriveModule::setMix(float m01)
{
    mix_ = juce::jlimit(0.0f, 1.0f, m01);
}

void DriveModule::setWetHPF(float hz)
{
    wetHPF_ = juce::jlimit(10.0f, 20000.0f, hz);
    wetFiltersDirty_ = true;
}

void DriveModule::setWetLPF(float hz)
{
    wetLPF_ = juce::jlimit(10.0f, 20000.0f, hz);
    wetFiltersDirty_ = true;
}

void DriveModule::setBypass(bool b)
{
    bypassed_ = b;
}

void DriveModule::setGain(float g)
{
    gain_ = juce::jlimit(0.0f, 4.0f, g);
}

//==============================================================================
// Filter coefficient updates
void DriveModule::updateToneFilter()
{
    const double fc = static_cast<double>(toneCutoff_);
    const double fs = sampleRate_;
    // 1-pole LP coefficient: a = exp(-2*pi*fc/fs)
    const double a = std::exp(-2.0 * juce::MathConstants<double>::pi * fc / fs);
    for (auto& f : toneFilters_)
        f.a = static_cast<float>(a);
}

void DriveModule::updateWetFilters()
{
    if (sampleRate_ <= 0.0)
        return;

    auto hpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        sampleRate_, wetHPF_, 0.707);
    auto lpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
        sampleRate_, wetLPF_, 0.707);

    *wetHPFFilter_.state = *hpfCoeffs;
    *wetLPFFilter_.state = *lpfCoeffs;
}

//==============================================================================
// State serialization
juce::ValueTree DriveModule::getState() const
{
    juce::ValueTree tree("DriveModule");
    tree.setProperty("mode", modeAtomic_.load(std::memory_order_relaxed), nullptr);
    tree.setProperty("drive", static_cast<double>(drive_), nullptr);
    tree.setProperty("tone", static_cast<double>(tone_), nullptr);
    tree.setProperty("mix", static_cast<double>(mix_), nullptr);
    tree.setProperty("wetHPF", static_cast<double>(wetHPF_), nullptr);
    tree.setProperty("wetLPF", static_cast<double>(wetLPF_), nullptr);
    tree.setProperty("bypass", bypassed_, nullptr);
    tree.setProperty("gain", static_cast<double>(gain_), nullptr);
    return tree;
}

void DriveModule::setState(const juce::ValueTree& tree)
{
    // jlimit ensures valid range before delegating to setters
    setMode(static_cast<DriveMode>(
        juce::jlimit(0, 6, static_cast<int>(tree.getProperty("mode", 0)))));
    setDrive(juce::jlimit(0.0f, 1.0f,
        static_cast<float>(tree.getProperty("drive", 0.5))));
    setTone(juce::jlimit(0.0f, 1.0f,
        static_cast<float>(tree.getProperty("tone", 1.0))));
    setMix(juce::jlimit(0.0f, 1.0f,
        static_cast<float>(tree.getProperty("mix", 1.0))));
    setWetHPF(juce::jlimit(10.0f, 20000.0f,
        static_cast<float>(tree.getProperty("wetHPF", 20.0))));
    setWetLPF(juce::jlimit(10.0f, 20000.0f,
        static_cast<float>(tree.getProperty("wetLPF", 20000.0))));
    setBypass(tree.getProperty("bypass", false));
    setGain(juce::jlimit(0.0f, 4.0f,
        static_cast<float>(tree.getProperty("gain", 1.0))));
}

} // namespace ana
