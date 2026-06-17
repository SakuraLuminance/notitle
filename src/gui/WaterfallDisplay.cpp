#include "WaterfallDisplay.h"
#include "CyberpunkTheme.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace ana {

//==============================================================================
// Helpers
//==============================================================================

static float magnitudeToNorm(float mag, float maxMag)
{
    if (maxMag < 1e-8f)
        return 0.0f;
    return juce::jlimit(0.0f, 1.0f, mag / maxMag);
}

//==============================================================================
WaterfallDisplay::WaterfallDisplay()
{
    history_.resize(static_cast<size_t>(maxHistory_));
    startTimerHz(30);
}

WaterfallDisplay::~WaterfallDisplay()
{
    stopTimer();
}

//==============================================================================
void WaterfallDisplay::updateSpectrum(const std::vector<float>& magnitudes)
{
    const juce::ScopedLock sl(lock_);
    currentSpectrum_ = magnitudes;
    hasData_ = true;
}

void WaterfallDisplay::updatePartials(const PartialDataSIMD& partials)
{
    // Map partials into frequency bins (0-20 kHz range)
    const int numBins = PartialDataSIMD::kMaxPartials;
    std::vector<float> magnitudes(static_cast<size_t>(numBins), 0.0f);

    for (int i = 0; i < numBins; ++i)
    {
        if (partials.isActive(i))
            magnitudes[static_cast<size_t>(i)] = partials.amplitude[i];
    }

    updateSpectrum(magnitudes);
}

//==============================================================================
void WaterfallDisplay::setPitch(float pitch)
{
    pitch_ = juce::jlimit(-90.0f, 90.0f, pitch);
    repaint();
}

void WaterfallDisplay::setYaw(float yaw)
{
    yaw_ = juce::jlimit(-180.0f, 180.0f, yaw);
    repaint();
}

void WaterfallDisplay::setZoom(float zoom)
{
    zoom_ = juce::jlimit(0.1f, 10.0f, zoom);
    repaint();
}

void WaterfallDisplay::setDecay(int frames)
{
    frames = juce::jlimit(kMinHistory, kMaxHistory, frames);

    const juce::ScopedLock sl(lock_);

    if (frames != maxHistory_)
    {
        std::vector<std::vector<float>> newHistory(
            static_cast<size_t>(frames),
            std::vector<float>(static_cast<size_t>(numBins_), 0.0f));

        // Preserve as many recent frames as possible
        const int validFrames = std::min(totalFramesWritten_, maxHistory_);
        const int copyCount = std::min(validFrames, frames);

        for (int i = 0; i < copyCount; ++i)
        {
            // Source: most recent frames in ring buffer order
            const int srcIdx = ((historyPos_ - 1 - i + maxHistory_) % maxHistory_);
            // Target: most recent frames at the end of new buffer
            const int dstIdx = (frames - 1 - i + frames) % frames;
            newHistory[static_cast<size_t>(dstIdx)] =
                history_[static_cast<size_t>(srcIdx)];
        }

        history_ = std::move(newHistory);
        maxHistory_ = frames;
        historyPos_ = 0;
        totalFramesWritten_ = copyCount;
    }

    repaint();
}

void WaterfallDisplay::setColourScheme(int scheme)
{
    colourScheme_ = juce::jlimit(0, 2, scheme);
    repaint();
}

//==============================================================================
void WaterfallDisplay::mouseDown(const juce::MouseEvent& e)
{
    lastMousePos_ = e.position;
    isDragging_ = true;
}

void WaterfallDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (!isDragging_)
        return;

    const auto delta = e.position - lastMousePos_;

    yaw_   = juce::jlimit(-180.0f, 180.0f, yaw_   + delta.x * 0.4f);
    pitch_ = juce::jlimit( -90.0f,  90.0f, pitch_ - delta.y * 0.4f);

    lastMousePos_ = e.position;
    repaint();
}

void WaterfallDisplay::mouseWheelMove(const juce::MouseEvent&,
                                       const juce::MouseWheelDetails& w)
{
    zoom_ = juce::jlimit(0.1f, 10.0f,
                         zoom_ * (w.deltaY > 0.0f ? 1.1f : 0.9f));
    repaint();
}

