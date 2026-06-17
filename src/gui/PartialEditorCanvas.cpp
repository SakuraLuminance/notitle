#include "PartialEditorCanvas.h"
#include "CyberpunkTheme.h"
#include <cmath>
#include <algorithm>

namespace ana {

// ============================================================================
// Colour mapping:  amplitude -> colour (Toxic Green)
//    0.0  = black
//    0.33 = dark green
//    0.66 = neon green
//    1.0  = white
// ============================================================================
juce::Colour PartialEditorCanvas::amplitudeToColour(float amplitude)
{
    amplitude = std::clamp(amplitude, 0.0f, 1.0f);

    if (amplitude < 0.33f)
    {
        // black -> dark green
        float t = amplitude / 0.33f;
        return juce::Colour::fromHSV(0.33f, 1.0f, t * 0.5f, 1.0f);
    }

    if (amplitude < 0.66f)
    {
        // dark green -> neon green
        float t = (amplitude - 0.33f) / 0.33f;
        return juce::Colour::fromHSV(0.33f + t * 0.05f, 1.0f, 0.5f + t * 0.5f, 1.0f);
    }

    // neon green -> white
    float t = (amplitude - 0.66f) / 0.34f;
    return juce::Colour::fromHSV(0.38f, 1.0f - t, 1.0f, 1.0f);
}

// ============================================================================
// Construction
// ============================================================================
PartialEditorCanvas::PartialEditorCanvas()
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

PartialEditorCanvas::~PartialEditorCanvas() = default;

// ============================================================================
// Geometry helpers
// ============================================================================
juce::Rectangle<int> PartialEditorCanvas::getCanvasBounds() const
{
    return getLocalBounds().reduced(0);
}

juce::Rectangle<int> PartialEditorCanvas::getCanvasArea() const
{
    auto bounds = getCanvasBounds();
    return {
        bounds.getX()      + marginLeft,
        bounds.getY()      + marginTop,
        bounds.getWidth()  - marginLeft - marginRight,
        bounds.getHeight() - marginTop  - marginBottom
    };
}

float PartialEditorCanvas::getCellWidth() const
{
    if (numFrames == 0) return 1.0f;
    return static_cast<float>(getCanvasArea().getWidth()) / static_cast<float>(numFrames) * zoomX;
}

float PartialEditorCanvas::getCellHeight() const
{
    if (numPartials == 0) return 1.0f;
    return static_cast<float>(getCanvasArea().getHeight()) / static_cast<float>(numPartials) * zoomY;
}

juce::Point<int> PartialEditorCanvas::pixelToGrid(juce::Point<float> pixel) const
{
    auto area = getCanvasArea().toFloat();
    float cellW = getCellWidth();
    float cellH = getCellHeight();

    if (cellW <= 0.0f || cellH <= 0.0f)
        return { 0, 0 };

    float relX = (pixel.x - area.getX() - panX) / cellW;
    float relY = (pixel.y - area.getY() - panY) / cellH;

    return {
        static_cast<int>(std::floor(relX)),
        static_cast<int>(std::floor(relY))
    };
}

juce::Point<float> PartialEditorCanvas::gridToPixel(int frame, int partial) const
{
    auto area = getCanvasArea().toFloat();
    return {
        area.getX() + static_cast<float>(frame)   * getCellWidth()  + panX,
        area.getY() + static_cast<float>(partial) * getCellHeight() + panY
    };
}

int PartialEditorCanvas::getVisibleFrameStart() const
{
    auto area = getCanvasArea().toFloat();
    float cellW = getCellWidth();
    if (cellW <= 0.0f) return 0;
    return std::max(0, static_cast<int>(std::floor(-panX / cellW)));
}

int PartialEditorCanvas::getVisibleFrameEnd() const
{
    auto area = getCanvasArea().toFloat();
    float cellW = getCellWidth();
    if (cellW <= 0.0f) return 0;
    return std::min(numFrames,
        static_cast<int>(std::ceil((-panX + area.getWidth()) / cellW)) + 1);
}

int PartialEditorCanvas::getVisiblePartialStart() const
{
    auto area = getCanvasArea().toFloat();
    float cellH = getCellHeight();
    if (cellH <= 0.0f) return 0;
    return std::max(0, static_cast<int>(std::floor(-panY / cellH)));
}

int PartialEditorCanvas::getVisiblePartialEnd() const
{
    auto area = getCanvasArea().toFloat();
    float cellH = getCellHeight();
    if (cellH <= 0.0f) return 0;
    return std::min(numPartials,
        static_cast<int>(std::ceil((-panY + area.getHeight()) / cellH)) + 1);
}

// ============================================================================
// Data I/O
// ============================================================================
void PartialEditorCanvas::setPartialData(const PartialData& data)
{
    undoStack.clear();
    redoStack.clear();

    numFrames = static_cast<int>(data.frames.size());
    numPartials = std::max(1, data.maxPartials);

    gridAmplitudes.clear();
    frameTimestamps.clear();

    for (int f = 0; f < numFrames; ++f)
    {
        frameTimestamps.push_back(data.frames[static_cast<size_t>(f)].timestamp);

        std::vector<float> row(static_cast<size_t>(numPartials), 0.0f);
        const auto& partials = data.frames[static_cast<size_t>(f)].partials;

        for (size_t p = 0; p < partials.size() && p < static_cast<size_t>(numPartials); ++p)
            row[p] = std::clamp(partials[p].amplitude, 0.0f, 1.0f);

        gridAmplitudes.push_back(std::move(row));
    }

    // Reset view
    zoomX = 1.0f;
    zoomY = 1.0f;
    panX  = 0.0f;
    panY  = 0.0f;

    repaint();
}

PartialData PartialEditorCanvas::getModifiedPartialData() const
{
    PartialData data;
    data.sampleRate   = 44100.0;
    data.hopSize      = 512.0;
    data.maxPartials  = numPartials;

    for (int f = 0; f < numFrames; ++f)
    {
        PartialFrame frame;
        frame.timestamp = (f < static_cast<int>(frameTimestamps.size()))
                              ? frameTimestamps[static_cast<size_t>(f)]
                              : 0.0;

        for (int p = 0; p < numPartials; ++p)
        {
            float freq = (static_cast<float>(p) + 0.5f)
                         / static_cast<float>(numPartials)
                         * (static_cast<float>(data.sampleRate) * 0.5f);
            float amp = (f < static_cast<int>(gridAmplitudes.size())
                         && p < static_cast<int>(gridAmplitudes[static_cast<size_t>(f)].size()))
                            ? gridAmplitudes[static_cast<size_t>(f)][static_cast<size_t>(p)]
                            : 0.0f;
            frame.partials.push_back({ freq, amp, 0.0f });
        }

        data.frames.push_back(std::move(frame));
    }

    return data;
}

// ============================================================================
// Brush settings
// ============================================================================
void PartialEditorCanvas::setBrushSize(int pixels)
{
    brushSize = std::clamp(pixels, 1, 50);
}

void PartialEditorCanvas::setBrushMode(BrushMode mode)
{
    brushMode = mode;
}

// ============================================================================
// Undo / Redo
// ============================================================================
void PartialEditorCanvas::pushUndoState()
{
    PreviousState state;
    state.amplitudes = gridAmplitudes;
    state.timestamps = frameTimestamps;

    undoStack.push_back(std::move(state));

    if (static_cast<int>(undoStack.size()) > maxUndoLevels)
        undoStack.erase(undoStack.begin());

    redoStack.clear();
}

void PartialEditorCanvas::undo()
{
    if (undoStack.empty())
        return;

    // Save current state so we can redo back to it
    PreviousState current;
    current.amplitudes = gridAmplitudes;
    current.timestamps = frameTimestamps;
    redoStack.push_back(std::move(current));

    // Restore previous state
    gridAmplitudes = std::move(undoStack.back().amplitudes);
    frameTimestamps = std::move(undoStack.back().timestamps);
    undoStack.pop_back();

    // Recompute dimensions
    numFrames   = static_cast<int>(gridAmplitudes.size());
    numPartials = numFrames > 0
                      ? static_cast<int>(gridAmplitudes[0].size())
                      : 0;

    repaint();
}

void PartialEditorCanvas::redo()
{
    if (redoStack.empty())
        return;

    // Save current state
    PreviousState current;
    current.amplitudes = gridAmplitudes;
    current.timestamps = frameTimestamps;
    undoStack.push_back(std::move(current));

    // Restore next state
    gridAmplitudes = std::move(redoStack.back().amplitudes);
    frameTimestamps = std::move(redoStack.back().timestamps);
    redoStack.pop_back();

    numFrames   = static_cast<int>(gridAmplitudes.size());
    numPartials = numFrames > 0
                      ? static_cast<int>(gridAmplitudes[0].size())
                      : 0;

    repaint();
}

// ============================================================================
// Clear / Normalize / Smooth
// ============================================================================
void PartialEditorCanvas::clear()
{
    pushUndoState();

    for (auto& row : gridAmplitudes)
        std::fill(row.begin(), row.end(), 0.0f);

    repaint();
}

void PartialEditorCanvas::normalize()
{
    pushUndoState();

    float maxAmp = 0.0f;
    for (const auto& row : gridAmplitudes)
        for (float amp : row)
            maxAmp = std::max(maxAmp, amp);

    if (maxAmp > 0.0f && maxAmp < 1.0f)
    {
        for (auto& row : gridAmplitudes)
            for (float& amp : row)
                amp /= maxAmp;
    }

    repaint();
}

void PartialEditorCanvas::smooth()
{
    if (numFrames < 3 || numPartials < 3)
        return;

    pushUndoState();

    // 3x3 box blur, leave border cells untouched
    auto smoothed = gridAmplitudes;

    for (int f = 1; f < numFrames - 1; ++f)
    {
        for (int p = 1; p < numPartials - 1; ++p)
        {
            float sum = 0.0f;
            for (int df = -1; df <= 1; ++df)
                for (int dp = -1; dp <= 1; ++dp)
                    sum += gridAmplitudes[static_cast<size_t>(f + df)]
                                         [static_cast<size_t>(p + dp)];
            smoothed[static_cast<size_t>(f)][static_cast<size_t>(p)] = sum / 9.0f;
        }
    }

    gridAmplitudes = std::move(smoothed);
    repaint();
}

// ============================================================================
// Brush drawing primitives
// ============================================================================
void PartialEditorCanvas::applyBrush(int frame, int partial, float value)
{
    int radius = brushSize / 2;

    for (int df = -radius; df <= radius; ++df)
    {
        for (int dp = -radius; dp <= radius; ++dp)
        {
            int f = frame + df;
            int p = partial + dp;

            if (f < 0 || f >= numFrames || p < 0 || p >= numPartials)
                continue;

            // Circular brush shape
            float distSq = static_cast<float>(df * df + dp * dp);
            float radSq  = static_cast<float>(radius * radius);
            if (radius > 0 && distSq > radSq)
                continue;

            gridAmplitudes[static_cast<size_t>(f)][static_cast<size_t>(p)]
                = std::clamp(value, 0.0f, 1.0f);
        }
    }
}

void PartialEditorCanvas::drawLinePoints(juce::Point<int> from,
                                         juce::Point<int> to,
                                         float value)
{
    // Bresenham's line algorithm
    int x1 = from.x, y1 = from.y;
    int x2 = to.x,   y2 = to.y;

    int dx = std::abs(x2 - x1);
    int dy = -std::abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx + dy;

    while (true)
    {
        applyBrush(x1, y1, value);

        if (x1 == x2 && y1 == y2)
            break;

        int e2 = 2 * err;

        if (e2 >= dy)
        {
            err += dy;
            x1 += sx;
        }

        if (e2 <= dx)
        {
            err += dx;
            y1 += sy;
        }
    }
}

void PartialEditorCanvas::fillRectangle(juce::Point<int> from,
                                        juce::Point<int> to,
                                        float value)
{
    int minF = std::clamp(std::min(from.x, to.x), 0, numFrames - 1);
    int maxF = std::clamp(std::max(from.x, to.x), 0, numFrames - 1);
    int minP = std::clamp(std::min(from.y, to.y), 0, numPartials - 1);
    int maxP = std::clamp(std::max(from.y, to.y), 0, numPartials - 1);

    for (int f = minF; f <= maxF; ++f)
        for (int p = minP; p <= maxP; ++p)
            gridAmplitudes[static_cast<size_t>(f)][static_cast<size_t>(p)]
                = std::clamp(value, 0.0f, 1.0f);
}

void PartialEditorCanvas::commitStroke()
{
    // Nothing extra needed; the undo state was pushed at mouseDown
    isDragging = false;
}

// ============================================================================
// Mouse handling
// ============================================================================
void PartialEditorCanvas::mouseDown(const juce::MouseEvent& e)
{
    if (numFrames == 0 || numPartials == 0)
        return;

    if (e.mods.isRightButtonDown())
    {
        // Right-click drag -> pan
        isPanning = true;
        lastMousePos = e.getPosition();
        return;
    }

    // Left-click -> draw
    pushUndoState();

    isDragging = true;
    dragStart  = pixelToGrid(e.position);
    dragCurrent = dragStart;

    if (brushMode == BrushMode::Draw || brushMode == BrushMode::Eraser)
    {
        float value = (brushMode == BrushMode::Eraser) ? 0.0f : 1.0f;
        applyBrush(dragStart.x, dragStart.y, value);
        lastMousePos = e.getPosition();
        repaint();
    }
}

void PartialEditorCanvas::mouseDrag(const juce::MouseEvent& e)
{
    if (isPanning)
    {
        auto delta = e.getPosition() - lastMousePos;
        panX += static_cast<float>(delta.x);
        panY += static_cast<float>(delta.y);
        lastMousePos = e.getPosition();
        repaint();
        return;
    }

    if (!isDragging)
        return;

    auto gridPos = pixelToGrid(e.position);

    if (brushMode == BrushMode::Draw || brushMode == BrushMode::Eraser)
    {
        float value = (brushMode == BrushMode::Eraser) ? 0.0f : 1.0f;

        // Interpolate between last position and current to avoid gaps
        drawLinePoints(dragCurrent, gridPos, value);
        dragCurrent = gridPos;

        // Also stamp at current position to cover brush radius well
        applyBrush(gridPos.x, gridPos.y, value);
    }
    else
    {
        // Line / Rectangle modes: just update preview position
        dragCurrent = gridPos;
    }

    lastMousePos = e.getPosition();
    repaint();
}

void PartialEditorCanvas::mouseUp(const juce::MouseEvent& e)
{
    if (isPanning)
    {
        isPanning = false;
        return;
    }

    if (!isDragging)
        return;

    auto gridEnd = pixelToGrid(e.position);

    if (brushMode == BrushMode::Line)
    {
        float value = 1.0f;
        drawLinePoints(dragStart, gridEnd, value);
    }
    else if (brushMode == BrushMode::Rectangle)
    {
        float value = 1.0f;
        fillRectangle(dragStart, gridEnd, value);
    }

    commitStroke();
    repaint();
}

void PartialEditorCanvas::mouseWheelMove(const juce::MouseEvent& e,
                                         const juce::MouseWheelDetails& wheel)
{
    if (numFrames == 0 || numPartials == 0)
        return;

    float zoomFactor = 1.0f + (wheel.deltaY > 0.0f ? 0.15f : -0.15f);
    if (wheel.deltaY == 0.0f)
        return;

    // Pinch-zoom delta for trackpads
    zoomFactor = 1.0f + wheel.deltaY * 0.5f;

    float newZoomX = std::clamp(zoomX * zoomFactor, 0.1f, 20.0f);
    float newZoomY = std::clamp(zoomY * zoomFactor, 0.1f, 20.0f);

    // Zoom toward mouse position
    auto area = getCanvasArea().toFloat();
    float mouseRelX = e.position.x - area.getX();
    float mouseRelY = e.position.y - area.getY();

    // Adjust pan so the cell under the cursor stays fixed
    float oldCellW = getCellWidth();
    float oldCellH = getCellHeight();
    zoomX = newZoomX;
    zoomY = newZoomY;
    float newCellW = getCellWidth();
    float newCellH = getCellHeight();

    panX -= mouseRelX * (1.0f - oldCellW / newCellW);
    panY -= mouseRelY * (1.0f - oldCellH / newCellH);

    repaint();
}

// ============================================================================
// Paint
// ============================================================================
void PartialEditorCanvas::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.fillAll(CyberpunkTheme::bg_);

