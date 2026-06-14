#pragma once
#include <juce_graphics/juce_graphics.h>
#include "PartialData.h"

namespace ana {

/**
 * Image Synthesis Engine: converts images to sound partials.
 *
 * Mapping:
 *   X axis (width)  → time frames  (each pixel column = one STFT frame)
 *   Y axis (height) → frequency bins (bottom of image = low frequency)
 *   Brightness      → amplitude (0 = black/silent, 1 = white/full)
 *
 * Dual-plane support:
 *   Gain Plane  — primary image; brightness maps to amplitude
 *   Pitch Plane — secondary image; brightness maps to pitch deviation
 *                 from the harmonic series (0.5 = no shift, <0.5 = down, >0.5 = up)
 */
class ImageSynthesizer
{
public:
    ImageSynthesizer();
    ~ImageSynthesizer();

    // ===== Image Loading =====

    /** Load a PNG/JPG image from file (supports any format JUCE can read). */
    bool loadImage(const juce::File& file);

    /** Set the gain (amplitude) image programmatically. Automatically
     *  converts to grayscale and normalizes brightness. */
    void setGainPlane(const juce::Image& image);

    /** Set the pitch deviation image programmatically. Must share the
     *  same dimensions as the gain plane for predictable results. */
    void setPitchPlane(const juce::Image& image);

    // ===== Data Access =====

    /** Build and return PartialData from the loaded images. */
    PartialData getPartialData() const;

    /** Image width in pixels (= number of time frames). */
    int getWidth() const;

    /** Image height in pixels (= number of frequency bins). */
    int getHeight() const;

    /** Number of time frames (image width). */
    int getNumFrames() const;

    /** Number of partials per frame (image height). */
    int getNumPartials() const;

    // ===== Configuration =====

    /** Set the frequency range mapped from the Y axis.
     *  @param minHz  bottom-of-image frequency (default 20 Hz)
     *  @param maxHz  top-of-image frequency    (default 20 kHz) */
    void setFrequencyRange(float minHz, float maxHz);

    /** Set the output sample rate for time-stamp calculation. */
    void setSampleRate(double sampleRate);

    /** Set the maximum pitch deviation range in semitones.
     *  Brightness 0 → -range, brightness 1 → +range (default 12). */
    void setPitchDeviationRange(float maxSemitones);

private:
    // ===== Image Processing =====

    /** Extract normalised brightness [0, 1] from a pixel using
     *  perceived-luminance weights (Rec. 601 luma). */
    static float getBrightness(const juce::Image& image, int x, int y);

    /** Convert an RGBA image to single-channel luminance. */
    static juce::Image convertToGrayscale(const juce::Image& image);

    /** Stretch brightness to fill the full [0, 1] range. */
    static juce::Image normalizeImage(const juce::Image& image);

    // ===== State =====

    juce::Image gainPlane;
    juce::Image pitchPlane;
    bool hasGainPlane  = false;
    bool hasPitchPlane = false;

    float minFrequency          = 20.0f;
    float maxFrequency          = 20000.0f;
    double sampleRate           = 44100.0;
    float pitchDeviationSemitones = 12.0f;
};

} // namespace ana
