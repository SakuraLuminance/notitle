#include "SpectrumEditorCanvas.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <unordered_set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ana {

// ============================================================================
// Construction / Destruction
// ============================================================================
SpectrumEditorCanvas::SpectrumEditorCanvas()
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    startTimerHz(30); // 30 fps smooth animation

    // Initialise waterfall ring buffer
    for (auto& frame : waterfallBuffer_)
    {
        std::memset(frame.frequency, 0, sizeof(frame.frequency));
        std::memset(frame.amplitude, 0, sizeof(frame.amplitude));
        frame.activeCount = 0;
    }
}

SpectrumEditorCanvas::~SpectrumEditorCanvas()
{
    stopTimer();

    if (is3DEnabled_)
        set3DEnabled(false);

    openGLContext_ = nullptr;
    renderer3D_ = nullptr;
}

// ============================================================================
// Timer — pushes waterfall frames and repaints
// ============================================================================
void SpectrumEditorCanvas::timerCallback()
{
    if (is3DEnabled_)
    {
        pushWaterfallFrame();

        if (openGLContext_ != nullptr)
            openGLContext_->triggerRepaint();
    }

    if (isDragging_)
        repaint();
}

// ============================================================================
// Data I/O
// ============================================================================
void SpectrumEditorCanvas::setPartials(const PartialDataSIMD& partials)
{
    {
        juce::ScopedLock sl(dataLock_);
        partials_ = partials;
        undoStack_.clear();
        redoStack_.clear();
    }

    repaint();

    if (onPartialEdited)
        onPartialEdited(partials_);
}

PartialDataSIMD SpectrumEditorCanvas::getEditedPartials() const
{
    juce::ScopedLock sl(dataLock_);
    return partials_;
}

// ============================================================================
// Tool settings
// ============================================================================
void SpectrumEditorCanvas::setActiveTool(Tool tool)     { activeTool_ = tool; }
SpectrumEditorCanvas::Tool SpectrumEditorCanvas::getActiveTool() const { return activeTool_; }

void SpectrumEditorCanvas::setBrushSize(int pixels)
{
    brushSize_ = std::clamp(pixels, 1, 100);
}

void SpectrumEditorCanvas::setBrushStrength(float strength)
{
    brushStrength_ = std::clamp(strength, 0.0f, 1.0f);
}

// ============================================================================
// Undo / Redo
// ============================================================================
void SpectrumEditorCanvas::saveState()
{
    juce::ScopedLock sl(dataLock_);

    undoStack_.push_back(partials_);

    if (static_cast<int>(undoStack_.size()) > kMaxUndoSteps)
        undoStack_.erase(undoStack_.begin());

    redoStack_.clear();
}

void SpectrumEditorCanvas::undo()
{
    juce::ScopedLock sl(dataLock_);

    if (undoStack_.empty())
        return;

    redoStack_.push_back(partials_);
    partials_ = undoStack_.back();
    undoStack_.pop_back();

    repaint();

    if (onPartialEdited)
        onPartialEdited(partials_);
}

void SpectrumEditorCanvas::redo()
{
    juce::ScopedLock sl(dataLock_);

    if (redoStack_.empty())
        return;

    undoStack_.push_back(partials_);
    partials_ = redoStack_.back();
    redoStack_.pop_back();

    repaint();

    if (onPartialEdited)
        onPartialEdited(partials_);
}

bool SpectrumEditorCanvas::canUndo() const { return !undoStack_.empty(); }
bool SpectrumEditorCanvas::canRedo() const { return !redoStack_.empty(); }

void SpectrumEditorCanvas::clearHistory()
{
    juce::ScopedLock sl(dataLock_);
    undoStack_.clear();
    redoStack_.clear();
}

// ============================================================================
// Display settings
// ============================================================================
void SpectrumEditorCanvas::setShowGrid(bool show)       { showGrid_ = show; repaint(); }
void SpectrumEditorCanvas::setShowLabels(bool show)     { showLabels_ = show; repaint(); }
void SpectrumEditorCanvas::setLogFreq(bool useLog)      { logFreq_ = useLog; repaint(); }
void SpectrumEditorCanvas::setAmplitudeScale(float scale){ ampScale_ = std::max(0.01f, scale); repaint(); }
void SpectrumEditorCanvas::setGridLinesX(int count)     { gridLinesX_ = std::max(1, count); repaint(); }
void SpectrumEditorCanvas::setGridLinesY(int count)     { gridLinesY_ = std::max(1, count); repaint(); }

// ============================================================================
// Colour settings
// ============================================================================
void SpectrumEditorCanvas::setBackgroundColour(juce::Colour c) { bgColour_ = c; repaint(); }
void SpectrumEditorCanvas::setGridColour(juce::Colour c)      { gridColour_ = c; repaint(); }
void SpectrumEditorCanvas::setBarColour(juce::Colour c)       { barColour_ = c; repaint(); }

// ============================================================================
// Canvas geometry
// ============================================================================
juce::Rectangle<int> SpectrumEditorCanvas::getCanvasArea() const
{
    auto bounds = getLocalBounds();
    return {
        bounds.getX()      + marginLeft,
        bounds.getY()      + marginTop,
        bounds.getWidth()  - marginLeft - marginRight,
        bounds.getHeight() - marginTop  - marginBottom
    };
}

// ============================================================================
// Coordinate mapping
// ============================================================================
int SpectrumEditorCanvas::freqToX(float freqHz) const
{
    auto area = getCanvasArea();
    if (area.getWidth() <= 0)
        return area.getX();

    constexpr float minFreq = 20.0f;
    constexpr float maxFreq = 20000.0f;

    float clamped = std::clamp(freqHz, minFreq, maxFreq);
    float t;

    if (logFreq_)
    {
        t = std::log(clamped / minFreq) / std::log(maxFreq / minFreq);
    }
    else
    {
        t = (clamped - minFreq) / (maxFreq - minFreq);
    }

    return area.getX() + static_cast<int>(t * static_cast<float>(area.getWidth()));
}