//==============================================================================
void WaterfallDisplay::paint(juce::Graphics& g)
{
    g.fillAll(CyberpunkTheme::bg_);

    auto bounds = getLocalBounds();

    if (!hasData_ || numBins_ < 2)
    {
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.5f));
        g.setFont(14.0f);
        g.drawText("No spectral data", bounds, juce::Justification::centred);
        return;
    }

    // Find global max magnitude for colour mapping
    float maxMag = 1e-8f;
    {
        const juce::ScopedLock sl(lock_);
        const int validFrames = std::min(totalFramesWritten_, maxHistory_);
        const int oldestIdx = (totalFramesWritten_ <= maxHistory_) ? 0 : historyPos_;

        for (int i = 0; i < validFrames; ++i)
        {
            const int idx = (oldestIdx + i) % maxHistory_;
            const auto& frame = history_[static_cast<size_t>(idx)];
            for (float v : frame)
            {
                if (v > maxMag)
                    maxMag = v;
            }
        }
    }

    // --- Render mesh: back to front (painter's algorithm) ---
    const int validFrames = std::min(totalFramesWritten_, maxHistory_);
    if (validFrames < 2)
    {
        // Not enough history yet — draw a single front ridge if we have data
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.5f));
        g.drawText("Accumulating history...", bounds, juce::Justification::centred);
        return;
    }

    const int oldestIdx = (totalFramesWritten_ <= maxHistory_) ? 0 : historyPos_;

    // Stride for rendering performance (skip none by default)
    const int binStride   = std::max(1, numBins_     / 128);
    const int frameStride = std::max(1, validFrames   / 64);

    // Draw mesh: connect adjacent bins x adjacent frames
    // Render from oldest (back) to newest (front) for painter's algorithm
    for (int fi = 0; fi < validFrames - frameStride; fi += frameStride)
    {
        const int fi1 = std::min(fi + frameStride, validFrames - 1);

        const int bufIdx0  = (oldestIdx + fi)  % maxHistory_;
        const int bufIdx1  = (oldestIdx + fi1) % maxHistory_;

        const auto& frame0 = history_[static_cast<size_t>(bufIdx0)];
        const auto& frame1 = history_[static_cast<size_t>(bufIdx1)];

        // Normalised depth: 0 at front (newest), 1 at back (oldest)
        const float z0 = static_cast<float>(validFrames - 1 - fi)
                       / static_cast<float>(validFrames - 1);
        const float z1 = static_cast<float>(validFrames - 1 - fi1)
                       / static_cast<float>(validFrames - 1);

        // Depth fade: older frames are dimmer
        const float alpha0 = 1.0f - z0 * 0.75f;
        const float alpha1 = 1.0f - z1 * 0.75f;

        for (int bi = 0; bi < numBins_ - binStride; bi += binStride)
        {
            const int bi1 = std::min(bi + binStride, numBins_ - 1);

            const float xBin0 = static_cast<float>(bi)
                              / static_cast<float>(numBins_ - 1) * 2.0f - 1.0f;
            const float xBin1 = static_cast<float>(bi1)
                              / static_cast<float>(numBins_ - 1) * 2.0f - 1.0f;

            const float y00 = frame0[static_cast<size_t>(bi)]  * 1.5f;
            const float y01 = frame0[static_cast<size_t>(bi1)] * 1.5f;
            const float y10 = frame1[static_cast<size_t>(bi)]  * 1.5f;
            const float y11 = frame1[static_cast<size_t>(bi1)] * 1.5f;

            // Skip quads that are completely flat (zero magnitude)
            if (y00 < 1e-6f && y01 < 1e-6f && y10 < 1e-6f && y11 < 1e-6f)
                continue;

            // Project the four corners of each quad
            float sx00, sy00, sx01, sy01, sx10, sy10, sx11, sy11;
            project3D(xBin0, y00, z0, sx00, sy00);
            project3D(xBin1, y01, z0, sx01, sy01);
            project3D(xBin0, y10, z1, sx10, sy10);
            project3D(xBin1, y11, z1, sx11, sy11);

            // Average magnitude for quad colour
            const float avgMag = (frame0[static_cast<size_t>(bi)]
                                + frame0[static_cast<size_t>(bi1)]
                                + frame1[static_cast<size_t>(bi)]
                                + frame1[static_cast<size_t>(bi1)]) * 0.25f;
            const auto fillColour = magnitudeColour(avgMag, maxMag)
                                        .withAlpha((alpha0 + alpha1) * 0.5f * 0.75f);
            const auto edgeColour = fillColour.brighter(0.4f).withAlpha(0.35f);

            // Draw filled quad
            juce::Path quad;
            quad.startNewSubPath(sx00, sy00);
            quad.lineTo(sx01, sy01);
            quad.lineTo(sx11, sy11);
            quad.lineTo(sx10, sy10);
            quad.closeSubPath();

            g.setColour(fillColour);
            g.fillPath(quad);

            // Wireframe edges
            g.setColour(edgeColour);
            g.strokePath(quad, juce::PathStrokeType(0.5f));
        }
    }

    // --- Highlight the front-most ridge ---
    {
        const int newestIdx = (oldestIdx + validFrames - 1) % maxHistory_;
        const auto& frontFrame = history_[static_cast<size_t>(newestIdx)];

        juce::Path frontRidge;
        bool started = false;

        for (int bi = 0; bi < numBins_; bi += binStride)
        {
            if (static_cast<size_t>(bi) >= frontFrame.size())
                continue;

            const float x = static_cast<float>(bi)
                          / static_cast<float>(numBins_ - 1) * 2.0f - 1.0f;
            const float y = frontFrame[static_cast<size_t>(bi)] * 1.5f;

            float sx, sy;
            project3D(x, y, 0.0f, sx, sy);

            if (!started)
            {
                frontRidge.startNewSubPath(sx, sy);
                started = true;
            }
            else
            {
                frontRidge.lineTo(sx, sy);
            }
        }

        if (started)
        {
            g.setColour(CyberpunkTheme::fg_.withAlpha(0.9f));
            g.strokePath(frontRidge, juce::PathStrokeType(2.0f));
        }
    }

    // --- Floor grid (base plane at y=0) ---
    {
        const int gridSteps = 8;
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.15f));

        // Radial lines from front to back
        for (int i = 0; i <= gridSteps; ++i)
        {
            const float x = static_cast<float>(i) / static_cast<float>(gridSteps) * 2.0f - 1.0f;

            float sx0, sy0, sx1, sy1;
            project3D(x, 0.0f, 0.0f, sx0, sy0);
            project3D(x, 0.0f, 1.0f, sx1, sy1);
            g.drawLine(sx0, sy0, sx1, sy1, 0.3f);
        }

        // Depth lines
        for (int i = 0; i <= 4; ++i)
        {
            const float z = static_cast<float>(i) / 4.0f;

            float sx0, sy0, sx1, sy1;
            project3D(-1.0f, 0.0f, z, sx0, sy0);
            project3D( 1.0f, 0.0f, z, sx1, sy1);
            g.drawLine(sx0, sy0, sx1, sy1, 0.3f);
        }
    }

    // --- Frequency labels along front edge ---
    {
        g.setFont(10.0f);
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.5f));

        static constexpr float labelPositions[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
#if JUCE_DEBUG
        static constexpr const char* labels[] = { "0", "5k", "10k", "15k", "20k" };
#else
        static constexpr const char* labels[] = { "0", "5k", "10k", "15k", "20k" };
#endif

        for (int i = 0; i < 5; ++i)
        {
            const float x = labelPositions[i] * 2.0f - 1.0f;
            float sx, sy;
            project3D(x, 0.0f, 0.0f, sx, sy);
            g.drawText(labels[i],
                       static_cast<int>(sx) - 14,
                       static_cast<int>(sy) + 3,
                       28, 12,
                       juce::Justification::centred);
        }
    }
}

