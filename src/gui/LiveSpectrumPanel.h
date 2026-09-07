#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

namespace ana
{

class LiveSpectrumPanel : public juce::Component
{
public:
    LiveSpectrumPanel();

    void paint(juce::Graphics&) override;
    void resized() override;

    void updateFromProcessor(AnaPlugAudioProcessor& processor);
    void updateFromSamples(const float* data, int numSamples);

private:
    void rebuildBarMap();
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;          // 2048
    static constexpr int numBins = fftSize / 2 + 1;        // 1025
    static constexpr float kMinDb = -72.0f;
    static constexpr float kMaxDb = 0.0f;
    static constexpr float kPeakDecay = 0.95f;             // per tick @30 Hz (~1 s)

    juce::dsp::FFT fftEngine_{ fftOrder };
    std::array<float, fftSize> hannWindow_{};
    std::vector<float> fftBuffer_;                          // 2*fftSize interleaved
    std::vector<float> magnitudes_;                         // numBins linear magnitudes
    std::vector<float> barLevels_;                          // per-bar normalised 0..1
    std::vector<float> peakLevels_;                         // per-bar peak hold
    std::vector<int>   barBinStart_;                        // per-bar first bin
    std::vector<int>   barBinEnd_;                          // per-bar last bin (exclusive)
    std::array<float, 3> freqTickHz_{ { 100.0f, 1000.0f, 10000.0f } };
};

} // namespace ana
