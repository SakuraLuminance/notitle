#include "SpectralFreezeEngine.h"

#include <algorithm>
#include <cstring>

namespace ana {

//==============================================================================
// Anonymous namespace: constants and internal helpers
//==============================================================================
namespace {

constexpr float kAmpThreshold     = 1e-6f;
constexpr float kMinFreq          = 20.0f;
constexpr float kMaxFreq          = 20000.0f;
constexpr int   kMaxPartials      = PartialDataSIMD::kMaxPartials;

/** Clamp value to [lo, hi]. */
inline float clamp(float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/** Compute the pitch-shift factor for a given number of semitones. */
inline float pitchFactor(float semitones) noexcept
{
    return std::pow(2.0f, semitones / 12.0f);
}

/** Linear interpolation between two values. */
inline float lerp(float a, float b, float t) noexcept
{
    return a + t * (b - a);
}

} // namespace

//==============================================================================
// Construction
//==============================================================================

SpectralFreezeEngine::SpectralFreezeEngine()
{
    frozenHistory_.reserve(2048);
    motionFrames_.reserve(static_cast<size_t>(motionMaxFrames_));
    frozenAudioBuffer_.reserve(static_cast<size_t>(fftSize_));
}

//==============================================================================
// Freeze control
//==============================================================================

void SpectralFreezeEngine::setFreezeMode(FreezeMode mode)
{
    mode_ = mode;
    kernelDirty_ = true;  // blur kernel may need recomputation
}

void SpectralFreezeEngine::setFreeze(bool shouldFreeze)
{
    if (shouldFreeze && !isFrozen_)
        pendingFreeze_ = true;
    else if (!shouldFreeze)
        isFrozen_ = false;
}

void SpectralFreezeEngine::triggerFreeze()
{
    // Reset frozen state and schedule a capture on the next process() call
    isFrozen_       = false;
    pendingFreeze_  = true;
    currentMix_     = 0.0f;
    freezeFrameIndex_ = 0;
}

void SpectralFreezeEngine::setFreezePoint(float normalizedTime)
{
    freezePoint_ = clamp(normalizedTime, 0.0f, 1.0f);
}

//==============================================================================
// Spectrum manipulation
//==============================================================================

void SpectralFreezeEngine::setPitchShift(float semitones)
{
    pitchShift_ = clamp(semitones, -24.0f, 24.0f);
}

void SpectralFreezeEngine::setSpectralBlur(float amount)
{
    spectralBlur_ = clamp(amount, 0.0f, 1.0f);
    kernelDirty_  = true;
}

void SpectralFreezeEngine::setSpectralTilt(float tilt)
{
    spectralTilt_ = clamp(tilt, -1.0f, 1.0f);
}

void SpectralFreezeEngine::setGrainSize(float ms)
{
    grainSizeMs_ = clamp(ms, 10.0f, 500.0f);
}

void SpectralFreezeEngine::setEvolutionRate(float rate)
{
    evolutionRate_ = clamp(rate, 0.01f, 1.0f);
}

//==============================================================================
// Mix & filtering
//==============================================================================

void SpectralFreezeEngine::setMix(float mix)
{
    mix_ = clamp(mix, 0.0f, 1.0f);
}

void SpectralFreezeEngine::setDryHP(float freqHz)
{
    dryHP_ = clamp(freqHz, 20.0f, 20000.0f);
    filtersDirty_ = true;
}

void SpectralFreezeEngine::setWetLP(float freqHz)
{
    wetLP_ = clamp(freqHz, 20.0f, 20000.0f);
    filtersDirty_ = true;
}

//==============================================================================
// Configuration
//==============================================================================

void SpectralFreezeEngine::setSampleRate(double sr)
{
    if (sr > 0.0)
    {
        sampleRate_   = sr;
        filtersDirty_ = true;

        // Recompute crossfade rate for ~10 ms transition
        crossfadeRate_ = 1.0f / static_cast<float>(sr * 0.01);
    }
}

void SpectralFreezeEngine::setFftSize(int size)
{
    // Snap to the nearest valid power of two in [256, 8192]
    int s = clamp(static_cast<float>(size), 256.0f, 8192.0f);
    int valid = 256;
    while (valid * 2 <= s && valid * 2 <= 8192)
        valid *= 2;

    if (valid != fftSize_)
    {
        fftSize_ = valid;
        frozenAudioBuffer_.clear();
        kernelDirty_ = true;
    }
}

//==============================================================================
// Process — additive partials path
//==============================================================================

void SpectralFreezeEngine::process(PartialDataSIMD& partials, int /*currentFrame*/)
{
    ++frameCount_;

    // ---- 1. Handle pending freeze trigger ----
    if (pendingFreeze_)
    {
        pendingFreeze_ = false;
        isFrozen_      = true;
        freezeFrameIndex_ = 0;

        switch (mode_)
        {
            case FreezeMode::Snapshot:
            case FreezeMode::Reverse:
                snapshotFreeze(partials);
                break;

            case FreezeMode::Accumulate:
            {
                // Start from current capture, will blend going forward
                snapshotFreeze(partials);
                break;
            }

            case FreezeMode::Motion:
            {
                // Position the read head at the freeze point within the ring buffer
                if (!motionFrames_.empty())
                {
                    const int maxIdx = static_cast<int>(motionFrames_.size()) - 1;
                    motionReadPosition_ = static_cast<int>(freezePoint_
                        * static_cast<float>(maxIdx));
                    motionReadPosition_ = clamp(
                        static_cast<float>(motionReadPosition_),
                        0.0f, static_cast<float>(maxIdx));
                }
                else
                {
                    // No history yet — capture current frame as the first motion frame
                    std::vector<float> initFrame(static_cast<size_t>(kMaxPartials), 0.0f);
                    for (int i = 0; i < kMaxPartials; ++i)
                        initFrame[static_cast<size_t>(i)] = partials.amplitude[i];
                    motionFrames_.push_back(std::move(initFrame));
                    motionFramePosition_ = 0;
                    motionReadPosition_  = 0;
                }
                break;
            }
        }
    }

    // ---- 2. Record current frame into history (needed for Motion/Reverse) ----
    recordToHistory(partials);

    // ---- 3. Update crossfade envelope ----
    {
        const float target = isFrozen_ ? mix_ : 0.0f;
        if (currentMix_ < target)
            currentMix_ = std::min(currentMix_ + crossfadeRate_, target);
        else if (currentMix_ > target)
            currentMix_ = std::max(currentMix_ - crossfadeRate_, target);
    }

    // Early-out if there is nothing to blend
    if (currentMix_ < kAmpThreshold && !isFrozen_)
        return;

    // ---- 4. Update frozen state based on active mode ----
    if (isFrozen_)
    {
        switch (mode_)
        {
            case FreezeMode::Accumulate:
                accumulateFreeze(partials);
                break;

            case FreezeMode::Motion:
                motionFreeze(partials, frameCount_);
                ++freezeFrameIndex_;
                break;

            case FreezeMode::Reverse:
                reverseFreeze(partials, frameCount_);
                ++freezeFrameIndex_;
                break;

            case FreezeMode::Snapshot:
            default:
                // Frozen state was captured once — nothing to update each frame
                break;
        }
    }

    // ---- 5. Apply frozen spectrum to output ----
    // This applies spectral effects (pitch, blur, tilt) and blends with the live signal
    // using the smoothed crossfade envelope.
    // Only do this if the previous step didn't already write to partials directly.
    // For Snapshot/Accumulate we apply the frozen spectrum on top of the live signal.
    // For Motion/Reverse the frozen state is already populated by the mode function.
    applyFrozenToOutput(partials);
}

//==============================================================================
// Process — audio buffer path
//==============================================================================

void SpectralFreezeEngine::processAudio(const juce::AudioBuffer<float>& input,
                                         juce::AudioBuffer<float>& output)
{
    const int numChannels = input.getNumChannels();
    const int numSamples  = input.getNumSamples();

    // Start from a copy of the dry input
    output.makeCopyOf(input);

    if (numChannels <= 0 || numSamples <= 0)
        return;

    // ---- 1. Initialise the frozen audio ring buffer if needed ----
    if (frozenAudioBuffer_.empty())
        frozenAudioBuffer_.assign(static_cast<size_t>(fftSize_), 0.0f);

    const int bufSize = static_cast<int>(frozenAudioBuffer_.size());

    // ---- 2. Handle pending freeze ----
    if (pendingFreeze_)
    {
        pendingFreeze_   = false;
        isFrozen_        = true;
        audioReadPos_    = audioWritePos_ - bufSize;  // start from one buffer-length ago
        if (audioReadPos_ < 0)
            audioReadPos_ += bufSize * 256;  // ensure positive for modulo
    }

    // ---- 3. Update crossfade ----
    {
        const float target = isFrozen_ ? mix_ : 0.0f;
        if (currentMix_ < target)
            currentMix_ = std::min(currentMix_ + crossfadeRate_, target);
        else if (currentMix_ > target)
            currentMix_ = std::max(currentMix_ - crossfadeRate_, target);
    }

    // ---- 4. Write incoming samples into ring buffer ----
    // Always write (even when dry) so the ring buffer is ready for instant freeze.
    {
        const float* inCh0 = input.getReadPointer(0);
        for (int s = 0; s < numSamples; ++s)
            frozenAudioBuffer_[static_cast<size_t>((audioWritePos_ + s) % bufSize)]
                = inCh0[s];
    }

    const float mix = currentMix_;
    if (mix < kAmpThreshold && !isFrozen_)
    {
        audioWritePos_ += numSamples;
        return;
    }

    // ---- 5. Ensure per-channel filters exist ----
    {
        const size_t chU = static_cast<size_t>(numChannels);
        if (dryHPFilters_.size() < chU)
        {
            dryHPFilters_.resize(chU);
            wetLPFilters_.resize(chU);
        }
        if (filtersDirty_)
            updateFilters();
    }

    // ---- 6. Process each channel ----
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* inSamples  = input.getReadPointer(ch);
        float*       outSamples = output.getWritePointer(ch);

        juce::dsp::IIR::Filter<float>& dryFilt = dryHPFilters_[static_cast<size_t>(ch)];
        juce::dsp::IIR::Filter<float>& wetFilt = wetLPFilters_[static_cast<size_t>(ch)];

        for (int s = 0; s < numSamples; ++s)
        {
            // Determine the frozen (wet) sample
            float wetSample;
            if (isFrozen_)
            {
                int readOffset;
                switch (mode_)
                {
                    case FreezeMode::Reverse:
                        readOffset = -s;
                        break;
                    case FreezeMode::Motion:
                        // Slowly advance through the buffer at the evolution rate
                        readOffset = static_cast<int>(
                            static_cast<float>(s) * evolutionRate_);
                        break;
                    default:
                        readOffset = s;
                        break;
                }

                int readPos = audioReadPos_ + readOffset;

                // Keep read position positive for safe modulo
                while (readPos < 0)
                    readPos += bufSize * 256;

                wetSample = frozenAudioBuffer_[static_cast<size_t>(readPos % bufSize)];
            }
            else
            {
                wetSample = inSamples[s];
            }

            // Apply dry HPF and wet LPF
            const float dryProc = dryFilt.processSample(inSamples[s]);
            const float wetProc = wetFilt.processSample(wetSample);

            // Blend
            outSamples[s] = dryProc * (1.0f - mix) + wetProc * mix;
        }
    }

