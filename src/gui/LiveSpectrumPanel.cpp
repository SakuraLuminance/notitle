#include "LiveSpectrumPanel.h"
#include "CyberpunkTheme.h"
#include <cmath>

namespace ana
{

LiveSpectrumPanel::LiveSpectrumPanel()
{
    fftBuffer_.assign(static_cast<size_t>(fftSize) * 2, 0.0f);
    magnitudes_.assign(numBins, 0.0f);
    for (int i = 0; i < fftSize; ++i)
        hannWindow_[(size_t) i] = 0.5f * (1.0f - std::cos(
            juce::MathConstants<float>::twoPi * static_cast<float>(i)
            / static_cast<float>(fftSize - 1)));
    rebuildBarMap();
}

void LiveSpectrumPanel::resized()
{
    rebuildBarMap();
}

void LiveSpectrumPanel::rebuildBarMap()
{
    const int barCount = juce::jlimit(40, 64, getWidth() / 6);
    barLevels_.assign(static_cast<size_t>(barCount), 0.0f);
    peakLevels_.assign(static_cast<size_t>(barCount), 0.0f);
    barBinStart_.assign(static_cast<size_t>(barCount), 0);
    barBinEnd_.assign(static_cast<size_t>(barCount), 1);

    // Nominal 48 kHz bin width for axis mapping (labels are approximate at other rates)
    constexpr float binHz = 48000.0f / static_cast<float>(fftSize);
    constexpr float fMin = 20.0f;
    constexpr float fMax = 20000.0f;
    const float ratio = fMax / fMin;

    for (int b = 0; b < barCount; ++b)
    {
        const float fLo = fMin * std::pow(ratio, static_cast<float>(b) / barCount);
        const float fHi = fMin * std::pow(ratio, static_cast<float>(b + 1) / barCount);
        int lo = static_cast<int>(std::floor(fLo / binHz));
        int hi = static_cast<int>(std::ceil(fHi / binHz));
        lo = juce::jlimit(1, numBins - 1, lo);
        hi = juce::jlimit(lo + 1, numBins, hi);
        barBinStart_[(size_t) b] = lo;
        barBinEnd_[(size_t) b] = hi;
    }
}

void LiveSpectrumPanel::updateFromSamples(const float* data, int numSamples)
{
    if (data == nullptr || numSamples <= 0)
        return;

    // Window into the interleaved real-FFT buffer (scratch half stays zeroed)
    const int n = juce::jlimit(1, fftSize, numSamples);
    for (int i = 0; i < n; ++i)
        fftBuffer_[(size_t) i] = data[i] * hannWindow_[(size_t) i];
    for (int i = n; i < fftSize; ++i)
        fftBuffer_[(size_t) i] = 0.0f;

    fftEngine_.performRealOnlyForwardTransform(fftBuffer_.data(), true);
    for (int i = 0; i < numBins; ++i)
    {
        const float re = fftBuffer_[(size_t) (2 * i)];
        const float im = fftBuffer_[(size_t) (2 * i + 1)];
        magnitudes_[(size_t) i] = std::sqrt(re * re + im * im);
    }

    // Aggregate bins into log-spaced bars (max energy per band)
    const int barCount = static_cast<int>(barLevels_.size());
    for (int b = 0; b < barCount; ++b)
    {
        float peak = 0.0f;
        for (int i = barBinStart_[(size_t) b]; i < barBinEnd_[(size_t) b]; ++i)
            peak = juce::jmax(peak, magnitudes_[(size_t) i]);
        const float db = juce::Decibels::gainToDecibels(juce::jmax(peak, 1.0e-9f));
        barLevels_[(size_t) b] = juce::jlimit(0.0f, 1.0f,
            (db - kMinDb) / (kMaxDb - kMinDb));
    }

    // Peak hold with decay
    for (int b = 0; b < barCount; ++b)
    {
        float& pk = peakLevels_[(size_t) b];
        pk *= kPeakDecay;
        pk = juce::jmax(pk, barLevels_[(size_t) b]);
    }

    repaint();
}

void LiveSpectrumPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll(CyberpunkTheme::bg_);
    CyberpunkTheme::drawGridBackground(g, getLocalBounds());

    const int barCount = static_cast<int>(barLevels_.size());
    if (barCount == 0)
        return;

    const float axisHeight = 12.0f;
    const float plotHeight = bounds.getHeight() - axisHeight;
    const float barW = bounds.getWidth() / static_cast<float>(barCount);

    for (int b = 0; b < barCount; ++b)
    {
        const float t = juce::jlimit(0.0f, 1.0f, barLevels_[(size_t) b]);
        const float h = t * plotHeight;
        const float x = bounds.getX() + b * barW;

        juce::Colour barColour;
        if (t < 0.6f)
            barColour = CyberpunkTheme::fg_.darker(0.4f)
                .interpolatedWith(CyberpunkTheme::cyan_, t / 0.6f);
        else
            barColour = CyberpunkTheme::cyan_
                .interpolatedWith(CyberpunkTheme::magenta_, (t - 0.6f) / 0.4f);

        if (h > 0.5f)
        {
            g.setColour(barColour);
            g.fillRect(x + 0.5f, bounds.getY() + plotHeight - h, barW - 1.0f, h);
        }

        const float pk = juce::jlimit(0.0f, 1.0f, peakLevels_[(size_t) b]);
        if (pk > 0.01f)
        {
            g.setColour(barColour.withAlpha(0.8f));
            g.fillRect(x + 0.5f, bounds.getY() + plotHeight - pk * plotHeight - 2.0f,
                       barW - 1.0f, 2.0f);
        }
    }

    // Frequency axis ticks (nominal 48 kHz mapping)
    g.setFont(CyberpunkTheme::getCyberFont(8.0f, false));
    g.setColour(CyberpunkTheme::fg_.withAlpha(0.4f));
    const float logSpan = std::log10(20000.0f / 20.0f);
    for (const float tick : freqTickHz_)
    {
        const float frac = std::log10(tick / 20.0f) / logSpan;
        const float x = bounds.getX() + frac * bounds.getWidth();
        g.drawText(tick >= 1000.0f
                       ? juce::String(tick / 1000.0f, 0) + "k"
                       : juce::String(tick, 0),
                   juce::Rectangle<float>(x - 12.0f, bounds.getBottom() - axisHeight,
                                          24.0f, axisHeight),
                   juce::Justification::centred);
    }

    // Idle marker when everything is silent
    bool anySignal = false;
    for (const float lv : barLevels_)
        if (lv > 0.01f) { anySignal = true; break; }
    if (!anySignal)
    {
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.35f));
        g.setFont(CyberpunkTheme::getCyberFont(12.0f, false));
        g.drawText("-inf dB", getLocalBounds(),
                   juce::Justification::centred);
    }
}

} // namespace ana
