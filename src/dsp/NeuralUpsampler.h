#pragma once

#include <vector>
#include <complex>
#include <cmath>
#include <cstdint>
#include <memory>

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "PartialDataSIMD.h"

namespace ana {

//==============================================================================
/**
    NeuralUpsampler — AI-inspired audio upsampling engine.

    Reconstructs high-quality audio from low sample-rate / low bit-depth input
    using a multi-stage signal-processing pipeline:

      1. sincInterpolate   — band-limited sample-rate conversion (Kaiser-windowed sinc)
      2. spectralUpsample  — STFT-based high-frequency extrapolation via spectral
                             envelope estimation and LPC prediction
      3. enhanceHarmonics  — F0-driven harmonic series regeneration
      4. applyNoiseShaping — error-feedback noise shaping for bit-depth reduction

    The "neural" name is aspirational — the approach is purely algorithmic
    (no ML libraries or external inference engines).

    All work buffers are pre-allocated and reused to avoid runtime allocations
    during real-time processing.

    @see PartialDataSIMD, STFTAnalyzer
*/
class NeuralUpsampler
{
public:
    //==============================================================================
    NeuralUpsampler();
    ~NeuralUpsampler() = default;

    //==============================================================================
    /** @name Input configuration */
    //@{
    /** Load the input audio buffer and its metadata.
        @param audio      Input samples (any length &gt; 0)
        @param sampleRate Original sample rate in Hz
        @param bitDepth   Original bit depth (8, 16, 24, or 32) — used for noise shaping
    */
    void setInput(const std::vector<float>& audio, double sampleRate, int bitDepth = 16);
    //@}

    //==============================================================================
    /** @name Upsampling quality and target */
    //@{
    /** Set processing quality.
        @param quality  1 (fast / low quality) to 5 (best / slowest)
    */
    void setQuality(int quality);

    /** Set the target output sample rate in Hz. */
    void setTargetSampleRate(double sr);

    /** Set the reconstruction bandwidth fraction.
        @param factor  0.0 (minimum bandwidth) to 1.0 (full Nyquist bandwidth)
    */
    void setBandwidth(float factor);
    //@}

    //==============================================================================
    /** @name Processing */
    //@{
    /** Standard time-domain upsampling.
        @return Output audio at the target sample rate.
    */
    std::vector<float> process();

    /** Spectral upsampling applied directly to partial data.
        Interpolates active partial amplitudes using the spectral envelope and
        generates additional high-frequency partials.
    */
    void process(PartialDataSIMD& partials);
    //@}

    //==============================================================================
    /** @name Spectral reconstruction controls */
    //@{
    /** Set harmonic enhancement amount.
        @param amount  0.0 (none) to 1.0 (full)
    */
    void setHarmonicEnhancement(float amount);

    /** Set noise reduction amount.
        @param amount  0.0 (none) to 1.0 (maximum reduction)
    */
    void setNoiseReduction(float amount);

    /** Set transient preservation amount.
        Higher values reduce spectral smoothing to keep attacks sharp.
        @param amount  0.0 (none) to 1.0 (maximum preservation)
    */
    void setTransientPreservation(float amount);
    //@}

    //==============================================================================
    /** @name Query */
    //@{
    /** Returns the actual output sample rate (may differ from target due to
        integer resampling constraints). */
    double getOutputSampleRate() const;

    /** Returns the effective output bit depth after noise shaping. */
    int getOutputBitDepth() const;
    //@}

    //==============================================================================
    /** Reset all internal state (filters, overlap-add buffers, FFT state). */
    void reset();

private:
    //==============================================================================
    // Core algorithms
    //==============================================================================

    /** Band-limited interpolation via Kaiser-windowed sinc convolution.
        Each output sample is computed as a weighted sum of surrounding input
        samples with the sinc kernel evaluated at the fractional offset.
        Quality controls the kernel half-length:
          quality 1  →  8  taps per side
          quality 5  →  64 taps per side
    */
    std::vector<float> sincInterpolate(const std::vector<float>& input,
                                       double inputSr, double outputSr);

