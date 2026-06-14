#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <deque>

namespace ana {

//==============================================================================
/**
    A 2D XY pad for real-time parameter control with cyberpunk aesthetics.
    
    Features:
    - Draggable crosshair for 2D (X/Y) control
    - Optional atomic parameter binding for bidirectional sync
    - Grid background with neon crosshair and motion trail
    - Colour-coded X (cyan) and Y (magenta) labels
*/
class XYPad : public juce::Component,
              public juce::Timer
{
public:
    XYPad();
    ~XYPad() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    //==============================================================================
    /** Bind an external atomic parameter for X axis.
        When bound, the XYPad reads from this atomic on timer ticks and writes
        to it on mouse drag.
    */
    void setXParameter(std::atomic<float>* param, const juce::String& label);

    /** Bind an external atomic parameter for Y axis.
        When bound, the XYPad reads from this atomic on timer ticks and writes
        to it on mouse drag.
    */
    void setYParameter(std::atomic<float>* param, const juce::String& label);

    //==============================================================================
    /** Get current normalized X value (0..1). */
    float getX() const { return x_.load(); }

    /** Get current normalized Y value (0..1). */
    float getY() const { return y_.load(); }

private:
    std::atomic<float> x_{0.5f};
    std::atomic<float> y_{0.5f};
    std::atomic<float>* xParam_ = nullptr;
    std::atomic<float>* yParam_ = nullptr;
    juce::String xLabel_{"X"};
    juce::String yLabel_{"Y"};

    bool isDragging_ = false;

    //==============================================================================
    // Motion trail — stores recent positions for fade-out trail effect
    struct TrailPoint {
        float x;
        float y;
        float age;  // 0..1, 1 = fully faded
    };
    std::deque<TrailPoint> trail_;
    static constexpr int maxTrailLength_ = 32;

    //==============================================================================
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    /** Convert mouse position within bounds to normalized (0..1) point. */
    juce::Point<float> getPosition() const;

    /** Set position from normalized point, clamped to [0,1]. */
    void setPosition(juce::Point<float> p);

    //==============================================================================
    /** Draw the grid background lines. */
    static void drawGrid(juce::Graphics& g, juce::Rectangle<float> bounds);
    /** Draw the crosshair at a normalized position. */
    static void drawCrosshair(juce::Graphics& g, juce::Rectangle<float> bounds,
                               juce::Point<float> pos, juce::Colour colour);
    /** Draw the motion trail as fading circles. */
    static void drawTrail(juce::Graphics& g, juce::Rectangle<float> bounds,
                           const std::deque<TrailPoint>& trail, juce::Colour colour);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XYPad)
};

} // namespace ana
