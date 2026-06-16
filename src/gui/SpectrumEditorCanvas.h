#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>
#include <vector>
#include <unordered_set>
#include <functional>
#include "../dsp/PartialDataSIMD.h"
#include "CyberpunkTheme.h"

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

    // =========================================================================
    // 3D Waterfall Mode
    // =========================================================================
    void set3DEnabled(bool enabled);
    bool is3DEnabled() const;

    // Camera controls
    void setCameraRotation(float yawDeg, float pitchDeg);
    void setCameraZoom(float zoom);
    void setCameraPanX(float x);
    void setCameraPanY(float y);
    float getYaw() const;
    float getPitch() const;
    float getZoom() const;

    // Partial selection and editing
    int getSelectedPartial() const;
    void setSelectedPartial(int index);
    void movePartial(int index, float newFreq, float newAmp);
    void batchMovePartials(const std::vector<int>& indices, float freqOffset, float ampOffset);
    void clearSelection();
    bool isPartialSelected(int index) const;
    std::vector<int> getSelectedPartials() const;

    // Box selection rectangle in screen space
    juce::Rectangle<float> getSelectionBox() const;

private:
    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    // Mouse handling
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& w) override;

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
    juce::Colour bgColour_    { CyberpunkTheme::bg_ };
    juce::Colour gridColour_  { CyberpunkTheme::bg_.brighter(0.1f) };
    juce::Colour barColour_   { CyberpunkTheme::cyan_ };
    juce::Colour toolColour_  { CyberpunkTheme::magenta_ };
    juce::Colour activeBar_   { CyberpunkTheme::yellow_ };

    // =========================================================================
    // 3D Waterfall OpenGL rendering
    // =========================================================================

    class Spectrum3DRenderer : public juce::OpenGLRenderer
    {
    public:
        explicit Spectrum3DRenderer(SpectrumEditorCanvas& owner) : owner_(owner) {}
        void newOpenGLContextCreated() override;
        void renderOpenGL() override;
        void openGLContextClosing() override;
    private:
        SpectrumEditorCanvas& owner_;
        JUCE_DECLARE_NON_COPYABLE(Spectrum3DRenderer)
    };
    friend class Spectrum3DRenderer;

    void render3DContent();
    void pushWaterfallFrame();
    int  hitTestPartial3D(juce::Point<float> mousePos);

    // Waterfall ring buffer — stores recent partial frames for the 3D depth view
    static constexpr int kWaterfallDepth = 128;
    static constexpr int k3DUpdateHz = 15;  // frame push rate

    struct WaterfallFrame
    {
        float frequency[PartialDataSIMD::kMaxPartials]{};
        float amplitude[PartialDataSIMD::kMaxPartials]{};
        int   activeCount = 0;
    };

    WaterfallFrame waterfallBuffer_[kWaterfallDepth];
    int waterfallWritePos_ = 0;
    int totalWaterfallFrames_ = 0;

    // 3D mode flag
    bool is3DEnabled_ = false;
    std::unique_ptr<juce::OpenGLContext> openGLContext_;
    std::unique_ptr<Spectrum3DRenderer> renderer3D_;

    // Camera state
    float yaw_   = -45.0f;  // degrees
    float pitch_ =  30.0f;  // degrees
    float zoom_  =   1.0f;  // multiplier
    float panX_  =   0.0f;
    float panY_  =   0.0f;

    // Selection state
    int selectedPartial_ = -1;
    std::unordered_set<int> multiSelection_;
    juce::Rectangle<float> selectionBox_;
    bool isBoxSelecting_ = false;

    // Mouse state for 3D interactions
    bool is3DDragging_ = false;
    bool isOrbiting_ = false;
    bool isPanning_ = false;
    bool isDraggingPartial_ = false;
    int  dragPartialIndex_ = -1;
    juce::Point<float> lastMousePos3D_;
    float dragStartFreq3D_ = 0.0f;
    float dragStartAmp3D_  = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumEditorCanvas)
};

} // namespace ana