float SpectrumEditorCanvas::xToFreq(int x) const
{
    auto area = getCanvasArea();
    if (area.getWidth() <= 0)
        return 20.0f;

    constexpr float minFreq = 20.0f;
    constexpr float maxFreq = 20000.0f;

    float t = static_cast<float>(x - area.getX()) / static_cast<float>(area.getWidth());
    t = std::clamp(t, 0.0f, 1.0f);

    if (logFreq_)
        return minFreq * std::pow(maxFreq / minFreq, t);
    else
        return minFreq + t * (maxFreq - minFreq);
}

int SpectrumEditorCanvas::ampToY(float amplitude) const
{
    auto area = getCanvasArea();
    if (area.getHeight() <= 0)
        return area.getY();

    float clamped = std::clamp(amplitude, 0.0f, 1.0f);
    return area.getY() + static_cast<int>((1.0f - clamped) * static_cast<float>(area.getHeight()));
}

float SpectrumEditorCanvas::yToAmp(int y) const
{
    auto area = getCanvasArea();
    if (area.getHeight() <= 0)
        return 0.0f;

    float t = static_cast<float>(y - area.getY()) / static_cast<float>(area.getHeight());
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - t; // top = 1.0, bottom = 0.0
}

// ============================================================================
// Brush falloff
// ============================================================================
float SpectrumEditorCanvas::brushFalloff(float dx, float dy, float radius) const
{
    if (radius <= 0.0f)
        return 1.0f;

    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist >= radius)
        return 0.0f;

    float t = dist / radius;
    return 1.0f - t * t;
}

// ============================================================================
// Tool application (Draw / Eraser) at screen-space position
// ============================================================================
void SpectrumEditorCanvas::applyToolAt(int mx, int my)
{
    juce::ScopedLock sl(dataLock_);

    float mouseAmp = yToAmp(my);
    int halfBrush = brushSize_ / 2;

    for (int i = 0; i < partials_.maxPartials; ++i)
    {
        float freq = partials_.frequency[i];
        if (freq <= 0.0f)
            continue;

        int px = freqToX(freq);
        int py = ampToY(partials_.amplitude[i]);

        float dx = static_cast<float>(px - mx);
        float dy = static_cast<float>(py - my);

        if (std::abs(dx) > static_cast<float>(halfBrush) ||
            std::abs(dy) > static_cast<float>(halfBrush))
            continue;

        float falloff = brushFalloff(dx, dy, static_cast<float>(brushSize_));
        if (falloff <= 0.0f)
            continue;

        switch (activeTool_)
        {
            case Tool::Draw:
            {
                float target = mouseAmp;
                float delta = (target - partials_.amplitude[i]) * brushStrength_ * falloff;
                if (delta > 0.0f)
                    partials_.amplitude[i] = std::clamp(partials_.amplitude[i] + delta, 0.0f, 1.0f);
                break;
            }

            case Tool::Eraser:
            {
                float delta = brushStrength_ * falloff;
                partials_.amplitude[i] = std::max(0.0f, partials_.amplitude[i] - delta);
                break;
            }

            default:
                break;
        }
    }

    partials_.updateActiveMask();
}

// ============================================================================
// Smooth / Sharpen
// ============================================================================
void SpectrumEditorCanvas::applySmooth(int mx, int my)
{
    juce::ScopedLock sl(dataLock_);

    float centerFreq = xToFreq(mx);

    int centerIdx = -1;
    float minDist = std::numeric_limits<float>::max();

    for (int i = 0; i < partials_.maxPartials; ++i)
    {
        if (partials_.frequency[i] <= 0.0f)
            continue;

        float df = std::abs(partials_.frequency[i] - centerFreq);
        if (df < minDist)
        {
            minDist = df;
            centerIdx = i;
        }
    }

    if (centerIdx < 0)
        return;

    int halfBrush = brushSize_ / 2;
    int start = std::max(1, centerIdx - halfBrush);
    int end   = std::min(partials_.maxPartials - 2, centerIdx + halfBrush);

    auto smoothed = partials_.amplitude;

    for (int i = start; i <= end; ++i)
    {
        if (partials_.frequency[i] <= 0.0f)
            continue;

        float sum = 0.0f;
        int count = 0;

        for (int d = -1; d <= 1; ++d)
        {
            int idx = i + d;
            if (idx >= 0 && idx < partials_.maxPartials && partials_.frequency[idx] > 0.0f)
            {
                sum += partials_.amplitude[idx];
                ++count;
            }
        }

        if (count > 0)
            smoothed[i] = sum / static_cast<float>(count);
    }

    for (int i = start; i <= end; ++i)
        partials_.amplitude[i] = smoothed[i];

    partials_.updateActiveMask();
}

void SpectrumEditorCanvas::applySharpen(int mx, int my)
{
    juce::ScopedLock sl(dataLock_);

    float centerFreq = xToFreq(mx);

    int centerIdx = -1;
    float minDist = std::numeric_limits<float>::max();

    for (int i = 0; i < partials_.maxPartials; ++i)
    {
        if (partials_.frequency[i] <= 0.0f)
            continue;

        float df = std::abs(partials_.frequency[i] - centerFreq);
        if (df < minDist)
        {
            minDist = df;
            centerIdx = i;
        }
    }

    if (centerIdx < 0)
        return;

    int halfBrush = brushSize_ / 2;
    int start = std::max(1, centerIdx - halfBrush);
    int end   = std::min(partials_.maxPartials - 2, centerIdx + halfBrush);

    auto smoothed = partials_.amplitude;

    for (int i = start; i <= end; ++i)
    {
        if (partials_.frequency[i] <= 0.0f)
            continue;

        float sum = 0.0f;
        int count = 0;

        for (int d = -1; d <= 1; ++d)
        {
            int idx = i + d;
            if (idx >= 0 && idx < partials_.maxPartials && partials_.frequency[idx] > 0.0f)
            {
                sum += partials_.amplitude[idx];
                ++count;
            }
        }

        if (count > 0)
            smoothed[i] = sum / static_cast<float>(count);
    }

    for (int i = start; i <= end; ++i)
    {
        if (partials_.frequency[i] <= 0.0f)
            continue;

        float diff = partials_.amplitude[i] - smoothed[i];
        partials_.amplitude[i] = std::clamp(
            partials_.amplitude[i] + brushStrength_ * 0.5f * diff,
            0.0f, 1.0f);
    }

    partials_.updateActiveMask();
}

