#pragma once
#include <vector>
#include <juce_dsp/juce_dsp.h>

namespace ana {

//==============================================================================
/**
    Harmonic-Percussive Source Separation (HPSS).
    
    Implements a lightweight median-filtering-based HPSS algorithm.
    It takes a mono audio buffer, computes the STFT, applies median filters
    across the time axis (for harmonics) and frequency axis (for percussives),
    creates a Wiener-like mask, and resynthesizes the separated signals.
*/
class HPSSEngine
{
public:
    HPSSEngine();
    ~HPSSEngine() = default;

    struct SeparationResult
    {
        std::vector<float> harmonic;
        std::vector<float> percussive;
        double sampleRate = 44100.0;
    };

    /**
        Separate the input audio into harmonic and percussive components.
        
        @param inputAudio Mono input audio buffer
        @param sampleRate Sample rate
        @param filterLengthTime Length of median filter in time frames (e.g., 17)
        @param filterLengthFreq Length of median filter in frequency bins (e.g., 17)
        @return SeparationResult containing the two separated audio buffers
    */
    SeparationResult separate(const std::vector<float>& inputAudio, 
                              double sampleRate,
                              int filterLengthTime = 17, 
                              int filterLengthFreq = 17);

private:
    /** 1D Median filter helper */
    void medianFilter1D(const float* input, float* output, int length, int filterSize, int stride = 1);
};

} // namespace ana