    /** Frequency-domain upsampling.
        1. STFT analysis of the input
        2. Spectral envelope estimation via cepstral smoothing
        3. LPC-based high-frequency extrapolation
        4. Harmonic regeneration
        5. ISTFT reconstruction at target sample rate

        The result is blended with the sinc-interpolated output for a
        full-bandwidth reconstruction.
    */
    std::vector<float> spectralUpsample(const std::vector<float>& input,
                                        double inputSr, double outputSr);

    /** F0-driven harmonic enhancement.
        Detects the fundamental frequency via autocorrelation and generates a
        harmonic series up to Nyquist.  Mixed back under control of
        harmonicEnhancement_.
    */
    void enhanceHarmonics(std::vector<float>& audio, double sampleRate);

    /** Error-feedback noise shaping.
        Implements a 1st-order high-pass noise shaper to push quantization
        noise above the audible range when reducing bit depth.
    */
    void applyNoiseShaping(std::vector<float>& audio, int targetBitDepth);

    /** Linear predictive extrapolation (autocorrelation / Levinson-Durbin).
        Given a low-frequency band, predicts numSamples of continuation using
        an order-p all-pole model.  Used inside spectralUpsample to generate
        plausible high-frequency content.
    */
    std::vector<float> lpcExtrapolate(const std::vector<float>& lowBand,
                                       int numSamples, int order);

    //==============================================================================
    // Helpers
    //==============================================================================

    /** Design a Kaiser-windowed sinc lowpass prototype filter. */
    std::vector<float> designKaiserSinc(int halfLength, double cutoff, double beta);

    /** Extract spectral envelope via low-order cepstral truncation.
        @param magSpectrum  Magnitude spectrum (size = half spectrum bins)
        @param cepstrumOrder  Number of cepstral coefficients to retain
        @param envelope     Output spectral envelope (same size as magSpectrum)
    */
    void computeSpectralEnvelope(const std::vector<float>& magSpectrum,
                                 int cepstrumOrder,
                                 std::vector<float>& envelope) const;

    /** Estimate fundamental frequency via normalized autocorrelation. */
    float estimateF0(const std::vector<float>& audio, double sampleRate) const;

    /** Compute the average spectral slope from the last N bins of a
        magnitude spectrum. */
    float computeSpectralSlope(const std::vector<float>& magSpectrum,
                               int startBin, int endBin) const;

    //==============================================================================
    // Input data
    //==============================================================================
    std::vector<float> inputAudio_;
    double inputSampleRate_ = 44100.0;
    int    inputBitDepth_   = 16;

    //==============================================================================
    // Configuration
    //==============================================================================
    int    quality_              = 3;
    double targetSampleRate_    = 44100.0;
    float  bandwidth_           = 0.9f;
    float  harmonicEnhancement_ = 0.3f;
    float  noiseReduction_      = 0.3f;
    float  transientPreservation_ = 0.7f;

    //==============================================================================
    // Pre-allocated work buffers
    //==============================================================================
    mutable std::vector<float> scratch_;
    mutable std::vector<float> scratch2_;
    mutable std::vector<float> windowBuf_;
    mutable std::vector<float> filterBuf_;

    //==============================================================================
    // FFT state
    //==============================================================================
    std::unique_ptr<juce::dsp::FFT> fft_;
    int fftSize_ = 2048;

    //==============================================================================
    // Overlap-add state
    //==============================================================================
    mutable std::vector<float> olaBuffer_;
    int olaWritePos_ = 0;

    //==============================================================================
    // Cached sinc filter (rebuilt on quality / SR change)
    //==============================================================================
    std::vector<float> cachedFilter_;
    double cachedFilterInputSr_  = 0.0;
    double cachedFilterOutputSr_ = 0.0;
    int    cachedFilterQuality_  = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeuralUpsampler)
};

} // namespace ana
