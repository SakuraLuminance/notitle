#include "WaveformDisplay.h"

namespace ana {

WaveformDisplay::WaveformDisplay()
{
}

WaveformDisplay::~WaveformDisplay()
{
}

void WaveformDisplay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);

    if (samples.empty())
        return;

    auto area = getLocalBounds().toFloat();
    const float width = area.getWidth();
    const float height = area.getHeight();
    const float centerY = height / 2.0f;

    // Draw waveform
    g.setColour(juce::Colours::cyan);
    juce::Path waveformPath;

    const int numSamples = static_cast<int>(samples.size());
    const float samplesPerPixel = static_cast<float>(numSamples) / width;

    for (int x = 0; x < static_cast<int>(width); ++x)
    {
        int sampleIdx = static_cast<int>(x * samplesPerPixel);
        if (sampleIdx >= numSamples)
            sampleIdx = numSamples - 1;

        float sample = samples[sampleIdx];
        float y = centerY - (sample * centerY);

        if (x == 0)
            waveformPath.startNewSubPath(area.getX() + x, y);
        else
            waveformPath.lineTo(area.getX() + x, y);
    }

    g.strokePath(waveformPath, juce::PathStrokeType(1.0f));

    // Draw playback position cursor
    if (playbackPosition >= 0.0 && !samples.empty())
    {
        g.setColour(juce::Colours::red);
        float cursorX = static_cast<float>(playbackPosition / samples.size()) * width;
        g.drawLine(area.getX() + cursorX, area.getY(),
                   area.getX() + cursorX, area.getBottom(), 2.0f);
    }
}

void WaveformDisplay::resized()
{
}

void WaveformDisplay::setSamples(const std::vector<float>& newSamples)
{
    samples = newSamples;
    repaint();
}

void WaveformDisplay::setPlaybackPosition(double position)
{
    playbackPosition = position;
    repaint();
}

void WaveformDisplay::setSampleRate(double rate)
{
    sampleRate = rate;
}

} // namespace ana
