#include "AdvancedGranularEngine.h"

#include <cmath>
#include <algorithm>
#include <stack>
#include <juce_core/juce_core.h>

namespace ana {

//==============================================================================
//  LSystem
//==============================================================================

void LSystem::setAxiom(const std::string& axiom)
{
    axiom_ = axiom;
}

void LSystem::addRule(char symbol, const std::string& replacement)
{
    rules_[symbol] = replacement;
}

void LSystem::setIterations(int n)
{
    iterations_ = std::max(0, n);
}

std::string LSystem::generate() const
{
    if (iterations_ == 0)
        return axiom_;

    std::string current = axiom_;

    for (int i = 0; i < iterations_; ++i)
    {
        std::string next;
        next.reserve(current.size() * 2); // rough growth estimate

        for (const char c : current)
        {
            const auto it = rules_.find(c);
            if (it != rules_.end())
                next += it->second;
            else
                next += c; // keep unchanged symbols
        }

        current = std::move(next);
    }

    return current;
}

std::vector<Grain> LSystem::interpretAsGrains(const std::string& lstring,
                                                float totalDuration,
                                                int numGrains) const
{
    std::vector<Grain> grains;
    grains.reserve(static_cast<size_t>(numGrains));

    struct SavedState
    {
        float time;
        float pan;
    };
    std::stack<SavedState> stateStack;

    float currentTime = 0.0f;
    float currentPan  = 0.0f;
    const float timeStep = totalDuration / static_cast<float>(std::max(1, numGrains));

    for (const char c : lstring)
    {
        if (grains.size() >= static_cast<size_t>(numGrains))
            break;

        switch (c)
        {
            case 'F':
            {
                Grain g;
                g.delay = currentTime;
                g.pan   = currentPan;
                g.amplitude = 0.5f;
                grains.push_back(g);
                currentTime += timeStep;
                break;
            }

            case '+':
                currentPan = std::min(currentPan + 0.1f, 1.0f);
                break;

            case '-':
                currentPan = std::max(currentPan - 0.1f, -1.0f);
                break;

            case '[':
                stateStack.push({ currentTime, currentPan });
                break;

            case ']':
                if (!stateStack.empty())
                {
                    currentTime = stateStack.top().time;
                    currentPan  = stateStack.top().pan;
                    stateStack.pop();
                }
                break;

            default:
                break;
        }
    }

    return grains;
}

//==============================================================================
//  GrainCloud
//==============================================================================

void GrainCloud::generateRandom(int count, float duration, float chaos)
{
    grains.clear();
    grains.reserve(static_cast<size_t>(count));

    std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> posDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> timeDist(0.0f, duration);
    std::uniform_real_distribution<float> panDist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> pitchDist(0.5f, 2.0f);
    std::uniform_real_distribution<float> ampDist(0.2f, 1.0f);

    for (int i = 0; i < count; ++i)
    {
        Grain g;

        // Base random distribution
        g.startSample  = posDist(rng);
        g.delay        = timeDist(rng);
        g.pan          = panDist(rng) * chaos;
        g.pitchShift   = 1.0f + (pitchDist(rng) - 1.0f) * chaos;
        g.amplitude    = 0.3f + 0.7f * ampDist(rng) * (1.0f - chaos * 0.5f);
        g.duration     = duration * (0.02f + 0.18f * (1.0f - chaos * 0.5f));

        grains.push_back(g);
    }
}

void GrainCloud::generateLSystem(const LSystem& lsys, float duration, int numGrains)
{
    const std::string lstring = lsys.generate();
    grains = lsys.interpretAsGrains(lstring, duration, numGrains);
}

void GrainCloud::generateSpiral(int count, float duration, float radius)
{
    grains.clear();
    grains.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i)
    {
        Grain g;

        const float t = static_cast<float>(i) / static_cast<float>(count);
        const float angle = t * 6.0f * juce::MathConstants<float>::twoPi;
        const float spiralRadius = radius * (1.0f - t * 0.5f);

        g.delay        = t * duration;
        g.startSample  = t;   // sweep through source
        g.pan          = std::sin(angle) * spiralRadius;
        g.pitchShift   = 1.0f + std::cos(angle * 0.5f) * 0.5f * radius;
        g.amplitude    = 0.3f + 0.7f * (1.0f - t * 0.5f);
        g.duration     = duration / static_cast<float>(count) * 4.0f;

        grains.push_back(g);
    }
}