    // ---- 6. Advance buffer positions ----
    audioWritePos_ += numSamples;
    if (isFrozen_)
        audioReadPos_ += numSamples;
}

//==============================================================================
// State management
//==============================================================================

void SpectralFreezeEngine::reset()
{
    std::memset(&frozenPartials_, 0, sizeof(frozenPartials_));
    std::memset(&originalFrozenPartials_, 0, sizeof(originalFrozenPartials_));

    isFrozen_         = false;
    pendingFreeze_    = false;
    freezeFrameIndex_ = 0;
    frameCount_       = 0;
    currentMix_       = 0.0f;

    frozenHistory_.clear();
    motionFrames_.clear();
    motionFramePosition_ = 0;
    motionReadPosition_  = 0;

    frozenAudioBuffer_.clear();
    audioWritePos_ = 0;
    audioReadPos_  = 0;

    dryHPFilters_.clear();
    wetLPFilters_.clear();
    filtersDirty_ = true;
}

void SpectralFreezeEngine::clearFrozen()
{
    std::memset(&frozenPartials_, 0, sizeof(frozenPartials_));
    std::memset(&originalFrozenPartials_, 0, sizeof(originalFrozenPartials_));
    isFrozen_      = false;
    pendingFreeze_ = false;
    currentMix_    = 0.0f;

    frozenHistory_.clear();
    frozenAudioBuffer_.clear();
    audioWritePos_ = 0;
    audioReadPos_  = 0;
}

//==============================================================================
// Freeze operation: Snapshot
//==============================================================================

void SpectralFreezeEngine::snapshotFreeze(const PartialDataSIMD& current)
{
    // Full capture of the current spectral state
    std::memcpy(frozenPartials_.frequency,
                current.frequency,
                sizeof(frozenPartials_.frequency));
    std::memcpy(frozenPartials_.amplitude,
                current.amplitude,
                sizeof(frozenPartials_.amplitude));
    std::memcpy(frozenPartials_.phase,
                current.phase,
                sizeof(frozenPartials_.phase));

    std::memcpy(originalFrozenPartials_.frequency,
                current.frequency,
                sizeof(originalFrozenPartials_.frequency));
    std::memcpy(originalFrozenPartials_.amplitude,
                current.amplitude,
                sizeof(originalFrozenPartials_.amplitude));
    std::memcpy(originalFrozenPartials_.phase,
                current.phase,
                sizeof(originalFrozenPartials_.phase));

    frozenPartials_.sampleRate  = current.sampleRate;
    frozenPartials_.hopSize     = current.hopSize;
    frozenPartials_.maxPartials = current.maxPartials;

    originalFrozenPartials_.sampleRate  = current.sampleRate;
    originalFrozenPartials_.hopSize     = current.hopSize;
    originalFrozenPartials_.maxPartials = current.maxPartials;

    frozenPartials_.updateActiveMask();
    originalFrozenPartials_.updateActiveMask();
}

//==============================================================================
// Freeze operation: Accumulate
//==============================================================================

void SpectralFreezeEngine::accumulateFreeze(const PartialDataSIMD& current)
{
    // Blend factor: higher = faster accumulation
    // Use a time-constant based on a fraction of a second at current sample rate
    const float blend = 0.05f;

    // Merge incoming energy into the frozen state
    for (int i = 0; i < kMaxPartials; ++i)
    {
        const float liveAmp = current.amplitude[i];

        // Blend frozen amplitude toward live value for smooth buildup/decay
        frozenPartials_.amplitude[i] += blend * (liveAmp - frozenPartials_.amplitude[i]);

        // Update frequency and phase from live (keep spectrum tracking)
        frozenPartials_.frequency[i] = current.frequency[i];
        frozenPartials_.phase[i]     = current.phase[i];
    }

    frozenPartials_.updateActiveMask();

    // Keep reference copy aligned
    std::memcpy(originalFrozenPartials_.frequency,
                frozenPartials_.frequency,
                sizeof(originalFrozenPartials_.frequency));
    std::memcpy(originalFrozenPartials_.amplitude,
                frozenPartials_.amplitude,
                sizeof(originalFrozenPartials_.amplitude));
    std::memcpy(originalFrozenPartials_.phase,
                frozenPartials_.phase,
                sizeof(originalFrozenPartials_.phase));
    originalFrozenPartials_.updateActiveMask();
}

//==============================================================================
// Freeze operation: Motion
//==============================================================================

void SpectralFreezeEngine::motionFreeze(const PartialDataSIMD& /*current*/, int /*frame*/)
{
    if (motionFrames_.empty())
        return;

    const size_t nFrames = motionFrames_.size();

    // Read position advances at the evolution rate
    float readPos = static_cast<float>(motionReadPosition_)
                  + evolutionRate_;

    // Wrap around
    while (readPos >= static_cast<float>(nFrames))
        readPos -= static_cast<float>(nFrames);
    while (readPos < 0.0f)
        readPos += static_cast<float>(nFrames);

    motionReadPosition_ = static_cast<int>(readPos);

    // Interpolate between adjacent frames for smooth motion
    const int idxLo = static_cast<int>(readPos) % static_cast<int>(nFrames);
    const int idxHi = (idxLo + 1) % static_cast<int>(nFrames);
    const float frac = readPos - static_cast<float>(idxLo);

    // Read amplitudes from the motion ring buffer (interpolated).
    // Frequencies are preserved from whatever was in frozenPartials_
    // (set by snapshotFreeze at trigger time or from previous frames)
    // since Motion mode freezes the spectral envelope, not the frequencies.
    for (int i = 0; i < kMaxPartials; ++i)
    {
        const size_t u = static_cast<size_t>(i);
        const float ampLo = motionFrames_[static_cast<size_t>(idxLo)][u];
        const float ampHi = motionFrames_[static_cast<size_t>(idxHi)][u];
        frozenPartials_.amplitude[i] = lerp(ampLo, ampHi, frac);
        // Frequencies preserved from existing frozenPartials_.frequency
        // Phases preserved from existing frozenPartials_.phase
    }

    frozenPartials_.updateActiveMask();
}

//==============================================================================
// Freeze operation: Reverse
//==============================================================================

void SpectralFreezeEngine::reverseFreeze(const PartialDataSIMD& /*current*/, int /*frame*/)
{
    if (frozenHistory_.empty())
        return;

    const size_t nHistory = frozenHistory_.size();

    // Play history in reverse: start from end, go backwards
    // freezeFrameIndex_ advances each frame, so we read from:
    // history[nHistory - 1 - freezeFrameIndex_]
    size_t readIdx;
    if (freezeFrameIndex_ >= static_cast<int>(nHistory))
    {
        // Wrap around or hold at the beginning
        readIdx = 0;
    }
    else
    {
        readIdx = nHistory - 1 - static_cast<size_t>(freezeFrameIndex_);
    }

    const auto& frame = frozenHistory_[readIdx];

    // Copy the stored spectral frame into frozenPartials_.
    // Frequencies are preserved from the snapshot capture (set at trigger time)
    // so the spectral identity remains intact.  Only amplitudes and phases
    // are updated from the reverse-playback history.
    for (int i = 0; i < kMaxPartials; ++i)
    {
        const size_t u = static_cast<size_t>(i);
        frozenPartials_.amplitude[i] = frame.amplitude[u];
        frozenPartials_.phase[i]     = frame.phase[u];
        // Frequencies remain from the snapshot capture in frozenPartials_
        // (set by snapshotFreeze at trigger time).
    }

    frozenPartials_.updateActiveMask();

    // Over-blend adjacent frames for smooth transitions (grain envelope)
    // When we have multiple frames, crossfade with the next one
    if (freezeFrameIndex_ > 0 && readIdx + 1 < nHistory)
    {
        const auto& nextFrame = frozenHistory_[readIdx + 1];
        const float crossfadeLen = std::max(1.0f,
            grainSizeMs_ * 0.001f * static_cast<float>(sampleRate_)
            / static_cast<float>(fftSize_));

        const float crossfadePos = static_cast<float>(freezeFrameIndex_)
                                 / crossfadeLen;
        const float cf = std::min(crossfadePos, 1.0f);

        if (cf < 1.0f)
        {
            for (int i = 0; i < kMaxPartials; ++i)
            {
                const size_t u = static_cast<size_t>(i);
                frozenPartials_.amplitude[i] = lerp(
                    frame.amplitude[u],
                    nextFrame.amplitude[u],
                    cf);
            }
            frozenPartials_.updateActiveMask();
        }
    }
}

//==============================================================================
// Record current partials to history buffers
//==============================================================================

void SpectralFreezeEngine::recordToHistory(const PartialDataSIMD& partials)
{
    // In Reverse mode we always record frames into the history buffer
    if (mode_ == FreezeMode::Reverse)
    {
        FrozenFrame ff;
        for (int i = 0; i < kMaxPartials; ++i)
        {
            const size_t u = static_cast<size_t>(i);
            ff.amplitude[u] = partials.amplitude[i];
            ff.phase[u]     = partials.phase[i];
        }

        frozenHistory_.push_back(std::move(ff));
    }

    // In Motion mode we push amplitude-only frames into the ring buffer
    if (mode_ == FreezeMode::Motion)
    {
        std::vector<float> frame(static_cast<size_t>(kMaxPartials), 0.0f);
        for (int i = 0; i < kMaxPartials; ++i)
            frame[static_cast<size_t>(i)] = partials.amplitude[i];

        if (static_cast<int>(motionFrames_.size()) < motionMaxFrames_)
        {
            motionFrames_.push_back(std::move(frame));
            motionFramePosition_ = static_cast<int>(motionFrames_.size()) - 1;
        }
        else
        {
            motionFrames_[static_cast<size_t>(motionFramePosition_)]
                = std::move(frame);
            motionFramePosition_ = (motionFramePosition_ + 1) % motionMaxFrames_;
        }
    }
}

//==============================================================================
// Apply frozen spectrum to output partials
//==============================================================================

void SpectralFreezeEngine::applyFrozenToOutput(PartialDataSIMD& output)
{
    const float mix = currentMix_;

    if (mix < kAmpThreshold)
        return;

    // Create a working copy of the frozen state.
    // Spectral effects are applied to this copy so they never corrupt the
    // persistent frozenPartials_ (important for Accumulate mode where the
    // state is built up over time, and for Snapshot mode where it must
    // remain pristine between frames).
    PartialDataSIMD workCopy;
    std::memcpy(&workCopy, &frozenPartials_, sizeof(workCopy));

    // For Snapshot mode, restore from the original capture so that
    // frozenPartials_ always reflects the exact moment of freeze.
    if (mode_ == FreezeMode::Snapshot)
    {
        std::memcpy(workCopy.frequency,
                    originalFrozenPartials_.frequency,
                    sizeof(workCopy.frequency));
        std::memcpy(workCopy.amplitude,
                    originalFrozenPartials_.amplitude,
                    sizeof(workCopy.amplitude));
        // Phases keep running for continuity
        workCopy.updateActiveMask();
    }

    // Apply spectral effects to the working copy
    applyPitchShift(workCopy, pitchShift_);
    applySpectralBlur(workCopy, spectralBlur_, gaussianKernel_,
                      kernelRadius_, kernelDirty_);
    applySpectralTilt(workCopy, spectralTilt_);

    // Blend into output
    for (int i = 0; i < kMaxPartials; ++i)
    {
        output.amplitude[i] = output.amplitude[i] * (1.0f - mix)
                            + workCopy.amplitude[i] * mix;
        output.frequency[i] = workCopy.frequency[i];
        output.phase[i]     = workCopy.phase[i];
    }

    output.updateActiveMask();
}

//==============================================================================
// Spectral effects (static — operate on arbitrary PartialDataSIMD)
//==============================================================================

void SpectralFreezeEngine::applyPitchShift(PartialDataSIMD& data, float semitones)
{
    if (std::abs(semitones) < 0.01f)
        return;

    const float factor = pitchFactor(semitones);

    for (int i = 0; i < kMaxPartials; ++i)
    {
        if (data.isActive(i))
        {
            data.frequency[i] *= factor;

            // Clamp to audible range
            if (data.frequency[i] > kMaxFreq)
                data.frequency[i] = kMaxFreq;
            if (data.frequency[i] < kMinFreq && data.frequency[i] > 0.0f)
                data.frequency[i] = kMinFreq;
        }
    }
}

void SpectralFreezeEngine::applySpectralBlur(PartialDataSIMD& data,
                                              float amount,
                                              std::vector<float>& kernel,
                                              int& kernelRadius,
                                              bool& kernelDirty)
{
    if (amount < 0.001f)
        return;

    // Build or rebuild the Gaussian kernel
    if (kernelDirty)
    {
        const int radius = std::max(1, static_cast<int>(amount * 20.0f));
        const float sigma = std::max(0.5f, amount * 10.0f);
        buildGaussianKernel(kernel, kernelRadius, radius, sigma);
        kernelDirty = false;
    }

    if (kernelRadius <= 0 || kernel.empty())
        return;

    // Convolve amplitudes with the Gaussian kernel
    if constexpr (kMaxPartials > 0)
        std::memset(blurred_, 0, sizeof(blurred_));

    for (int i = 0; i < kMaxPartials; ++i)
    {
        float sum = 0.0f;
        for (int j = -kernelRadius; j <= kernelRadius; ++j)
        {
            const int idx = std::max(0, std::min(kMaxPartials - 1, i + j));
            sum += data.amplitude[idx]
                 * kernel[static_cast<size_t>(j + kernelRadius)];
        }
        blurred_[i] = sum;
    }

    // Write back
    for (int i = 0; i < kMaxPartials; ++i)
        data.amplitude[i] = blurred_[i];

    data.updateActiveMask();
}

void SpectralFreezeEngine::applySpectralTilt(PartialDataSIMD& data, float tilt)
{
    if (std::abs(tilt) < 0.001f)
        return;

    // Tilt scales amplitudes linearly with frequency:
    //   amp *= 1.0 + tilt * normFreq
    for (int i = 0; i < kMaxPartials; ++i)
    {
        const float normFreq = static_cast<float>(i)
                             / static_cast<float>(kMaxPartials - 1);
        const float gain = 1.0f + tilt * normFreq;

        // Limit extreme gains
        data.amplitude[i] *= std::max(0.01f, std::min(10.0f, gain));
    }

    data.updateActiveMask();
}

//==============================================================================
// Gaussian kernel builder
//==============================================================================

void SpectralFreezeEngine::buildGaussianKernel(std::vector<float>& kernel,
                                                int& radius,
                                                int newRadius, float sigma)
{
    radius = newRadius;
    const int size = 2 * radius + 1;
    kernel.resize(static_cast<size_t>(size));

    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i)
    {
        const float val = std::exp(-0.5f * (static_cast<float>(i) * static_cast<float>(i))
                                         / (sigma * sigma));
        kernel[static_cast<size_t>(i + radius)] = val;
        sum += val;
    }

    // Normalise so the kernel is energy-preserving
    if (sum > 0.0f)
    {
        const float invSum = 1.0f / sum;
        for (auto& k : kernel)
            k *= invSum;
    }
}

//==============================================================================
// Filter management
//==============================================================================

void SpectralFreezeEngine::updateFilters()
{
    if (sampleRate_ <= 0.0)
        return;

    // Dry HPF — removes low frequencies from the dry signal
    {
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(
            sampleRate_, dryHP_);
        for (auto& f : dryHPFilters_)
            f.coefficients = coeffs;
    }

    // Wet LPF — removes high frequencies from the frozen signal
    {
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
            sampleRate_, wetLP_);
        for (auto& f : wetLPFilters_)
            f.coefficients = coeffs;
    }

    filtersDirty_ = false;
}

} // namespace ana
