#pragma once
#include "../EffectsChain.h"
#include "EQModule.h"
#include "DriveModule.h"
#include "DeEsserModule.h"
#include "BreathNoiseGenerator.h"
#include "FormantTuner.h"
#include "StereoWidenerEffect.h"
#include "PitchModule.h"
#include "CompressorEffect.h"
#include "SpaceModule.h"
#include <atomic>
#include <juce_dsp/juce_dsp.h>

namespace ana {

//==============================================================================
/**
    Seven vocal-character modes that configure existing DSP modules
    into distinct vocal processing chains.

    Each mode is a preset — selecting a mode does not create new DSP
    but re-configures and re-routes the owned sub-modules via applyMode().

    Modes:
        Chest     — Sub-octave + low shelf + LP + soft drive
        Head      — Formant shift + high shelf + stereo width
        Breathy   — Breath noise + de-esser
        Telephone — BPF 300-3400Hz + bitcrush + notch
        Choir     — Harmony unison + formant spread + hall reverb
        Megaphone — Heavy compression + BPF + hard drive + mid boost
        Whisper   — Noise gate + HPF + de-ess + presence cut
*/
enum class VocalCharacter
{
    Chest = 0,
    Head,
    Breathy,
    Telephone,
    Choir,
    Megaphone,
    Whisper,
    NumModes
};

//==============================================================================
/**
    Composite vocal-character effect that routes audio through a mode-specific
    chain of existing DSP modules.

    Owns the union of all sub-modules required across the 7 modes.
    applyMode() sets parameters, bypass states, and filter coefficients.
    process() runs the active chain for the current mode.
*/
class VocalProcessor : public EffectBase
{
public:
    VocalProcessor();
    ~VocalProcessor() override = default;

    //==============================================================================
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    //==============================================================================
    /** Re-configure all sub-modules for the given vocal character.
        Sets per-mode parameters, bypass states, and filter coefficients. */
    void applyMode(VocalCharacter mode);

    /** Return the currently active mode. */
    VocalCharacter getCurrentMode() const noexcept { return currentMode_; }

    //==============================================================================
    juce::ValueTree getState() const override;
    void setState(const juce::ValueTree& state) override;

    //==============================================================================
    /** @name Mode names for UI population. */
    static const char* getModeName(VocalCharacter mode);
    static int getNumModes() noexcept { return static_cast<int>(VocalCharacter::NumModes); }

private:
    //==============================================================================
    // Owned sub-modules (union across all 7 modes)

    PitchModule          pitch_;        // Chest (sub octave), Choir (harmony)
    EQModule             eq_;           // Chest, Head, Telephone, Megaphone, Whisper
    DriveModule          drive_;        // Chest (Soft), Telephone (Crush), Megaphone (Hard)
    DeEsserModule        deEsser_;      // Breathy, Whisper
    BreathNoiseGenerator breath_;       // Breathy
    FormantTuner         formant_;      // Head (shift), Choir (spread)
    StereoWidenerEffect  widener_;      // Head
    CompressorEffect     compressor_;   // Megaphone, Whisper (gate)
    SpaceModule          space_;        // Choir

    //==============================================================================
    // Inline IIR filters for mode-specific LP / BP / HP

    /** Low-pass filter (Chest: 4 kHz cut). Used together with hpf_ for
        band-pass in Telephone and Megaphone modes. */
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                    juce::dsp::IIR::Coefficients<float>> lpf_;

    /** High-pass filter (Whisper: 500 Hz). Used together with lpf_ for
        band-pass in Telephone and Megaphone modes. */
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                    juce::dsp::IIR::Coefficients<float>> hpf_;

    //==============================================================================
    /** Simple noise gate for Whisper mode.
        Attenuates the signal when envelope falls below threshold. */
    struct NoiseGate
    {
        void prepare(double sampleRate);
        void setThreshold(float db);
        void process(juce::AudioBuffer<float>& buffer);

        float envelope   = 0.0f;
        float threshold_ = -40.0f;   // dB
        float attackCoeff_  = 0.0f;
        float releaseCoeff_ = 0.0f;
        double sampleRate_  = 44100.0;
    } noiseGate_;

    //==============================================================================
    VocalCharacter currentMode_ = VocalCharacter::Chest;

    double sampleRate_ = 44100.0;
    int    blockSize_  = 512;
    int    numChannels_ = 2;

    /** Samples remaining in the anti-zipper gain ramp after mode change. */
    std::atomic<int> rampSamplesLeft_{0};

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalProcessor)
};

} // namespace ana
