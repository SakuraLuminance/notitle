#include "ImageSynthesizer.h"
#include <cmath>
#include <algorithm>

namespace ana {

// ============================================================================
// Construction / Destruction
// ============================================================================

ImageSynthesizer::ImageSynthesizer()  = default;
ImageSynthesizer::~ImageSynthesizer() = default;

// ============================================================================
// Image Loading
// ============================================================================

bool ImageSynthesizer::loadImage(const juce::File& file)
{
    auto image = juce::ImageFileFormat::loadFrom(file);
    if (image.isNull())
        return false;

    image = convertToGrayscale(image);
    image = normalizeImage(image);

    gainPlane     = image;
    hasGainPlane  = true;
    return true;
}

void ImageSynthesizer::setGainPlane(const juce::Image& image)
{
    if (image.isNull())
        return;

    gainPlane    = normalizeImage(convertToGrayscale(image));
    hasGainPlane = true;
}

void ImageSynthesizer::setPitchPlane(const juce::Image& image)
{
    if (image.isNull())
        return;

    pitchPlane    = normalizeImage(convertToGrayscale(image));
    hasPitchPlane = true;
}

// ============================================================================
// Data Access
// ============================================================================

PartialData ImageSynthesizer::getPartialData() const
{
    PartialData data;
    data.sampleRate = sampleRate;
    data.hopSize    = 512.0;

    if (!hasGainPlane)
        return data;

    const int width  = gainPlane.getWidth();
    const int height = gainPlane.getHeight();

    if (width < 1 || height < 1)
        return data;

    data.maxPartials = height;
    data.frames.reserve(static_cast<std::size_t>(width));

    const int pitchHeight = hasPitchPlane ? pitchPlane.getHeight() : height;

    for (int x = 0; x < width; ++x)
    {
        PartialFrame frame;
        frame.timestamp = static_cast<double>(x) * data.hopSize / data.sampleRate;
        frame.partials.reserve(static_cast<std::size_t>(height));

        for (int y = 0; y < height; ++y)
        {
            const float brightness = getBrightness(gainPlane, x, y);

            // Skip silent / near-black pixels
            if (brightness < 0.001f)
                continue;

            Partial partial;

            // ── Frequency mapping ────────────────────────────────────────
            // Pixel row 0 (top of image)     → maxFrequency  (high)
            // Pixel row height-1 (bottom)    → minFrequency  (low)
            // Logarithmic scale between minFrequency and maxFrequency.
            float normalisedY;
            if (height > 1)
                normalisedY = static_cast<float>(height - 1 - y)
                            / static_cast<float>(height - 1);
            else
                normalisedY = 0.5f;   // single row → middle of range

            partial.frequency = minFrequency
                              * std::pow(maxFrequency / minFrequency,
                                         normalisedY);

            // ── Pitch plane deviation ────────────────────────────────────
            if (hasPitchPlane)
            {
                const int py        = std::min(y, pitchHeight - 1);
                const float pbright = getBrightness(pitchPlane, x, py);

                // Brightness 0.5  → 0 semitones (no shift)
                // Brightness 0.0  → -range semitones
                // Brightness 1.0  → +range semitones
                const float deviationSemitones =
                    (pbright - 0.5f) * 2.0f * pitchDeviationSemitones;

                partial.frequency *= std::pow(2.0f,
                                              deviationSemitones / 12.0f);
            }

            // ── Amplitude ────────────────────────────────────────────────
            partial.amplitude = brightness;
            partial.phase     = 0.0f;

            frame.partials.push_back(partial);
        }

        data.frames.push_back(std::move(frame));
    }

    return data;
}

int ImageSynthesizer::getWidth() const
{
    return hasGainPlane ? gainPlane.getWidth() : 0;
}

int ImageSynthesizer::getHeight() const
{
    return hasGainPlane ? gainPlane.getHeight() : 0;
}

int ImageSynthesizer::getNumFrames() const
{
    return getWidth();
}

int ImageSynthesizer::getNumPartials() const
{
    return getHeight();
}

// ============================================================================
// Configuration
// ============================================================================

void ImageSynthesizer::setFrequencyRange(float minHz, float maxHz)
{
    minFrequency = std::max(1.0f, minHz);
    maxFrequency = std::max(minFrequency + 1.0f, maxHz);
}

void ImageSynthesizer::setSampleRate(double sr)
{
    sampleRate = std::max(1000.0, sr);
}

void ImageSynthesizer::setPitchDeviationRange(float maxSemitones)
{
    pitchDeviationSemitones = std::max(0.0f, maxSemitones);
}

// ============================================================================
// Private Helpers — Image Processing
// ============================================================================

float ImageSynthesizer::getBrightness(const juce::Image& image, int x, int y)
{
    const auto pixel = image.getPixelAt(x, y);

    // Rec. 601 luma (perceived luminance) — gives a better perceptual match
    // than a simple average, especially for saturated colours.
    return 0.299f * pixel.getFloatRed()
         + 0.587f * pixel.getFloatGreen()
         + 0.114f * pixel.getFloatBlue();
}

juce::Image ImageSynthesizer::convertToGrayscale(const juce::Image& image)
{
    const auto w = image.getWidth();
    const auto h = image.getHeight();

    juce::Image result(juce::Image::ARGB, w, h, true);

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const auto pixel  = image.getPixelAt(x, y);
            const float luma = 0.299f * pixel.getFloatRed()
                             + 0.587f * pixel.getFloatGreen()
                             + 0.114f * pixel.getFloatBlue();

            result.setPixelAt(
                x, y,
                juce::Colour::fromFloatRGBA(luma, luma, luma,
                                            pixel.getFloatAlpha()));
        }
    }

    return result;
}

juce::Image ImageSynthesizer::normalizeImage(const juce::Image& image)
{
    const auto w = image.getWidth();
    const auto h = image.getHeight();

    // ── Find min / max brightness ─────────────────────────────────────────
    float minBright = 1.0f;
    float maxBright = 0.0f;

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float b = getBrightness(image, x, y);
            minBright = std::min(minBright, b);
            maxBright = std::max(maxBright, b);
        }

    const float range = maxBright - minBright;
    if (range < 0.0001f)
        return image;   // already uniform — nothing to stretch

    // ── Stretch to full [0, 1] ────────────────────────────────────────────
    juce::Image result(juce::Image::ARGB, w, h, true);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float b         = getBrightness(image, x, y);
            const float normalised = (b - minBright) / range;

            result.setPixelAt(
                x, y,
                juce::Colour::fromFloatRGBA(normalised, normalised, normalised,
                                            1.0f));
        }

    return result;
}

} // namespace ana
