#include "SpectralSequencer.h"

namespace ana {

//==============================================================================
// Construction
//==============================================================================
SpectralSequencer::SpectralSequencer()
{
    steps_.resize(numSteps_);

    // Default: all steps active, default parameters
    for (auto& step : steps_)
        step = SpectralStep{};

    prevStep_ = SpectralStep{};

    // 10 ms crossfade at 44.1 kHz
    crossfadeSamples_ = 441.0f;

    // Clear feedback buffers
    std::memset(feedbackFreq_, 0, sizeof(feedbackFreq_));
    std::memset(feedbackAmp_, 0, sizeof(feedbackAmp_));

    // Pre-allocate scratch buffers to avoid heap allocation in audio thread
    scratch_weights_.reserve(512);
    scratch_activeIndices_.reserve(512);
    scratch_blurredAmp_.reserve(512);
    scratch_partialEntries_.reserve(512);

    computeStepLength();
}

//==============================================================================
// Step management
//==============================================================================
void SpectralSequencer::setNumSteps(int numSteps)
{
    numSteps = std::clamp(numSteps, 1, 64);
    steps_.resize(static_cast<size_t>(numSteps));
    numSteps_ = numSteps;

    if (currentStep_ >= numSteps_)
        currentStep_ = 0;

    computeStepLength();
}

void SpectralSequencer::setStep(int index, const SpectralStep& step)
{
    if (index >= 0 && index < numSteps_)
        steps_[static_cast<size_t>(index)] = step;
}

const SpectralStep& SpectralSequencer::getStep(int index) const
{
    return steps_[static_cast<size_t>(index)];
}

SpectralStep& SpectralSequencer::getStep(int index)
{
    return steps_[static_cast<size_t>(index)];
}

//==============================================================================
// Global parameters
//==============================================================================
void SpectralSequencer::setTempo(float bpm)
{
    tempo_ = std::max(1.0f, bpm);
    computeStepLength();
}

void SpectralSequencer::setSwing(float amount)
{
    swing_ = std::clamp(amount, 0.0f, 1.0f);
}

void SpectralSequencer::setStepLength(int samples)
{
    stepLengthOverride_ = std::max(64, samples);
}

void SpectralSequencer::setStepLengthBeats(float beats)
{
    stepLengthBeats_ = std::max(0.0625f, beats);
    computeStepLength();
}

void SpectralSequencer::setBeatDivision(int division)
{
    beatDivision_ = std::clamp(division, 1, 64);
    computeStepLength();
}

void SpectralSequencer::setSampleRate(double sampleRate)
{
    sampleRate_ = std::max(44100.0, sampleRate);
    crossfadeSamples_ = static_cast<float>(sampleRate_ * 0.01); // 10 ms
    computeStepLength();
}

//==============================================================================
// Transport
//==============================================================================
void SpectralSequencer::start()
{
    playing_ = true;
    if (currentStep_ >= numSteps_)
        currentStep_ = 0;
    totalSamplesInStep_ = 0;
}

void SpectralSequencer::stop()
{
    playing_ = false;
}

bool SpectralSequencer::isPlaying() const
{
    return playing_;
}

void SpectralSequencer::resetPosition()
{
    currentStep_ = 0;
    totalSamplesInStep_ = 0;
    crossfadeCount_ = 0;
}

void SpectralSequencer::setPosition(int step)
{
    if (step >= 0 && step < numSteps_)
    {
        currentStep_ = step;
        totalSamplesInStep_ = 0;
        crossfadeCount_ = 0;
    }
}

//==============================================================================
// Randomization
//==============================================================================
void SpectralSequencer::randomizeSteps(float amount)
{
    amount = std::clamp(amount, 0.0f, 1.0f);
    for (int i = 0; i < numSteps_; ++i)
        randomizeStep(i, amount);
}

void SpectralSequencer::randomizeStep(int index, float amount)
{
    if (index < 0 || index >= numSteps_)
        return;

    amount = std::clamp(amount, 0.0f, 1.0f);
    auto& step = steps_[static_cast<size_t>(index)];

    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    const float a = amount;

    // Only randomize if amount > 0
    if (a > 0.0f)
    {
        step.pitchShift      = 1.0f + (dist(rng_) * 2.0f - 1.0f) * a * 2.0f;
        step.pitchShift      = std::clamp(step.pitchShift, 0.25f, 4.0f);
        step.pitchQuantize   = dist(rng_) * a * 12.0f;
        step.filterCutoff    = 50.0f * std::pow(400.0f, dist(rng_) * a);
        step.filterCutoff    = std::clamp(step.filterCutoff, 20.0f, 20000.0f);
        step.filterResonance = dist(rng_) * a;
        step.spectralBlur    = dist(rng_) * a;
        step.spectralTilt    = (dist(rng_) * 2.0f - 1.0f) * a;
        step.formantShift    = (dist(rng_) * 24.0f - 12.0f) * a;
        step.feedback        = dist(rng_) * a * 0.5f;
        step.noise           = dist(rng_) * a * 0.3f;
        step.bitcrush        = dist(rng_) * a;
        step.gain            = 0.25f + dist(rng_) * 0.75f * (1.0f - a * 0.5f);
        step.active          = dist(rng_) > a * 0.3f;
    }
}

//==============================================================================
// Preset patterns
//==============================================================================
void SpectralSequencer::loadPreset(Preset preset)
{
    stop();
    resetPosition();

    switch (preset)
    {
        case Preset::Off:           loadPresetGate();       break;
        case Preset::Gate:          loadPresetGate();       break;
        case Preset::FilterSweep:   loadPresetFilterSweep(); break;
        case Preset::PitchRise:     loadPresetPitchRise();  break;
        case Preset::Chaos:         loadPresetChaos();      break;
        case Preset::FormantWobble: loadPresetFormantWobble(); break;
        case Preset::HarmonicSweep: loadPresetHarmonicSweep(); break;
    }
}

void SpectralSequencer::loadPresetGate()
{
    setNumSteps(16);
    setBeatDivision(16);
    setTempo(120.0f);
    setSwing(0.0f);

    for (int i = 0; i < numSteps_; ++i)
    {
        auto& s = steps_[static_cast<size_t>(i)];
        s = SpectralStep{};

        // Classic 16th note gate: on off on off on off on off...
        // With occasional variations
        const bool gateOn = ((i % 4) < 2) || (i == 8) || (i == 12);
        s.gain     = gateOn ? 1.0f : 0.0f;
        s.active   = true;
        s.numBeats = 1;

        // Accent on 1, 5, 9, 13
        if (i % 4 == 0)
            s.gain = 1.2f;

        // Ghost on step 11
        if (i == 11)
            s.gain = 0.4f;
    }
}

void SpectralSequencer::loadPresetFilterSweep()
{
    setNumSteps(32);
    setBeatDivision(16);
    setTempo(100.0f);
    setSwing(0.2f);

    for (int i = 0; i < numSteps_; ++i)
    {
        auto& s = steps_[static_cast<size_t>(i)];
        s = SpectralStep{};

        const float progress = static_cast<float>(i) / static_cast<float>(numSteps_);
        const float cutoffMin = 80.0f;
        const float cutoffMax = 18000.0f;

        // Cutoff sweeps up then down with some curve
        const float sweepPos = std::sin(progress * juce::MathConstants<float>::pi);
        s.filterCutoff = cutoffMin + (cutoffMax - cutoffMin) * sweepPos;
        s.filterResonance = 0.2f + 0.6f * (1.0f - sweepPos);
        s.gain = 0.8f + 0.2f * sweepPos;
        s.active = (i % 2 == 0) || (sweepPos > 0.5f);

        // Subtle spectral tilt to match
        s.spectralTilt = (sweepPos - 0.5f) * 0.4f;
    }
}

void SpectralSequencer::loadPresetPitchRise()
{
    setNumSteps(16);
    setBeatDivision(8);
    setTempo(90.0f);
    setSwing(0.0f);

    for (int i = 0; i < numSteps_; ++i)
    {
        auto& s = steps_[static_cast<size_t>(i)];
        s = SpectralStep{};

        const float progress = static_cast<float>(i) / static_cast<float>(numSteps_ - 1);

        // Pitch rises from 0.5 to 3.0, exponential
        s.pitchShift = 0.5f * std::pow(6.0f, progress);

        // Quantization increases as pitch rises
        s.pitchQuantize = progress * 12.0f;

        // Filter opens as pitch rises
        s.filterCutoff = 200.0f + 19800.0f * progress;
        s.filterResonance = 0.7f * (1.0f - progress);

        s.gain = 0.9f;

        // Spread gets wider
        s.spectralTilt = progress * 0.3f;

        // Add bitcrush at the top
        s.bitcrush = progress > 0.7f ? (progress - 0.7f) * 3.0f * 0.5f : 0.0f;
    }
}

void SpectralSequencer::loadPresetChaos()
{
    setNumSteps(32);
    setBeatDivision(16);
    setTempo(130.0f);
    setSwing(0.3f);

    // Overwrite every step with chaos
    randomizeSteps(0.9f);

    // But pin a few steps for structure
    for (int i = 0; i < numSteps_; i += 8)
    {
        auto& s = steps_[static_cast<size_t>(i)];
        s.gain = 1.2f;
        s.active = true;
    }
}

void SpectralSequencer::loadPresetFormantWobble()
{
    setNumSteps(32);
    setBeatDivision(16);
    setTempo(110.0f);
    setSwing(0.1f);

    for (int i = 0; i < numSteps_; ++i)
    {
        auto& s = steps_[static_cast<size_t>(i)];
        s = SpectralStep{};

        // Formant shift oscillates like a slow LFO
        const float phase = static_cast<float>(i) / 8.0f * juce::MathConstants<float>::pi;
        s.formantShift = std::sin(phase) * 8.0f;
        s.gain = 0.85f + 0.15f * (0.5f + 0.5f * std::sin(phase * 0.7f));

        // Spectral wobble
        s.spectralBlur = 0.1f + 0.3f * (0.5f + 0.5f * std::cos(phase * 0.5f));
        s.filterCutoff = 5000.0f + 3000.0f * (0.5f + 0.5f * std::sin(phase * 0.3f));
        s.filterResonance = 0.3f;
    }
}

void SpectralSequencer::loadPresetHarmonicSweep()
{
    setNumSteps(64);
    setBeatDivision(16);
    setTempo(140.0f);
    setSwing(0.4f);

    for (int i = 0; i < numSteps_; ++i)
    {
        auto& s = steps_[static_cast<size_t>(i)];
        s = SpectralStep{};

        const float progress = static_cast<float>(i) / static_cast<float>(numSteps_);

        // Harmonic content changes in stages
        const int stage = (i / 16) % 4;
        switch (stage)
        {
            case 0: // Bright
                s.filterCutoff = 8000.0f;
                s.filterResonance = 0.1f;
                s.spectralTilt = 0.2f;
                break;
            case 1: // Warm
                s.filterCutoff = 800.0f;
                s.filterResonance = 0.3f;
                s.spectralTilt = -0.2f;
                break;
            case 2: // Metallic
                s.filterCutoff = 3000.0f;
                s.filterResonance = 0.7f;
                s.spectralBlur = 0.1f;
                s.spectralTilt = 0.4f;
                break;
            case 3: // Subdued
                s.filterCutoff = 400.0f;
                s.filterResonance = 0.5f;
                s.spectralTilt = -0.4f;
                s.gain = 0.5f;
                break;
        }

        // Pitch drift within each stage
        const float stagePhase = (i % 16) * juce::MathConstants<float>::twoPi / 16.0f;
        s.pitchShift = 1.0f + 0.3f * std::sin(stagePhase);

        // Subtle formant movement
        s.formantShift = 0.5f * std::sin(stagePhase * 0.7f + 1.2f);

        s.active = true;
    }
}

//==============================================================================
// Processing
//==============================================================================
void SpectralSequencer::process(PartialDataSIMD& partials, int numSamples)
{
    if (!playing_)
        return;

    // Advance timing
    totalSamplesInStep_ += numSamples;

    // Handle step advancement (may cross multiple step boundaries)
    int stepLen = getEffectiveStepLength(currentStep_);

    while (totalSamplesInStep_ >= stepLen && playing_)
    {
        totalSamplesInStep_ -= stepLen;

        // Store previous step before advancing
        prevStep_ = steps_[static_cast<size_t>(currentStep_)];
        crossfadeCount_ = 0;

        advanceStep();

        // Recalculate step length for new step (swing may differ)
        stepLen = getEffectiveStepLength(currentStep_);
    }

    // Compute effective step with crossfade if transitioning
    SpectralStep effectiveStep;
    const int crossfadeSamplesInt = static_cast<int>(crossfadeSamples_);

    if (crossfadeCount_ < crossfadeSamplesInt && crossfadeSamplesInt > 0)
    {
        const float t = static_cast<float>(crossfadeCount_)
                      / crossfadeSamples_;
        effectiveStep = SpectralStep::lerp(prevStep_,
                                           steps_[static_cast<size_t>(currentStep_)],
                                           t);
        crossfadeCount_ += numSamples;
    }
    else
    {
        effectiveStep = steps_[static_cast<size_t>(currentStep_)];
    }

    // Apply step effects
    applyStep(effectiveStep, partials);
}

void SpectralSequencer::processAudio(juce::AudioBuffer<float>& buffer)
{
    if (!playing_)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // For audio processing, we advance timing per sample and apply
    // per-sample gain changes
    for (int s = 0; s < numSamples; ++s)
    {
        ++totalSamplesInStep_;

        int stepLen = getEffectiveStepLength(currentStep_);

        if (totalSamplesInStep_ >= stepLen)
        {
            totalSamplesInStep_ = 0;
            prevStep_ = steps_[static_cast<size_t>(currentStep_)];
            crossfadeCount_ = 0;
            advanceStep();
        }

        // Compute effective gain with crossfade
        float effectiveGain;
        const int crossfadeSamplesInt = static_cast<int>(crossfadeSamples_);

        if (crossfadeCount_ < crossfadeSamplesInt && crossfadeSamplesInt > 0)
        {
            const float t = static_cast<float>(crossfadeCount_)
                          / crossfadeSamples_;
            const float prevGain = prevStep_.gain;
            const float curGain = steps_[static_cast<size_t>(currentStep_)].gain;
            effectiveGain = prevGain + (curGain - prevGain) * t;
            ++crossfadeCount_;
        }
        else
        {
            effectiveGain = steps_[static_cast<size_t>(currentStep_)].gain;
        }

        // Apply gain per sample across all channels
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* channelData = buffer.getWritePointer(ch);
            channelData[s] *= effectiveGain;
        }
    }
}

