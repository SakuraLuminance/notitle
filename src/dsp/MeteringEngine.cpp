#include "MeteringEngine.h"

// libebur128 is a C library — wrap in extern "C" for C++ linkage
extern "C" {
#include <ebur128.h>
}

namespace ana {

MeteringEngine::MeteringEngine()
{
    for (auto& tp : truePeak_)
        tp.store(-HUGE_VAL);
}

MeteringEngine::~MeteringEngine()
{
    if (state_) {
        ebur128_destroy(&state_);
        state_ = nullptr;
    }
}

void MeteringEngine::prepare(double sampleRate, int numChannels, int maxBlockSize)
{
    // Destroy any previous state first
    if (state_) {
        ebur128_destroy(&state_);
        state_ = nullptr;
    }

    numChannels_ = numChannels;

    // Initialise libebur128 with all features we need:
    //   EBUR128_MODE_I        → integrated (includes momentary)
    //   EBUR128_MODE_LRA      → loudness range (includes short-term)
    //   EBUR128_MODE_TRUE_PEAK → true peak (4x oversampled)
    const int mode = EBUR128_MODE_I | EBUR128_MODE_LRA | EBUR128_MODE_TRUE_PEAK;
    state_ = ebur128_init(static_cast<unsigned int>(numChannels),
                          static_cast<unsigned long>(sampleRate),
                          mode);
    if (state_ == nullptr)
        return;

    // Pre-allocate interleave buffer so the audio callback never allocates
    const auto interleaveSize = static_cast<size_t>(maxBlockSize)
                              * static_cast<size_t>(numChannels);
    interleaveBuffer_.assign(interleaveSize, 0.0f);

    // Reset all meter values
    momentaryLUFS_.store(-HUGE_VAL);
    shortTermLUFS_.store(-HUGE_VAL);
    integratedLUFS_.store(-HUGE_VAL);
    lra_.store(-HUGE_VAL);
    for (auto& tp : truePeak_)
        tp.store(-HUGE_VAL);
}

void MeteringEngine::process(const juce::AudioBuffer<float>& buffer)
{
    if (state_ == nullptr)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin(numChannels_, buffer.getNumChannels());

    if (numSamples <= 0 || numCh <= 0)
        return;

    // Grow interleave buffer if the block size has increased since prepare()
    const auto needed = static_cast<size_t>(numSamples) * static_cast<size_t>(numCh);
    if (interleaveBuffer_.size() < needed)
        interleaveBuffer_.resize(needed);

    // Interleave: JUCE stores audio deinterleaved (per-channel arrays),
    // libebur128 expects interleaved float frames.
    for (int ch = 0; ch < numCh; ++ch) {
        const float* src = buffer.getReadPointer(ch);
        for (int s = 0; s < numSamples; ++s)
            interleaveBuffer_[static_cast<size_t>(s * numCh + ch)] = src[s];
    }

    // Feed interleaved frames to libebur128
    if (ebur128_add_frames_float(state_,
                                  interleaveBuffer_.data(),
                                  static_cast<size_t>(numSamples)) != EBUR128_SUCCESS)
        return;

    // Read and cache all meter values
    double val = 0.0;

    if (ebur128_loudness_momentary(state_, &val) == EBUR128_SUCCESS)
        momentaryLUFS_.store(val);

    if (ebur128_loudness_shortterm(state_, &val) == EBUR128_SUCCESS)
        shortTermLUFS_.store(val);

    if (ebur128_loudness_global(state_, &val) == EBUR128_SUCCESS)
        integratedLUFS_.store(val);

    if (ebur128_loudness_range(state_, &val) == EBUR128_SUCCESS)
        lra_.store(val);

    for (int ch = 0; ch < numCh && ch < kMaxChannels; ++ch) {
        if (ebur128_true_peak(state_, static_cast<unsigned int>(ch), &val) == EBUR128_SUCCESS)
            truePeak_[static_cast<size_t>(ch)].store(val);
    }
}

void MeteringEngine::reset()
{
    if (state_) {
        ebur128_destroy(&state_);
        state_ = nullptr;
    }

    momentaryLUFS_.store(-HUGE_VAL);
    shortTermLUFS_.store(-HUGE_VAL);
    integratedLUFS_.store(-HUGE_VAL);
    lra_.store(-HUGE_VAL);
    for (auto& tp : truePeak_)
        tp.store(-HUGE_VAL);
}

double MeteringEngine::getTruePeak(int channel) const noexcept
{
    if (channel < 0 || channel >= kMaxChannels)
        return -HUGE_VAL;
    return truePeak_[static_cast<size_t>(channel)].load();
}

} // namespace ana
