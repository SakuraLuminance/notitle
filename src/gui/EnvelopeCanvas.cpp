#include "EnvelopeCanvas.h"
#include "../dsp/EnvelopeEditOps.h"

#include <cmath>

namespace ana
{

namespace
{
const char* loopModeName(LoopMode mode)
{
    switch (mode)
    {
        case LoopMode::Forward:  return "LOOP FWD";
        case LoopMode::PingPong: return "LOOP PP";
        case LoopMode::Sustain:  return "SUSTAIN";
        default:                 return "ONCE";
    }
}

int nearestIndex(const MultiPointEnvelope& env, float x, juce::Rectangle<float> area)
{
    int best = 0;
    float bestDist = 1.0e9f;

    for (int i = 0; i < env.getNumBreakpoints(); ++i)
    {
        const float d = std::abs(
            EnvelopeEditOps::timeToX(env, env.getBreakpoint(i).time, area) - x);
        if (d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }

    return best;
}
}

//==============================================================================
EnvelopeCanvas::EnvelopeCanvas()
{
    setWantsKeyboardFocus(false);
}

void EnvelopeCanvas::setEnvelope(MultiPointEnvelope* env)
{
    env_ = env;
    dragPoint_ = -1;
    dragMarker_ = -1;
    hoverPoint_ = -1;
    repaint();
}

void EnvelopeCanvas::setPlayhead(double time, float value)
{
    playheadTime_ = time;
    playheadValue_ = value;
    repaint();
}

void EnvelopeCanvas::setReadoutVisible(bool shouldShow)
{
    readoutVisible_ = shouldShow;
    repaint();
}

juce::Rectangle<float> EnvelopeCanvas::getPlotArea() const
{
    return getLocalBounds().toFloat().reduced(10.0f, 12.0f);
}

void EnvelopeCanvas::notifyEdited()
{
    if (onEdited)
        onEdited();
}

std::unique_ptr<juce::SpinLock::ScopedLockType> EnvelopeCanvas::lockEdit() const
{
    if (editLock_ != nullptr)
        return std::make_unique<juce::SpinLock::ScopedLockType>(*editLock_);

    return nullptr;
}

//==============================================================================
void EnvelopeCanvas::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour(CyberpunkTheme::bg_.darker(0.25f));
    g.fillRect(bounds);

    g.setColour(CyberpunkTheme::fg_.withAlpha(0.25f));
    g.drawRect(bounds, 1.0f);

    const auto plot = getPlotArea();
    if (plot.getWidth() <= 1.0f || plot.getHeight() <= 1.0f)
        return;

    // --- Grid ---
    g.setColour(CyberpunkTheme::fg_.withAlpha(0.10f));
    for (int i = 0; i <= 4; ++i)
    {
        const float y = plot.getY() + plot.getHeight() * static_cast<float>(i) / 4.0f;
        g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
    }
    for (int i = 0; i <= 8; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * static_cast<float>(i) / 8.0f;
        g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
    }

    g.setColour(CyberpunkTheme::fg_.withAlpha(0.35f));
    g.drawRect(plot, 1.0f);

    const int n = (env_ != nullptr) ? env_->getNumBreakpoints() : 0;