void WaterfallDisplay::resized()
{
}

void WaterfallDisplay::timerCallback()
{
    // Grab latest spectrum from the audio thread
    std::vector<float> newSpectrum;
    {
        const juce::ScopedLock sl(lock_);
        if (!hasData_)
            return;
        newSpectrum = currentSpectrum_;
    }

    // Push into ring buffer
    if (!newSpectrum.empty())
    {
        numBins_ = static_cast<int>(newSpectrum.size());

        // Ensure ring buffer is sized correctly
        if (history_.size() != static_cast<size_t>(maxHistory_))
            history_.resize(static_cast<size_t>(maxHistory_));

        // Ensure each frame has the right bin count
        for (auto& frame : history_)
            if (static_cast<int>(frame.size()) != numBins_)
                frame.resize(static_cast<size_t>(numBins_), 0.0f);

        history_[static_cast<size_t>(historyPos_)] = std::move(newSpectrum);
        historyPos_ = (historyPos_ + 1) % maxHistory_;
        ++totalFramesWritten_;
    }

    repaint();
}

//==============================================================================
void WaterfallDisplay::project3D(float x, float y, float z,
                                  float& sx, float& sy) const
{
    // Input: x in [-1,1] (frequency), y >= 0 (magnitude), z in [0,1] (depth)
    //
    // 1. Pitch (rotate around X axis)
    const float pitchRad = pitch_ * juce::MathConstants<float>::pi / 180.0f;
    const float cosP = std::cos(pitchRad);
    const float sinP = std::sin(pitchRad);

    // 2. Yaw (rotate around Y axis)
    const float yawRad = yaw_ * juce::MathConstants<float>::pi / 180.0f;
    const float cosY = std::cos(yawRad);
    const float sinY = std::sin(yawRad);

    // Apply yaw first, then pitch
    // Yaw (Y rotation): x' = x*cosY + z*sinY, z' = -x*sinY + z*cosY
    const float xYaw = x * cosY + z * sinY;
    const float zYaw = -x * sinY + z * cosY;

    // Pitch (X rotation): y' = y*cosP - z*sinP, z' = y*sinP + z*cosP
    const float yPitch = y * cosP - zYaw * sinP;
    const float zPitch = y * sinP + zYaw * cosP;

    // Perspective projection: scale = zoom / (1 + z * depthFactor)
    const float depthFactor = 3.0f;
    const float scale = zoom_ / (1.0f + zPitch * depthFactor);

    const auto bounds = getLocalBounds().toFloat();
    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();
    const float halfW = bounds.getWidth()  * 0.45f;
    const float halfH = bounds.getHeight() * 0.45f;

    sx = cx + xYaw * scale * halfW;
    sy = cy - yPitch * scale * halfH;
}

