#pragma once
#include <vector>
#include <atomic>
#include <juce_core/juce_core.h>
#include "PartialDataSIMD.h"

namespace ana {

class WavetableEngine {
public:
    WavetableEngine() = default;
    ~WavetableEngine() = default;

    // Load a wav file and build wavetable frames
    bool loadWavetable(const juce::File& file);

    // Load raw audio samples and build wavetable frames
    bool loadWavetable(const std::vector<float>& audio, double sampleRate);

    // Load pre-computed partial frames directly
    bool loadFromPartials(const std::vector<PartialDataSIMD>& frames);

    // Wavetable position 0.0-1.0 (interpolates between frames)
    void setPosition(float pos);
    float getPosition() const;

    // Get the interpolated partial frame at current position (out-param avoids return-by-value of large SIMD struct)
    void getCurrentFrame(PartialDataSIMD& out) const;

    // Get raw frame at index (no interpolation)
    PartialDataSIMD getFrame(int index) const;

    int getNumFrames() const;
    bool isLoaded() const;
    void clear();

private:
    std::vector<PartialDataSIMD> frames_;
    std::atomic<float> position_{0.0f};
    bool loaded_ = false;
};

} // namespace ana