    if (n < 2)
    {
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.5f));
        g.setFont(CyberpunkTheme::getCyberFont(14.0f));
        g.drawText("NO ENVELOPE", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // --- Loop markers ---
    {
        const int ls = juce::jlimit(0, n - 1, env_->getLoopStart());
        const float xls = EnvelopeEditOps::timeToX(*env_, env_->getBreakpoint(ls).time, plot);
        g.setColour(CyberpunkTheme::cyan_.withAlpha(0.55f));
        g.drawLine(xls, plot.getY(), xls, plot.getBottom(), 1.0f);
        g.setFont(CyberpunkTheme::getCyberFont(8.0f));
        g.drawText("LS", juce::Rectangle<float>(xls - 9.0f, plot.getY() + 1.0f, 18.0f, 9.0f),
                   juce::Justification::centred);

        const int le = env_->getLoopEnd();
        if (le >= 0 && le < n)
        {
            const float xle = EnvelopeEditOps::timeToX(*env_, env_->getBreakpoint(le).time, plot);
            g.setColour(CyberpunkTheme::magenta_.withAlpha(0.75f));
            g.drawLine(xle, plot.getY(), xle, plot.getBottom(), 1.0f);
            g.drawText("HOLD", juce::Rectangle<float>(xle - 16.0f, plot.getY() + 1.0f, 32.0f, 9.0f),
                       juce::Justification::centred);
        }
    }

    // --- Curve ---
    {
        juce::Path& curve = curvePath_;
        curve.clear();

        for (int i = 0; i < n - 1; ++i)
        {
            const auto a = env_->getBreakpoint(i);
            const auto b = env_->getBreakpoint(i + 1);
            const float x0 = EnvelopeEditOps::timeToX(*env_, a.time, plot);
            const float x1 = EnvelopeEditOps::timeToX(*env_, b.time, plot);

            if (i == 0)
                curve.startNewSubPath(x0, EnvelopeEditOps::valueToY(a.value, plot));

            constexpr int steps = 24;
            for (int s = 1; s <= steps; ++s)
            {
                const float t = static_cast<float>(s) / static_cast<float>(steps);
                const float v = MultiPointEnvelope::interpolateValue(a.value, b.value, t, b.curve);
                curve.lineTo(x0 + (x1 - x0) * t, EnvelopeEditOps::valueToY(v, plot));
            }
        }

        juce::Path& filled = fillPath_;
        filled.clear();
        filled.addPath(curve);
        filled.lineTo(plot.getRight(), plot.getBottom());
        filled.lineTo(plot.getX(), plot.getBottom());
        filled.closeSubPath();
        g.setColour(CyberpunkTheme::cyan_.withAlpha(0.12f));
        g.fillPath(filled);

        g.setColour(CyberpunkTheme::cyan_);
        g.strokePath(curve, juce::PathStrokeType(1.6f));
    }

    // --- Breakpoints ---
    for (int i = 0; i < n; ++i)
    {
        const auto bp = env_->getBreakpoint(i);
        const float x = EnvelopeEditOps::timeToX(*env_, bp.time, plot);
        const float y = EnvelopeEditOps::valueToY(bp.value, plot);
        const bool hot = (i == hoverPoint_ || i == dragPoint_);
        const float r = hot ? 5.5f : 4.0f;

        g.setColour(i == dragPoint_ ? juce::Colours::white : CyberpunkTheme::yellow_);
        g.fillEllipse(x - r, y - r, r * 2.0f, r * 2.0f);
        g.setColour(CyberpunkTheme::bg_);
        g.drawEllipse(x - r, y - r, r * 2.0f, r * 2.0f, 1.0f);
    }

    // --- Playhead ---
    if (playheadTime_ >= 0.0)
    {
        const float x = EnvelopeEditOps::timeToX(*env_, static_cast<float>(playheadTime_), plot);
        const float y = EnvelopeEditOps::valueToY(playheadValue_, plot);
        g.setColour(CyberpunkTheme::yellow_.withAlpha(0.7f));
        g.drawLine(x, plot.getY(), x, plot.getBottom(), 1.0f);
        g.fillEllipse(x - 3.0f, y - 3.0f, 6.0f, 6.0f);
    }

    // --- Readouts ---
    g.setFont(CyberpunkTheme::getCyberFont(9.5f));

    if (readoutVisible_)
    {
        const auto adsr = EnvelopeEditOps::deriveADSR(*env_);
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.85f));
        g.drawText(juce::String::formatted("A %.3fs  D %.3fs  S %.2f  R %.3fs",
                                           adsr.attack, adsr.decay, adsr.sustain, adsr.release),
                   getLocalBounds().reduced(12, 3), juce::Justification::topLeft);
    }

    g.setColour(CyberpunkTheme::fg_.withAlpha(0.6f));
    g.drawText(env_->getSyncMode() ? juce::String(loopModeName(env_->getLoopMode())) + "  SYNC"
                                   : loopModeName(env_->getLoopMode()),
               getLocalBounds().reduced(12, 3), juce::Justification::topRight);

    if (hoverPoint_ >= 0 && hoverPoint_ < n)
    {
        const auto bp = env_->getBreakpoint(hoverPoint_);
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.7f));
        g.drawText(juce::String::formatted("%.3f / %.2f", bp.time, bp.value),
                   getLocalBounds().reduced(12, 3), juce::Justification::bottomRight);
    }
}

//==============================================================================
void EnvelopeCanvas::mouseDown(const juce::MouseEvent& e)
{
    if (env_ == nullptr)
        return;

    const auto plot = getPlotArea();
    mousePos_ = e.position;

    const int pointIndex = EnvelopeEditOps::hitTestPoint(*env_, e.position, plot, 9.0f);

    if (e.mods.isPopupMenu())
    {
        showContextMenu(e.getScreenPosition(), pointIndex);
        return;
    }

    if (pointIndex >= 0)
    {
        dragPoint_ = pointIndex;
        dragMarker_ = -1;
    }
    else
    {
        dragPoint_ = -1;
        dragMarker_ = EnvelopeEditOps::hitTestMarker(*env_, e.position, plot, 7.0f);
    }

    repaint();
}

