#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "../dsp/PartialData.h"

namespace ana {

enum class BrushMode
{
    Draw,
    Line,
    Rectangle,
    Eraser
};

class PartialEditorCanvas : public juce::Component
{
public:
    PartialEditorCanvas();
    ~PartialEditorCanvas() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& wheel) override;

    void setPartialData(const PartialData& data);
    PartialData getModifiedPartialData() const;

    void setBrushSize(int pixels);
    void setBrushMode(BrushMode mode);

    void undo();
    void redo();
    void clear();
    void normalize();
    void smooth();

private:
    // Canvas margin for axis labels
    static constexpr int marginLeft   = 45;
    static constexpr int marginBottom = 28;
    static constexpr int marginTop    = 8;
    static constexpr int marginRight  = 8;

    // Internal 2D amplitude grid
    std::vector<std::vector<float>> gridAmplitudes;
    std::vector<double> frameTimestamps;
    int numPartials = 0;
    int numFrames   = 0;

    // View transform
    float zoomX = 1.0f;
    float zoomY = 1.0f;
    float panX  = 0.0f;
    float panY  = 0.0f;
    bool  isPanning = false;
    juce::Point<int> lastMousePos;

    // Brush state
    BrushMode brushMode = BrushMode::Draw;
    int brushSize = 5;
    juce::Point<int> dragStart;
    juce::Point<int> dragCurrent;
    bool  isDragging = false;

    // Geometry helpers
    juce::Rectangle<int> getCanvasBounds() const;
    juce::Rectangle<int> getCanvasArea() const;
    float getCellWidth() const;
    float getCellHeight() const;
    juce::Point<int> pixelToGrid(juce::Point<float> pixel) const;
    juce::Point<float> gridToPixel(int frame, int partial) const;
    int getVisibleFrameStart() const;
    int getVisibleFrameEnd() const;
    int getVisiblePartialStart() const;
    int getVisiblePartialEnd() const;

    // Drawing operations
    void applyBrush(int frame, int partial, float value);
    void drawLinePoints(juce::Point<int> from, juce::Point<int> to, float value);
    void fillRectangle(juce::Point<int> from, juce::Point<int> to, float value);
    void commitStroke();

    // Undo / redo
    struct PreviousState
    {
        std::vector<std::vector<float>> amplitudes;
        std::vector<double> timestamps;
    };

    std::vector<PreviousState> undoStack;
    std::vector<PreviousState> redoStack;
    static constexpr int maxUndoLevels = 20;

    void pushUndoState();

    // Colour helpers
    static juce::Colour amplitudeToColour(float amplitude);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PartialEditorCanvas)
};

} // namespace ana