// ============================================================================
// Line / Rectangle tools
// ============================================================================
void SpectrumEditorCanvas::drawLineTo(float freqFrom, float ampFrom,
                                       float freqTo,   float ampTo)
{
    juce::ScopedLock sl(dataLock_);

    float minFreq = std::min(freqFrom, freqTo);
    float maxFreq = std::max(freqFrom, freqTo);

    if (maxFreq - minFreq < 1.0f)
    {
        float freq = (freqFrom + freqTo) * 0.5f;
        float amp  = (ampFrom + ampTo) * 0.5f;

        int nearest = -1;
        float best = std::numeric_limits<float>::max();

        for (int i = 0; i < partials_.maxPartials; ++i)
        {
            if (partials_.frequency[i] <= 0.0f)
                continue;

            float df = std::abs(partials_.frequency[i] - freq);
            if (df < best)
            {
                best = df;
                nearest = i;
            }
        }

        if (nearest >= 0)
            partials_.amplitude[nearest] = std::clamp(amp, 0.0f, 1.0f);

        partials_.updateActiveMask();
        return;
    }

    for (int i = 0; i < partials_.maxPartials; ++i)
    {
        float f = partials_.frequency[i];
        if (f <= 0.0f)
            continue;

        if (f >= minFreq && f <= maxFreq)
        {
            float t = (f - freqFrom) / (freqTo - freqFrom);
            float targetAmp = ampFrom + t * (ampTo - ampFrom);
            partials_.amplitude[i] = std::clamp(targetAmp, 0.0f, 1.0f);
        }
    }

    partials_.updateActiveMask();
}

void SpectrumEditorCanvas::fillRectTo(float freqFrom, float ampFrom,
                                       float freqTo,   float ampTo)
{
    juce::ScopedLock sl(dataLock_);

    float minFreq = std::min(freqFrom, freqTo);
    float maxFreq = std::max(freqFrom, freqTo);
    float fillAmp = std::max(ampFrom, ampTo);

    for (int i = 0; i < partials_.maxPartials; ++i)
    {
        float f = partials_.frequency[i];
        if (f <= 0.0f)
            continue;

        if (f >= minFreq && f <= maxFreq)
            partials_.amplitude[i] = std::clamp(fillAmp, 0.0f, 1.0f);
    }

    partials_.updateActiveMask();
}

// ============================================================================
// Mouse handling — 2D mode (existing) and 3D mode
// ============================================================================
void SpectrumEditorCanvas::mouseDown(const juce::MouseEvent& e)
{
    if (is3DEnabled_)
    {
        lastMousePos3D_ = e.position;

        // Right mouse button or Ctrl+left → orbit
        if (e.mods.isRightButtonDown() || e.mods.isCtrlDown())
        {
            is3DDragging_ = true;
            isOrbiting_ = true;
            return;
        }

        // Middle mouse button → pan
        if (e.mods.isMiddleButtonDown())
        {
            is3DDragging_ = true;
            isPanning_ = true;
            return;
        }

        // Left click → hit test partials
        int hitIdx = hitTestPartial3D(e.position);

        if (e.mods.isShiftDown())
        {
            // Box selection start
            isBoxSelecting_ = true;
            is3DDragging_ = true;
            selectionBox_ = juce::Rectangle<float>(e.position.x, e.position.y, 0, 0);
            return;
        }

        if (e.mods.isAltDown())
        {
            // Ctrl-click / Alt-click → toggle selection
            is3DDragging_ = true;
            if (hitIdx >= 0)
            {
                if (multiSelection_.count(hitIdx))
                    multiSelection_.erase(hitIdx);
                else
                    multiSelection_.insert(hitIdx);
                selectedPartial_ = hitIdx;
            }
            return;
        }

        // Simple click → select and possibly drag
        is3DDragging_ = true;
        if (hitIdx >= 0)
        {
            selectedPartial_ = hitIdx;
            multiSelection_.clear();
            multiSelection_.insert(hitIdx);
            isDraggingPartial_ = true;
            dragPartialIndex_ = hitIdx;

            // Save undo state and record start position
            saveState();
            juce::ScopedLock sl(dataLock_);
            dragStartFreq3D_ = partials_.frequency[hitIdx];
            dragStartAmp3D_  = partials_.amplitude[hitIdx];
        }
        else
        {
            // Click on empty space → orbit
            isOrbiting_ = true;
        }

        return;
    }

    // === 2D mode (existing) ===
    auto area = getCanvasArea();
    if (!area.contains(e.getPosition()))
        return;

    bool hasActive = false;
    for (int i = 0; i < partials_.maxPartials && !hasActive; ++i)
        if (partials_.isActive(i))
            hasActive = true;

    if (!hasActive)
        return;

    saveState();

    int mx = e.getPosition().x;
    int my = e.getPosition().y;

    isDragging_ = true;
    hasPendingStroke_ = false;

    float startFreq = xToFreq(mx);
    float startAmp  = yToAmp(my);

    dragStartFreq_ = startFreq;
    dragStartAmp_  = startAmp;
    dragEndFreq_   = startFreq;
    dragEndAmp_    = startAmp;

    switch (activeTool_)
    {
        case Tool::Draw:
        case Tool::Eraser:
            applyToolAt(mx, my);
            repaint();
            break;

        case Tool::Smooth:
            applySmooth(mx, my);
            repaint();
            break;

        case Tool::Sharpen:
            applySharpen(mx, my);
            repaint();
            break;

        case Tool::Line:
        case Tool::Rectangle:
            hasPendingStroke_ = true;
            repaint();
            break;

        default:
            break;
    }
}

