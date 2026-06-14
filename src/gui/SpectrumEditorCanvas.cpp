#include "SpectrumEditorCanvas.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace ana {

// ============================================================================
// Construction / Destruction
// ============================================================================
SpectrumEditorCanvas::SpectrumEditorCanvas()
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    startTimerHz(30); // 30 fps smooth animation
}

SpectrumEditorCanvas::~SpectrumEditorCanvas()
{
    stopTimer();
}

// ============================================================================
// Timer (30 fps smooth repaint during drag operations)
// ============================================================================
void SpectrumEditorCanvas::timerCallback()
{
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
void SpectrumEditorCanvas::setActiveTool(Tool tool)
{
    activeTool_ = tool;
}

SpectrumEditorCanvas::Tool SpectrumEditorCanvas::getActiveTool() const
{
    return activeTool_;
}

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

    // Save current state to redo stack
    redoStack_.push_back(partials_);

    // Restore previous state
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

    // Save current state to undo stack
    undoStack_.push_back(partials_);

    // Restore next state
    partials_ = redoStack_.back();
    redoStack_.pop_back();

    repaint();

    if (onPartialEdited)
        onPartialEdited(partials_);
}

bool SpectrumEditorCanvas::canUndo() const
{
    return !undoStack_.empty();
}

bool SpectrumEditorCanvas::canRedo() const
{
    return !redoStack_.empty();
}

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

    // Quadratic falloff: smooth at centre, zero at edges
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
                // Additive draw: push amplitude up toward mouse Y position
                float target = mouseAmp;
                float delta = (target - partials_.amplitude[i]) * brushStrength_ * falloff;
                if (delta > 0.0f)
                    partials_.amplitude[i] = std::clamp(partials_.amplitude[i] + delta, 0.0f, 1.0f);
                break;
            }

            case Tool::Eraser:
            {
                // Subtractive erase: push amplitude down
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
// Smooth: 3-point moving average around brush centre
// ============================================================================
void SpectrumEditorCanvas::applySmooth(int mx, int my)
{
    juce::ScopedLock sl(dataLock_);

    float centerFreq = xToFreq(mx);

    // Find nearest partial index to mouse frequency
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

    // Work on a copy to avoid sequential dependency
    auto smoothed = partials_.amplitude; // copies the array

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

    // Write back
    for (int i = start; i <= end; ++i)
        partials_.amplitude[i] = smoothed[i];

    partials_.updateActiveMask();
}

// ============================================================================
// Sharpen: enhance contrast (unsharp mask) around brush centre
// ============================================================================
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

    // Compute smoothed values first
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

    // Apply unsharp mask: original + strength * (original - smoothed)
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
// Line tool: draw a straight line between two (freq, amp) points
// ============================================================================
void SpectrumEditorCanvas::drawLineTo(float freqFrom, float ampFrom,
                                       float freqTo,   float ampTo)
{
    juce::ScopedLock sl(dataLock_);

    float minFreq = std::min(freqFrom, freqTo);
    float maxFreq = std::max(freqFrom, freqTo);

    if (maxFreq - minFreq < 1.0f)
    {
        // Single point: just set the nearest partial
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

        // Only affect partials within the line's frequency span
        if (f >= minFreq && f <= maxFreq)
        {
            // Linear interpolation of amplitude along frequency
            float t = (f - freqFrom) / (freqTo - freqFrom);
            float targetAmp = ampFrom + t * (ampTo - ampFrom);
            partials_.amplitude[i] = std::clamp(targetAmp, 0.0f, 1.0f);
        }
    }

    partials_.updateActiveMask();
}

// ============================================================================
// Rectangle tool: fill a frequency × amplitude region
// ============================================================================
void SpectrumEditorCanvas::fillRectTo(float freqFrom, float ampFrom,
                                       float freqTo,   float ampTo)
{
    juce::ScopedLock sl(dataLock_);

    float minFreq = std::min(freqFrom, freqTo);
    float maxFreq = std::max(freqFrom, freqTo);
    float fillAmp = std::max(ampFrom, ampTo); // fill with the top amplitude

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
// Mouse handling
// ============================================================================
void SpectrumEditorCanvas::mouseDown(const juce::MouseEvent& e)
{
    // Ignore clicks outside the canvas area
    auto area = getCanvasArea();
    if (!area.contains(e.getPosition()))
        return;

    // Check for any active partial; if none, ignore editing
    bool hasActive = false;
    for (int i = 0; i < partials_.maxPartials && !hasActive; ++i)
        if (partials_.isActive(i))
            hasActive = true;

    if (!hasActive)
        return;

    // Save undo state at the start of every stroke
    saveState();

    int mx = e.getPosition().x;
    int my = e.getPosition().y;

    isDragging_ = true;
    hasPendingStroke_ = false;

    float startFreq = xToFreq(mx);
    float startAmp  = yToAmp(my);

    // Store drag start for Line / Rectangle / general interpolation
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
            // Preview is drawn in paint(); no data modification until mouseUp
            repaint();
            break;

        default:
            break;
    }
}

void SpectrumEditorCanvas::mouseUp(const juce::MouseEvent& e)
{
    if (!isDragging_)
        return;

    auto area = getCanvasArea();
    int mx = std::clamp(e.getPosition().x, area.getX(), area.getRight());
    int my = std::clamp(e.getPosition().y, area.getY(), area.getBottom());

    dragEndFreq_ = xToFreq(mx);
    dragEndAmp_  = yToAmp(my);

    // Apply line / rectangle on mouse-up
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

    // Notify listeners
    if (onPartialEdited)
        onPartialEdited(partials_);
}

// ============================================================================
// Paint
// ============================================================================
void SpectrumEditorCanvas::paint(juce::Graphics& g)
{
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

        // Calculate bar width based on number of active partials
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

            // Clamp to canvas
            if (x > area.getRight() || x + barWidth < area.getX())
                continue;

            // Colour: brighter for higher amplitudes
            float alpha = 0.25f + 0.75f * amp;
            g.setColour(barColour_.withMultipliedAlpha(alpha));
            g.fillRect(x, y, barWidth, barHeight);
        }
    }

    // --- Grid lines ---
    if (showGrid_)
    {
        g.setColour(gridColour_);

        // Vertical lines (frequency divisions)
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

        // Horizontal lines (amplitude divisions)
        for (int i = 0; i <= gridLinesY_; ++i)
        {
            float amp = static_cast<float>(i) / static_cast<float>(gridLinesY_);
            int y = ampToY(amp);
            if (y >= area.getY() && y <= area.getBottom())
                g.drawHorizontalLine(y, static_cast<float>(area.getX()),
                                     static_cast<float>(area.getRight()));
        }
    }

    // --- Canvas border ---
    g.setColour(gridColour_.brighter(0.5f));
    g.drawRect(area, 1);

    // --- Labels ---
    if (showLabels_)
    {
        g.setFont(juce::Font(10.0f, juce::Font::plain));
        g.setColour(juce::Colours::lightgrey);

        // Frequency labels (bottom axis)
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

        // Amplitude labels (left axis)
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

        // Axis title
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

    // --- Tool preview (Line / Rectangle) ---
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

            // Draw end points
            g.fillEllipse(static_cast<float>(x1) - 3.0f,
                          static_cast<float>(y1) - 3.0f, 6.0f, 6.0f);
            g.fillEllipse(static_cast<float>(x2) - 3.0f,
                          static_cast<float>(y2) - 3.0f, 6.0f, 6.0f);
        }
        else // Rectangle
        {
            int rx = std::min(x1, x2);
            int ry = std::min(y1, y2);
            int rw = std::abs(x2 - x1);
            int rh = std::abs(y2 - y1);

            g.drawRect(rx, ry, rw, rh, 2.0f);

            // Semi-transparent fill
            g.setColour(toolColour_.withAlpha(0.15f));
            g.fillRect(rx, ry, rw, rh);
        }
    }
}

void SpectrumEditorCanvas::resized()
{
    repaint();
}

} // namespace ana