void GrainCloud::applyChaos(float amount)
{
    std::mt19937 rng{ std::random_device{}() };
    amount = std::clamp(amount, 0.0f, 1.0f);

    for (auto& g : grains)
    {
        std::uniform_real_distribution<float> dist(-amount, amount);

        g.delay       += dist(rng) * g.duration * 0.5f;
        g.pan         += dist(rng) * amount;
        g.pitchShift  *= 1.0f + dist(rng) * 0.5f;
        g.amplitude   += dist(rng) * amount * 0.5f;
        g.startSample += dist(rng) * amount;

        g.delay        = std::max(g.delay, 0.0f);
        g.pan          = std::clamp(g.pan, -1.0f, 1.0f);
        g.pitchShift   = std::clamp(g.pitchShift, 0.25f, 4.0f);
        g.amplitude    = std::clamp(g.amplitude, 0.0f, 1.0f);
        g.startSample  = std::max(g.startSample, 0.0f);
    }
}

void GrainCloud::applySyncopation(float amount)
{
    std::mt19937 rng{ std::random_device{}() };
    amount = std::clamp(amount, 0.0f, 1.0f);

    // Find the longest delay to normalise against
    float maxDelay = 0.0f;
    for (const auto& g : grains)
        maxDelay = std::max(maxDelay, g.delay);

    if (maxDelay <= 0.0f)
        return;

    const float quantize = maxDelay / 16.0f; // 16th-note grid

    for (auto& g : grains)
    {
        const float normalized = g.delay / maxDelay;

        // Probability of syncopation: peaks at triplet-ish divisions
        const float syncProb = 0.3f + 0.5f * std::sin(normalized * juce::MathConstants<float>::twoPi * 6.0f);

        std::uniform_real_distribution<float> probDist(0.0f, 1.0f);
        if (probDist(rng) < syncProb * amount)
        {
            // Shift by half a grid step
            std::uniform_real_distribution<float> shiftDist(-0.5f, 0.5f);
            g.delay += shiftDist(rng) * quantize;
            g.delay  = std::max(g.delay, 0.0f);
        }
    }
}

void GrainCloud::scatter(float timeAmount, float pitchAmount, float panAmount)
{
    std::mt19937 rng{ std::random_device{}() };

    for (auto& g : grains)
    {
        std::uniform_real_distribution<float> timeDist(-timeAmount, timeAmount);
        std::uniform_real_distribution<float> pitchDist(-pitchAmount, pitchAmount);
        std::uniform_real_distribution<float> panDist(-panAmount, panAmount);

        g.delay       += timeDist(rng) * g.duration;
        g.pitchShift  *= 1.0f + pitchDist(rng);
        g.pan         += panDist(rng);

        g.delay       = std::max(g.delay, 0.0f);
        g.pitchShift  = std::clamp(g.pitchShift, 0.25f, 4.0f);
        g.pan         = std::clamp(g.pan, -1.0f, 1.0f);
    }
}

//==============================================================================
//  AdvancedGranularEngine
//==============================================================================

AdvancedGranularEngine::AdvancedGranularEngine()
    : rng_(std::random_device{}())
{
    generateWindowTable();
}

AdvancedGranularEngine::~AdvancedGranularEngine() = default;

