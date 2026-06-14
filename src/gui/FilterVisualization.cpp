#include "FilterVisualization.h"
#include <cmath>

namespace ana {

static const juce::Colour filterColours[] = {
    juce::Colours::blue,
    juce::Colours::red,
    juce::Colours::green,
    juce::Colours::yellow,
    juce::Colours::cyan,
    juce::Colours::magenta,
    juce::Colours::orange,
    juce::Colours::white
};

FilterVisualization::FilterVisualization()
{
    startTimerHz(15);
}

FilterVisualization::~FilterVisualization()
{
    stopTimer();
}

void FilterVisualization::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    auto area = getLocalBounds().toFloat();
    const float width = area.getWidth();
    const float height = area.getHeight();

    // Draw grid
    g.setColour(juce::Colours::darkgrey.withAlpha(0.3f));

    // Frequency grid lines (log scale)
    for (float freq = 100.0f; freq <= 10000.0f; freq *= 10.0f)
    {
        for (float f = freq; f < freq * 10.0f; f += freq)
        {
            float x = area.getX() + (std::log10(f / minFreq) / std::log10(maxFreq / minFreq)) * width;
            g.drawLine(x, area.getY(), x, area.getBottom(), 0.5f);
        }
    }

    // dB grid lines
    for (float db = -20.0f; db <= 20.0f; db += 6.0f)
    {
        float y = area.getBottom() - ((db - minDb) / (maxDb - minDb)) * height;
        g.drawLine(area.getX(), y, area.getRight(), y, 0.5f);
    }

    // Draw 0 dB line
    g.setColour(juce::Colours::grey.withAlpha(0.5f));
    float zeroY = area.getBottom() - ((0.0f - minDb) / (maxDb - minDb)) * height;
    g.drawLine(area.getX(), zeroY, area.getRight(), zeroY, 1.0f);

    // Draw each filter response
    for (size_t fi = 0; fi < filterStates.size(); ++fi)
    {
        const auto& state = filterStates[fi];
        juce::Path path;
        bool first = true;

        for (int px = 0; px < static_cast<int>(width); ++px)
        {
            float freq = minFreq * std::pow(maxFreq / minFreq, px / width);
            float mag = getMagnitudeAtFrequency(freq, state);
            float db = 20.0f * std::log10(mag + 1e-10f);
            db = std::max(minDb, std::min(maxDb, db));

            float x = area.getX() + px;
            float y = area.getBottom() - ((db - minDb) / (maxDb - minDb)) * height;

            if (first)
            {
                path.startNewSubPath(x, y);
                first = false;
            }
            else
            {
                path.lineTo(x, y);
            }
        }

        g.setColour(state.colour.withAlpha(0.8f));
        g.strokePath(path, juce::PathStrokeType(2.0f));
    }

    // Draw combined response
    if (filterStates.size() > 1)
    {
        juce::Path combinedPath;
        bool first = true;

        for (int px = 0; px < static_cast<int>(width); ++px)
        {
            float freq = minFreq * std::pow(maxFreq / minFreq, px / width);
            float combinedMag = 1.0f;

            for (const auto& state : filterStates)
                combinedMag *= getMagnitudeAtFrequency(freq, state);

            float db = 20.0f * std::log10(combinedMag + 1e-10f);
            db = std::max(minDb, std::min(maxDb, db));

            float x = area.getX() + px;
            float y = area.getBottom() - ((db - minDb) / (maxDb - minDb)) * height;

            if (first)
            {
                combinedPath.startNewSubPath(x, y);
                first = false;
            }
            else
            {
                combinedPath.lineTo(x, y);
            }
        }

        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.strokePath(combinedPath, juce::PathStrokeType(3.0f));
    }

    // Draw frequency labels
    g.setColour(juce::Colours::grey);
    g.setFont(10.0f);
    const float freqLabels[] = { 100.0f, 1000.0f, 10000.0f };
    for (float freq : freqLabels)
    {
        float x = area.getX() + (std::log10(freq / minFreq) / std::log10(maxFreq / minFreq)) * width;
        juce::String label = (freq >= 1000.0f) ? juce::String(freq / 1000.0f, 1) + "k"
                                                : juce::String(static_cast<int>(freq));
        g.drawText(label, static_cast<int>(x) - 15, static_cast<int>(area.getBottom()) - 15,
                   30, 15, juce::Justification::centred);
    }
}

void FilterVisualization::resized() {}

void FilterVisualization::timerCallback() { repaint(); }

void FilterVisualization::setNumFilters(int num)
{
    filterStates.resize(static_cast<size_t>(num));
    for (size_t i = 0; i < filterStates.size(); ++i)
        filterStates[i].colour = filterColours[i % 8];
}

void FilterVisualization::setFilterCoefficients(int filterIndex,
                                                  const std::array<double, 5>& coeffs,
                                                  double sampleRate)
{
    if (filterIndex >= 0 && filterIndex < static_cast<int>(filterStates.size()))
    {
        filterStates[filterIndex].coefficients = coeffs;
        filterStates[filterIndex].sampleRate = sampleRate;
    }
}

void FilterVisualization::clear()
{
    filterStates.clear();
}

float FilterVisualization::getMagnitudeAtFrequency(double frequency, const FilterState& state) const
{
    const auto& c = state.coefficients;
    const double w = 2.0 * juce::MathConstants<double>::pi * frequency / state.sampleRate;
    const double cosW = std::cos(w);
    const double sinW = std::sin(w);
    const double cos2W = std::cos(2.0 * w);
    const double sin2W = std::sin(2.0 * w);

    // Full biquad: H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
    const double b0 = c[0], b1 = c[1], b2 = c[2];
    const double a1 = c[3], a2 = c[4];

    const double numReal = b0 + b1 * cosW + b2 * cos2W;
    const double numImag = -b1 * sinW - b2 * sin2W;
    const double denReal = 1.0 + a1 * cosW + a2 * cos2W;
    const double denImag = -a1 * sinW - a2 * sin2W;

    const double numMag = numReal * numReal + numImag * numImag;
    const double denMag = denReal * denReal + denImag * denImag;

    return static_cast<float>(std::sqrt(numMag / (denMag + 1e-20)));
}

} // namespace ana