//==============================================================================
// Current step info
//==============================================================================
int SpectralSequencer::getCurrentStep() const
{
    return currentStep_;
}

float SpectralSequencer::getStepProgress() const
{
    const int stepLen = getEffectiveStepLength(currentStep_);
    if (stepLen <= 0)
        return 0.0f;

    return static_cast<float>(totalSamplesInStep_)
         / static_cast<float>(stepLen);
}

//==============================================================================
// Reset
//==============================================================================
void SpectralSequencer::reset()
{
    stop();
    resetPosition();
    crossfadeCount_ = 0;
    std::memset(feedbackFreq_, 0, sizeof(feedbackFreq_));
    std::memset(feedbackAmp_, 0, sizeof(feedbackAmp_));
}

//==============================================================================
// Private helpers
//==============================================================================
void SpectralSequencer::advanceStep()
{
    currentStep_ = (currentStep_ + 1) % numSteps_;
}

void SpectralSequencer::computeStepLength()
{
    // samples per beat = sampleRate * 60 / tempo
    const double samplesPerBeat = sampleRate_ * 60.0
                                 / static_cast<double>(tempo_);

    // Number of beats per division
    // beatDivision_ = 16 means 16th notes → 0.25 beats per division
    const double beatsPerDivision = 4.0 / static_cast<double>(beatDivision_);

    // Samples per division tick
    const double samplesPerDivision = samplesPerBeat * beatsPerDivision;

    // Step length = samples per division * beats per step
    stepLengthSamples_ = static_cast<int>(samplesPerDivision
                                          * static_cast<double>(stepLengthBeats_));

    // Minimum step length
    stepLengthSamples_ = std::max(16, stepLengthSamples_);
}

