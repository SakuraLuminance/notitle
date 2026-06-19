#include "VocalNoiseReducer.h"
#include <algorithm>
#include <cmath>

namespace ana {

//==============================================================================
// Construction / Destruction
//==============================================================================

VocalNoiseReducer::VocalNoiseReducer() {}

//==============================================================================
// EffectBase interface
//==============================================================================

void VocalNoiseReducer::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate_    = spec.sampleRate;
    numChannels_  = static_cast<int>(spec.numChannels);
    maxBlockSize_ = static_cast<int>(spec.maximumBlockSize);

    // Create FFT engine
    fft_ = std::make_unique<juce::dsp::FFT>(fftOrder_);

    // Pre-compute Hann window (periodic, size = fftSize_)
    window_.resize(fftSize_);
    for (int i = 0; i < fftSize_; ++i)
        window_[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(
            2.0 * juce::MathConstants<double>::pi * i / fftSize_));

    // Pre-allocate per-channel input buffers
    // Size = fftSize_ + maxBlockSize_  (enough for one full frame plus any incoming block)
    const int inputBufSize = fftSize_ + maxBlockSize_;
    inputBuf_.resize(numChannels_);
    for (auto& buf : inputBuf_)
        buf.resize(inputBufSize, 0.0f);
    inputPos_.assign(numChannels_, 0);

    // Pre-allocate per-channel overlap-add buffers
    overlapBuf_.resize(numChannels_);
    for (auto& buf : overlapBuf_)
        buf.resize(fftSize_, 0.0f);

    // Pre-allocate per-channel noise estimates (magnitude² per unique bin)
    noiseEstimate_.resize(numChannels_);
    for (auto& est : noiseEstimate_)
    {
        est.resize(numBins_);
        std::fill(est.begin(), est.end(), 1e-6f); // small positive initial value
    }

    // Pre-allocate FFT temp buffer
    fftBuf_.resize(fftSize_, 0.0f);

    // Compute derived coefficients
    updateAlpha();
    updateTimeConstants();
}

void VocalNoiseReducer::process(juce::AudioBuffer<float>& buffer)
{
    // Snapshot parameters once per block (message thread vs audio thread safety)
    const bool  bypassed      = bypassed_;
    const int   numChannels   = numChannels_;
    const float alpha         = alpha_;
    const float floor         = floor_;
    const float attackCoeff   = attackCoeff_;
    const float releaseCoeff  = releaseCoeff_;

    if (bypassed)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numCh      = juce::jmin(numChannels, buffer.getNumChannels());

    if (numSamples <= 0)
        return;

    for (int ch = 0; ch < numCh; ++ch)
    {
        processChannel(ch,
                       buffer.getReadPointer(ch),
                       buffer.getWritePointer(ch),
                       numSamples,
                       alpha, floor, attackCoeff, releaseCoeff);
    }

    // Mono -> mono passthrough: if input is mono we've already processed it above.
    // If numCh < numChannels (e.g. mono input to stereo effect), remaining
    // channel buffers are left untouched (already zero from the DAW, or we
    // just leave them as-is).  The overlap buffer may contain stale data but
    // we only output what was requested.
}

void VocalNoiseReducer::reset()
{
    // Clear all input ring buffers
    for (auto& buf : inputBuf_)
        std::fill(buf.begin(), buf.end(), 0.0f);
    std::fill(inputPos_.begin(), inputPos_.end(), 0);

    // Clear all overlap-add buffers
    for (auto& buf : overlapBuf_)
        std::fill(buf.begin(), buf.end(), 0.0f);

    // Reset noise estimates to small positive value
    for (auto& est : noiseEstimate_)
        std::fill(est.begin(), est.end(), 1e-6f);

    // Clear FFT temp buffer
    std::fill(fftBuf_.begin(), fftBuf_.end(), 0.0f);
}

//==============================================================================
// Parameter setters
//==============================================================================

void VocalNoiseReducer::setReduction(float percent)
{
    reduction_ = std::max(0.0f, std::min(100.0f, percent));
    updateAlpha();
}

