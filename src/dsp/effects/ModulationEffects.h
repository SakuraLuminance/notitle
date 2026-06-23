#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

//==============================================================================
/**
    Tremolo + Auto-Pan consolidated effect.

    Two modes driven by a single LFO:
        Tremolo — amplitude modulation on all channels
        AutoPan — stereo panning with configurable stereo offset

    LFO shapes: Sine, Triangle, Square
*/
enum class TremoloMode { Tremolo, AutoPan };
enum class TremoloShape { Sine, Triangle, Square };

class ModulationEffects : public EffectBase
{
public:
    ModulationEffects();
    ~ModulationEffects() override = default;

    //==============================================================================
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    //==============================================================================
    /** @name Parameter Setters */
    void setMode(TremoloMode m);
    void setRate(float hz);               // 0.1 – 20.0 Hz
    void setDepth(float d01);             // 0…1  (0 = no effect, 1 = full depth)
    void setShape(TremoloShape s);
    void setStereoOffset(float degrees);  // 0 – 180° (AutoPan only)
    void setMix(float wet);               // 0…1
    void setWetHPF(float hz);
    void setWetLPF(float hz);
    void setBypass(bool b);

    //==============================================================================
    juce::ValueTree getState() const override;
    void setState(const juce::ValueTree& tree) override;

    //==============================================================================
    /** Returns the list of factory preset names. */
    static juce::StringArray getFactoryPresets();

    /** Returns the parameter ValueTree for a named factory preset. */
    static juce::ValueTree getFactoryPreset(const juce::String& name);

private:
    //==============================================================================
    /** Generate a normalised LFO value [-1, 1] from the current phase and shape. */
    float lfoGen(float phase) const;

    //==============================================================================
    /** Process a block in Tremolo mode. */
    void processTremolo(juce::AudioBuffer<float>& buffer);

    /** Process a block in AutoPan mode. */
    void processAutoPan(juce::AudioBuffer<float>& buffer);

    //==============================================================================
    void applyWetFilters(juce::AudioBuffer<float>& buffer);
    void applyDryWetMix(juce::AudioBuffer<float>& buffer);

    //==============================================================================
    /** Parameter state. */
    TremoloMode  mode_           = TremoloMode::Tremolo;
    TremoloShape shape_          = TremoloShape::Sine;
    float        rateHz_         = 5.0f;
    float        depth01_        = 0.5f;
    float        stereoOffsetDeg_ = 90.0f;   // 0 – 180
    float        mixVal_         = 1.0f;
    bool         bypassed_       = false;

    //==============================================================================
    /** Processing specs. */
    double sampleRate_ = 44100.0;
    int    numChannels_ = 2;

    //==============================================================================
    /** Per-channel LFO phase in [0, 1). */
    std::vector<float> lfoPhase_;

    //==============================================================================
    /** Wet-signal HPF / LPF. */
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> wetHPF_;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> wetLPF_;

    //==============================================================================
    /** Dry-signal buffer for wet/dry blend. */
    juce::AudioBuffer<float> dryBuffer_;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationEffects)
};

} // namespace ana
