#pragma once

#include <vector>
#include <cstdint>
#include <juce_core/juce_core.h>

#include "PartialDataSIMD.h"

namespace ana {

// ============================================================================
/** AI-inspired generative timbre designer using evolutionary/genetic algorithms
    plus interpolation and morphing between timbres.

    A 64-dimensional latent vector controls all aspects of timbre:
      values[0-19]   - Harmonic amplitude envelope (harmonics 1-20)
      values[20-29]  - Formant peaks (freq, amp, bandwidth x3 + spare)
      values[30-39]  - Noise characteristics (tilt, density, brightness, etc.)
      values[40-44]  - Inharmonicity (stretch, compression, randomness, decay)
      values[45-49]  - Modulation (tremolo depth, vibrato depth, speed, shape)
      values[50-54]  - Filter characteristics (cutoff, resonance, envelope, type)
      values[55-59]  - Effects (reverb mix, delay mix, chorus depth, width)
      values[60-63]  - Global controls (volume, stereo width, brightness, warmth)

    Each dimension is in [-1, 1].
*/
class GenerativeTimbreDesigner
{
public:
    // ========================================================================
    // Latent space representation — 64-dim vector controls timbre
    // ========================================================================
    struct LatentVector
    {
        float values[64] = { 0.0f };

        /** Fill all 64 dimensions with uniform random values in [-1, 1]. */
        void randomize(juce::Random& rng);

        /** Linear interpolation: this = a * (1-t) + b * t. */
        void interpolate(const LatentVector& a, const LatentVector& b, float t);

        /** Perturb each dimension by a random amount in [-amount, amount]. */
        void mutate(float amount, juce::Random& rng);

        /** Euclidean distance to another latent vector. */
        float distance(const LatentVector& other) const;
    };

    // ========================================================================
    // A timbre individual for use in the genetic algorithm
    // ========================================================================
    struct Individual
    {
        LatentVector genome;
        float fitness = 0.0f;
        PartialDataSIMD timbre;     // the generated timbre from genome

        /** Generate partial data from genome using baseHarmonics as
            the frequency series (e.g. { 100, 200, 300, ... }). */
        void generate(PartialDataSIMD& output,
                      const std::vector<float>& baseHarmonics);
    };

    // ========================================================================
    // Construction / destruction
    // ========================================================================
    GenerativeTimbreDesigner();
    ~GenerativeTimbreDesigner() = default;

    // ========================================================================
    // Latent vector control
    // ========================================================================
    /** Replace the current latent vector. */
    void setLatentVector(const LatentVector& v);

    /** Retrieve a copy of the current latent vector. */
    LatentVector getLatentVector() const;

    /** Randomise the current latent vector in place. */
    void randomizeLatent();

    /** Apply mutation to the current latent vector. */
    void mutateLatent(float amount);

    // ========================================================================
    // Interpolation / morphing
    // ========================================================================
    /** Set the target latent vector for morphing. */
    void setTargetTimbre(const LatentVector& target);

    /** Morph current latent toward target by t ∈ [0,1].
        t=0 → current unchanged; t=1 → current becomes target. */
    void morphToTarget(float t);

    /** How fast morph progress advances per call (default 0.01). */
    void setMorphSpeed(float speed);

    // ========================================================================
    // Genetic algorithm evolution
    // ========================================================================
    /** Set population size (clamped to 8–128, default 32). */
    void setPopulationSize(int size);

    /** Set per-gene mutation rate [0, 1] (default 0.1). */
    void setMutationRate(float rate);

    /** Set crossover probability [0, 1] (default 0.7). */
    void setCrossoverRate(float rate);

    /** Run one generation of evolution. Lazily initialises the population
        on the first call. */
    void evolve();

    /** Clear population, reset generation counter and fittest. */
    void resetEvolution();

    /** Return the fittest individual found so far. */
    const Individual& getFittest() const;

    /** Current generation number (0 after reset / before any evolve). */
    int getGeneration() const;

