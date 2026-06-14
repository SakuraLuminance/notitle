#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "../dsp/PartialDataSIMD.h"

namespace ana {

class WaterfallDisplay : public juce::Component,
                         public juce::Timer
{
public:
    WaterfallDisplay();
    ~WaterfallDisplay() override;

    void updateSpectrum(const std::vector<float>& magnitudes);
    void updatePartials(const PartialDataSIMD& partials);

    void setPitch(float pitch);       // -90 to 90
    void setYaw(float yaw);           // -180 to 180
    void setZoom(float zoom);         // 0.1 to 10.0
    void setDecay(int frames);        // history depth (32-512)
    void setColourScheme(int scheme); // 0=fire 1=rainbow 2=mono

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& w) override;

private:
    void paint(juce::Graphics& g) override;
    void timerCallback() override;
    void resized() override;

    void project3D(float x, float y, float z,
                   float& sx, float& sy) const;
    juce::Colour magnitudeColour(float mag, float maxMag) const;

    // Ring buffer of spectral history
    std::vector<std::vector<float>> history_;
    int historyPos_ = 0;
    int maxHistory_ = 256;
    int totalFramesWritten_ = 0;
    int numBins_ = 0;

    // 3D view parameters
    float pitch_ = 30.0f;
    float yaw_ = -45.0f;
    float zoom_ = 1.0f;
    int colourScheme_ = 0;

    // Current data (pushed to ring buffer by timer)
    std::vector<float> currentSpectrum_;
    mutable juce::CriticalSection lock_;
    bool hasData_ = false;

    // Mouse interaction state
    juce::Point<float> lastMousePos_;
    bool isDragging_ = false;

    static constexpr int kDefaultNumBins = 128;
    static constexpr int kMinHistory = 32;
    static constexpr int kMaxHistory = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaterfallDisplay)
};

} // namespace ana