//==============================================================================
juce::Colour WaterfallDisplay::magnitudeColour(float mag, float maxMag) const
{
    const float t = juce::jlimit(0.0f, 1.0f, (maxMag > 1e-8f) ? mag / maxMag : 0.0f);

    switch (colourScheme_)
    {
        case 1: // 1: Toxic Rainbow: green -> cyan -> blue -> white
        {
            const float hue = 0.30f + t * 0.30f;  // green (0.30) to blue (0.60)
            const float sat = t > 0.8f ? 1.0f - (t - 0.8f) * 5.0f : 1.0f;
            const float val = 0.2f + t * 0.8f;
            return juce::Colour::fromHSV(hue, sat, val, 1.0f);
        }

        case 2: // 2: Toxic Mono: dark green -> light green
        {
            const float hue = 0.33f;
            const float sat = 0.6f - t * 0.4f;
            const float val = t;
            return juce::Colour::fromHSV(hue, sat, val, 1.0f);
        }

        default: // 0: Toxic Fire: black -> dark green -> neon green -> white
        {
            if (t < 0.33f)
            {
                const float u = t / 0.33f;
                return juce::Colour::fromHSV(0.33f, 1.0f, u * 0.5f, 1.0f);
            }
            if (t < 0.66f)
            {
                const float u = (t - 0.33f) / 0.33f;
                return juce::Colour::fromHSV(0.33f + u * 0.05f, 1.0f, 0.5f + u * 0.5f, 1.0f);
            }
            const float u = (t - 0.66f) / 0.34f;
            return juce::Colour::fromHSV(0.38f, 1.0f - u, 1.0f, 1.0f);
        }
    }
}

} // namespace ana