void SpectrumEditorCanvas::mouseDrag(const juce::MouseEvent& e)
{
    if (is3DEnabled_)
    {
        if (!is3DDragging_)
            return;

        auto delta = e.position - lastMousePos3D_;

        if (isOrbiting_)
        {
            yaw_   = std::clamp(yaw_   + delta.x * 0.4f, -180.0f, 180.0f);
            pitch_ = std::clamp(pitch_ - delta.y * 0.4f, -89.0f, 89.0f);
            lastMousePos3D_ = e.position;

            if (openGLContext_ != nullptr)
                openGLContext_->triggerRepaint();
            return;
        }

        if (isPanning_)
        {
            // Pan in screen space — scale by zoom for consistent feel
            float sens = 0.01f / zoom_;
            panX_ += delta.x * sens;
            panY_ -= delta.y * sens;
            lastMousePos3D_ = e.position;

            if (openGLContext_ != nullptr)
                openGLContext_->triggerRepaint();
            return;
        }

        if (isBoxSelecting_)
        {
            float x1 = selectionBox_.getX();
            float y1 = selectionBox_.getY();
            float x2 = static_cast<float>(e.position.x);
            float y2 = static_cast<float>(e.position.y);
            selectionBox_ = juce::Rectangle<float>::leftTopRightBottom(
                std::min(x1, x2), std::min(y1, y2),
                std::max(x1, x2), std::max(y1, y2));

            if (openGLContext_ != nullptr)
                openGLContext_->triggerRepaint();
            return;
        }

        if (isDraggingPartial_ && dragPartialIndex_ >= 0)
        {
            // Convert mouse delta to frequency/amplitude change
            // Use screen-space delta projected onto the 3D scene
            float freqDelta = delta.x * 20.0f / zoom_;
            float ampDelta  = -delta.y * 0.005f / zoom_;

            // Apply to the dragged partial
            {
                juce::ScopedLock sl(dataLock_);
                int idx = dragPartialIndex_;
                float newFreq = std::clamp(dragStartFreq3D_ + freqDelta, 20.0f, 20000.0f);
                float newAmp  = std::clamp(dragStartAmp3D_  + ampDelta, 0.0f, 1.0f);

                // Move partial frequency/amplitude
                partials_.frequency[idx] = newFreq;
                partials_.amplitude[idx] = newAmp;
                partials_.updateActiveMask();
            }

            if (onPartialEdited)
                onPartialEdited(partials_);

            if (openGLContext_ != nullptr)
                openGLContext_->triggerRepaint();
            return;
        }

        return;
    }

    // === 2D mode ===
    if (!isDragging_)
        return;

    auto area = getCanvasArea();
    int mx = std::clamp(e.getPosition().x, area.getX(), area.getRight());
    int my = std::clamp(e.getPosition().y, area.getY(), area.getBottom());

    dragEndFreq_ = xToFreq(mx);
    dragEndAmp_  = yToAmp(my);

    switch (activeTool_)
    {
        case Tool::Draw:
        case Tool::Eraser:
            applyToolAt(mx, my);
            repaint();
            break;

        case Tool::Smooth:
            applySmooth(mx, my);
            repaint();
            break;

        case Tool::Sharpen:
            applySharpen(mx, my);
            repaint();
            break;

        case Tool::Line:
        case Tool::Rectangle:
            repaint();
            break;

        default:
            break;
    }
}

void SpectrumEditorCanvas::mouseUp(const juce::MouseEvent& e)
{
    if (is3DEnabled_)
    {
        if (isBoxSelecting_ && !selectionBox_.isEmpty())
        {
            // Finalise box selection: hit-test partials in the box
            multiSelection_.clear();

            auto mousePos = e.position;
            float x1 = selectionBox_.getX();
            float y1 = selectionBox_.getY();
            float x2 = mousePos.x;
            float y2 = mousePos.y;

            auto box = juce::Rectangle<float>::leftTopRightBottom(
                std::min(x1, x2), std::min(y1, y2),
                std::max(x1, x2), std::max(y1, y2));

            // For each active partial, test if its projected 3D position falls in the box
            {
                juce::ScopedLock sl(dataLock_);
                for (int i = 0; i < partials_.maxPartials; ++i)
                {
                    if (!partials_.isActive(i))
                        continue;

                    // Quick screen-space test using the front-most frame depth
                    float freq = partials_.frequency[i];
                    float amp  = partials_.amplitude[i];

                    // Normalised frequency: 0..1
                    float nx = std::log(freq / 20.0f) / std::log(20000.0f / 20.0f);
                    // Approximate screen y from amplitude (0 at bottom, 1 at top)
                    float ny = amp;

                    // Project to screen (simplified — uses front Z=0)
                    float sx, sy;
                    {
                        float pitchRad = pitch_ * static_cast<float>(M_PI) / 180.0f;
                        float yawRad   = yaw_   * static_cast<float>(M_PI) / 180.0f;
                        float nx3 = (nx * 2.0f - 1.0f);   // -1..1
                        float ny3 = ny * 2.0f;              // 0..2 (amplitude up)
                        float nz3 = 0.0f;                    // front

                        // Yaw rotation
                        float xYaw = nx3 * std::cos(yawRad) + nz3 * std::sin(yawRad);
                        float zYaw = -nx3 * std::sin(yawRad) + nz3 * std::cos(yawRad);
                        // Pitch rotation
                        float yPitch = ny3 * std::cos(pitchRad) - zYaw * std::sin(pitchRad);
                        float zPitch = ny3 * std::sin(pitchRad) + zYaw * std::cos(pitchRad);

                        float scale = zoom_ / (1.0f + zPitch * 3.0f);
                        float cx = getWidth() * 0.5f + panX_ * getWidth();
                        float cy = getHeight() * 0.5f + panY_ * getHeight();
                        sx = cx + xYaw * scale * getWidth() * 0.4f;
                        sy = cy - yPitch * scale * getHeight() * 0.4f;
                    }

                    if (box.contains(sx, sy))
                        multiSelection_.insert(i);
                }
            }

            selectedPartial_ = multiSelection_.empty() ? -1 : *multiSelection_.begin();
        }

        if (isDraggingPartial_ && dragPartialIndex_ >= 0)
        {
            // Notify listeners of the edit
            if (onPartialEdited)
                onPartialEdited(partials_);
        }

        is3DDragging_ = false;
        isOrbiting_ = false;
        isPanning_ = false;
        isBoxSelecting_ = false;
        isDraggingPartial_ = false;
        dragPartialIndex_ = -1;

        if (openGLContext_ != nullptr)
            openGLContext_->triggerRepaint();

        return;
    }

    // === 2D mode ===
    if (!isDragging_)
        return;

    auto area = getCanvasArea();
    int mx = std::clamp(e.getPosition().x, area.getX(), area.getRight());
    int my = std::clamp(e.getPosition().y, area.getY(), area.getBottom());

    dragEndFreq_ = xToFreq(mx);
    dragEndAmp_  = yToAmp(my);

    if (hasPendingStroke_)
    {
        switch (activeTool_)
        {
            case Tool::Line:
                drawLineTo(dragStartFreq_, dragStartAmp_,
                           dragEndFreq_,   dragEndAmp_);
                repaint();
                break;

            case Tool::Rectangle:
                fillRectTo(dragStartFreq_, dragStartAmp_,
                           dragEndFreq_,   dragEndAmp_);
                repaint();
                break;

            default:
                break;
        }
    }

    isDragging_ = false;
    hasPendingStroke_ = false;

    if (onPartialEdited)
        onPartialEdited(partials_);
}