int SpectralSequencer::getEffectiveStepLength(int stepIndex) const
{
    if (stepLengthOverride_ > 0)
        return stepLengthOverride_;

    int len = stepLengthSamples_;

    // Swing: delay every 2nd step
    // For steps where (stepIndex % 2 == 1), add extra samples
    if (swing_ > 0.0f && (stepIndex % 2) == 1)
    {
        const int swingSamples = static_cast<int>(static_cast<float>(len) * swing_ * 0.5f);
        len += swingSamples;
    }

    return len;
}

//==============================================================================
// Step application
//==============================================================================
void SpectralSequencer::applyStep(const SpectralStep& step,
                                   PartialDataSIMD& partials)
{
    if (!step.active)
        return;

    // Apply each effect in order
    applyPitchShift(step, partials);
    applyPitchQuantize(step, partials);
    applySpectralFilter(step, partials);
    applySpectralBlur(step, partials);
    applySpectralTilt(step, partials);
    applyFormantShift(step, partials);
    applyFeedback(step, partials);
    applyNoise(step, partials);
    applyBitcrush(step, partials);
    applyGain(step, partials);
}

void SpectralSequencer::applyStepAudio(const SpectralStep& step,
                                        juce::AudioBuffer<float>& buffer)
{
    if (!step.active)
    {
        buffer.clear();
        return;
    }

    buffer.applyGain(step.gain);
}