    // ========================================================================
    // Presets
    // ========================================================================
    enum class Preset
    {
        Warm,
        Bright,
        Dark,
        Metallic,
        Glassy,
        Hollow,
        Rich,
        Thin
    };

    /** Load a preset: replaces currentLatent_ with the preset's vector. */
    void loadPreset(Preset preset);

    /** Write a preset's latent vector into out without changing internal state. */
    void loadLatentPreset(Preset preset, LatentVector& out);

    // ========================================================================
    // Timbre generation
    // ========================================================================
    /** Generate partials from the current latent vector.
        The result is also stored internally for applyToPartials(). */
    void generate(PartialDataSIMD& output);

    /** Generate partials from an arbitrary latent vector.
        The result is also stored internally for applyToPartials(). */
    void generateFromLatent(const LatentVector& latent, PartialDataSIMD& output);

    // ========================================================================
    // Apply / mix
    // ========================================================================
    /** Mix the internally stored generated timbre into partials.
        mix=0 leaves partials untouched; mix=1 fully replaces. */
    void applyToPartials(PartialDataSIMD& partials, float mix = 1.0f);

    // ========================================================================
    // Capture / analyse
    // ========================================================================
    /** Inverse mapping: analyse partial amplitudes to estimate a latent vector. */
    void captureFromPartials(const PartialDataSIMD& partials);

    // ========================================================================
    // Reset
    // ========================================================================
    /** Full reset — latent vectors, morph state, evolution, generated data. */
    void reset();

private:
    // ------------------------------------------------------------------------
    // Core algorithm
    // ------------------------------------------------------------------------
    /** Convert a 64-dim latent vector into 512 harmonic partials. */
    void latentToPartials(const LatentVector& latent, PartialDataSIMD& output);

    // ------------------------------------------------------------------------
    // Evolution helpers
    // ------------------------------------------------------------------------
    /** Evaluate fitness of one individual (used internally by evolve). */
    void evaluateFitness(Individual& individual);

    /** Tournament selection: pick tournamentSize random individuals,
        return the fittest. */
    const Individual& tournamentSelect(const std::vector<Individual>& pop,
                                       juce::Random& rng,
                                       int tournamentSize = 3) const;

    /** Uniform-crossover two parent genomes into a child. */
    Individual crossover(const Individual& a, const Individual& b) const;

    /** Apply per-gene mutation to an individual in place. */
    void mutate(Individual& individual);

    // ------------------------------------------------------------------------
    // Latent-space utilities
    // ------------------------------------------------------------------------
    /** Normalise and decorrelate latent vector (algorithmic substitute for
        real PCA whitening — clamps to [-1,1] and spreads values). */
    void applyPcaWhitening(LatentVector& v);

    // ------------------------------------------------------------------------
    // Preset initialisation
    // ------------------------------------------------------------------------
    /** Fill static preset vectors (idempotent, guarded by presetsInitialized_). */
    void initPresets();

    // ========================================================================
    // Member state
    // ========================================================================
    LatentVector currentLatent_;
    LatentVector targetLatent_;

    float morphProgress_ = 0.0f;
    float morphSpeed_    = 0.01f;

    std::vector<Individual> population_;
    Individual fittest_;
    PartialDataSIMD generatedTimbre_;   // last generated output (cached for mix)

    int     populationSize_ = 32;
    float   mutationRate_   = 0.1f;
    float   crossoverRate_  = 0.7f;
    int     generation_     = 0;
    bool    evolutionReady_ = false;

    double  sampleRate_ = 44100.0;

    // --------------------------------------------------------------------
    // Static preset storage (shared across instances)
    // --------------------------------------------------------------------
    static LatentVector presetWarm_;
    static LatentVector presetBright_;
    static LatentVector presetDark_;
    static LatentVector presetMetallic_;
    static LatentVector presetGlassy_;
    static LatentVector presetHollow_;
    static LatentVector presetRich_;
    static LatentVector presetThin_;
    static bool         presetsInitialized_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenerativeTimbreDesigner)
};

} // namespace ana
