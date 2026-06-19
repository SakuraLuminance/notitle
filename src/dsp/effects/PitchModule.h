#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_data_structures/juce_data_structures.h>
#include "../PitchCorrector.h"
#include <vector>
#include <memory>

namespace ana {

enum class PitchMode { AutoTune, PitchShift, Harmonize, Formant };

class PitchModule {
public:
    PitchModule();
    ~PitchModule() = default;

    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::AudioBuffer<float>& buffer);
    void reset();

    // Mode
    void setMode(PitchMode mode);
    PitchMode getMode() const { return mode_; }

    // AutoTune
    void setRetuneSpeed(float ms);
    void setAmount(float v);
    void setScale(const std::vector<bool>& scale);

    // PitchShift
    void setSemitones(float st);
    void setCents(float c);
    void setWindow(int size);

    // Harmonize
    void setInterval(float st);
    void setHarmonyMix(float v);
    void setVoices(int count);

    // Formant
    void setFormantShift(float st);
    void setFormantPreserve(float v);

    // Shared
    void setMix(float v);
    float getMix() const { return mix_; }
    void setWetLowCut(float hz);
    void setWetHighCut(float hz);

    juce::ValueTree getState() const;
    void setState(const juce::ValueTree& state);

private:
    void processAutoTune(juce::AudioBuffer<float>& buffer);
    void processPitchShift(juce::AudioBuffer<float>& buffer);
    void processHarmonize(juce::AudioBuffer<float>& buffer);
    void processFormant(juce::AudioBuffer<float>& buffer);
    void applyWetBlend(juce::AudioBuffer<float>& buffer);
    void ensureHarmonyVoices(int count);
    float getNearestNote(float midiNote) const;

    PitchMode mode_ = PitchMode::AutoTune;

    // AutoTune
    float retuneSpeed_ = 50.0f;
    float amount_ = 1.0f;
    std::vector<bool> scale_;
    float currentShiftSemitones_ = 0.0f;

    // PitchShift
    float semitones_ = 0.0f;
    float cents_ = 0.0f;
    int window_ = 2048;

    // Harmonize
    float interval_ = 7.0f;
    float harmonyMix_ = 0.5f;
    int voices_ = 2;

    // Formant
    float formantShift_ = 0.0f;
    float formantPreserve_ = 1.0f;

    // Shared
    float mix_ = 1.0f;
    float wetLowCut_ = 20.0f;
    float wetHighCut_ = 20000.0f;

    // DSP
    double sampleRate_ = 44100.0;
    PitchCorrector pitchCorrector_;
    std::vector<std::unique_ptr<PitchCorrector>> harmonyVoices_;

    juce::AudioBuffer<float> dryBuffer_;
    juce::AudioBuffer<float> tempBuffer_;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                    juce::dsp::IIR::Coefficients<float>> wetHPF_;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                    juce::dsp::IIR::Coefficients<float>> wetLPF_;

    std::vector<float> monoBuffer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchModule)
};

} // namespace ana