//==============================================================================
// Individual effect processors
//==============================================================================
void SpectralSequencer::applyPitchShift(const SpectralStep& step,
                                         PartialDataSIMD& partials)
{
    const float shift = step.pitchShift;
    if (std::abs(shift - 1.0f) < 1e-6f)
        return;

    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    for (int i = 0; i < kMax; ++i)
    {
        if (partials.isActive(i))
        {
            partials.frequency[i] *= shift;

            // Clamp frequency to valid range
            partials.frequency[i] = std::clamp(partials.frequency[i],
                                               1.0f, 20000.0f);
        }
    }
}

void SpectralSequencer::applyPitchQuantize(const SpectralStep& step,
                                            PartialDataSIMD& partials)
{
    const float quantize = step.pitchQuantize;
    if (quantize <= 0.0f)
        return;

    constexpr int kMax = PartialDataSIMD::kMaxPartials;
    const float divisionsPerOctave = std::max(1.0f, quantize);
    const float invDivisions = 1.0f / divisionsPerOctave;

    for (int i = 0; i < kMax; ++i)
    {
        if (partials.isActive(i))
        {
            const float freq = partials.frequency[i];
            if (freq <= 0.0f) continue;

            // Convert to log2 space, quantize, convert back
            const float logFreq = std::log2(freq);
            const float quantized = std::round(logFreq * divisionsPerOctave)
                                  * invDivisions;
            partials.frequency[i] = std::pow(2.0f, quantized);

            // Clamp
            partials.frequency[i] = std::clamp(partials.frequency[i],
                                               1.0f, 20000.0f);
        }
    }
}

