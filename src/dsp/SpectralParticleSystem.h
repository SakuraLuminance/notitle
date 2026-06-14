#pragma once

#include <vector>
#include <cstdint>
#include <juce_core/juce_core.h>
#include "PartialDataSIMD.h"

namespace ana {

//==============================================================================
/**
    SpectralParticleSystem — physics-based spectral particle simulation.

    Decomposes a spectrum into individual particles with physical properties
    (velocity, acceleration, mass, damping) that evolve under configurable
    force fields (gravity, repulsion, vortex, turbulence, attractor).

    Particles are emitted from spectrum data, partials, or as random bursts,
    and can be rendered back into spectral or partial representations for
    synthesis.

    @see PartialDataSIMD, ForceField
*/
class SpectralParticleSystem
{
public:
    //==============================================================================
    /** A single spectral particle with physical properties. */
    struct Particle
    {
        float frequency     = 440.0f;   /**< Current frequency in Hz.                  */
        float amplitude     = 0.0f;     /**< Current amplitude (0.0 – 1.0).            */
        float phase         = 0.0f;     /**< Phase in radians.                         */
        float velocity      = 0.0f;     /**< Frequency change rate (Hz/s).             */
        float acceleration  = 0.0f;     /**< Current acceleration (Hz/s²).             */
        float mass          = 1.0f;     /**< Particle mass (affects force response).   */
        float damping      = 0.995f;   /**< Velocity damping per frame (0.9 – 1.0).   */
        float life          = 1.0f;     /**< Remaining life (1.0 = full, 0.0 = dead).  */
        float decay         = 0.001f;   /**< Life decay rate (per second).             */
        float hue           = 0.0f;     /**< Visual hue mapping (for UI).              */
        float brightness    = 1.0f;     /**< Visual brightness (tracks life).          */
        bool  active        = false;    /**< Whether this particle slot is in use.      */
    };

    //==============================================================================
    /** A force field that influences particles within a frequency region. */
    struct ForceField
    {
        enum class Type { Gravity, Repulsion, Vortex, Turbulence, Attractor };

        Type    type     = Type::Gravity;
        float   strength = 0.1f;        /**< Force magnitude.                         */
        float   position = 0.5f;        /**< Normalised frequency position [0, 1].    */
        float   radius   = 0.2f;        /**< Normalised influence radius [0, 1].      */
    };

    //==============================================================================
    SpectralParticleSystem();
    ~SpectralParticleSystem() = default;

    //==============================================================================
    /** @name Configuration */
    //@{
    void setMaxParticles(int max);       /**< Maximum particle count (64 – 2048).     */
    void setEmissionRate(float rate);    /**< Particles emitted per second.            */
    void setParticleLife(float seconds); /**< Particle lifetime in seconds (0.1–10).  */
    void setDamping(float damping);      /**< Velocity damping per frame (0.9 – 1.0). */
    void setGravity(float strength);     /**< Global gravity acceleration.             */
    void setTurbulence(float amount);    /**< Random acceleration amount (0.0 – 1.0). */
    //@}

    //==============================================================================
    /** @name Force field management */
    //@{
    void addForceField(const ForceField& field);
    void clearForceFields();
    int  getNumForceFields() const;
    ForceField& getForceField(int index);
    //@}

    //==============================================================================
    /** @name Emission */
    //@{
    /** Emit particles from FFT magnitude data. Creates one particle per
        significant bin with frequency proportional to bin index. */
    void emitFromSpectrum(const std::vector<float>& magnitudes,
                          double sampleRate, int fftSize);

    /** Emit particles from active partial data. */
    void emitFromPartials(const PartialDataSIMD& partials);

    /** Emit a burst of random particles (creative effects). */
    void emitBurst(int count);
    //@}

    //==============================================================================
    /** @name Processing */
    //@{
    /** Advance the simulation by deltaTime seconds.
        Applies force fields, updates particle physics and lifecycles. */
    void update(double deltaTime);

    /** Render active particles into the partial data structure.
        Maps particles to partial slots by frequency proximity. */
    void process(PartialDataSIMD& partials);

    /** Render active particles into an FFT magnitude spectrum. */
    std::vector<float> renderToSpectrum(int numBins, double sampleRate);
    //@}