void EnvelopeCanvas::mouseDrag(const juce::MouseEvent& e)
{
    if (env_ == nullptr)
        return;

    const auto plot = getPlotArea();
    mousePos_ = e.position;
    const int n = env_->getNumBreakpoints();

    const bool edited = (dragPoint_ >= 0 && dragPoint_ < n) || (dragMarker_ >= 0);

    if (edited)
    {
        auto guard = lockEdit();

        if (dragPoint_ >= 0 && dragPoint_ < n)
        {
            EnvelopeEditOps::movePoint(*env_, dragPoint_,
                                       EnvelopeEditOps::xToTime(*env_, e.position.x, plot),
                                       EnvelopeEditOps::yToValue(e.position.y, plot));
        }
        else
        {
            const int idx = nearestIndex(*env_, e.position.x, plot);

            if (dragMarker_ == 0)
            {
                const int limit = (env_->getLoopEnd() >= 0) ? env_->getLoopEnd() : idx;
                env_->setLoopStart(juce::jmin(idx, limit));
            }
            else
            {
                env_->setLoopEnd(juce::jmax(idx, env_->getLoopStart()));
            }
        }

        guard.reset();
        notifyEdited();
    }

    repaint();
}

void EnvelopeCanvas::mouseUp(const juce::MouseEvent&)
{
    dragPoint_ = -1;
    dragMarker_ = -1;
    repaint();
}

void EnvelopeCanvas::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (env_ == nullptr)
        return;

    const auto plot = getPlotArea();
    if (EnvelopeEditOps::hitTestPoint(*env_, e.position, plot, 9.0f) >= 0)
        return;

    {
        auto guard = lockEdit();
        EnvelopeEditOps::addPoint(*env_,
                                  EnvelopeEditOps::xToTime(*env_, e.position.x, plot),
                                  EnvelopeEditOps::yToValue(e.position.y, plot),
                                  CurveType::Linear);
    }
    notifyEdited();
    repaint();
}

void EnvelopeCanvas::mouseMove(const juce::MouseEvent& e)
{
    if (env_ == nullptr)
        return;

    mousePos_ = e.position;
    hoverPoint_ = EnvelopeEditOps::hitTestPoint(*env_, e.position, getPlotArea(), 9.0f);
    repaint();
}

void EnvelopeCanvas::mouseExit(const juce::MouseEvent&)
{
    hoverPoint_ = -1;
    repaint();
}

//==============================================================================
void EnvelopeCanvas::showContextMenu(juce::Point<int> screenPos, int pointIndex)
{
    if (env_ == nullptr)
        return;

    juce::PopupMenu menu;

    if (pointIndex >= 0)
    {
        const auto curve = env_->getBreakpoint(pointIndex).curve;
        menu.addItem(1, "Curve: Linear",      true, curve == CurveType::Linear);
        menu.addItem(2, "Curve: Exponential", true, curve == CurveType::Exponential);
        menu.addItem(3, "Curve: S-Curve",     true, curve == CurveType::SCurve);
        menu.addSeparator();
        menu.addItem(4, "Delete point");
    }
    else
    {
        menu.addItem(5, "Add point here");
    }

    const auto clickPos = mousePos_;
    const auto plot = getPlotArea();

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [this, pointIndex, clickPos, plot](int result)
        {
            if (env_ == nullptr || result == 0)
                return;

            {
                auto guard = lockEdit();

                if (result == 1)      env_->setBreakpointCurve(pointIndex, CurveType::Linear);
                else if (result == 2) env_->setBreakpointCurve(pointIndex, CurveType::Exponential);
                else if (result == 3) env_->setBreakpointCurve(pointIndex, CurveType::SCurve);
                else if (result == 4) EnvelopeEditOps::removePoint(*env_, pointIndex);
                else if (result == 5)
                    EnvelopeEditOps::addPoint(*env_,
                                              EnvelopeEditOps::xToTime(*env_, clickPos.x, plot),
                                              EnvelopeEditOps::yToValue(clickPos.y, plot),
                                              CurveType::Linear);
            }

            notifyEdited();
            repaint();
        });
}

} // namespace ana