void VocalNoiseReducer::setFloor(float f)
{
    floor_ = std::max(0.01f, std::min(0.1f, f));
}

void VocalNoiseReducer::setAttack(float ms)
{
    attackMs_ = std::max(50.0f, std::min(500.0f, ms));
    updateTimeConstants();
}

void VocalNoiseReducer::setRelease(float ms)
{
    releaseMs_ = std::max(200.0f, std::min(2000.0f, ms));
    updateTimeConstants();
}

void VocalNoiseReducer::setBypass(bool b)
{
    bypassed_ = b;
}

//==============================================================================
// State serialization
//==============================================================================

juce::ValueTree VocalNoiseReducer::getState() const
{
    juce::ValueTree tree("VocalNoiseReducer");
    tree.setProperty("reduction", static_cast<double>(reduction_), nullptr);
    tree.setProperty("floor",     static_cast<double>(floor_), nullptr);
    tree.setProperty("attack",    static_cast<double>(attackMs_), nullptr);
    tree.setProperty("release",   static_cast<double>(releaseMs_), nullptr);
    tree.setProperty("bypass",    bypassed_, nullptr);
    return tree;
}

void VocalNoiseReducer::setState(const juce::ValueTree& state)
{
    setReduction(static_cast<float>(state.getProperty("reduction", 50.0)));
    setFloor(static_cast<float>(state.getProperty("floor", 0.05)));
    setAttack(static_cast<float>(state.getProperty("attack", 50.0)));
    setRelease(static_cast<float>(state.getProperty("release", 500.0)));
    setBypass(state.getProperty("bypass", false));
}

//==============================================================================
// Internal processing
//==============================================================================

