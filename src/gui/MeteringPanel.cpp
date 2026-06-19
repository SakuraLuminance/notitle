#include "MeteringPanel.h"
#include "../PluginProcessor.h"  // for MeteringEngine access
#include <cmath>

namespace ana {

//==============================================================================
MeteringPanel::MeteringPanel(AnaPlugAudioProcessor& proc)
    : processor_(proc)
{
    // Reset button — magenta accent, small
    resetButton_.setButtonText("R");
    resetButton_.setTooltip("Reset Integrated LUFS & LRA");
    resetButton_.onClick = [this]()
    {
        processor_.getMeteringEngine().reset();
    };
    addAndMakeVisible(resetButton_);

    startTimerHz(20);  // 50 ms refresh
}

MeteringPanel::~MeteringPanel()
{
    stopTimer();
}

//==============================================================================
void MeteringPanel::timerCallback()
{
    // Lock-free reads from MeteringEngine atomics
    const auto& eng = processor_.getMeteringEngine();

    const double m  = eng.getMomentaryLUFS();
    const double st = eng.getShortTermLUFS();
    const double i  = eng.getIntegratedLUFS();
    const double l  = eng.getLRA();
    const double tpL = eng.getTruePeak(0);
    const double tpR = eng.getTruePeak(1);

    // Clamp infinities to floor value
    momentaryLUFS_  = static_cast<float>(m  <= -HUGE_VAL / 2 ? -70.0f : m);
    shortTermLUFS_  = static_cast<float>(st <= -HUGE_VAL / 2 ? -70.0f : st);
    integratedLUFS_ = static_cast<float>(i  <= -HUGE_VAL / 2 ? -70.0f : i);
    lra_            = static_cast<float>(l  <= -HUGE_VAL / 2 ?  0.0f : l);
    truePeakL_      = static_cast<float>(tpL <= -HUGE_VAL / 2 ? -70.0f : tpL);
    truePeakR_      = static_cast<float>(tpR <= -HUGE_VAL / 2 ? -70.0f : tpR);

    repaint();
}

//==============================================================================
void MeteringPanel::resized()
{
    auto r = getLocalBounds().reduced(2);

    // Reserve right edge for reset button
    resetArea_ = r.removeFromRight(22).reduced(1);
    resetButton_.setBounds(resetArea_);

    // Reserve far-left for title "LUFS"
    titleArea_ = r.removeFromLeft(36);

    // Remaining width split into bar regions
    const int barW = static_cast<int>((r.getWidth() - 36) / 4);  // 4 zones

    // Separate top and bottom halves
    auto topHalf    = r.removeFromTop(r.getHeight() / 2);
    auto bottomHalf = r;

    // Top row: Momentary + Short-term + LRA
    momentaryBarArea_ = topHalf.removeFromLeft(barW).reduced(1, 0);
    shortTermBarArea_ = topHalf.removeFromLeft(barW).reduced(1, 0);

    // Remaining top is LRA + spacer
    lraArea_ = topHalf.reduced(1, 3);

    // Bottom row: Integrated + True-peak + spacer
    integratedBarArea_ = bottomHalf.removeFromLeft(barW).reduced(1, 0);
    truePeakArea_      = bottomHalf.removeFromLeft(barW).reduced(1, 0);
    // bottomHalf remainder is spacer (balances the layout)
}

//==============================================================================
void MeteringPanel::paint(juce::Graphics& g)
{
    // --- Panel background ---
    g.fillAll(CyberpunkTheme::bg_.withAlpha(0.85f));

    // Thin border glow
    auto bounds = getLocalBounds().toFloat();
    g.setColour(CyberpunkTheme::cyan_.withAlpha(0.15f));
    g.drawRoundedRectangle(bounds.reduced(1), 3.0f, 2.0f);
    g.setColour(CyberpunkTheme::cyan_.withAlpha(0.4f));
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    // --- Title ---
    {
        auto ta = titleArea_.toFloat();
        g.setFont(CyberpunkTheme::getCyberFont(11.0f, true));
        g.setColour(CyberpunkTheme::cyan_);
        g.drawText("LUFS", ta, juce::Justification::centred);
    }

    // --- Bars ---
    drawHorizontalLUFSBar(g, momentaryBarArea_,  momentaryLUFS_,  "M");
    drawHorizontalLUFSBar(g, shortTermBarArea_,  shortTermLUFS_,  "ST");
    drawHorizontalLUFSBar(g, integratedBarArea_, integratedLUFS_, "INT");
    drawTruePeakBars(g, truePeakArea_, truePeakL_, truePeakR_);
    drawLRA(g, lraArea_, lra_);
}

//==============================================================================
// Static helper: map LUFS value to normalised [0, 1] for bar fill
float MeteringPanel::lufsToNormalised(float lufs, float min, float max)
{
    return juce::jmap(juce::jlimit(min, max, lufs), min, max, 0.0f, 1.0f);
}

//==============================================================================
juce::Colour MeteringPanel::zoneColour(float lufs)
{
    // EBU R128 zones mapped to cyberpunk palette
    if (lufs > -9.0f)  return CyberpunkTheme::magenta_;   // danger (too loud)
    if (lufs > -18.0f) return CyberpunkTheme::yellow_;     // caution
    return CyberpunkTheme::cyan_;                           // safe / below target
}

//==============================================================================
void MeteringPanel::drawHorizontalLUFSBar(juce::Graphics& g,
                                           juce::Rectangle<int> bounds,
                                           float lufs,
                                           const juce::String& label,
                                           float rangeMin,
                                           float rangeMax)
{
    if (bounds.isEmpty())
        return;

    const auto b = bounds.toFloat();
    const float fill = lufsToNormalised(lufs, rangeMin, rangeMax);

    // --- Background track ---
    g.setColour(CyberpunkTheme::bg_.brighter(0.2f));
    g.fillRoundedRectangle(b, 2.0f);

    // --- Zone background strips (subtle) ---
    // Green zone: rangeMin → -18
    {
        const float gEnd = lufsToNormalised(-18.0f, rangeMin, rangeMax);
        auto zone = b.withWidth(b.getWidth() * gEnd);
        g.setColour(CyberpunkTheme::cyan_.withAlpha(0.08f));
        g.fillRoundedRectangle(zone, 2.0f);
    }
    // Yellow zone: -18 → -9
    {
        const float yStart = lufsToNormalised(-18.0f, rangeMin, rangeMax);
        const float yEnd   = lufsToNormalised(-9.0f, rangeMin, rangeMax);
        auto zone = b.withTrimmedLeft(b.getWidth() * yStart)
                     .withWidth(b.getWidth() * (yEnd - yStart));
        g.setColour(CyberpunkTheme::yellow_.withAlpha(0.08f));
        g.fillRoundedRectangle(zone, 2.0f);
    }
    // Red zone: -9 → rangeMax
    {
        const float rStart = lufsToNormalised(-9.0f, rangeMin, rangeMax);
        auto zone = b.withTrimmedLeft(b.getWidth() * rStart);
        g.setColour(CyberpunkTheme::magenta_.withAlpha(0.08f));
        g.fillRoundedRectangle(zone, 2.0f);
    }

    // --- Fill bar ---
    if (fill > 0.01f)
    {
        auto fillBounds = b.withWidth(b.getWidth() * fill);
        g.setColour(zoneColour(lufs).withAlpha(0.85f));
        g.fillRoundedRectangle(fillBounds, 2.0f);
    }

    // --- Edge glow on filled portion ---
    if (fill > 0.01f)
    {
        auto fillBounds = b.withWidth(b.getWidth() * fill);
        g.setColour(zoneColour(lufs).withAlpha(0.25f));
        g.drawRoundedRectangle(fillBounds.reduced(0.5f), 2.0f, 1.5f);
    }

    // --- Target marker at -23 LUFS ---
    {
        const float tPos = lufsToNormalised(-23.0f, rangeMin, rangeMax);
        const float tx = b.getX() + b.getWidth() * tPos;
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.5f));
        g.drawVerticalLine(static_cast<int>(tx),
                           static_cast<int>(b.getY()),
                           static_cast<int>(b.getBottom()));
    }

    // --- Numeric label below bar ---
    {
        g.setFont(CyberpunkTheme::getCyberFont(9.0f, false));
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.8f));

        juce::String txt = label + ":";
        if (lufs > -70.0f)
            txt += juce::String(lufs, 1);
        else
            txt += "--";

        g.drawText(txt, bounds.withTrimmedTop(bounds.getHeight() - 10),
                   juce::Justification::centred);
    }
}