    //==============================================================================
    /** @name Access */
    //@{
    const std::vector<Particle>& getParticles() const;
    int getActiveCount() const;
    //@}

    //==============================================================================
    /** @name Noise oscillator */
    //@{
    enum class NoiseType
    {
        White,  /**< Equal energy per Hz — flat spectral density.            */
        Pink,   /**< Equal energy per octave — -3dB/oct rolloff.            */
        Brown   /**< Random walk / brownian — -6dB/oct rolloff (darker).    */
    };

    /** Set the noise colour / spectrum type. */
    void setNoiseType(NoiseType type);
    /** Return the current noise type. */
    NoiseType getNoiseType() const;
    /** Set noise spectral brightness (0 = dark, 0.5 = flat, 1 = bright). */
    void setNoiseColor(float brightness);
    /** Return the noise brightness parameter. */
    float getNoiseColor() const;
    /** Set overall noise amplitude 0–1 (0 = silent). */
    void setNoiseAmplitude(float amp);
    /** Return the noise amplitude. */
    float getNoiseAmplitude() const;

    /** Attack time in milliseconds for the per-burst noise envelope. */
    void setNoiseEnvAttack(float ms);
    /** Decay time in milliseconds for the per-burst noise envelope. */
    void setNoiseEnvDecay(float ms);
    /** Sustain level 0–1 for the per-burst noise envelope. */
    void setNoiseEnvSustain(float level);
    /** Release time in milliseconds for the per-burst noise envelope. */
    void setNoiseEnvRelease(float ms);

    /** Generate noise partials into the given PartialDataSIMD structure.
        The noise oscillator writes into the last numActivePartials slots
        so it can coexist with spectral particles written by process().

        @param partials           The partial data to write into.
        @param numActivePartials  How many partial slots to fill with noise.
        @param envelopeLevel      External gate/amplitude (0 = silent, >0 = on).
    */
    void generateNoisePartials(PartialDataSIMD& partials,
                               int numActivePartials, float envelopeLevel);
    //@}

    //==============================================================================
    /** @name Reset */
    //@{
    void reset();
    void killAll();
    //@}

private:
    //==============================================================================
    /** Apply a single force field to all active particles. */
    void applyForceField(const ForceField& field, double dt);

    /** Advance one particle's physics and lifecycle by dt seconds. */
    void updateParticle(Particle& p, double dt);

    /** Find the first inactive particle slot. Returns -1 if full. */
    int findInactiveParticle();

    /** Create a particle at the given frequency, amplitude, and phase. */
    void emitParticle(float freq, float amp, float phase);

    //==============================================================================
    std::vector<Particle>    particles_;
    std::vector<ForceField>  forceFields_;
    int    maxParticles_     = 512;
    float  emissionRate_     = 100.0f;
    float  particleLife_     = 2.0f;
    float  damping_          = 0.995f;
    float  gravity_          = 0.0f;
    float  turbulence_       = 0.0f;

    double emissionAccum_    = 0.0;
    double sampleRate_       = 44100.0;
    int    fftSize_          = 2048;

    juce::Random random_;

    //==============================================================================
    // --- Noise oscillator state ---

    static constexpr int kNoisePartialsMax = 64;   /**< Max noise partials per frame. */

    NoiseType noiseType_           = NoiseType::White;
    float     noiseColor_          = 0.5f;          /**< Spectral tilt (0=dark, 1=bright). */
    float     noiseAmplitude_      = 0.0f;          /**< Overall noise gain.             */

    float     noiseEnvAttack_      = 10.0f;         /**< Attack time in ms.              */
    float     noiseEnvDecay_       = 100.0f;        /**< Decay time in ms.               */
    float     noiseEnvSustain_     = 0.5f;           /**< Sustain level 0–1.              */
    float     noiseEnvRelease_     = 200.0f;         /**< Release time in ms.             */

    // --- Internal ADSR envelope state ---
    enum class EnvStage : uint8_t { Idle, Attack, Decay, Sustain, Release };

    EnvStage noiseEnvStage_       = EnvStage::Idle;
    float    noiseEnvValue_       = 0.0f;
    bool     noiseEnvTriggered_   = false;  /**< Previous gate state for edge detection. */

    /** Advance the noise ADSR envelope by dt seconds. */
    void advanceNoiseEnvelope(float dt);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralParticleSystem)
};

} // namespace ana