void SpectralSequencer::applySpectralFilter(const SpectralStep& step,
                                              PartialDataSIMD& partials)
{
    const float cutoff = step.filterCutoff;
    const float res    = step.filterResonance;

    // If cutoff is at max and resonance is 0, no effect
    if (cutoff >= 19900.0f && res < 0.01f)
        return;

    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    // Compute filter envelope for each active partial
    // Uses a resonant low-pass shape:
    // gain(f) = 1.0 / sqrt(1.0 + (f/fc)^4) + resonance boost near fc
    const float fc = cutoff;
    const float Q  = 1.0f + res * 9.0f; // Map 0-1 to Q 1-10
    const float fcSq = fc * fc;

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i))
            continue;

        const float f = partials.frequency[i];
        if (f <= 0.0f) continue;

        // Normalized frequency ratio
        const float ratio = f / fc;

        // 2-pole lowpass magnitude response
        const float ratioSq = ratio * ratio;
        float gain = 1.0f / std::sqrt(1.0f + ratioSq * ratioSq);

        // Resonance: boost at cutoff
        if (res > 0.0f)
        {
            // Peaking near cutoff
            const float peakGain = Q * res;
            const float resonanceEnv = 1.0f / (1.0f + ratioSq * ratioSq / (Q * Q));
            gain = gain + peakGain * resonanceEnv * (1.0f - gain);

            // Clamp
            gain = std::min(gain, 1.0f + res * 2.0f);
        }

        partials.amplitude[i] *= gain;

        // Remove partials that become too quiet
        if (partials.amplitude[i] < 1e-6f)
            partials.amplitude[i] = 0.0f;
    }

    partials.updateActiveMask();
}

