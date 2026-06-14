#include "VisualFeedbackPanel.h"
#include <cmath>

namespace ana {

//==============================================================================
VisualFeedbackPanel::VisualFeedbackPanel()
{
    peakLevels_.resize(static_cast<size_t>(PartialDataSIMD::kMaxPartials), 0.0f);
    startTimerHz(30); // 30 fps refresh
}

VisualFeedbackPanel::~VisualFeedbackPanel()
{
    stopTimer();
}

//==============================================================================
void VisualFeedbackPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    const int w = bounds.getWidth();
    const int h = bounds.getHeight();

    // 1. Background
    g.fillAll(bgColour_);

    if (displayPartials_.activeCount == 0)
    {
        g.setColour(envelopeColour_.withAlpha(0.3f));
        g.setFont(14.0f);
        g.drawText("No partials active", bounds,
                   juce::Justification::centred);
        return;
    }

    // 2. Partial bars (bottom layer)
    drawPartialBars(g, w, h);

    // 3. Peak hold (on top of bars)
    drawPeakHold(g, w, h);

    // 4. Spectral envelope (on top of peak hold)
    drawEnvelope(g, w, h);

    // 5. Status text (topmost)
    drawStatusText(g, w, h);
}

void VisualFeedbackPanel::resized()
{
    const auto neededSize = static_cast<size_t>(PartialDataSIMD::kMaxPartials);
    if (peakLevels_.size() != neededSize)
    {
        peakLevels_.resize(neededSize, 0.0f);
    }
}

void VisualFeedbackPanel::timerCallback()
{
    // Swap in the latest partial data from the audio thread
    {
        const juce::ScopedLock sl(dataLock);
        displayPartials_ = currentPartials_;
    }

    // Update peak levels from the current display data
    if (showPeakHold_)
    {
        for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
        {
            const float amp = displayPartials_.amplitude[i];
            if (amp > peakLevels_[static_cast<size_t>(i)])
                peakLevels_[static_cast<size_t>(i)] = amp;

            // Decay peak toward current level
            peakLevels_[static_cast<size_t>(i)] *= peakDecay_;

            // Floor at current amplitude so peak never sits below signal
            if (peakLevels_[static_cast<size_t>(i)] < amp)
                peakLevels_[static_cast<size_t>(i)] = amp;
        }
    }

    repaint();
}

//==============================================================================
void VisualFeedbackPanel::updatePartials(const PartialDataSIMD& partials)
{
    const juce::ScopedLock sl(dataLock);
    currentPartials_ = partials;
}

//==============================================================================
// Configuration setters
//==============================================================================
void VisualFeedbackPanel::setShowPeakHold(bool show)
{
    showPeakHold_ = show;
    if (!show)
        std::fill(peakLevels_.begin(), peakLevels_.end(), 0.0f);
}

void VisualFeedbackPanel::setPeakDecay(float decay)
{
    peakDecay_ = juce::jlimit(0.0f, 1.0f, decay);
}

void VisualFeedbackPanel::setUseLogFreq(bool useLog)
{
    useLogFreq_ = useLog;
}

void VisualFeedbackPanel::setAmplitudeRange(float max)
{
    ampRange_ = juce::jmax(0.001f, max);
}

//==============================================================================
// Colour setters
//==============================================================================
void VisualFeedbackPanel::setBarColour(juce::Colour colour)       { barColour_ = colour; }
void VisualFeedbackPanel::setEnvelopeColour(juce::Colour colour)  { envelopeColour_ = colour; }
void VisualFeedbackPanel::setPeakColour(juce::Colour colour)      { peakColour_ = colour; }
void VisualFeedbackPanel::setBackgroundColour(juce::Colour colour){ bgColour_ = colour; }

//==============================================================================
float VisualFeedbackPanel::freqToX(float freq, float width) const
{
    if (useLogFreq_ && freq > 0.0f)
    {
        const float norm = std::log(freq / minFreq_)
                         / std::log(maxFreq_ / minFreq_);
        return juce::jlimit(0.0f, width, norm * width);
    }

    return juce::jlimit(0.0f, width, (freq / maxFreq_) * width);
}