//==============================================================================
void AdvancedGranularEngine::setSourceBuffer(const std::vector<float>& audio, double sampleRate)
{
    sourceBuffer_ = audio;
    sourceSampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void AdvancedGranularEngine::setGrainCloud(const GrainCloud& cloud)
{
    cloud_ = cloud;
    nextGrainToActivate_ = 0;
    grainStates_.clear();
    activeGrains_.clear();
    totalSamplesProcessed_ = 0;
}

void AdvancedGranularEngine::setNumGrains(int numGrains)
{
    maxGrains_ = std::clamp(numGrains, 1, 1024);
}

void AdvancedGranularEngine::setDensity(float grainsPerSecond)
{
    density_ = std::clamp(grainsPerSecond, 0.1f, 500.0f);

    // Auto-calculate numGrains: expected simultaneous = density * grainDuration / 1000
    const float avgSimultaneous = density_ * grainDurationMs_ / 1000.0f;
    maxGrains_ = std::clamp(static_cast<int>(std::ceil(avgSimultaneous)), 1, 1024);
}

void AdvancedGranularEngine::setChaos(float chaos)
{
    chaos_ = std::clamp(chaos, 0.0f, 1.0f);
}

void AdvancedGranularEngine::setPitchRandom(float amount)
{
    pitchRandom_ = std::clamp(amount, 0.0f, 1.0f);
}

void AdvancedGranularEngine::setPanRandom(float amount)
{
    panRandom_ = std::clamp(amount, 0.0f, 1.0f);
}

void AdvancedGranularEngine::setAmpRandom(float amount)
{
    ampRandom_ = std::clamp(amount, 0.0f, 1.0f);
}

void AdvancedGranularEngine::setReverseProb(float probability)
{
    reverseProb_ = std::clamp(probability, 0.0f, 1.0f);
}

void AdvancedGranularEngine::setStutterProb(float probability)
{
    stutterProb_ = std::clamp(probability, 0.0f, 1.0f);
}

void AdvancedGranularEngine::setGrainDuration(float ms)
{
    grainDurationMs_ = std::clamp(ms, 1.0f, 2000.0f);
    generateWindowTable();
}

void AdvancedGranularEngine::setGrainAttack(float percent)
{
    grainAttack_ = std::clamp(percent, 0.0f, 100.0f);
    generateWindowTable();
}

void AdvancedGranularEngine::setGrainDecay(float percent)
{
    grainDecay_ = std::clamp(percent, 0.0f, 100.0f);
    generateWindowTable();
}

void AdvancedGranularEngine::setPanSpread(float spread)
{
    panSpread_ = std::clamp(spread, 0.0f, 1.0f);
}

void AdvancedGranularEngine::setPanPosition(float pan)
{
    panPosition_ = std::clamp(pan, -1.0f, 1.0f);
}

//==============================================================================
void AdvancedGranularEngine::generateWindowTable()
{
    constexpr int tableSize = 1024;
    windowTable_.resize(tableSize);

    const float attackFrac = grainAttack_ / 100.0f;
    const float decayFrac  = grainDecay_ / 100.0f;
    const float sustainEnd = 1.0f - decayFrac;

    for (int i = 0; i < tableSize; ++i)
    {
        const float pos = static_cast<float>(i) / static_cast<float>(tableSize - 1);

        if (pos < attackFrac)
        {
            // Attack: linear ramp up 0 -> 1
            windowTable_[static_cast<size_t>(i)] = attackFrac > 0.0f
                ? pos / attackFrac
                : 1.0f;
        }
        else if (pos > sustainEnd)
        {
            // Decay: linear ramp down 1 -> 0
            windowTable_[static_cast<size_t>(i)] = decayFrac > 0.0f
                ? (1.0f - pos) / decayFrac
                : 0.0f;
        }
        else
        {
            // Sustain: hold at 1
            windowTable_[static_cast<size_t>(i)] = 1.0f;
        }
    }
}

//==============================================================================
float AdvancedGranularEngine::getGrainEnvelope(float position, const Grain& grain) const
{
    if (windowTable_.empty())
        return grain.envelope;

    const int idx = std::clamp(static_cast<int>(position * 1023.0f), 0, 1023);
    return windowTable_[static_cast<size_t>(idx)] * grain.envelope;
}

//==============================================================================
float AdvancedGranularEngine::interpolateSource(double position) const
{
    const int len = static_cast<int>(sourceBuffer_.size());
    if (len < 1)
        return 0.0f;

    const double clampedPos = std::clamp(position, 0.0, static_cast<double>(len - 1));

    const int i0    = static_cast<int>(clampedPos);
    const int i1    = std::min(i0 + 1, len - 1);
    const float frac = static_cast<float>(clampedPos - static_cast<double>(i0));

    return (1.0f - frac) * sourceBuffer_[static_cast<size_t>(i0)]
         + frac        * sourceBuffer_[static_cast<size_t>(i1)];
}

//==============================================================================
void AdvancedGranularEngine::updateGrains(int numSamples)
{
    // ---------------------------------------------------------------------------
    // 1. Remove finished grains from the runtime list
    // ---------------------------------------------------------------------------
    grainStates_.erase(
        std::remove_if(grainStates_.begin(), grainStates_.end(),
            [](const GrainState& gs) { return !gs.active; }),
        grainStates_.end());

    // Also update activeGrains_ to reflect the culled list
    activeGrains_.clear();
    for (const auto& gs : grainStates_)
        activeGrains_.push_back(gs.config);

    // ---------------------------------------------------------------------------
    // 2. Activate new grains from the cloud whose delay falls within this buffer
    // ---------------------------------------------------------------------------
    const int64_t bufferStart = totalSamplesProcessed_;
    const int64_t bufferEnd   = bufferStart + numSamples;

    while (nextGrainToActivate_ < cloud_.grains.size()
           && grainStates_.size() < static_cast<size_t>(maxGrains_))
    {
        const auto& g = cloud_.grains[nextGrainToActivate_];

        const int64_t activationSample = static_cast<int64_t>(g.delay * sampleRate_);
        const int64_t grainEndSample   = activationSample
                                       + static_cast<int64_t>(g.duration * sampleRate_);

        // Skip grains that finished before this buffer
        if (grainEndSample < bufferStart)
        {
            ++nextGrainToActivate_;
            continue;
        }

        // Grains that start after this buffer are for later
        if (activationSample >= bufferEnd)
            break;

        // -----------------------------------------------------------------------
        // 3. Initialise runtime state for this grain
        // -----------------------------------------------------------------------
        GrainState gs;
        gs.config         = g;
        gs.durationSamples = std::max(1, static_cast<int>(g.duration * sampleRate_));

        // Compute pitch ratio (with random deviation)
        gs.pitchRatio = std::clamp(g.pitchShift, 0.25f, 4.0f);
        if (pitchRandom_ > 0.0f)
        {
            std::uniform_real_distribution<float> pitchDist(-pitchRandom_, pitchRandom_);
            gs.pitchRatio *= (1.0f + pitchDist(rng_));
            gs.pitchRatio  = std::clamp(gs.pitchRatio, 0.25f, 4.0f);
        }

        // Apply playback speed and reversal as a combined ratio
        if (g.reversed)
            gs.pitchRatio = -gs.pitchRatio;
        gs.pitchRatio *= std::max(g.playbackSpeed, 0.01f);

        // Effective amplitude (with random deviation)
        gs.amplitude = std::clamp(g.amplitude, 0.0f, 1.0f);
        if (ampRandom_ > 0.0f)
        {
            std::uniform_real_distribution<float> ampDist(-ampRandom_, ampRandom_);
            gs.amplitude *= (1.0f + ampDist(rng_));
            gs.amplitude  = std::clamp(gs.amplitude, 0.0f, 1.0f);
        }

        // Pan (with random deviation)
        float pan = g.pan;
        if (panRandom_ > 0.0f)
        {
            std::uniform_real_distribution<float> panDist(-panRandom_, panRandom_);
            pan += panDist(rng_);
            pan  = std::clamp(pan, -1.0f, 1.0f);
        }

        // Apply spread around pan position
        float effectivePan = panPosition_ + (pan - panPosition_) * panSpread_;
        effectivePan = std::clamp(effectivePan, -1.0f, 1.0f);

        // Constant-power panning
        const float panNorm = (effectivePan + 1.0f) * 0.5f;
        gs.panL = std::cos(panNorm * juce::MathConstants<float>::halfPi);
        gs.panR = std::sin(panNorm * juce::MathConstants<float>::halfPi);

        // Source position
        gs.sourcePosition = static_cast<double>(g.startSample);

        // Handle reverse probability
        bool isReversed = g.reversed;
        if (reverseProb_ > 0.0f && !isReversed)
        {
            std::uniform_real_distribution<float> revDist(0.0f, 1.0f);
            if (revDist(rng_) < reverseProb_)
            {
                isReversed = true;
                gs.pitchRatio = -gs.pitchRatio;
            }
        }

        // Calculate current sample offset if the grain started before this buffer
        if (activationSample < bufferStart)
        {
            const int64_t samplesPlayed = bufferStart - activationSample;
            gs.currentSample = static_cast<int>(samplesPlayed);
            gs.sourcePosition += g.pitchShift
                                 * static_cast<double>(samplesPlayed)
                                 * (isReversed ? -1.0 : 1.0);
        }
        else
        {
            gs.currentSample = 0;
        }

        gs.active = true;
        grainStates_.push_back(gs);
        activeGrains_.push_back(g.config);
        ++nextGrainToActivate_;
    }
}

//==============================================================================
void AdvancedGranularEngine::renderGrain(const Grain& grain,
                                          juce::AudioBuffer<float>& buffer,
                                          int startSample)
{
    const int numSamples      = buffer.getNumSamples();
    const int numChannels     = buffer.getNumChannels();
    const int renderEndSample = std::min(startSample + static_cast<int>(grain.duration * sampleRate_),
                                         numSamples);

    if (startSample >= numSamples || startSample < 0)
        return;

    // Pre-compute per-grain values
    const double pitchRatio  = static_cast<double>(std::clamp(grain.pitchShift, 0.25f, 4.0f));
    const float  amplitude   = grain.amplitude * grain.envelope;
    const float  panNorm     = (grain.pan + 1.0f) * 0.5f;
    const float  panL        = std::cos(panNorm * juce::MathConstants<float>::halfPi);
    const float  panR        = std::sin(panNorm * juce::MathConstants<float>::halfPi);

    double sourcePos = static_cast<double>(grain.startSample);

    for (int sample = startSample; sample < renderEndSample; ++sample)
    {
        const int grainSample = sample - startSample;

        // Read source with linear interpolation
        const float srcSample = interpolateSource(sourcePos);

        // Window envelope
        const float pos = static_cast<float>(grainSample)
                        / static_cast<float>(grain.duration * sampleRate_);
        const float env = getGrainEnvelope(pos, grain);

        const float value = srcSample * env * amplitude;

        if (numChannels >= 1)
            buffer.addSample(0, sample, value * panL);
        if (numChannels >= 2)
            buffer.addSample(1, sample, value * panR);

        sourcePos += pitchRatio;
    }
}

//==============================================================================
void AdvancedGranularEngine::process(juce::AudioBuffer<float>& buffer)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (sourceBuffer_.empty() || numSamples <= 0)
        return;

    buffer.clear();

    // ---------------------------------------------------------------------------
    // 1. Update active grains from the cloud
    // ---------------------------------------------------------------------------
    updateGrains(numSamples);

    // ---------------------------------------------------------------------------
    // 2. Render each sample by summing all active grains
    // ---------------------------------------------------------------------------
    float* channelData[2] = { nullptr, nullptr };
    if (numChannels >= 1)
        channelData[0] = buffer.getWritePointer(0);
    if (numChannels >= 2)
        channelData[1] = buffer.getWritePointer(1);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float left  = 0.0f;
        float right = 0.0f;

        for (auto& gs : grainStates_)
        {
            if (!gs.active)
                continue;

            // Skip if this grain has finished
            if (gs.currentSample >= gs.durationSamples)
            {
                // Check for stutter
                if (stutterProb_ > 0.0f)
                {
                    std::uniform_real_distribution<float> stutterDist(0.0f, 1.0f);
                    if (stutterDist(rng_) < stutterProb_ && grainStates_.size() < static_cast<size_t>(maxGrains_))
                    {
                        gs.currentSample    = 0;
                        gs.sourcePosition   = static_cast<double>(gs.config.startSample);
                        continue;
                    }
                }
                gs.active = false;
                continue;
            }

            // Read source with linear interpolation
            const float srcSample = interpolateSource(gs.sourcePosition);

            // Apply window envelope
            const float normPos = static_cast<float>(gs.currentSample)
                                / static_cast<float>(gs.durationSamples);
            const float env = getGrainEnvelope(normPos, gs.config);

            const float value = srcSample * env * gs.amplitude;

            left  += value * gs.panL;
            right += value * gs.panR;

            // Advance grain playhead
            gs.sourcePosition += gs.pitchRatio;
            ++gs.currentSample;
        }

        // Write output
        if (channelData[0] != nullptr)
            channelData[0][sample] = left;

        if (channelData[1] != nullptr)
            channelData[1][sample] = right;
    }

    // ---------------------------------------------------------------------------
    // 3. Clean up finished grains from the list
    // ---------------------------------------------------------------------------
    grainStates_.erase(
        std::remove_if(grainStates_.begin(), grainStates_.end(),
            [](const GrainState& gs) { return !gs.active; }),
        grainStates_.end());

    activeGrains_.clear();
    for (const auto& gs : grainStates_)
        activeGrains_.push_back(gs.config);

    // ---------------------------------------------------------------------------
    // 4. Advance global sample counter
    // ---------------------------------------------------------------------------
    totalSamplesProcessed_ += numSamples;
}

//==============================================================================
void AdvancedGranularEngine::reset()
{
    grainStates_.clear();
    activeGrains_.clear();
    nextGrainToActivate_ = 0;
    totalSamplesProcessed_ = 0;
}

void AdvancedGranularEngine::clearCloud()
{
    cloud_.grains.clear();
    reset();
}

} // namespace ana