void SpectralSequencer::applySpectralBlur(const SpectralStep& step,
                                            PartialDataSIMD& partials)
{
    const float blur = step.spectralBlur;
    if (blur <= 0.0f)
        return;

    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    // Blur radius proportional to blur amount
    const int radius = std::max(1, static_cast<int>(blur * 8.0f));

    // Gaussian-like weights
    scratch_weights_.assign(static_cast<size_t>(radius * 2 + 1), 0.0f);
    auto& weights = scratch_weights_;
    float weightSum = 0.0f;
    for (int j = -radius; j <= radius; ++j)
    {
        const float w = std::exp(-static_cast<float>(j * j)
                                 / (2.0f * blur * 4.0f + 0.001f));
        weights[static_cast<size_t>(j + radius)] = w;
        weightSum += w;
    }
    // Normalize
    const float invSum = 1.0f / weightSum;
    for (auto& w : weights)
        w *= invSum;

    // Collect active partial indices
    scratch_activeIndices_.clear();
    auto& activeIndices = scratch_activeIndices_;
    for (int i = 0; i < kMax; ++i)
    {
        if (partials.isActive(i))
            activeIndices.push_back(i);
    }

    const int numActive = static_cast<int>(activeIndices.size());
    if (numActive < 2)
        return;

    // Apply blur in partial-index space
    scratch_blurredAmp_.assign(static_cast<size_t>(numActive), 0.0f);
    auto& blurredAmp = scratch_blurredAmp_;

    for (int i = 0; i < numActive; ++i)
    {
        float sum = 0.0f;
        for (int j = -radius; j <= radius; ++j)
        {
            const int idx = i + j;
            if (idx < 0 || idx >= numActive)
                continue;

            const float w = weights[static_cast<size_t>(j + radius)];
            sum += w * partials.amplitude[activeIndices[static_cast<size_t>(idx)]];
        }
        blurredAmp[static_cast<size_t>(i)] = sum;
    }

    // Write back
    for (int i = 0; i < numActive; ++i)
        partials.amplitude[activeIndices[static_cast<size_t>(i)]] = blurredAmp[static_cast<size_t>(i)];
}

void SpectralSequencer::applySpectralTilt(const SpectralStep& step,
                                            PartialDataSIMD& partials)
{
    const float tilt = step.spectralTilt;
    if (std::abs(tilt) < 1e-6f)
        return;

    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    // Find min/max frequencies among active partials
    float minFreq = 20000.0f;
    float maxFreq = 0.0f;

    for (int i = 0; i < kMax; ++i)
    {
        if (partials.isActive(i))
        {
            const float f = partials.frequency[i];
            if (f < minFreq) minFreq = f;
            if (f > maxFreq) maxFreq = f;
        }
    }

    const float freqRange = maxFreq - minFreq;
    if (freqRange < 1.0f)
        return;

    const float invRange = 1.0f / freqRange;

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i))
            continue;

        // Normalized position (0 = low, 1 = high)
        const float normPos = (partials.frequency[i] - minFreq) * invRange;

        // Tilt: at normPos=0, gain = 1 - tilt/2; at normPos=1, gain = 1 + tilt/2
        const float tiltGain = 1.0f + tilt * (normPos - 0.5f);

        partials.amplitude[i] *= std::max(0.0f, tiltGain);
    }
}

void SpectralSequencer::applyFormantShift(const SpectralStep& step,
                                           PartialDataSIMD& partials)
{
    const float shiftSemitones = step.formantShift;
    if (std::abs(shiftSemitones) < 0.01f)
        return;

    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    // Collect active partial data
    scratch_partialEntries_.clear();
    auto& active = scratch_partialEntries_;

    for (int i = 0; i < kMax; ++i)
    {
        if (partials.isActive(i))
        {
            active.push_back({i,
                              partials.frequency[i],
                              partials.amplitude[i]});
        }
    }

    const int numActive = static_cast<int>(active.size());
    if (numActive < 2)
        return;

    // Formant shift: shift the spectral envelope
    // Build an amplitude envelope from current partials, resample at shifted
    // frequency positions, and apply the resampled envelope.
    // The shift amount in semitones maps to a frequency multiplier:
    const float envShift = std::pow(2.0f, shiftSemitones / 12.0f);

    // For each partial, find the amplitude at the shifted frequency
    // using linear interpolation of the envelope
    for (int i = 0; i < numActive; ++i)
    {
        const float targetFreq = active[static_cast<size_t>(i)].freq * envShift;

        // Find neighbors in the active list
        float newAmp = 0.0f;
        if (targetFreq <= active.front().freq)
        {
            newAmp = active.front().amp;
        }
        else if (targetFreq >= active.back().freq)
        {
            newAmp = active.back().amp;
        }
        else
        {
            // Binary search for the two surrounding partials
            int lo = 0;
            int hi = numActive - 1;
            while (hi - lo > 1)
            {
                const int mid = (lo + hi) / 2;
                if (active[static_cast<size_t>(mid)].freq < targetFreq)
                    lo = mid;
                else
                    hi = mid;
            }

            // Linear interpolation
            const float fLo = active[static_cast<size_t>(lo)].freq;
            const float fHi = active[static_cast<size_t>(hi)].freq;
            const float aLo = active[static_cast<size_t>(lo)].amp;
            const float aHi = active[static_cast<size_t>(hi)].amp;

            if (fHi - fLo > 0.0f)
            {
                const float t = (targetFreq - fLo) / (fHi - fLo);
                newAmp = aLo + (aHi - aLo) * t;
            }
            else
            {
                newAmp = aLo;
            }
        }

        partials.amplitude[active[static_cast<size_t>(i)].index] = newAmp;
    }
}