//==============================================================================
void VisualFeedbackPanel::drawPartialBars(juce::Graphics& g, int w, int h)
{
    constexpr float statusHeight = 20.0f;
    const float bottom = static_cast<float>(h) - statusHeight;
    const float topMargin = 4.0f;

    if (bottom <= topMargin)
        return;

    const float drawHeight = bottom - topMargin;
    const float barWidth = std::max(1.0f, static_cast<float>(w)
                                  / static_cast<float>(PartialDataSIMD::kMaxPartials));

    g.setColour(barColour_.withAlpha(0.85f));

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        if (!displayPartials_.isActive(i))
            continue;

        const float freq = displayPartials_.frequency[i];
        if (freq <= 0.0f || freq > maxFreq_)
            continue;

        const float amp = displayPartials_.amplitude[i] / ampRange_;
        const float clampedAmp = juce::jlimit(0.0f, 1.0f, amp);

        if (clampedAmp < 1e-6f)
            continue;

        // Map frequency to X position
        const float x = freqToX(freq, static_cast<float>(w));

        // Bar height proportional to amplitude
        const float barHeight = clampedAmp * drawHeight;
        const float y1 = bottom;
        const float y2 = bottom - barHeight;

        g.drawLine(x, y1, x, y2, barWidth * 0.7f);
    }
}

//==============================================================================
void VisualFeedbackPanel::drawEnvelope(juce::Graphics& g, int w, int h)
{
    constexpr float statusHeight = 20.0f;
    const float bottom = static_cast<float>(h) - statusHeight;
    const float topMargin = 4.0f;

    if (bottom <= topMargin)
        return;

    const float drawHeight = bottom - topMargin;

    juce::Path envelopePath;
    bool pathStarted = false;

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        if (!displayPartials_.isActive(i))
            continue;

        const float freq = displayPartials_.frequency[i];
        if (freq <= 0.0f || freq > maxFreq_)
            continue;

        const float amp = displayPartials_.amplitude[i] / ampRange_;
        const float clampedAmp = juce::jlimit(0.0f, 1.0f, amp);

        if (clampedAmp < 1e-6f)
            continue;

        const float x = freqToX(freq, static_cast<float>(w));
        const float y = bottom - clampedAmp * drawHeight;

        if (!pathStarted)
        {
            envelopePath.startNewSubPath(x, y);
            pathStarted = true;
        }
        else
        {
            envelopePath.lineTo(x, y);
        }
    }

    if (pathStarted)
    {
        g.setColour(envelopeColour_.withAlpha(0.7f));
        g.strokePath(envelopePath, juce::PathStrokeType(1.5f));
    }
}

//==============================================================================
void VisualFeedbackPanel::drawPeakHold(juce::Graphics& g, int w, int h)
{
    if (!showPeakHold_)
        return;

    constexpr float statusHeight = 20.0f;
    const float bottom = static_cast<float>(h) - statusHeight;
    const float topMargin = 4.0f;

    if (bottom <= topMargin)
        return;

    const float drawHeight = bottom - topMargin;

    g.setColour(peakColour_.withAlpha(0.8f));

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        if (!displayPartials_.isActive(i))
            continue;

        const float freq = displayPartials_.frequency[i];
        if (freq <= 0.0f || freq > maxFreq_)
            continue;

        const float peak = peakLevels_[static_cast<size_t>(i)];
        if (peak <= 1e-6f)
            continue;

        const float x = freqToX(freq, static_cast<float>(w));
        const float y = bottom - juce::jlimit(0.0f, 1.0f, peak) * drawHeight;

        // Draw a small horizontal tick at the peak level
        g.drawLine(x - 2.0f, y, x + 2.0f, y, 2.0f);
    }
}

//==============================================================================
void VisualFeedbackPanel::drawStatusText(juce::Graphics& g, int w, int h)
{
    g.setColour(juce::Colours::lightgrey.withAlpha(0.6f));
    g.setFont(11.0f);

    const int activeCount = displayPartials_.activeCount;
    const int usedPartials = displayPartials_.maxPartials;

    juce::String text = juce::String(activeCount)
                        + " / "
                        + juce::String(usedPartials)
                        + " partials active";

    g.drawText(text, 6, 3, w - 12, 16, juce::Justification::topLeft);
}

} // namespace ana
