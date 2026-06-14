#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cstdint>

#include "PartialDataSIMD.h"

namespace ana {

//==============================================================================
/**
 * Physical modeling synthesis engine integrating Karplus-Strong waveguide and
 * modal synthesis with the additive partial engine.
 *
 * Each partial can become a waveguide resonator, allowing hybrid physical/
 * additive sound design.
 *
 * Two output paths:
 *   - generate():  Compute partial amplitudes/frequencies from the model
 *                  (feeds the additive engine)
 *   - processAudio(): Generate audio through the waveguide and modal resonators
 *                  (direct signal path)
 */
class PhysicalModel
{
public:
    enum class ModelType { String, Pluck, Blow, Membrane, Plate };
    enum class Excitation { Noise, Impulse, Sweep, Sample };

    PhysicalModel();
    ~PhysicalModel() = default;

    //==============================================================================
    /** Sets the physical model topology (string, pluck, blow, membrane, plate). */
    void setModelType(ModelType type);

    /** Sets the excitation method (noise burst, impulse, frequency sweep, sample). */
    void setExcitation(Excitation excitation);

    //==============================================================================
    // Physical parameters
    //==============================================================================

    /** Material stiffness [0, 1]. Controls brightness and inharmonicity. */
    void setMaterial(float stiffness);

    /** Damping [0, 1]. Controls how quickly high frequencies decay. */
    void setDamping(float damping);

    /** Tension [0, 1]. Controls pitch stability and brightness. */
    void setTension(float tension);

    /** Inharmonicity [0, 1]. Stretches partial frequencies. */
    void setInharmonicity(float amount);

    /** Decay [0, 1]. Overall rate of energy dissipation. */
    void setDecay(float decay);

    /** Excitation position [0, 1]. Suppresses modes at this point on the string. */
    void setPosition(float position);

    //==============================================================================
    // Fundamental
    //==============================================================================

    /** Sets the fundamental frequency in Hz. Clamped to [20, 20000]. */
    void setFrequency(float freqHz);

    /** Sets the sample rate. Must be called before processing. */
    void setSampleRate(double sr);

    //==============================================================================
    // Processing
    //==============================================================================

    /**
     * Computes partial amplitudes and frequencies from the physical model
     * and writes them into the given PartialDataSIMD.
     *
     * String:      amplitudes follow 1/f² rolloff with stiffness-dependent variation
     * Pluck:       position-dependent mode suppression (harmonics missing at
     *              excitation point)
     * Blow:        odd harmonics emphasised (reed-like spectrum)
     * Membrane:    2D mode distribution (Bessel-function-inspired ratios)
     * Plate:       2D plate mode distribution
     *
     * Inharmonicity stretches partial frequencies by (1 + inharmonicity * (n² - n))
     */
    void generate(PartialDataSIMD& output);

    /**
     * Processes audio through the Karplus-Strong waveguide and modal resonators.
     *
     * The waveguide delay line is filled with the chosen excitation on trigger(),
     * then each sample goes through: delay read -> lowpass filter -> feedback -> delay write.
     * Mode resonators are driven by the waveguide output for additional richness.
     */
    void processAudio(juce::AudioBuffer<float>& buffer);

    //==============================================================================
    // Lifecycle
    //==============================================================================

    /** Triggers the model: fills waveguides with excitation and starts processing. */
    void trigger();

    /** Releases the model: stops excitation, natural decay follows. */
    void release();

    /** Resets all state: clears waveguides, modes, and flags. */
    void reset();

    /** Returns true while the model is actively producing sound. */
    bool isActive() const noexcept { return active_; }

private:
    //==============================================================================
    // Waveguide (Karplus-Strong)
    //==============================================================================
    struct Waveguide {
        std::vector<float> delayLine;
        int writePos = 0;
        float lowpassState = 0.0f;

        /** Initialises the delay line for a given frequency and sample rate. */
        void init(float freq, double sampleRate);

        /** Processes one sample through the waveguide delay line.
            @param input    Input sample
            @param lpCoeff  Pre-computed one-pole lowpass coefficient
            @param feedback Pre-computed feedback gain
        */
        float process(float input, float lpCoeff, float feedback);
    };

    //==============================================================================
    // Modal resonator (2nd-order IIR)
    //==============================================================================
    struct Mode {
        float frequency = 0.0f;
        float amplitude = 0.0f;
        float decay = 0.0f;
        float phase = 0.0f;
        float state[2] = {0.0f, 0.0f};  // y[n-1], y[n-2]

        /** Processes one sample through this 2-pole resonator. */
        float process(float input, double sampleRate);
    };

    //==============================================================================
    // Internal helpers
    //==============================================================================

    /** Computes the amplitude for a given harmonic index based on model parameters. */
    float computeAmplitude(int partialIndex) const;

    /** Fills a delay line with the current excitation type. */
    void fillExcitation(std::vector<float>& delayLine, std::minstd_rand& rng);

    //==============================================================================
    // Member data
    //==============================================================================

    std::vector<Waveguide> waveguides_;
    std::vector<Mode> modes_;

    ModelType modelType_ = ModelType::Pluck;
    Excitation excitation_ = Excitation::Noise;

    float stiffness_ = 0.5f;
    float damping_ = 0.3f;
    float tension_ = 0.5f;
    float inharmonicity_ = 0.0f;
    float decay_ = 0.5f;
    float position_ = 0.3f;

    float fundamentalFreq_ = 440.0f;
    double sampleRate_ = 44100.0;

    bool active_ = false;
    int sampleCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhysicalModel)
};

} // namespace ana
