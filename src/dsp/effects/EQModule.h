#pragma once
#include "EQCommon.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>

namespace ana {

enum class EQMode { Band3, Band5, Tilt, Para };

struct EQBandParams {
    float frequency = 1000.0f;
    float gain = 0.0f;
    float q = 0.707f;
    EQBandType type = EQBandType::Peaking;
};

class EQModule {
public:
    EQModule();
    ~EQModule() = default;

    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::AudioBuffer<float>& buffer);
    void reset();

    // Mode
    void setMode(EQMode mode);
    EQMode getMode() const { return mode_; }

    // 3-Band
    void setLowGain(float db);
    void setMidGain(float db);
    void setHighGain(float db);
    void setMidFreq(float hz);

    // 5-Band
    void setSubGain(float db);
    void setSubFreq(float hz);
    void setLowGain5(float db);
    void setLowFreq5(float hz);
    void setMidGain5(float db);
    void setMidFreq5(float hz);
    void setHighGain5(float db);
    void setHighFreq5(float hz);
    void setAirGain(float db);
    void setAirFreq(float hz);

    // Tilt
    void setTiltAmount(float db);
    void setCenterFreq(float hz);

    // Para - 5 bands fully parametric
    void setParaBand(int index, float freq, float gain, float q, EQBandType type);

    // Shared
    void setMix(float v);
    float getMix() const { return mix_; }
    void setWetLowCut(float hz);
    void setWetHighCut(float hz);

    juce::ValueTree getState() const;
    void setState(const juce::ValueTree& state);

private:
    void configureFilters();

    EQMode mode_ = EQMode::Band3;

    // 3-Band (LowShelf@200 / Peaking@midFreq / HighShelf@5kHz)
    float lowGain_ = 0.0f;
    float midGain_ = 0.0f;
    float highGain_ = 0.0f;
    float midFreq_ = 1000.0f;

    // 5-Band
    float subGain_ = 0.0f;    float subFreq_ = 60.0f;
    float lowGain5_ = 0.0f;   float lowFreq5_ = 250.0f;
    float midGain5_ = 0.0f;   float midFreq5_ = 1000.0f;
    float highGain5_ = 0.0f;  float highFreq5_ = 5000.0f;
    float airGain_ = 0.0f;    float airFreq_ = 12000.0f;

    // Tilt
    float tiltAmount_ = 0.0f;
    float centerFreq_ = 1000.0f;

    // Para - 5 fully parametric bands
    std::array<EQBandParams, 5> paraBands_;

    // Shared
    float mix_ = 1.0f;
    float wetLowCut_ = 20.0f;
    float wetHighCut_ = 20000.0f;

    // DSP
    static constexpr int kMaxFilters = 5;
    std::array<juce::dsp::IIR::Filter<float>, kMaxFilters> filters_;
    int activeFilters_ = 3;
    double sampleRate_ = 44100.0;

    juce::AudioBuffer<float> dryBuffer_;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                    juce::dsp::IIR::Coefficients<float>> wetHPF_;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                    juce::dsp::IIR::Coefficients<float>> wetLPF_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQModule)
};

} // namespace ana