//==============================================================================
void MeteringPanel::drawTruePeakBars(juce::Graphics& g,
                                      juce::Rectangle<int> bounds,
                                      float peakL,
                                      float peakR,
                                      float rangeMin,
                                      float rangeMax)
{
    if (bounds.getWidth() < 20)
        return;

    const auto area = bounds.toFloat();
    const int halfW = bounds.getWidth() / 2;

    auto drawTPBar = [&](juce::Rectangle<float> barArea, float dbTP,
                          const juce::String& chLabel)
    {
        const float fill = lufsToNormalised(dbTP, rangeMin, rangeMax);

        // Background
        g.setColour(CyberpunkTheme::bg_.brighter(0.2f));
        g.fillRoundedRectangle(barArea, 1.5f);

        // Red zone (above -9)
        {
            const float rStart = lufsToNormalised(-9.0f, rangeMin, rangeMax);
            auto zone = barArea.withTrimmedLeft(barArea.getWidth() * rStart);
            g.setColour(CyberpunkTheme::magenta_.withAlpha(0.10f));
            g.fillRoundedRectangle(zone, 1.5f);
        }

        // Fill
        if (fill > 0.01f)
        {
            auto fillBounds = barArea.withWidth(barArea.getWidth() * fill);
            // True-peak always uses magenta for danger aesthetic
            g.setColour(CyberpunkTheme::magenta_.withAlpha(0.8f));
            g.fillRoundedRectangle(fillBounds, 1.5f);
            g.setColour(CyberpunkTheme::magenta_.withAlpha(0.2f));
            g.drawRoundedRectangle(fillBounds.reduced(0.5f), 1.5f, 1.0f);
        }

        // Numeric label
        {
            g.setFont(CyberpunkTheme::getCyberFont(8.0f, false));
            g.setColour(CyberpunkTheme::fg_.withAlpha(0.7f));
            juce::String txt = chLabel + ":";
            if (dbTP > -70.0f)
                txt += juce::String(dbTP, 1);
            else
                txt += "--";
            g.drawText(txt, barArea.toNearestInt().withTrimmedTop(
                           barArea.toNearestInt().getHeight() - 9),
                       juce::Justification::centred);
        }
    };

    auto leftBar  = area.withWidth(static_cast<float>(halfW) - 1);
    auto rightBar = area.withLeft(area.getX() + static_cast<float>(halfW) + 1)
                        .withWidth(static_cast<float>(halfW) - 1);

    drawTPBar(leftBar,  peakL, "L");
    drawTPBar(rightBar, peakR, "R");
}

//==============================================================================
void MeteringPanel::drawLRA(juce::Graphics& g,
                             juce::Rectangle<int> bounds,
                             float lra)
{
    if (bounds.isEmpty())
        return;

    g.setFont(CyberpunkTheme::getCyberFont(10.0f, true));
    g.setColour(CyberpunkTheme::yellow_);
    g.drawText("LRA", bounds.removeFromTop(bounds.getHeight() / 2),
               juce::Justification::centred);

    g.setFont(CyberpunkTheme::getCyberFont(12.0f, true));
    g.setColour(CyberpunkTheme::fg_);
    juce::String txt = (lra > 0.0f) ? juce::String(lra, 1) : "--";
    g.drawText(txt, bounds, juce::Justification::centred);
}

} // namespace ana