void SpectralSequencer::applyFeedback(const SpectralStep& step,
                                       PartialDataSIMD& partials)
{
    const float fb = step.feedback;
    if (fb <= 0.0f)
    {
        // Still update feedback buffer
        for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
        {
            if (partials.isActive(i))
            {
                feedbackFreq_[i] = partials.frequency[i];
                feedbackAmp_[i]  = partials.amplitude[i];
            }
        }
        return;
    }

    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i) && feedbackAmp_[i] < 1e-6f)
            continue;

        float newAmp = partials.amplitude[i];
        float newFreq = partials.frequency[i];

        // Blend with feedback
        partials.amplitude[i]  = newAmp * (1.0f - fb) + feedbackAmp_[i] * fb;
        partials.frequency[i]  = newFreq * (1.0f - fb) + feedbackFreq_[i] * fb;

        // Update feedback buffer
        feedbackFreq_[i] = partials.frequency[i];
        feedbackAmp_[i]  = partials.amplitude[i];
    }

    partials.updateActiveMask();
}

void SpectralSequencer::applyNoise(const SpectralStep& step,
                                    PartialDataSIMD& partials)
{
    const float noiseAmt = step.noise;
    if (noiseAmt <= 0.0f)
        return;

    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    std::uniform_real_distribution<float> ampNoise(-1.0f, 1.0f);
    std::uniform_real_distribution<float> phaseNoise(-juce::MathConstants<float>::pi,
                                                      juce::MathConstants<float>::pi);

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i))
            continue;

        // Add noise to amplitude
        partials.amplitude[i] += noiseAmt * ampNoise(rng_) * 0.2f;
        partials.amplitude[i]  = std::clamp(partials.amplitude[i], 0.0f, 2.0f);

        // Add noise to phase
        partials.phase[i] += noiseAmt * phaseNoise(rng_) * 0.3f;
    }

    partials.updateActiveMask();
}

void SpectralSequencer::applyBitcrush(const SpectralStep& step,
                                       PartialDataSIMD& partials)
{
    const float crush = step.bitcrush;
    if (crush <= 0.0f)
        return;

    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    // Map bitcrush 0-1 to number of quantization steps per octave (1 to 24)
    const float stepsPerOctave = std::max(1.0f, 1.0f + crush * 23.0f);
    const float invSteps = 1.0f / stepsPerOctave;

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i))
            continue;

        const float freq = partials.frequency[i];
        if (freq <= 0.0f) continue;

        // Quantize in log2 space
        const float logFreq = std::log2(freq);
        const float quantized = std::round(logFreq * stepsPerOctave)
                              * invSteps;
        partials.frequency[i] = std::pow(2.0f, quantized);

        partials.frequency[i] = std::clamp(partials.frequency[i],
                                           1.0f, 20000.0f);
    }
}

void SpectralSequencer::applyGain(const SpectralStep& step,
                                   PartialDataSIMD& partials)
{
    const float g = step.gain;
    if (std::abs(g - 1.0f) < 1e-6f)
        return;

    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    for (int i = 0; i < kMax; ++i)
    {
        if (partials.isActive(i))
            partials.amplitude[i] *= g;
    }

    partials.updateActiveMask();
}

} // namespace ana
