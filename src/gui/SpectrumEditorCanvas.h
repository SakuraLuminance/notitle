#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <functional>
#include "../dsp/PartialDataSIMD.h"

namespace ana {

class SpectrumEditorCanvas : public juce::Component, public juce::Timer
{
public:
    enum class Tool
    {
        Draw,
        Line,
        Rectangle,
        Eraser,
        Smooth,
        Sharpen
    };

    SpectrumEditorCanvas();
    ~SpectrumEditorCanvas() override;

    // Data
    void setPartials(const PartialDataSIMD& partials);
    PartialDataSIMD getEditedPartials() const;

    // Tool
    void setActiveTool(Tool tool);
    Tool getActiveTool() const;
    void setBrushSize(int pixels);
    void setBrushStrength(float strength);

    // Undo / Redo
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    void clearHistory();

    // Display
    void setShowGrid(bool show);
    void setShowLabels(bool show);
    void setLogFreq(bool useLog);
    void setAmplitudeScale(float scale);
    void setGridLinesX(int count);
    void setGridLinesY(int count);

    // Colours
    void setBackgroundColour(juce::Colour c);
    void setGridColour(juce::Colour c);
    void setBarColour(juce::Colour c);

    // Events
    std::function<void(const PartialDataSIMD&)> onPartialEdited;

private:
    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    // Mouse handling
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Canvas geometry
    static constexpr int marginLeft   = 50;
    static constexpr int marginBottom = 28;
    static constexpr int marginTop    = 8;
    static constexpr int marginRight  = 8;

    juce::Rectangle<int> getCanvasArea() const;

    // Coordinate mapping
    int   freqToX(float freqHz) const;
    float xToFreq(int x) const;
    int   ampToY(float amplitude) const;
    float yToAmp(int y) const;

    // Tool application
    void applyToolAt(int x, int y);
    void applyDraw(float freq, float amp, float strength);
    void applyEraser(float freq, float amp, float strength);
    void applySmooth(int cx, int cy);
    void applySharpen(int cx, int cy);

    // Line / Rectangle helpers
    void drawLineTo(float freqFrom, float ampFrom, float freqTo, float ampTo);
    void fillRectTo(float freqFrom, float ampFrom, float freqTo, float ampTo);

    // Interpolation
    float brushFalloff(float dx, float dy, float radius) const;

    // Undo
    void saveState();

    // Data
    PartialDataSIMD partials_;
    mutable juce::CriticalSection dataLock_;

    // Undo / Redo
    std::vector<PartialDataSIMD> undoStack_;
    std::vector<PartialDataSIMD> redoStack_;
    static constexpr int kMaxUndoSteps = 64;

    // Tool state
    Tool activeTool_ = Tool::Draw;
    int brushSize_ = 5;
    float brushStrength_ = 0.5f;
    bool isDragging_ = false;
    bool hasPendingStroke_ = false;

    // Line / Rectangle drag start (in freq/amp space)
    float dragStartFreq_ = 0.0f;
    float dragStartAmp_  = 0.0f;
    float dragEndFreq_   = 0.0f;
    float dragEndAmp_    = 0.0f;

    // Display
    bool showGrid_ = true;
    bool showLabels_ = true;
    bool logFreq_ = true;
    float ampScale_ = 1.0f;
    int gridLinesX_ = 10;
    int gridLinesY_ = 8;

    // Colours
    juce::Colour bgColour_    { 0x12, 0x12, 0x26 };  // very dark blue
    juce::Colour gridColour_  { 0x30, 0x30, 0x50 };  // dim blue
    juce::Colour barColour_   { 0xEE, 0xCC, 0x66 };  // gold
    juce::Colour toolColour_  { 0x66, 0xCC, 0xFF };  // light blue
    juce::Colour activeBar_   { 0xFF, 0xCC, 0x44 };  // brighter gold for active

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumEditorCanvas)
};

} // namespace ana
