#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include "../PitchCorrector.h"
#include <vector>

namespace ana {

//==============================================================================
/**
    Realtime Auto-Tune Effect.
    
    Uses PitchCorrector to analyze incoming audio blocks, determine the fundamental
    frequency, snap it to a musical scale, and smoothly pitch-shift the audio.
*/
class AutoTuneEffect
{
public:
    AutoTuneEffect();
    ~AutoTuneEffect() = default;

    void setSampleRate(double sr);
    
    /** Pre-allocate buffers for the given spec. Must be called before processing. */
    void prepare(const juce::dsp::ProcessSpec& spec);
    
    /** Set retune speed in milliseconds (0 = instant, >0 = smooth transition). */
    void setRetuneSpeed(float ms);
    
    /** Set active notes in the chromatic scale (12 booleans, true = active). 
        Empty or all true = Chromatic scale. */
    void setScale(const std::vector<bool>& activeNotesInOctave);
    
    /** Enable or disable the effect. */
    void setEnabled(bool e);
    
    /** Set effect wet/dry amount (0.0 to 1.0). */
    void setAmount(float amount);
    
    /** Choose the pitch correction algorithm (from PitchCorrector). */
    void setAlgorithm(PitchAlgorithm algo);

    /** Process audio in-place. */
    void processBlock(juce::AudioBuffer<float>& buffer);

    /** Reset internal state (e.g., smoothing filters). */
    void reset();

    juce::ValueTree getState() const
    {
        juce::ValueTree tree("AutoTuneEffect");
        tree.setProperty("retuneSpeed", retuneSpeed_, nullptr);
        tree.setProperty("amount", amount_, nullptr);
        tree.setProperty("enabled", enabled_, nullptr);
        tree.setProperty("scale", scaleMask, nullptr);
        return tree;
    }

    void setState(const juce::ValueTree& state)
    {
        setRetuneSpeed(state.getProperty("retuneSpeed", 50.0f));
        setAmount(state.getProperty("amount", 1.0f));
        setEnabled(state.getProperty("enabled", false));
        scaleMask = (int)state.getProperty("scale", 0);
    }

private:
    PitchCorrector pitchCorrector_;
    std::vector<bool> scaleNotes_;
    int scaleMask = 0;
    
    double sampleRate_ = 44100.0;
    float retuneSpeed_ = 50.0f; // ms
    float amount_ = 1.0f;
    bool enabled_ = false;

    float currentShiftSemitones_ = 0.0f;
    
    // For pitch detection
    std::vector<float> monoBuffer_;

    float getNearestNote(float midiNote) const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoTuneEffect)
};

} // namespace ana
