#pragma once

#include "CyberpunkTheme.h"
#include "../dsp/MultiPointEnvelope.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace ana
{

//==============================================================================
/**
    Editable multi-point envelope canvas.

    - drag breakpoints / double-click empty space to add / right-click to delete
      a point or change its segment curve
    - drag the LOOP START and LOOP END (= sustain HOLD) markers
    - optional live playhead fed by the processor

    All edits go through `EnvelopeEditOps`, so clamping rules stay testable.
*/
class EnvelopeCanvas : public juce::Component
{
public:
    EnvelopeCanvas();

    /** Binds the envelope to edit.  Does not take ownership. */
    void setEnvelope(MultiPointEnvelope* env);

    MultiPointEnvelope* getEnvelope() const noexcept { return env_; }

    /** Live playhead position (axis units) and value, published by the host. */
    void setPlayhead(double time, float value);

    /** Optional lock held while mutating the envelope (see PluginProcessor). */
    void setEditLock(juce::SpinLock* lock) noexcept { editLock_ = lock; }

    void setReadoutVisible(bool shouldShow);

    void paint(juce::Graphics&) override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    /** Called after any edit that changes breakpoints or loop markers. */
    std::function<void()> onEdited;

private:
    juce::Rectangle<float> getPlotArea() const;
    void showContextMenu(juce::Point<int> screenPos, int pointIndex);
    void notifyEdited();
    std::unique_ptr<juce::SpinLock::ScopedLockType> lockEdit() const;

    MultiPointEnvelope* env_ = nullptr;
    juce::SpinLock* editLock_ = nullptr;

    int dragPoint_  = -1;   // breakpoint index being dragged
    int dragMarker_ = -1;   // 0 = LOOP START, 1 = LOOP END / HOLD
    int hoverPoint_ = -1;

    double playheadTime_ = -1.0;
    float  playheadValue_ = 0.0f;
    bool   readoutVisible_ = true;

    juce::Point<float> mousePos_;

    // Reused by paint() so repaints do not allocate path storage.
    juce::Path curvePath_;
    juce::Path fillPath_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnvelopeCanvas)
};

} // namespace ana