void SpectrumEditorCanvas::mouseWheelMove(const juce::MouseEvent& e,
                                           const juce::MouseWheelDetails& w)
{
    if (is3DEnabled_)
    {
        zoom_ = std::clamp(zoom_ * (w.deltaY > 0.0f ? 1.1f : 0.9f), 0.1f, 10.0f);

        if (openGLContext_ != nullptr)
            openGLContext_->triggerRepaint();

        return;
    }
}

// ============================================================================
// 3D Waterfall Mode — Public API
// ============================================================================
void SpectrumEditorCanvas::set3DEnabled(bool enabled)
{
    if (is3DEnabled_ == enabled)
        return;

    is3DEnabled_ = enabled;

    if (enabled)
    {
        // Create OpenGL context and renderer
        openGLContext_ = std::make_unique<juce::OpenGLContext>();
        renderer3D_ = std::make_unique<Spectrum3DRenderer>(*this);
        openGLContext_->setRenderer(renderer3D_.get());
        openGLContext_->setComponentPaintingEnabled(false);
        openGLContext_->attachTo(*this);

        // Reset waterfall buffer on enable
        totalWaterfallFrames_ = 0;
        waterfallWritePos_ = 0;

        // Push current partial data as initial frame
        pushWaterfallFrame();

        // Increase timer rate for 3D mode
        startTimerHz(k3DUpdateHz);
    }
    else
    {
        // Detach OpenGL
        if (openGLContext_ != nullptr)
        {
            openGLContext_->detach();
            openGLContext_ = nullptr;
            renderer3D_ = nullptr;
        }

        // Restore timer to normal rate
        startTimerHz(30);

        repaint();
    }
}

bool SpectrumEditorCanvas::is3DEnabled() const
{
    return is3DEnabled_;
}

// ============================================================================
// Camera controls
// ============================================================================
void SpectrumEditorCanvas::setCameraRotation(float yawDeg, float pitchDeg)
{
    yaw_   = std::clamp(yawDeg, -180.0f, 180.0f);
    pitch_ = std::clamp(pitchDeg, -89.0f, 89.0f);
    if (openGLContext_ != nullptr)
        openGLContext_->triggerRepaint();
}

void SpectrumEditorCanvas::setCameraZoom(float zoom)
{
    zoom_ = std::clamp(zoom, 0.1f, 10.0f);
    if (openGLContext_ != nullptr)
        openGLContext_->triggerRepaint();
}

void SpectrumEditorCanvas::setCameraPanX(float x) { panX_ = x; if (openGLContext_ != nullptr) openGLContext_->triggerRepaint(); }
void SpectrumEditorCanvas::setCameraPanY(float y) { panY_ = y; if (openGLContext_ != nullptr) openGLContext_->triggerRepaint(); }
float SpectrumEditorCanvas::getYaw()   const { return yaw_; }
float SpectrumEditorCanvas::getPitch() const { return pitch_; }
float SpectrumEditorCanvas::getZoom()  const { return zoom_; }

// ============================================================================
// Partial selection API
// ============================================================================
int SpectrumEditorCanvas::getSelectedPartial() const
{
    return selectedPartial_;
}

void SpectrumEditorCanvas::setSelectedPartial(int index)
{
    selectedPartial_ = index;
    multiSelection_.clear();
    if (index >= 0)
        multiSelection_.insert(index);
    if (openGLContext_ != nullptr)
        openGLContext_->triggerRepaint();
    repaint();
}

void SpectrumEditorCanvas::movePartial(int index, float newFreq, float newAmp)
{
    if (index < 0 || index >= partials_.maxPartials)
        return;

    {
        juce::ScopedLock sl(dataLock_);
        partials_.frequency[index] = std::clamp(newFreq, 20.0f, 20000.0f);
        partials_.amplitude[index] = std::clamp(newAmp, 0.0f, 1.0f);
        partials_.updateActiveMask();
    }

    if (onPartialEdited)
        onPartialEdited(partials_);

    if (openGLContext_ != nullptr)
        openGLContext_->triggerRepaint();
    repaint();
}

void SpectrumEditorCanvas::batchMovePartials(const std::vector<int>& indices,
                                              float freqOffset, float ampOffset)
{
    if (indices.empty())
        return;

    saveState();

    {
        juce::ScopedLock sl(dataLock_);
        for (int idx : indices)
        {
            if (idx < 0 || idx >= partials_.maxPartials)
                continue;

            partials_.frequency[idx] = std::clamp(
                partials_.frequency[idx] + freqOffset, 20.0f, 20000.0f);
            partials_.amplitude[idx] = std::clamp(
                partials_.amplitude[idx] + ampOffset, 0.0f, 1.0f);
        }
        partials_.updateActiveMask();
    }

    if (onPartialEdited)
        onPartialEdited(partials_);

    if (openGLContext_ != nullptr)
        openGLContext_->triggerRepaint();
    repaint();
}

void SpectrumEditorCanvas::clearSelection()
{
    selectedPartial_ = -1;
    multiSelection_.clear();
    isBoxSelecting_ = false;
    selectionBox_ = {};
    if (openGLContext_ != nullptr)
        openGLContext_->triggerRepaint();
    repaint();
}

bool SpectrumEditorCanvas::isPartialSelected(int index) const
{
    return multiSelection_.count(index) > 0;
}

std::vector<int> SpectrumEditorCanvas::getSelectedPartials() const
{
    return { multiSelection_.begin(), multiSelection_.end() };
}

juce::Rectangle<float> SpectrumEditorCanvas::getSelectionBox() const
{
    return selectionBox_;
}

