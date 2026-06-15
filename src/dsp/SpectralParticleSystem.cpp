#include "SpectralParticleSystem.h"

#include <cmath>
#include <cstring>

namespace ana {

//==============================================================================
//  Internal constants
//==============================================================================
namespace {

/** Minimum frequency in Hz that particles can occupy. */
constexpr float kMinFreq        = 20.0f;

/** Minimum amplitude threshold for spectrum bin significance. */
constexpr float kAmpThreshold   = 0.01f;

/** Small epsilon to prevent division by zero in falloff calculations. */
constexpr float kEpsilon        = 1e-6f;

/** Acceleration scale factor to bring force values into a musically
    useful range when combined with typical dt values (~0.5 – 2 ms). */
constexpr float kAccelScale     = 500.0f;

/** The 1/r² attractor falloff uses this epsilon in the denominator to
    avoid singularities when a particle is exactly at the field centre. */
constexpr float kAttractorEps   = 1.0f;

//==============================================================================
//  Noise oscillator constants
//==============================================================================

/** Lowest noise partial frequency (Hz). Avoids DC. */
constexpr float kNoiseMinFreq        = 30.0f;

/** Noise partials extend up to this proportion of nyquist. */
constexpr float kNoiseMaxFreqProp    = 0.95f;

/** Small offset for normalised frequency to avoid division by zero
    in spectral tilt and noise-colour filter equations. */
constexpr float kNoiseNormEps        = 0.01f;

/** Maximum raw amplitude before gain-scaling, used to clamp extreme
    low-frequency amplification from pink/brown spectral shapes. */
constexpr float kNoiseRawAmpMax      = 5.0f;

/** Envelope time minimum (seconds) to avoid zero-divide. */
constexpr float kEnvTimeMin          = 0.0001f;

} // namespace

//==============================================================================
//  Construction
//==============================================================================

SpectralParticleSystem::SpectralParticleSystem()
{
    particles_.resize(maxParticles_);
}

//==============================================================================
//  Configuration setters
//==============================================================================

void SpectralParticleSystem::setMaxParticles(int max)
{
    maxParticles_ = juce::jlimit(64, 2048, max);

    // Resize the particle vector — live particles are preserved;
    // new slots are inactive by default (Particle{} default-initialised).
    const int oldSize = static_cast<int>(particles_.size());
    particles_.resize(static_cast<size_t>(maxParticles_));

    // Any newly added slots are already defaulted, but ensure any that
    // were truncated beyond the new limit are gone.
    if (maxParticles_ < oldSize)
    {
        // Particles beyond the new limit — they are already removed by
        // resize(), so nothing else to do.
    }
}

void SpectralParticleSystem::setEmissionRate(float rate)
{
    emissionRate_ = juce::jmax(0.0f, rate);
}

void SpectralParticleSystem::setParticleLife(float seconds)
{
    particleLife_ = juce::jlimit(0.1f, 10.0f, seconds);
}

void SpectralParticleSystem::setDamping(float damping)
{
    damping_ = juce::jlimit(0.9f, 1.0f, damping);
}

void SpectralParticleSystem::setGravity(float strength)
{
    gravity_ = strength;
}

void SpectralParticleSystem::setTurbulence(float amount)
{
    turbulence_ = juce::jlimit(0.0f, 1.0f, amount);
}

//==============================================================================
//  Force field management
//==============================================================================

void SpectralParticleSystem::addForceField(const ForceField& field)
{
    forceFields_.push_back(field);
}

void SpectralParticleSystem::clearForceFields()
{
    forceFields_.clear();
}

int SpectralParticleSystem::getNumForceFields() const
{
    return static_cast<int>(forceFields_.size());
}

SpectralParticleSystem::ForceField& SpectralParticleSystem::getForceField(int index)
{
    return forceFields_[static_cast<size_t>(index)];
}

//==============================================================================
//  Emission
//==============================================================================

void SpectralParticleSystem::emitFromSpectrum(const std::vector<float>& magnitudes,
                                               double sampleRate, int fftSize)
{
    sampleRate_ = sampleRate;
    fftSize_    = fftSize;

    const int    numBins  = static_cast<int>(magnitudes.size());
    const float  binWidth = static_cast<float>(sampleRate)
                          / static_cast<float>(fftSize);
    const float  nyquist  = static_cast<float>(sampleRate) * 0.5f;

    for (int i = 0; i < numBins; ++i)
    {
        if (magnitudes[static_cast<size_t>(i)] <= kAmpThreshold)
            continue;

        if (getActiveCount() >= maxParticles_)
            break;

        // Convert bin index to frequency with a small random offset for richness
        float freq = static_cast<float>(i) * binWidth;
        freq += (random_.nextFloat() - 0.5f) * binWidth * 0.5f;
        freq = juce::jmax(kMinFreq, juce::jmin(nyquist * 0.999f, freq));

        const float amp   = magnitudes[static_cast<size_t>(i)];
        const float phase = random_.nextFloat() * juce::MathConstants<float>::twoPi;

        emitParticle(freq, amp, phase);
    }
}

void SpectralParticleSystem::emitFromPartials(const PartialDataSIMD& partials)
{
    if (partials.activeCount <= 0)
        return;

    for (int i = 0; i < partials.maxPartials; ++i)
    {
        if (!partials.isActive(i))
            continue;

        if (getActiveCount() >= maxParticles_)
            break;

        emitParticle(partials.frequency[i],
                     partials.amplitude[i],
                     partials.phase[i]);
    }
}

void SpectralParticleSystem::emitBurst(int count)
{
    const float nyquist = static_cast<float>(sampleRate_) * 0.5f;

    for (int i = 0; i < count; ++i)
    {
        if (getActiveCount() >= maxParticles_)
            break;

        const float freq   = kMinFreq + random_.nextFloat() * (nyquist - kMinFreq);
        const float amp    = 0.1f + random_.nextFloat() * 0.5f;
        const float phase  = random_.nextFloat() * juce::MathConstants<float>::twoPi;

        emitParticle(freq, amp, phase);
    }
}

//==============================================================================
//  Noise oscillator setters / getters
//==============================================================================

void SpectralParticleSystem::setNoiseType(NoiseType type)  { noiseType_ = type; }
SpectralParticleSystem::NoiseType SpectralParticleSystem::getNoiseType() const { return noiseType_; }

void SpectralParticleSystem::setNoiseColor(float brightness)
{
    noiseColor_ = juce::jlimit(0.0f, 1.0f, brightness);
}

float SpectralParticleSystem::getNoiseColor() const { return noiseColor_; }

void SpectralParticleSystem::setNoiseAmplitude(float amp)
{
    noiseAmplitude_ = juce::jlimit(0.0f, 1.0f, amp);
}

float SpectralParticleSystem::getNoiseAmplitude() const { return noiseAmplitude_; }

void SpectralParticleSystem::setNoiseEnvAttack(float ms)
{
    noiseEnvAttack_ = juce::jmax(0.0f, ms);
}

void SpectralParticleSystem::setNoiseEnvDecay(float ms)
{
    noiseEnvDecay_ = juce::jmax(0.0f, ms);
}

void SpectralParticleSystem::setNoiseEnvSustain(float level)
{
    noiseEnvSustain_ = juce::jlimit(0.0f, 1.0f, level);
}

void SpectralParticleSystem::setNoiseEnvRelease(float ms)
{
    noiseEnvRelease_ = juce::jmax(0.0f, ms);
}

//==============================================================================
//  Internal ADSR envelope
//==============================================================================

void SpectralParticleSystem::advanceNoiseEnvelope(float dt)
{
    if (dt <= 0.0f)
        return;

    const float attackSec  = juce::jmax(kEnvTimeMin, noiseEnvAttack_  / 1000.0f);
    const float decaySec   = juce::jmax(kEnvTimeMin, noiseEnvDecay_   / 1000.0f);
    const float releaseSec = juce::jmax(kEnvTimeMin, noiseEnvRelease_ / 1000.0f);

    switch (noiseEnvStage_)
    {
        case EnvStage::Idle:
            noiseEnvValue_ = 0.0f;
            break;

        case EnvStage::Attack:
            noiseEnvValue_ += dt / attackSec;
            if (noiseEnvValue_ >= 1.0f)
            {
                noiseEnvValue_ = 1.0f;
                noiseEnvStage_ = EnvStage::Decay;
            }
            break;

        case EnvStage::Decay:
            noiseEnvValue_ -= dt / decaySec * (1.0f - noiseEnvSustain_);
            if (noiseEnvValue_ <= noiseEnvSustain_)
            {
                noiseEnvValue_ = noiseEnvSustain_;
                noiseEnvStage_ = EnvStage::Sustain;
            }
            break;

        case EnvStage::Sustain:
            noiseEnvValue_ = noiseEnvSustain_;
            break;

        case EnvStage::Release:
            // Exponential decay: V(t) = V₀ · exp(-t / releaseSec)
            noiseEnvValue_ *= 1.0f - dt / releaseSec;
            if (noiseEnvValue_ < 0.001f)
            {
                noiseEnvValue_ = 0.0f;
                noiseEnvStage_ = EnvStage::Idle;
            }
            break;
    }
}

//==============================================================================
//  Noise partial generation
//==============================================================================

void SpectralParticleSystem::generateNoisePartials(PartialDataSIMD& partials,
                                                    int numActivePartials,
                                                    float envelopeLevel)
{
    if (noiseAmplitude_ <= 0.0f || numActivePartials <= 0)
        return;

    const double sr = (partials.sampleRate > 0.0) ? partials.sampleRate
                                                   : sampleRate_;
    const float dt  = static_cast<float>(1.0 / sr);
    const float nyquist = static_cast<float>(sr) * 0.5f;

    // ---- Gate edge detection ----
    const bool gateOpen = (envelopeLevel > 0.0f);

    if (gateOpen && !noiseEnvTriggered_)
    {
        // Gate just opened → (re)start attack from zero
        noiseEnvStage_ = EnvStage::Attack;
        noiseEnvValue_ = 0.0f;
    }
    else if (!gateOpen && noiseEnvTriggered_
             && noiseEnvStage_ != EnvStage::Idle)
    {
        // Gate just closed → enter release (unless already idle)
        noiseEnvStage_ = EnvStage::Release;
    }

    noiseEnvTriggered_ = gateOpen;

    // ---- Advance envelope ----
    advanceNoiseEnvelope(dt);

    if (noiseEnvValue_ <= 0.0f)
        return;

    // ---- Fill partial slots ----
    const int N = juce::jmin(numActivePartials,
                             juce::jmin(kNoisePartialsMax, partials.maxPartials));

    // Write into the last N slots so spectral particles and noise coexist
    const int startSlot = partials.maxPartials - N;
    const float envAmp  = noiseAmplitude_ * noiseEnvValue_ * envelopeLevel;

    for (int i = 0; i < N; ++i)
    {
        const int slot = startSlot + i;

        // Random frequency across the audible spectrum
        const float freqRange = nyquist * kNoiseMaxFreqProp - kNoiseMinFreq;
        const float freq = kNoiseMinFreq + random_.nextFloat() * freqRange;

        // Normalised frequency [0, 1] for spectral shape calculations
        const float normFreq = juce::jmax(kNoiseNormEps,
                                          (freq - kNoiseMinFreq)
                                        / (nyquist - kNoiseMinFreq));

        // ---- Base amplitude by noise type ----
        float rawAmp;

        switch (noiseType_)
        {
            case NoiseType::Pink:
                // -3 dB / octave: amplitude ∝ 1 / sqrt(f)
                rawAmp = (0.5f + random_.nextFloat() * 0.5f)
                       * std::sqrt(1.0f / normFreq);
                break;

            case NoiseType::Brown:
                // -6 dB / octave: amplitude ∝ 1 / f
                rawAmp = (0.5f + random_.nextFloat() * 0.5f)
                       * (1.0f / normFreq);
                break;

            case NoiseType::White:
            default:
                // Flat spectral density
                rawAmp = 0.5f + random_.nextFloat() * 0.5f;
                break;
        }

        // ---- Noise colour (spectral tilt) ----
        // brightness 0 = dark (−3 dB/oct tilt-down)
        //           0.5 = flat (no tilt)
        //           1.0 = bright (+3 dB/oct tilt-up)
        if (std::abs(noiseColor_ - 0.5f) > 0.001f)
        {
            const float tilt = (noiseColor_ - 0.5f) * 2.0f;   // [-1, 1]
            rawAmp *= std::pow(normFreq, tilt * 0.5f);
        }

        // ---- Clamp and apply gain ----
        rawAmp = juce::jmin(rawAmp, kNoiseRawAmpMax);
        const float amp = rawAmp * envAmp;

        partials.frequency[slot] = freq;
        partials.amplitude[slot] += amp;
        partials.phase[slot] = random_.nextFloat()
                             * juce::MathConstants<float>::twoPi;
    }

    // Update the active mask to reflect newly added noise content
    partials.updateActiveMask();
}

//==============================================================================
//  Main processing
//==============================================================================

void SpectralParticleSystem::update(double deltaTime)
{
    // Clamp delta-time to prevent physics explosion on buffer underruns
    const double dt = juce::jlimit(0.0, 0.05, deltaTime);
    const float  dtf = static_cast<float>(dt);

    // ---- Auto-emission (rate-limited) ----
    if (emissionRate_ > 0.0f && getActiveCount() < maxParticles_)
    {
        emissionAccum_ += static_cast<double>(emissionRate_) * dt;

        const float nyquist = static_cast<float>(sampleRate_) * 0.5f;

        while (emissionAccum_ >= 1.0 && getActiveCount() < maxParticles_)
        {
            const float freq  = kMinFreq + random_.nextFloat() * (nyquist - kMinFreq);
            const float amp   = 0.05f + random_.nextFloat() * 0.3f;
            const float phase = random_.nextFloat() * juce::MathConstants<float>::twoPi;

            emitParticle(freq, amp, phase);
            emissionAccum_ -= 1.0;
        }
    }

    // ---- Reset accelerations ----
    for (auto& p : particles_)
    {
        if (p.active)
            p.acceleration = 0.0f;
    }

    // ---- Apply force fields ----
    for (const auto& field : forceFields_)
        applyForceField(field, dt);

    // ---- Apply global forces ----
    if (turbulence_ > 0.0f)
    {
        const float turbStrength = turbulence_ * kAccelScale;
        for (auto& p : particles_)
        {
            if (!p.active) continue;
            p.acceleration += (random_.nextFloat() * 2.0f - 1.0f) * turbStrength;
        }
    }

    if (gravity_ != 0.0f)
    {
        // Global gravity pulls all particles downward (negative frequency
        // direction), or upward if gravity_ is negative.
        const float gForce = gravity_ * kAccelScale;
        for (auto& p : particles_)
        {
            if (!p.active) continue;
            p.acceleration += gForce;
        }
    }

    // ---- Advance each particle ----
    for (auto& p : particles_)
    {
        if (p.active)
            updateParticle(p, dt);
    }
}

//==============================================================================
//  Render to partials
//==============================================================================

void SpectralParticleSystem::process(PartialDataSIMD& partials)
{
    // Clear all partial data
    std::memset(partials.amplitude, 0, sizeof(partials.amplitude));
    std::memset(partials.frequency, 0, sizeof(partials.frequency));
    std::memset(partials.phase,     0, sizeof(partials.phase));

    const float nyquist = static_cast<float>(partials.sampleRate) * 0.5f;
    const int   N       = partials.maxPartials;

    if (nyquist <= 0.0f || N <= 0)
    {
        partials.activeCount = 0;
        std::memset(partials.activeMask, 0, sizeof(partials.activeMask));
        return;
    }

    // Scatter each active particle into the nearest partial slot by
    // frequency-ratio mapping.  Multiple particles landing in the same
    // slot have their amplitudes summed.
    for (const auto& p : particles_)
    {
        if (!p.active) continue;

        // Normalised frequency [0, 1) → partial index
        const float normFreq = juce::jlimit(0.0f, 0.999f,
                                            p.frequency / nyquist);
        const int idx = juce::jlimit(0, N - 1,
                                     static_cast<int>(normFreq * static_cast<float>(N)));

        partials.frequency[idx] = p.frequency;
        partials.amplitude[idx] += p.amplitude * p.brightness;
        if (partials.phase[idx] == 0.0f)
            partials.phase[idx] = p.phase;
    }

    // Update the bitmask-based active tracking
    partials.updateActiveMask();
}

//==============================================================================
//  Render to spectrum
//==============================================================================

std::vector<float> SpectralParticleSystem::renderToSpectrum(int numBins,
                                                             double sampleRate)
{
    std::vector<float> spectrum(static_cast<size_t>(numBins), 0.0f);

    if (numBins <= 0)
        return spectrum;

    const float nyquist  = static_cast<float>(sampleRate) * 0.5f;
    const float binWidth = nyquist / static_cast<float>(numBins);

    if (binWidth <= 0.0f)
        return spectrum;

    for (const auto& p : particles_)
    {
        if (!p.active) continue;

        const int bin = juce::jlimit(0, numBins - 1,
            static_cast<int>(p.frequency / binWidth));

        spectrum[static_cast<size_t>(bin)] += p.amplitude * p.brightness;
    }

    return spectrum;
}

//==============================================================================
//  Access
//==============================================================================

const std::vector<SpectralParticleSystem::Particle>&
SpectralParticleSystem::getParticles() const
{
    return particles_;
}

int SpectralParticleSystem::getActiveCount() const
{
    int count = 0;
    for (const auto& p : particles_)
        if (p.active)
            ++count;
    return count;
}

//==============================================================================
//  Reset
//==============================================================================

void SpectralParticleSystem::reset()
{
    killAll();
    forceFields_.clear();
    emissionAccum_ = 0.0;
    maxParticles_  = 512;
    emissionRate_  = 100.0f;
    particleLife_  = 2.0f;
    damping_       = 0.995f;
    gravity_       = 0.0f;
    turbulence_    = 0.0f;
    sampleRate_    = 44100.0;
    fftSize_       = 2048;

    // Reset noise state
    noiseType_          = NoiseType::White;
    noiseColor_         = 0.5f;
    noiseAmplitude_     = 0.0f;
    noiseEnvAttack_     = 10.0f;
    noiseEnvDecay_      = 100.0f;
    noiseEnvSustain_    = 0.5f;
    noiseEnvRelease_    = 200.0f;
    noiseEnvStage_      = EnvStage::Idle;
    noiseEnvValue_      = 0.0f;
    noiseEnvTriggered_  = false;

    particles_.resize(maxParticles_);
}

void SpectralParticleSystem::killAll()
{
    for (auto& p : particles_)
    {
        p.active     = false;
        p.amplitude  = 0.0f;
        p.life       = 0.0f;
        p.velocity   = 0.0f;
        p.acceleration = 0.0f;
        p.brightness = 0.0f;
    }

    // Kill noise envelope
    noiseEnvStage_     = EnvStage::Idle;
    noiseEnvValue_     = 0.0f;
    noiseEnvTriggered_ = false;
}

//==============================================================================
//  Private helpers
//==============================================================================

void SpectralParticleSystem::applyForceField(const ForceField& field, double dt)
{
    const float nyquist = static_cast<float>(sampleRate_) * 0.5f;
    if (nyquist <= 0.0f)
        return;

    // Convert normalised field position to Hz
    const float fieldCenterHz = kMinFreq + field.position * (nyquist - kMinFreq);
    const float influenceHz   = field.radius * nyquist;
    const float strength      = field.strength * kAccelScale;

    for (auto& p : particles_)
    {
        if (!p.active) continue;

        const float dx      = p.frequency - fieldCenterHz;
        const float dist    = std::abs(dx);
        const float sign    = (dx >= 0.0f) ? 1.0f : -1.0f;

        switch (field.type)
        {
            case ForceField::Type::Gravity:
            {
                // Pull particle toward field centre proportional to distance
                const float falloff = juce::jmin(1.0f, dist / (influenceHz + kEpsilon));
                p.acceleration += -sign * strength * falloff;
                break;
            }

            case ForceField::Type::Repulsion:
            {
                // Push particle away from field centre
                if (dist < influenceHz && dist > 1.0f)
                {
                    const float falloff = 1.0f - dist / (influenceHz + kEpsilon);
                    p.acceleration += sign * strength * falloff;
                }
                break;
            }

            case ForceField::Type::Vortex:
            {
                // Tangential acceleration: creates orbital motion around the
                // field centre.  Particles offset in one direction are pushed
                // further in that direction, while gravity (if active) pulls
                // them back, creating a circulating dynamic.
                if (dist < influenceHz && dist > 1.0f)
                {
                    const float falloff = 1.0f - dist / (influenceHz + kEpsilon);
                    p.acceleration += sign * strength * falloff;
                }
                break;
            }

            case ForceField::Type::Turbulence:
            {
                // Random acceleration within the field's influence region
                if (dist < influenceHz)
                {
                    const float falloff = 1.0f - dist / (influenceHz + kEpsilon);
                    const float rnd = (random_.nextFloat() * 2.0f - 1.0f);
                    p.acceleration += rnd * strength * falloff;
                }
                break;
            }

            case ForceField::Type::Attractor:
            {
                // Strong pull toward field centre with 1/r² falloff
                if (dist > 0.5f) // avoid extreme forces at very close range
                {
                    const float force = strength
                                      / (dist * dist + kAttractorEps);
                    p.acceleration += -sign * force;
                }
                break;
            }
        }
    }
}

void SpectralParticleSystem::updateParticle(Particle& p, double dt)
{
    const float dtf = static_cast<float>(dt);

    // ---- Physics ----
    p.velocity      += p.acceleration * dtf;
    p.frequency     += p.velocity * dtf;
    p.velocity      *= p.damping;

    // ---- Frequency clamping ----
    const float nyquist = static_cast<float>(sampleRate_) * 0.5f;
    p.frequency = juce::jlimit(kMinFreq, nyquist * 0.999f, p.frequency);

    // ---- Lifecycle ----
    p.life -= p.decay * dtf;

    if (p.life <= 0.0f)
    {
        p.active     = false;
        p.amplitude  = 0.0f;
        p.life       = 0.0f;
        p.brightness = 0.0f;
        return;
    }

    // Brightness tracks remaining life for smooth amplitude fading
    p.brightness = p.life;
}

int SpectralParticleSystem::findInactiveParticle()
{
    for (int i = 0; i < maxParticles_; ++i)
        if (!particles_[static_cast<size_t>(i)].active)
            return i;

    return -1;
}

void SpectralParticleSystem::emitParticle(float freq, float amp, float phase)
{
    const int idx = findInactiveParticle();
    if (idx < 0)
        return;

    Particle& p = particles_[static_cast<size_t>(idx)];

    p.frequency    = freq;
    p.amplitude    = amp;
    p.phase        = phase;
    p.velocity     = 0.0f;
    p.acceleration = 0.0f;
    p.mass         = 1.0f;
    p.damping      = damping_;
    p.life         = 1.0f;
    p.decay        = 1.0f / particleLife_; // life reaches 0 in particleLife_ secs
    p.hue          = 0.0f;
    p.brightness   = 1.0f;
    p.active       = true;
}

} // namespace ana
