#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <deque>

// Forward declaration — include PluginProcessor.h in .cpp only
class AnaPlugAudioProcessor;

namespace ana {

//==============================================================================
/**
    A 2D XY pad for real-time parameter control with cyberpunk aesthetics.

    Features:
    - Draggable crosshair for 2D (X/Y) control in [0, 1] range
    - Smooth interpolation: ramps from current to target over configurable time
    - X axis → morphAmount (morph between A/B spectra)
    - Y axis → selectable target parameter (Cutoff, Resonance, Volume, LFORate, LFODepth)
    - MIDI Learn: right-click → context menu → map a MIDI CC to X or Y
    - Listener callback interface for value-change notification
    - Motion trail showing recent drag history
    - Colour-coded X (cyan) and Y (magenta) labels
*/
class XYPad : public juce::Component,
              public juce::Timer
{
public:
    //==============================================================================
    /** Y-axis target parameters. */
    enum class YTarget
    {
        Cutoff,
        Resonance,
        Volume,
        LFORate,
        LFODepth
    };

    //==============================================================================
    /** Listener interface for value changes. */
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void xyPadChanged(XYPad* pad, float x, float y) {}
    };

    //==============================================================================
    /** Construct an XY Pad that can access the processor for MIDI Learn. */
    explicit XYPad(AnaPlugAudioProcessor& processor);
    ~XYPad() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    //==============================================================================
    /** Bind an external atomic parameter for X axis.
        When bound, the XYPad writes the smoothly-interpolated X value to this
        atomic on every timer tick.
    */
    void setXParameter(std::atomic<float>* param, const juce::String& label);

    /** Bind an external atomic parameter for Y axis.
        When bound, the XYPad writes the smoothly-interpolated Y value to this
        atomic on every timer tick.
    */
    void setYParameter(std::atomic<float>* param, const juce::String& label);

    //==============================================================================
    /** Get current smoothed X value [0, 1]. */
    float getX() const { return x_.load(); }

    /** Get current smoothed Y value [0, 1]. */
    float getY() const { return y_.load(); }

    //==============================================================================
    /** Y-axis target parameter selection. */
    void setYTarget(YTarget target);
    YTarget getYTarget() const { return yTarget_; }

    /** Convert YTarget enum to human-readable string. */
    static juce::String yTargetToString(YTarget t);

    //==============================================================================
    /** Configure the smooth interpolation ramp time.
        @param ms  Ramp duration in milliseconds (default 30ms).
    */
    void setRampTimeMs(float ms) { rampTimeMs_ = juce::jmax(1.0f, ms); }
    float getRampTimeMs() const  { return rampTimeMs_; }

    //==============================================================================
    /** Listener management. */
    void addListener(Listener* l)      { listeners_.add(l); }
    void removeListener(Listener* l)   { listeners_.remove(l); }

    //==============================================================================
    /** Access the internal MIDI-learn atomics so the editor can register them
        with the processor's MidiLearn system.
    */
    std::atomic<float>& getXLearnAtomic() { return xLearnAtomic_; }
    std::atomic<float>& getYLearnAtomic() { return yLearnAtomic_; }

    /** Human-readable paramId strings for MIDI Learn persistence. */
    static juce::String getXParamId() { return "xy_x"; }
    static juce::String getYParamId() { return "xy_y"; }

private:
    AnaPlugAudioProcessor& processor_;

    //==============================================================================
    // Smooth interpolation state
    std::atomic<float> x_{0.5f};        // current smoothed output
    std::atomic<float> y_{0.5f};        // current smoothed output
    float targetX_ = 0.5f;              // desired X position
    float targetY_ = 0.5f;              // desired Y position
    float rampTimeMs_ = 30.0f;          // ramp duration (ms)

    //==============================================================================
    // External parameter binding (XYPad writes to these on timer tick)
    std::atomic<float>* xParam_ = nullptr;
    std::atomic<float>* yParam_ = nullptr;
    juce::String xLabel_{"MORPH"};
    juce::String yLabel_{"Y"};

    //==============================================================================
    // MIDI Learn atomics (processor's MidiLearn writes to these)
    std::atomic<float> xLearnAtomic_{0.5f};
    std::atomic<float> yLearnAtomic_{0.5f};
    float lastXLearn_ = 0.5f;   // previous frame MIDI value for change detection
    float lastYLearn_ = 0.5f;

    //==============================================================================
    // Y-axis target selection
    YTarget yTarget_ = YTarget::Cutoff;

    //==============================================================================
    // Listener notification
    juce::ListenerList<Listener> listeners_;

    //==============================================================================
    // Drag state
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

    /** Convert mouse position to normalized [0, 1] coordinates (with shrink). */
    juce::Point<float> mouseToNormalized(juce::Point<float> mousePos) const;

    /** Set target position from normalized point, clamped to [0, 1]. */
    void setTargetPosition(juce::Point<float> p);

    /** Show right-click context menu with MIDI Learn + Y Target options. */
    void showContextMenu(const juce::MouseEvent& e);

    /** Issue a MIDI Learn on the X axis. */
    void startLearnX();
    /** Issue a MIDI Learn on the Y axis. */
    void startLearnY();

    //==============================================================================
    // Drawing helpers
    static void drawGrid(juce::Graphics& g, juce::Rectangle<float> bounds);
    static void drawCrosshair(juce::Graphics& g, juce::Rectangle<float> bounds,
                               juce::Point<float> pos, juce::Colour colour);
    static void drawTrail(juce::Graphics& g, juce::Rectangle<float> bounds,
                           const std::deque<TrailPoint>& trail, juce::Colour colour);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XYPad)
};

} // namespace ana