// ============================================================================
// Waterfall buffer
// ============================================================================
void SpectrumEditorCanvas::pushWaterfallFrame()
{
    juce::ScopedLock sl(dataLock_);

    auto& frame = waterfallBuffer_[waterfallWritePos_];
    int count = 0;

    for (int i = 0; i < partials_.maxPartials; ++i)
    {
        if (partials_.isActive(i))
        {
            frame.frequency[count] = partials_.frequency[i];
            frame.amplitude[count] = partials_.amplitude[i];
            ++count;
        }
    }

    frame.activeCount = count;

    // Zero out the rest
    for (int i = count; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        frame.frequency[i] = 0.0f;
        frame.amplitude[i] = 0.0f;
    }

    waterfallWritePos_ = (waterfallWritePos_ + 1) % kWaterfallDepth;
    if (totalWaterfallFrames_ < kWaterfallDepth)
        ++totalWaterfallFrames_;
}

// ============================================================================
// 3D hit testing — projects partials to screen and finds nearest
// ============================================================================
int SpectrumEditorCanvas::hitTestPartial3D(juce::Point<float> mousePos)
{
    juce::ScopedLock sl(dataLock_);

    constexpr float hitRadius = 8.0f;  // pixels
    int bestIdx = -1;
    float bestDist = hitRadius;

    // Build the projection from the latest frame
    for (int i = 0; i < partials_.maxPartials; ++i)
    {
        if (!partials_.isActive(i))
            continue;

        float freq = partials_.frequency[i];
        float amp  = partials_.amplitude[i];

        // Normalise frequency to 0..1 (log scale)
        float nx = std::log(freq / 20.0f) / std::log(20000.0f / 20.0f);
        // Project to screen using current camera
        float sx, sy;
        {
            float pitchRad = pitch_ * static_cast<float>(M_PI) / 180.0f;
            float yawRad   = yaw_   * static_cast<float>(M_PI) / 180.0f;
            float nx3 = (nx * 2.0f - 1.0f);
            float ny3 = amp * 2.0f;
            float nz3 = 0.0f;  // front depth

            float xYaw = nx3 * std::cos(yawRad) + nz3 * std::sin(yawRad);
            float zYaw = -nx3 * std::sin(yawRad) + nz3 * std::cos(yawRad);
            float yPitch = ny3 * std::cos(pitchRad) - zYaw * std::sin(pitchRad);
            float zPitch = ny3 * std::sin(pitchRad) + zYaw * std::cos(pitchRad);

            float scale = zoom_ / (1.0f + zPitch * 3.0f);
            float cx = getWidth() * 0.5f + panX_ * getWidth();
            float cy = getHeight() * 0.5f + panY_ * getHeight();
            sx = cx + xYaw * scale * getWidth() * 0.4f;
            sy = cy - yPitch * scale * getHeight() * 0.4f;
        }

        float dx = mousePos.x - sx;
        float dy = mousePos.y - sy;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < bestDist)
        {
            bestDist = dist;
            bestIdx = i;
        }
    }

    return bestIdx;
}

// ============================================================================
// Paint — 2D mode only
// ============================================================================
void SpectrumEditorCanvas::paint(juce::Graphics& g)
{
    if (is3DEnabled_)
    {
        // 3D mode: OpenGL renders, but paint may still be called
        // (e.g. when component painting is not disabled)
        g.fillAll(bgColour_);
        return;
    }

    auto bounds = getLocalBounds();
    g.fillAll(bgColour_);

    auto area = getCanvasArea();

    // --- Check for loaded data ---
    bool hasData = false;
    {
        juce::ScopedLock sl(dataLock_);

        for (int i = 0; i < partials_.maxPartials; ++i)
        {
            if (partials_.isActive(i))
            {
                hasData = true;
                break;
            }
        }
    }

    if (!hasData)
    {
        g.setColour(juce::Colours::grey);
        g.setFont(juce::Font(14.0f, juce::Font::plain));
        g.drawText("No partial data loaded", bounds, juce::Justification::centred);
        return;
    }

    // --- Draw spectrum bars ---
    {
        juce::ScopedLock sl(dataLock_);

        int activeCount = partials_.activeCount;
        int barWidth = activeCount > 0
                           ? std::max(1, area.getWidth() / activeCount)
                           : 2;

        for (int i = 0; i < partials_.maxPartials; ++i)
        {
            if (!partials_.isActive(i))
                continue;

            float freq = partials_.frequency[i];
            float amp  = std::clamp(partials_.amplitude[i] * ampScale_, 0.0f, 1.0f);

            if (amp <= 0.0f || freq <= 0.0f)
                continue;

            int x = freqToX(freq);
            int barHeight = static_cast<int>(amp * static_cast<float>(area.getHeight()));
            int y = area.getBottom() - barHeight;

            if (x > area.getRight() || x + barWidth < area.getX())
                continue;

            float alpha = 0.25f + 0.75f * amp;
            g.setColour(barColour_.withMultipliedAlpha(alpha));
            g.fillRect(x, y, barWidth, barHeight);
        }
    }

    // --- Grid lines ---
    if (showGrid_)
    {
        g.setColour(gridColour_);

        for (int i = 0; i <= gridLinesX_; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(gridLinesX_);
            float freq;
            if (logFreq_)
                freq = 20.0f * std::pow(20000.0f / 20.0f, t);
            else
                freq = 20.0f + t * (20000.0f - 20.0f);

            int x = freqToX(freq);
            if (x >= area.getX() && x <= area.getRight())
                g.drawVerticalLine(x, static_cast<float>(area.getY()),
                                   static_cast<float>(area.getBottom()));
        }

        for (int i = 0; i <= gridLinesY_; ++i)
        {
            float amp = static_cast<float>(i) / static_cast<float>(gridLinesY_);
            int y = ampToY(amp);
            if (y >= area.getY() && y <= area.getBottom())
                g.drawHorizontalLine(y, static_cast<float>(area.getX()),
                                     static_cast<float>(area.getRight()));
        }
    }

    g.setColour(gridColour_.brighter(0.5f));
    g.drawRect(area, 1);

    // --- Labels ---
    if (showLabels_)
    {
        g.setFont(juce::Font(10.0f, juce::Font::plain));
        g.setColour(juce::Colours::lightgrey);

        for (int i = 0; i <= gridLinesX_; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(gridLinesX_);
            float freq;
            if (logFreq_)
                freq = 20.0f * std::pow(20000.0f / 20.0f, t);
            else
                freq = 20.0f + t * (20000.0f - 20.0f);

            int x = freqToX(freq);
            if (x < area.getX() || x > area.getRight())
                continue;

            juce::String label;
            if (freq >= 1000.0f)
                label = juce::String(freq / 1000.0f, 1) + "k";
            else
                label = juce::String(static_cast<int>(freq));

            g.drawText(label,
                       x - 15,
                       bounds.getBottom() - marginBottom + 4,
                       30, marginBottom - 6,
                       juce::Justification::centred);
        }

        for (int i = 0; i <= gridLinesY_; ++i)
        {
            float amp = static_cast<float>(gridLinesY_ - i) / static_cast<float>(gridLinesY_);
            int y = ampToY(amp);
            if (y < area.getY() || y > area.getBottom())
                continue;

            g.drawText(juce::String(amp, 2),
                       bounds.getX() + 2,
                       y - 6,
                       marginLeft - 5, 12,
                       juce::Justification::centredRight);
        }

        g.setFont(juce::Font(9.0f, juce::Font::plain));
        g.setColour(juce::Colours::grey);

        g.drawText("Frequency",
                   area.getX(), bounds.getBottom() - marginBottom + 14,
                   area.getWidth(), 12,
                   juce::Justification::centred);

        g.drawText("Amp",
                   2, area.getCentreY() - 6,
                   marginLeft - 5, 12,
                   juce::Justification::centredRight);
    }

    // --- Tool preview ---
    if (isDragging_ && hasPendingStroke_ &&
        (activeTool_ == Tool::Line || activeTool_ == Tool::Rectangle))
    {
        int x1 = freqToX(dragStartFreq_);
        int y1 = ampToY(dragStartAmp_);
        int x2 = freqToX(dragEndFreq_);
        int y2 = ampToY(dragEndAmp_);

        g.setColour(toolColour_.withAlpha(0.6f));

        if (activeTool_ == Tool::Line)
        {
            g.drawLine(static_cast<float>(x1), static_cast<float>(y1),
                       static_cast<float>(x2), static_cast<float>(y2), 2.0f);
            g.fillEllipse(static_cast<float>(x1) - 3.0f,
                          static_cast<float>(y1) - 3.0f, 6.0f, 6.0f);
            g.fillEllipse(static_cast<float>(x2) - 3.0f,
                          static_cast<float>(y2) - 3.0f, 6.0f, 6.0f);
        }
        else
        {
            int rx = std::min(x1, x2);
            int ry = std::min(y1, y2);
            int rw = std::abs(x2 - x1);
            int rh = std::abs(y2 - y1);

            g.drawRect(rx, ry, rw, rh, 2.0f);
            g.setColour(toolColour_.withAlpha(0.15f));
            g.fillRect(rx, ry, rw, rh);
        }
    }
}

