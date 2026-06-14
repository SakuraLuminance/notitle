#pragma once
#include <vector>

namespace ana {

//==============================================================================
/** 
    Represents a single sliced segment of an audio buffer.
*/
struct SampleSlice {
    int startSample = 0;
    int endSample = 0;
    float peakAmplitude = 0.0f;
    float energy = 0.0f;
};

//==============================================================================
/**
    SampleSlicer detects transients in an audio buffer and splits it into
    multiple slice regions, commonly used for drum loop slicing and mapping
    slices to individual MIDI keys.
*/
class SampleSlicer {
public:
    SampleSlicer() = default;
    ~SampleSlicer() = default;

    void setSampleRate(double sr);
    
    /**
        Detect slices based on transient detection.
        @param audioData    The mono audio buffer to slice.
        @param sensitivity  0.0 to 1.0 (higher = more slices, more sensitive to soft transients).
        @return             A vector of SampleSlice descriptors.
    */
    std::vector<SampleSlice> slice(const std::vector<float>& audioData, float sensitivity = 0.5f);

private:
    double sampleRate_ = 44100.0;
};

} // namespace ana
