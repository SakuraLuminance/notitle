#include "TimeStretchEngine.h"
#include <algorithm>
#include <cstring>

namespace ana {

//==============================================================================
// StretchCurve
//==============================================================================

void StretchCurve::addPoint(double time, double speed)
{
    time  = std::max(0.0, std::min(1.0, time));
    speed = std::max(0.1, std::min(10.0, speed));

    // Insert sorted by time
    auto it = std::lower_bound(points.begin(), points.end(), time,
        [](const StretchCurvePoint& p, double t) { return p.time < t; });

    if (it != points.end() && it->time == time)
        it->speed = speed;   // Replace existing point at this time
    else
        points.insert(it, { time, speed });
}

void StretchCurve::clear()
{
    points.clear();
}

double StretchCurve::getSpeedAt(double normalizedTime) const
{
    if (points.empty())
        return 1.0;

    normalizedTime = std::max(0.0, std::min(1.0, normalizedTime));

    if (points.size() == 1 || normalizedTime <= points.front().time)
        return points.front().speed;

    if (normalizedTime >= points.back().time)
        return points.back().speed;

    for (size_t i = 0; i < points.size() - 1; ++i)
    {
        if (normalizedTime >= points[i].time && normalizedTime <= points[i + 1].time)
        {
            double range = points[i + 1].time - points[i].time;
            if (range < 1e-12)
                return points[i].speed;

            double t = (normalizedTime - points[i].time) / range;
            return points[i].speed + t * (points[i + 1].speed - points[i].speed);
        }
    }

    return points.back().speed;
}

//==============================================================================
// TimeStretchEngine
//==============================================================================

TimeStretchEngine::TimeStretchEngine()
{
    const int order = static_cast<int>(std::log2(fftSize_));
    fft_ = std::make_unique<juce::dsp::FFT>(order);
    recomputeWindow();
}

//==============================================================================
// Setters
//==============================================================================

void TimeStretchEngine::setStretchRatio(float ratio)
{
    stretchRatio_ = std::max(0.25f, std::min(4.0f, ratio));
}

void TimeStretchEngine::setSpeedCurve(const StretchCurve& curve)
{
    curve_ = curve;
    hasCurve_ = !curve_.points.empty();
    mapping_.clear();
}

void TimeStretchEngine::setDefaultSpeed(float speed)
{
    defaultSpeed_ = std::max(0.1f, std::min(10.0f, speed));
}

void TimeStretchEngine::setMidiFitMode(bool enabled)
{
    midiFitMode_ = enabled;
}

void TimeStretchEngine::setTargetDuration(double seconds)
{
    targetDuration_ = std::max(0.001, seconds);
}

void TimeStretchEngine::setNoteVelocity(float velocity)
{
    noteVelocity_ = std::max(0.0f, std::min(1.0f, velocity));
}

void TimeStretchEngine::setSampleRate(double sr)
{
    sampleRate_ = std::max(8000.0, sr);
}

//==============================================================================
// Reset
//==============================================================================

void TimeStretchEngine::reset()
{
    analysisData_.clear();
    synPhase_.clear();
    mapping_.clear();
}

//==============================================================================
// Public processing
//==============================================================================

std::vector<float> TimeStretchEngine::process(const std::vector<float>& input,
                                                int numSamples)
{
    const int actualSamples = std::min(numSamples, static_cast<int>(input.size()));

    if (actualSamples == 0)
        return {};

    // Determine effective stretch ratio
    double effectiveRatio = stretchRatio_;

    if (midiFitMode_)
    {
        const double inputDuration = static_cast<double>(actualSamples) / sampleRate_;
        if (inputDuration > 0.0 && targetDuration_ > 0.0)
            effectiveRatio = inputDuration / targetDuration_;
    }

    // Clamp to supported range
    effectiveRatio = std::max(0.25, std::min(4.0, effectiveRatio));

    // Choose algorithm based on ratio and mode
    if (effectiveRatio < 0.33 || effectiveRatio > 3.0)
        return processGranular(input, effectiveRatio);
    
    // For moderate stretches (especially for percussive material), WSOLA is often better
    // Since noteVelocity_ > 0.8 is meant for percussive/transient preservation, use WSOLA then
    if (noteVelocity_ > 0.8f && effectiveRatio >= 0.5 && effectiveRatio <= 2.0)
        return processWSOLA(input, effectiveRatio);

    return processPhaseVocoder(input, effectiveRatio);
}

std::vector<float> TimeStretchEngine::processToDuration(
    const std::vector<float>& input, double durationSeconds)
{
    if (input.empty() || durationSeconds <= 0.0)
        return {};

    const double inputDuration = static_cast<double>(input.size()) / sampleRate_;
    double ratio = inputDuration / durationSeconds;
    ratio = std::max(0.25, std::min(4.0, ratio));

    if (ratio < 0.33 || ratio > 3.0)
        return processGranular(input, ratio);

    if (noteVelocity_ > 0.8f && ratio >= 0.5 && ratio <= 2.0)
        return processWSOLA(input, ratio);

    return processPhaseVocoder(input, ratio);
}

//==============================================================================
// Window computation
//==============================================================================

void TimeStretchEngine::recomputeWindow()
{
    windowTable_.resize(fftSize_);
    for (int i = 0; i < fftSize_; ++i)
    {
        windowTable_[i] = 0.5f * (1.0f - std::cos(
            juce::MathConstants<float>::twoPi * i / (fftSize_ - 1)));
    }
}

//==============================================================================
// Speed curve helpers
//==============================================================================

double TimeStretchEngine::getEffectiveSpeed(double normalizedSourcePos) const
{
    if (hasCurve_)
        return curve_.getSpeedAt(normalizedSourcePos);
    return stretchRatio_;
}

void TimeStretchEngine::precomputeMapping(int numInputSamples)
{
    mapping_.clear();

    if (numInputSamples <= 0)
        return;

    const double numFramesD = static_cast<double>(
        std::max(0, (numInputSamples - fftSize_) / hopSize_ + 1));

    double cumOutPos = 0.0;

    for (int i = 0; i < static_cast<int>(numFramesD); ++i)
    {
        const double srcPos = static_cast<double>(i) * hopSize_;
        const double normPos = srcPos / static_cast<double>(numInputSamples);
        const double speed = getEffectiveSpeed(normPos);

        mapping_.push_back({ cumOutPos, srcPos });

        // Output increment for this analysis hop
        cumOutPos += static_cast<double>(hopSize_) / std::max(0.001, speed);
    }

    // Add the final point (end of source)
    if (!mapping_.empty())
    {
        const double finalSrcPos = static_cast<double>(numInputSamples);
        const double finalSpeed  = getEffectiveSpeed(1.0);
        const double finalOutPos = cumOutPos +
            (finalSrcPos - mapping_.back().sourcePos) / std::max(0.001, finalSpeed);
        mapping_.push_back({ finalOutPos, finalSrcPos });
    }
}

double TimeStretchEngine::getSourceFromOutput(double outputPos) const
{
    // Linear fallback (no curve)
    if (!hasCurve_ || mapping_.empty())
        return outputPos * stretchRatio_;

    // Clamp to within the mapping range
    if (outputPos <= mapping_.front().outputPos)
        return mapping_.front().sourcePos;

    if (outputPos >= mapping_.back().outputPos)
        return mapping_.back().sourcePos;

    // Binary search for the segment containing outputPos
    auto it = std::lower_bound(mapping_.begin(), mapping_.end(), outputPos,
        [](const MappingPoint& p, double val) { return p.outputPos < val; });

    if (it == mapping_.begin())
        return it->sourcePos;

    if (it == mapping_.end())
        return mapping_.back().sourcePos;

    const auto prev = it - 1;
    const double range = it->outputPos - prev->outputPos;

    if (range < 1e-12)
        return prev->sourcePos;

    const double t = (outputPos - prev->outputPos) / range;
    return prev->sourcePos + t * (it->sourcePos - prev->sourcePos);
}

//==============================================================================
// PhaseVocoder — analysis
//==============================================================================

std::vector<float> TimeStretchEngine::processPhaseVocoder(
    const std::vector<float>& input, double stretchRatio)
{
    const int numInputSamples = static_cast<int>(input.size());
    const int numBins = fftSize_ / 2 + 1;

    // Not enough samples for a single FFT frame
    if (numInputSamples < fftSize_)
    {
        // Can't stretch; return input as-is (padded to reasonable length)
        if (stretchRatio >= 1.0)
            return std::vector<float>(input.begin(), input.begin() + numInputSamples);
        else
            return std::vector<float>(input.begin(), input.begin() + numInputSamples);
    }

    // ------ Analysis: STFT into magnitude/phase frames ------
    const int numAnalysisFrames =
        (numInputSamples - fftSize_) / hopSize_ + 1;

    analysisData_.clear();
    analysisData_.reserve(static_cast<size_t>(numAnalysisFrames));

    std::vector<float> real(static_cast<size_t>(fftSize_), 0.0f);
    std::vector<float> imag(static_cast<size_t>(fftSize_), 0.0f);

    for (int i = 0; i < numAnalysisFrames; ++i)
    {
        const int startPos = i * hopSize_;

        // Windowed frame copy via SIMD
        SIMDKernels::vectorMul(real.data(),
                                input.data() + startPos,
                                windowTable_.data(),
                                fftSize_);

        // Zero-pad remaining analysis buffer positions
        // (already zeroed from vector ctor, but ensure frames near end are safe)
        const int remaining = fftSize_ - (numInputSamples - startPos);
        if (remaining > 0)
            std::memset(real.data() + fftSize_ - remaining, 0,
                        static_cast<size_t>(remaining) * sizeof(float));

        // Forward FFT
        std::fill(imag.begin(), imag.end(), 0.0f);
        fft_->perform(real.data(), imag.data(), false);

        // Extract magnitude and phase (bins 0 .. fftSize/2)
        AnalysisFrame frame;
        frame.magnitude.resize(static_cast<size_t>(numBins));
        frame.phase.resize(static_cast<size_t>(numBins));

        for (int k = 0; k < numBins; ++k)
        {
            const float r = real[static_cast<size_t>(k)];
            const float j = imag[static_cast<size_t>(k)];
            frame.magnitude[static_cast<size_t>(k)] = std::sqrt(r * r + j * j);
            frame.phase[static_cast<size_t>(k)]     = std::atan2(j, r);
        }

        analysisData_.push_back(std::move(frame));
    }

    // ------ Precompute speed-curve mapping ------
    if (hasCurve_)
        precomputeMapping(numInputSamples);

    // ------ Estimate output length ------
    int outputLength;
    if (hasCurve_ && !mapping_.empty())
        outputLength = static_cast<int>(std::ceil(mapping_.back().outputPos)) + fftSize_;
    else
        outputLength = static_cast<int>(std::ceil(
            static_cast<double>(numInputSamples) / stretchRatio)) + fftSize_;

    outputLength = std::max(outputLength, fftSize_);

    std::vector<float> output(static_cast<size_t>(outputLength), 0.0f);

    // ------ Synthesis: phase-vocoder overlap-add ------
    synPhase_.resize(static_cast<size_t>(numBins));
    std::fill(synPhase_.begin(), synPhase_.end(), 0.0f);

    std::vector<float> frameReal(fftSize_, 0.0f);
    std::vector<float> frameImag(fftSize_, 0.0f);
    std::vector<float> windowedOut(fftSize_, 0.0f);

    double prevSrcPos = 0.0;
    bool firstFrame = true;

    for (int v = 0;; ++v)
    {
        const double outPos = static_cast<double>(v) * hopSize_;

        // Source position for this output frame
        double srcPos;
        if (hasCurve_)
            srcPos = getSourceFromOutput(outPos);
        else
            srcPos = outPos * stretchRatio;

        // Stop when we run out of source audio
        if (srcPos >= static_cast<double>(numInputSamples - 1))
            break;

        srcPos = std::max(0.0, srcPos);

        // Find analysis frame(s) for this source position
        const double frameIdx = srcPos / static_cast<double>(hopSize_);
        const int u0 = std::max(0, std::min(
            static_cast<int>(frameIdx), numAnalysisFrames - 1));
        const int u1 = std::min(u0 + 1, numAnalysisFrames - 1);
        const float interpAlpha = static_cast<float>(frameIdx - std::floor(frameIdx));

        const double srcStep = firstFrame ? 0.0 : (srcPos - prevSrcPos);
        const float srcStepF = static_cast<float>(srcStep);

        // Process each bin
        for (int k = 0; k < numBins; ++k)
        {
            const size_t kz = static_cast<size_t>(k);

            // --- Magnitude ---
            float mag;
            if (firstFrame || interpAlpha < 1e-6f)
            {
                mag = analysisData_[static_cast<size_t>(u0)].magnitude[kz];
            }
            else
            {
                const float m0 = analysisData_[static_cast<size_t>(u0)].magnitude[kz];
                const float m1 = analysisData_[static_cast<size_t>(u1)].magnitude[kz];

                // Velocity-controlled interpolation: at high velocity we
                // lean toward nearest neighbour to preserve transients
                if (noteVelocity_ > 0.5f)
                {
                    // Blend: more nearest-neighbour at high velocity
                    const float nnBlend = (noteVelocity_ - 0.5f) * 2.0f; // 0..1
                    const float nn = (interpAlpha < 0.5f) ? m0 : m1;
                    mag = (1.0f - nnBlend) * ((1.0f - interpAlpha) * m0 + interpAlpha * m1)
                        + nnBlend * nn;
                }
                else
                {
                    mag = (1.0f - interpAlpha) * m0 + interpAlpha * m1;
                }
            }

            // Apply velocity-based spectral tilt (gentle)
            // Higher velocity → slightly boosted highs for percussive feel
            const float tilt = 1.0f + (noteVelocity_ - 0.5f) * 0.2f *
                               static_cast<float>(k) / static_cast<float>(numBins);
            mag = std::max(0.0f, mag * tilt);

            // --- Phase: instantaneous-frequency propagation ---
            float phase;
            if (firstFrame || u0 == 0)
            {
                // First frame: use analysis phase directly
                phase = analysisData_[static_cast<size_t>(u0)].phase[kz];
            }
            else
            {
                const float phiPrev = analysisData_[static_cast<size_t>(u0 - 1)].phase[kz];
                const float phiCurr = analysisData_[static_cast<size_t>(u0)].phase[kz];

                // Unwrapped phase difference
                float deltaPhi = phiCurr - phiPrev;
                const float expected = juce::MathConstants<float>::twoPi *
                    static_cast<float>(k) * static_cast<float>(hopSize_) /
                    static_cast<float>(fftSize_);

                // Deviation from bin-centre frequency (wrap to [-π, π])
                float deviation = deltaPhi - expected;
                deviation = std::atan2(std::sin(deviation), std::cos(deviation));

                // Instantaneous frequency = bin centre + deviation / Ha
                // Phase increment = instFreq * sourceStep
                const float instFreq = juce::MathConstants<float>::twoPi *
                    static_cast<float>(k) / static_cast<float>(fftSize_)
                    + deviation / static_cast<float>(hopSize_);

                const float phaseInc = instFreq * srcStepF;
                phase = synPhase_[kz] + phaseInc;
            }

            // Wrap phase
            phase = std::atan2(std::sin(phase), std::cos(phase));
            synPhase_[kz] = phase;

            // Build complex spectrum
            frameReal[kz] = mag * std::cos(phase);
            frameImag[kz] = mag * std::sin(phase);
        }

        // Mirror spectrum to maintain conjugate symmetry for the IFFT
        // Bin 0 (DC) and bin fftSize/2 (Nyquist) must be purely real
        frameImag[0] = 0.0f;
        frameImag[static_cast<size_t>(fftSize_ / 2)] = 0.0f;

        for (int k = fftSize_ / 2 + 1; k < fftSize_; ++k)
        {
            const size_t kz = static_cast<size_t>(k);
            const size_t mirrorK = static_cast<size_t>(fftSize_ - k);
            frameReal[kz] =  frameReal[mirrorK];
            frameImag[kz] = -frameImag[mirrorK];
        }

        // Inverse FFT
        fft_->perform(frameReal.data(), frameImag.data(), true);

        // Apply synthesis window and overlap-add
        SIMDKernels::vectorMul(windowedOut.data(),
                                frameReal.data(),
                                windowTable_.data(),
                                fftSize_);

        const int writePos = static_cast<int>(std::round(outPos));
        for (int i = 0; i < fftSize_; ++i)
        {
            const int wp = writePos + i;
            if (wp >= 0 && wp < outputLength)
                output[static_cast<size_t>(wp)] += windowedOut[static_cast<size_t>(i)];
        }

        prevSrcPos = srcPos;
        firstFrame = false;
    }

    // Find the last non-zero region and trim trailing silence
    int lastActive = static_cast<int>(output.size()) - 1;
    while (lastActive > 0 && std::abs(output[static_cast<size_t>(lastActive)]) < 1e-8f)
        --lastActive;

    // Keep a bit of trailing tail for natural decay
    const int trimmedLen = std::min(
        static_cast<int>(output.size()),
        lastActive + hopSize_ + 1);

    output.resize(static_cast<size_t>(trimmedLen));
    return output;
}

//==============================================================================
// Granular time stretch (fallback for extreme ratios)
//==============================================================================

std::vector<float> TimeStretchEngine::processGranular(
    const std::vector<float>& input, double stretchRatio)
{
    const int numIn = static_cast<int>(input.size());
    const int grainSize = fftSize_;
    const int halfGrain = grainSize / 2;

    // Source hop: for extreme stretching we want sufficient grain density
    // in the output, so we adjust the source hop accordingly.
    double sourceHop;
    if (stretchRatio < 1.0)
    {
        // Slowing down: use a smaller source hop to ensure output overlap
        // Aim for output hop ≈ hopSize_ (75% overlap)
        sourceHop = static_cast<double>(hopSize_) * stretchRatio;
        sourceHop = std::max(64.0, sourceHop);
    }
    else
    {
        // Speeding up: standard source hop is fine; grains will naturally
        // be denser in the output.
        sourceHop = static_cast<double>(hopSize_);
    }

    const double outHop = sourceHop / stretchRatio;

    // Estimate output length
    const int numGrains = std::max(0,
        (numIn - grainSize) / static_cast<int>(sourceHop) + 1);
    const int estimatedOutLen = static_cast<int>(
        std::ceil(static_cast<double>(numGrains) * outHop)) + grainSize + 1;

    std::vector<float> output(static_cast<size_t>(estimatedOutLen), 0.0f);
    std::vector<float> normEnv(static_cast<size_t>(estimatedOutLen), 0.0f);

    // Hann envelope for each grain
    std::vector<float> grainEnv(static_cast<size_t>(grainSize));
    for (int i = 0; i < grainSize; ++i)
    {
        grainEnv[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(
            juce::MathConstants<float>::twoPi * i / (grainSize - 1)));
    }

    // Process grains
    double outPos = 0.0;

    for (int srcPos = 0; srcPos + grainSize <= numIn;
         srcPos += static_cast<int>(std::round(sourceHop)))
    {
        // Write position in output (centred grain)
        const int writePos = static_cast<int>(std::round(outPos)) - halfGrain;

        for (int i = 0; i < grainSize; ++i)
        {
            const int wp = writePos + i;
            if (wp >= 0 && wp < estimatedOutLen)
            {
                const float sample = input[static_cast<size_t>(srcPos + i)]
                                   * grainEnv[static_cast<size_t>(i)];
                output[static_cast<size_t>(wp)] += sample;
                normEnv[static_cast<size_t>(wp)] += grainEnv[static_cast<size_t>(i)];
            }
        }

        outPos += outHop;
    }

    // Normalize to compensate for varying overlap density
    for (int i = 0; i < estimatedOutLen; ++i)
    {
        const size_t iz = static_cast<size_t>(i);
        if (normEnv[iz] > 1e-6f)
            output[iz] /= normEnv[iz];
    }

    // Trim trailing silence
    int lastActive = estimatedOutLen - 1;
    while (lastActive > 0 && std::abs(output[static_cast<size_t>(lastActive)]) < 1e-8f)
        --lastActive;

    output.resize(static_cast<size_t>(std::min(estimatedOutLen, lastActive + hopSize_ + 1)));
    return output;
}

//==============================================================================
// WSOLA time stretch
//==============================================================================

std::vector<float> TimeStretchEngine::processWSOLA(
    const std::vector<float>& input, double stretchRatio)
{
    const int numIn = static_cast<int>(input.size());
    const int windowSize = 2048; // WSOLA uses smaller windows typically (e.g., 40-50ms)
    const int halfWindow = windowSize / 2;
    const int searchRadius = windowSize / 4; // Search range for similarity

    double synHop = static_cast<double>(windowSize) / 2.0; // 50% overlap synthesis
    double anaHop = synHop / stretchRatio;

    // Estimate output length
    const int numFrames = std::max(0, static_cast<int>((numIn - windowSize - searchRadius) / anaHop));
    const int estimatedOutLen = static_cast<int>(numFrames * synHop) + windowSize;

    std::vector<float> output(static_cast<size_t>(estimatedOutLen), 0.0f);
    std::vector<float> normEnv(static_cast<size_t>(estimatedOutLen), 0.0f);

    // Hann envelope
    std::vector<float> env(static_cast<size_t>(windowSize));
    for (int i = 0; i < windowSize; ++i)
    {
        env[i] = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi * i / (windowSize - 1)));
    }

    int currentSynPos = 0;
    double currentAnaPos = 0.0;
    int lastSrcPos = 0;

    // First frame is copied directly
    if (numIn >= windowSize)
    {
        for (int i = 0; i < windowSize; ++i)
        {
            float val = input[i] * env[i];
            output[i] += val;
            normEnv[i] += env[i];
        }
    }
    
    currentSynPos += static_cast<int>(synHop);
    currentAnaPos += anaHop;

    while (currentAnaPos + windowSize + searchRadius < numIn && currentSynPos + windowSize < estimatedOutLen)
    {
        // We want to find a block in [targetAnaPos - searchRadius, targetAnaPos + searchRadius]
        // that best matches the natural continuation of the last block.
        // The natural continuation of the last block started at lastSrcPos + synHop
        
        const int naturalContinuationPos = lastSrcPos + static_cast<int>(synHop);
        const int targetAnaPos = static_cast<int>(std::round(currentAnaPos));
        
        int bestDelta = 0;
        
        if (naturalContinuationPos + windowSize <= numIn)
        {
            float maxCorr = -1e9f;
            
            for (int delta = -searchRadius; delta <= searchRadius; ++delta)
            {
                const int candidatePos = targetAnaPos + delta;
                if (candidatePos < 0 || candidatePos + windowSize > numIn) continue;

                // Cross-correlation between naturalContinuation and candidate (usually done over half window)
                float corr = 0.0f;
                for (int i = 0; i < halfWindow; ++i)
                {
                    corr += input[naturalContinuationPos + i] * input[candidatePos + i];
                }

                if (corr > maxCorr)
                {
                    maxCorr = corr;
                    bestDelta = delta;
                }
            }
        }

        const int selectedPos = targetAnaPos + bestDelta;

        // OLA the selected block
        for (int i = 0; i < windowSize; ++i)
        {
            float val = input[selectedPos + i] * env[i];
            output[currentSynPos + i] += val;
            normEnv[currentSynPos + i] += env[i];
        }

        lastSrcPos = selectedPos;
        currentSynPos += static_cast<int>(synHop);
        currentAnaPos += anaHop;
    }

    // Normalize
    for (int i = 0; i < estimatedOutLen; ++i)
    {
        if (normEnv[i] > 1e-6f)
            output[i] /= normEnv[i];
    }

    // Trim trailing silence
    int lastActive = estimatedOutLen - 1;
    while (lastActive > 0 && std::abs(output[lastActive]) < 1e-8f)
        --lastActive;

    output.resize(std::min(estimatedOutLen, lastActive + windowSize));
    return output;
}

} // namespace ana