void SpectrumEditorCanvas::resized()
{
    // When using OpenGL, the context needs to know about the resize
    if (openGLContext_ != nullptr)
        openGLContext_->triggerRepaint();

    repaint();
}

// ============================================================================
// 3D Waterfall — OpenGL Renderer
// ============================================================================

void SpectrumEditorCanvas::Spectrum3DRenderer::newOpenGLContextCreated()
{
    using namespace juce::gl;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glClearColor(0.04f, 0.04f, 0.08f, 1.0f);  // dark navy
}

void SpectrumEditorCanvas::Spectrum3DRenderer::openGLContextClosing()
{
    // Nothing to clean up
}

void SpectrumEditorCanvas::Spectrum3DRenderer::renderOpenGL()
{
    owner_.render3DContent();
}

// ============================================================================
// 3D Waterfall — Core OpenGL rendering
// ============================================================================
void SpectrumEditorCanvas::render3DContent()
{
    using namespace juce::gl;

    int w = getWidth();
    int h = getHeight();
    if (w <= 0 || h <= 0)
        return;

    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // --- Perspective projection ---
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = static_cast<float>(w) / static_cast<float>(h);
    float fovY = 45.0f * static_cast<float>(M_PI) / 180.0f;
    float nearP = 0.1f, farP = 50.0f;
    float topP = nearP * std::tan(fovY * 0.5f);
    float bottomP = -topP;
    float leftP = bottomP * aspect;
    float rightP = topP * aspect;
    glFrustum(leftP, rightP, bottomP, topP, nearP, farP);

    // --- Camera modelview ---
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float dist = 5.0f / zoom_;
    glTranslatef(panX_ * 3.0f, panY_ * 2.0f, -dist);
    glRotatef(pitch_, 1.0f, 0.0f, 0.0f);
    glRotatef(yaw_, 0.0f, 1.0f, 0.0f);

    // Data volume spans [-1, 1] in X (frequency), [-1, 1] in Y (depth/time),
    // and [0, 2] in Z (amplitude / height)

    // ====================================================================
    // 1. Floor grid
    // ====================================================================
    glLineWidth(1.0f);
    glBegin(GL_LINES);

    // XY grid lines at Z = 0 (frequency × depth plane)
    const int gridDivX = 10;  // frequency divisions
    const int gridDivY = 8;   // depth divisions

    for (int i = 0; i <= gridDivX; ++i)
    {
        float x = static_cast<float>(i) / static_cast<float>(gridDivX) * 2.0f - 1.0f;
        glColor4f(0.15f, 0.15f, 0.3f, 0.8f);
        glVertex3f(x, -1.0f, 0.0f);
        glVertex3f(x,  1.0f, 0.0f);
    }

    for (int i = 0; i <= gridDivY; ++i)
    {
        float y = static_cast<float>(i) / static_cast<float>(gridDivY) * 2.0f - 1.0f;
        glColor4f(0.15f, 0.15f, 0.3f, 0.8f);
        glVertex3f(-1.0f, y, 0.0f);
        glVertex3f( 1.0f, y, 0.0f);
    }

    // Z-axis line (amplitude)
    glColor4f(0.3f, 0.3f, 0.5f, 0.5f);
    glVertex3f(-1.0f, -1.0f, 0.0f);
    glVertex3f(-1.0f, -1.0f, 2.0f);

    glEnd();

    // ====================================================================
    // 2. Waterfall bars (back to front for proper depth ordering)
    // ====================================================================
    int validFrames = std::min(totalWaterfallFrames_, kWaterfallDepth);
    if (validFrames < 1)
    {
        // No data yet — render empty state text
        return;
    }

    // Determine the oldest frame index in the ring buffer
    int oldestIdx;
    if (totalWaterfallFrames_ < kWaterfallDepth)
        oldestIdx = 0;
    else
        oldestIdx = waterfallWritePos_;

    // Render from back (oldest) to front (newest)
    for (int fi = 0; fi < validFrames; ++fi)
    {
        int bufIdx = (oldestIdx + fi) % kWaterfallDepth;
        const auto& frame = waterfallBuffer_[bufIdx];

        // Normalised depth: 0 = front (newest), 1 = back (oldest)
        float depth = 1.0f - static_cast<float>(fi) / static_cast<float>(validFrames - 1);
        // Map depth to Y position: front = -1, back = +1
        float yPos = -depth * 2.0f + 1.0f;

        // Opacity: front = fully opaque, back = more transparent
        float alpha = 0.85f - depth * 0.5f;

        if (frame.activeCount <= 0)
            continue;

        for (int pi = 0; pi < frame.activeCount; ++pi)
        {
            float freq = frame.frequency[pi];
            float amp  = frame.amplitude[pi];

            if (amp < 1e-6f || freq <= 0.0f)
                continue;

            // Normalise frequency to -1..1
            float nx = std::log(freq / 20.0f) / std::log(20000.0f / 20.0f);
            float xPos = nx * 2.0f - 1.0f;

            // Amplitude → height (0 at floor, 2*amp at top)
            float zPos = amp * 2.0f;

            // Color: amplitude maps to blue→cyan→yellow→red
            float t = std::clamp(amp, 0.0f, 1.0f);
            float r, g, b;
            if (t < 0.25f)
            {
                // Dark blue → blue
                float u = t / 0.25f;
                r = 0.1f; g = 0.1f + u * 0.6f; b = 0.4f + u * 0.6f;
            }
            else if (t < 0.5f)
            {
                // Blue → cyan
                float u = (t - 0.25f) / 0.25f;
                r = 0.1f + u * 0.3f; g = 0.7f; b = 1.0f - u * 0.3f;
            }
            else if (t < 0.75f)
            {
                // Cyan → yellow
                float u = (t - 0.5f) / 0.25f;
                r = 0.4f + u * 0.6f; g = 1.0f; b = 0.7f - u * 0.7f;
            }
            else
            {
                // Yellow → red
                float u = (t - 0.75f) / 0.25f;
                r = 1.0f; g = 1.0f - u; b = 0.0f;
            }

            // Check if this partial is selected
            bool isSelected = isPartialSelected(pi);
            float barWidth = 0.02f;  // proportional to view volume

            // Render as a 3D bar (quad)
            float x0 = xPos - barWidth;
            float x1 = xPos + barWidth;
            float z0 = 0.0f;
            float z1 = zPos;

            // Front face
            glBegin(GL_QUADS);
            if (isSelected)
                glColor4f(1.0f, 1.0f, 1.0f, alpha);   // white highlight
            else
                glColor4f(r, g, b, alpha);

            glVertex3f(x0, yPos, z0);
            glVertex3f(x1, yPos, z0);
            glVertex3f(x1, yPos, z1);
            glVertex3f(x0, yPos, z1);
            glEnd();
        }
    }

    // ====================================================================
    // 3. Front ridge highlight (newest frame — drawn last on top)
    // ====================================================================
    if (validFrames > 0)
    {
        int newestIdx;
        if (totalWaterfallFrames_ < kWaterfallDepth)
            newestIdx = totalWaterfallFrames_ - 1;
        else
            newestIdx = (waterfallWritePos_ - 1 + kWaterfallDepth) % kWaterfallDepth;

        const auto& frontFrame = waterfallBuffer_[newestIdx];

        glLineWidth(2.5f);
        glBegin(GL_LINE_STRIP);
        glColor4f(0.0f, 0.9f, 1.0f, 0.9f);  // neon cyan

        for (int pi = 0; pi < frontFrame.activeCount; ++pi)
        {
            float freq = frontFrame.frequency[pi];
            float amp  = frontFrame.amplitude[pi];

            if (amp < 1e-6f || freq <= 0.0f)
                continue;

            float nx = std::log(freq / 20.0f) / std::log(20000.0f / 20.0f);
            float xPos = nx * 2.0f - 1.0f;
            float zPos = amp * 2.0f;

            glVertex3f(xPos, -1.0f, zPos);
        }
        glEnd();
    }

    // ====================================================================
    // 4. Selection box overlay
    // ====================================================================
    if (isBoxSelecting_ && !selectionBox_.isEmpty())
    {
        // Convert screen-space selection box to NDC for overlay
        float sx = (selectionBox_.getX() / static_cast<float>(w)) * 2.0f - 1.0f;
        float sy = (selectionBox_.getY() / static_cast<float>(h)) * -2.0f + 1.0f;
        float sx2 = ((selectionBox_.getX() + selectionBox_.getWidth()) / static_cast<float>(w)) * 2.0f - 1.0f;
        float sy2 = ((selectionBox_.getY() + selectionBox_.getHeight()) / static_cast<float>(h)) * -2.0f + 1.0f;

        // Switch to 2D ortho overlay
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glDisable(GL_DEPTH_TEST);

        glBegin(GL_LINE_LOOP);
        glColor4f(0.0f, 1.0f, 1.0f, 0.8f);
        glVertex2f(sx, sy);
        glVertex2f(sx2, sy);
        glVertex2f(sx2, sy2);
        glVertex2f(sx, sy2);
        glEnd();

        // Semi-transparent fill
        glBegin(GL_QUADS);
        glColor4f(0.0f, 1.0f, 1.0f, 0.1f);
        glVertex2f(sx, sy);
        glVertex2f(sx2, sy);
        glVertex2f(sx2, sy2);
        glVertex2f(sx, sy2);
        glEnd();

        glEnable(GL_DEPTH_TEST);

        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
    }
}

} // namespace ana
