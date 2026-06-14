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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralParticleSystem)
};

} // namespace ana