    if (numFrames == 0 || numPartials == 0)
    {
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.5f));
        g.setFont(14.0f);
        g.drawText("No partial data loaded", bounds,
                   juce::Justification::centred);
        return;
    }

    auto area = getCanvasArea().toFloat();
    float cellW = getCellWidth();
    float cellH = getCellHeight();

    // --- Draw spectral cells (only visible ones) ---
    int vfStart = getVisibleFrameStart();
    int vfEnd   = getVisibleFrameEnd();
    int vpStart = getVisiblePartialStart();
    int vpEnd   = getVisiblePartialEnd();

    for (int f = vfStart; f < vfEnd; ++f)
    {
        for (int p = vpStart; p < vpEnd; ++p)
        {
            float amp = gridAmplitudes[static_cast<size_t>(f)]
                                       [static_cast<size_t>(p)];
            if (amp <= 0.0f)
                continue;

            auto pos = gridToPixel(f, p);
            g.setColour(amplitudeToColour(amp));
            g.fillRect(pos.x, pos.y, cellW + 0.5f, cellH + 0.5f);
        }
    }

    // --- Grid overlay ---
    g.setColour(CyberpunkTheme::fg_.withAlpha(0.15f));

    // Vertical grid lines (every 10 frames, or fewer if zoomed out)
    int frameStep = std::max(1, numFrames / 40);
    for (int f = vfStart; f <= vfEnd; f += frameStep)
    {
        auto pos = gridToPixel(f, 0);
        float x = pos.x;
        if (x >= area.getX() && x <= area.getRight())
            g.drawLine(x, area.getY(), x, area.getBottom(), 0.5f);
    }

    // Horizontal grid lines (every 10 partials, or fewer if zoomed out)
    int partialStep = std::max(1, numPartials / 20);
    for (int p = vpStart; p <= vpEnd; p += partialStep)
    {
        auto pos = gridToPixel(0, p);
        float y = pos.y;
        if (y >= area.getY() && y <= area.getBottom())
            g.drawLine(area.getX(), y, area.getRight(), y, 0.5f);
    }

    // --- Canvas border ---
    g.setColour(CyberpunkTheme::bg_.brighter(0.15f));
    g.drawRect(area.toNearestInt(), 1);

    // --- Axis labels ---
    g.setFont(juce::Font(10.0f, juce::Font::plain));

    // Frequency labels (Y axis, left margin)
    g.setColour(CyberpunkTheme::fg_.withAlpha(0.6f));
    for (int p = vpStart; p < vpEnd; p += partialStep)
    {
        auto pos = gridToPixel(0, p);
        float freq = (static_cast<float>(p) / static_cast<float>(numPartials)) * 22050.0f;
        juce::String label;

        if (freq >= 1000.0f)
            label = juce::String(freq / 1000.0f, 1) + "k";
        else
            label = juce::String(static_cast<int>(freq));

        if (pos.y >= area.getY() && pos.y <= area.getBottom())
        {
            g.drawText(label,
                       bounds.getX() + 2,
                       static_cast<int>(pos.y) - 6,
                       marginLeft - 5, 12,
                       juce::Justification::centredRight);
        }
    }

    // Time labels (X axis, bottom margin)
    for (int f = vfStart; f < vfEnd; f += frameStep)
    {
        auto pos = gridToPixel(f, 0);
        double t = (f < static_cast<int>(frameTimestamps.size()))
                       ? frameTimestamps[static_cast<size_t>(f)]
                       : static_cast<double>(f) / 100.0;
        juce::String label = juce::String(t, 2) + "s";

        if (pos.x >= area.getX() && pos.x <= area.getRight())
        {
            g.drawText(label,
                       static_cast<int>(pos.x) - 15,
                       bounds.getBottom() - marginBottom + 4,
                       30, marginBottom - 6,
                       juce::Justification::centred);
        }
    }

    // --- Live preview for Line / Rectangle modes ---
    if (isDragging && (brushMode == BrushMode::Line || brushMode == BrushMode::Rectangle))
    {
        auto startPixel = gridToPixel(dragStart.x, dragStart.y);
        auto endPixel   = gridToPixel(dragCurrent.x, dragCurrent.y);

        g.setColour(CyberpunkTheme::fg_.withAlpha(0.5f));

        if (brushMode == BrushMode::Line)
        {
            g.drawLine(
                startPixel.x + cellW * 0.5f,
                startPixel.y + cellH * 0.5f,
                endPixel.x   + cellW * 0.5f,
                endPixel.y   + cellH * 0.5f,
                2.0f);
        }
        else // Rectangle
        {
            float rx = std::min(startPixel.x, endPixel.x);
            float ry = std::min(startPixel.y, endPixel.y);
            float rw = std::abs(endPixel.x - startPixel.x) + cellW;
            float rh = std::abs(endPixel.y - startPixel.y) + cellH;
            g.drawRect(rx, ry, rw, rh, 2.0f);
        }
    }
}

void PartialEditorCanvas::resized()
{
    repaint();
}

} // namespace ana