void VocalNoiseReducer::processChannel(int channel, const float* in, float* out, int numSamples,
                                       float alpha, float floor, float attackCoeff, float releaseCoeff)
{
    const size_t idx = static_cast<size_t>(channel);
    auto& inBuf    = inputBuf_[idx];
    int&  fill     = inputPos_[idx];
    auto& overlap  = overlapBuf_[idx];
    auto& noiseEst = noiseEstimate_[idx];

    // ---- 1. Append incoming samples to the ring buffer ----
    const size_t bufCapacity = inBuf.size();
    if (static_cast<size_t>(fill) + static_cast<size_t>(numSamples) <= bufCapacity)
    {
        std::copy(in, in + numSamples, inBuf.begin() + fill);
        fill += numSamples;
    }
    else
    {
        // Safety clamp — should never happen with reasonable block sizes
        const int room = static_cast<int>(bufCapacity) - fill;
        if (room > 0)
        {
            std::copy(in, in + room, inBuf.begin() + fill);
            fill += room;
        }
    }

    // ---- 2. Process as many STFT frames as possible ----
    while (fill >= fftSize_)
    {
        // Window the frame at position 0 of the input buffer
        for (int i = 0; i < fftSize_; ++i)
            fftBuf_[static_cast<size_t>(i)] = inBuf[static_cast<size_t>(i)]
                                            * window_[static_cast<size_t>(i)];

        // ---- Forward FFT (in-place, packed format) ----
        fft_->performRealOnlyForwardTransform(fftBuf_.data());

        // ---- Spectral subtraction per bin ----
        // FFT packed format for N=2048 (fftSize_):
        //   index 0: DC (real)
        //   index 1: Nyquist (real)
        //   index 2k, 2k+1 for k=1..N/2-1: Re(bin k), Im(bin k)
        for (int k = 0; k < numBins_; ++k)
        {
            float mag2;
            float* rePtr = nullptr;
            float* imPtr = nullptr;

            if (k == 0)
            {
                // DC bin — purely real at fftBuf_[0]
                rePtr = &fftBuf_[0];
                imPtr = nullptr;
                mag2 = (*rePtr) * (*rePtr);
            }
            else if (k == fftSize_ / 2)
            {
                // Nyquist bin — purely real at fftBuf_[1]
                rePtr = &fftBuf_[1];
                imPtr = nullptr;
                mag2 = (*rePtr) * (*rePtr);
            }
            else
            {
                // Regular complex bin at fftBuf_[2k], fftBuf_[2k+1]
                rePtr = &fftBuf_[static_cast<size_t>(2 * k)];
                imPtr = &fftBuf_[static_cast<size_t>(2 * k + 1)];
                mag2 = (*rePtr) * (*rePtr) + (*imPtr) * (*imPtr);
            }

            // ---- Update noise estimate (running min with attack/release) ----
            float& noise = noiseEst[static_cast<size_t>(k)];
            if (mag2 < noise)
            {
                // Magnitude dropped below estimate → fast attack
                noise += attackCoeff * (mag2 - noise);
            }
            else
            {
                // Magnitude rose above estimate → slow release
                noise += releaseCoeff * (mag2 - noise);
            }
            noise += 1e-15f; // denormal protection
            // Prevent noise estimate from going to zero
            noise = std::max(noise, 1e-10f);

            // ---- Compute spectral subtraction gain ----
            const float denom = std::max(mag2, 1e-10f);
            float G = 1.0f - alpha * noise / denom;
            G = std::max(G, floor);               // apply floor
            const float gain = std::sqrt(G);        // apply to magnitude

            // ---- Apply gain to bin ----
            *rePtr *= gain;
            if (imPtr != nullptr)
                *imPtr *= gain;
        }

        // ---- Inverse FFT (in-place, unpacked back to time domain) ----
        fft_->performRealOnlyInverseTransform(fftBuf_.data());

        // ---- Apply window again (COLA symmetry) ----
        for (int i = 0; i < fftSize_; ++i)
            fftBuf_[static_cast<size_t>(i)] *= window_[static_cast<size_t>(i)];

        // ---- Overlap-add into output buffer ----
        for (int i = 0; i < fftSize_; ++i)
            overlap[static_cast<size_t>(i)] += fftBuf_[static_cast<size_t>(i)];

        // ---- Consume hopSize_ samples from input buffer ----
        const int remaining = fill - hopSize_;
        if (remaining > 0)
        {
            std::copy(inBuf.begin() + hopSize_,
                      inBuf.begin() + fill,
                      inBuf.begin());
            fill = remaining;
        }
        else
        {
            fill = 0;
        }
    }

    // ---- 3. Copy output from overlap buffer ----
    const int toCopy = std::min(numSamples, fftSize_);
    std::copy(overlap.begin(), overlap.begin() + toCopy, out);

    // ---- 4. Shift overlap buffer left by numSamples (consume output) ----
    const size_t shift = static_cast<size_t>(numSamples);
    if (shift < static_cast<size_t>(fftSize_))
    {
        std::copy(overlap.begin() + shift, overlap.end(), overlap.begin());
        std::fill(overlap.end() - shift, overlap.end(), 0.0f);
    }
    else
    {
        std::fill(overlap.begin(), overlap.end(), 0.0f);
    }
}

//==============================================================================
// Coefficient helpers
//==============================================================================

void VocalNoiseReducer::updateAlpha()
{
    // Map reduction 0..100 %  →  alpha 2..4
    alpha_ = 2.0f + (reduction_ / 100.0f) * 2.0f;
}

void VocalNoiseReducer::updateTimeConstants()
{
    // One-pole smoothing coefficients from time constants.
    // coeff = 1 - exp(-hopDuration / timeConstant)
    const float hopDuration = static_cast<float>(hopSize_)
                            / static_cast<float>(sampleRate_);

    const float attackTime  = attackMs_  * 0.001f; // ms → s
    const float releaseTime = releaseMs_ * 0.001f;

    attackCoeff_  = 1.0f - std::exp(-hopDuration / std::max(attackTime,  0.001f));
    releaseCoeff_ = 1.0f - std::exp(-hopDuration / std::max(releaseTime, 0.001f));
}

} // namespace ana
