#pragma once

#include <vector>
#include <cmath>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "PartialDataSIMD.h"
#include "SIMDSupport.h"
#include "STFTConfig.h"

namespace ana {

//==============================================================================
/**
    Spectral features extracted from audio for style transfer.

    Stores per-frame spectral descriptors and aggregated perceptual
    characteristics such as brightness, warmth, and roughness.
*/
struct StyleFeatures
{
    std::vector<float> spectralCentroid;      ///< Per-frame spectral centroid (Hz)
    std::vector<float> spectralFlux;          ///< Per-frame spectral flux
    std::vector<float> spectralRolloff;       ///< Per-frame spectral rolloff (Hz)
    std::vector<float> mfcc;                  ///< 13 MFCC coefficients per frame
    std::vector<float> chroma;                ///< 12 chroma features per frame
    std::vector<float> temporalEnvelope;      ///< RMS amplitude envelope per frame
    std::vector<float> zeroCrossingRate;      ///< Zero-crossing rate per frame
    float avgBrightness = 0.0f;               ///< Mean spectral centroid across all frames
    float avgWarmth     = 0.0f;               ///< Low-frequency energy ratio
    float roughness     = 0.0f;               ///< Std-dev of spectral flux
};

//==============================================================================
/**
    Algorithmic audio style transfer engine.

    Applies the spectral "style" (timbre, texture) of one audio signal to the
    pitch / temporal "content" of another using only signal-processing
    techniques — no machine learning framework required.

    The core algorithm:
        1. STFT analysis of both content and style audio.
        2. Cepstral spectral envelope extraction from style.
        3. Multi-iteration envelope replacement + histogram matching.
        4. ISTFT overlap-add reconstruction.

    @par PartialDataSIMD processing
        A convenience overload converts additive partials to audio, runs the
        full-band style transfer, and maps the result back onto partial
        amplitudes.
*/
class NeuralStyleTransfer
{
public:
    //==============================================================================
    NeuralStyleTransfer();
    ~NeuralStyleTransfer() = default;

    //==============================================================================
    /** @name Audio Input */
    //@{

    /** Set the content audio (provides pitch + temporal structure). */
    void setContent(const std::vector<float>& audio, double sampleRate);

    /** Set the style audio (provides spectral characteristics). */
    void setStyle(const std::vector<float>& audio, double sampleRate);

    //@}

    //==============================================================================
    /** @name Parameters */
    //@{

    /** Style transfer strength (0.0 = no change, 1.0 = full style). */
    void setStrength(float amount);

    /** Preserve transient attack information from the content. */
    void setPreserveTransients(bool preserve);

    /** Number of style-transfer iterations (1 = fastest, 10 = highest quality). */
    void setIterations(int count);

    /** Spectral smoothness of the style envelope (0.0 = detailed, 1.0 = very smooth). */
    void setSpectralSmoothness(float amount);

    /** Temporal smoothing of frame-to-frame magnitudes (0.0 = none, 1.0 = very smooth). */
    void setTemporalSmoothness(float amount);

    /** Set the processing sample rate. */
    void setSampleRate(double sr);

    /** Set the FFT size (must be a power of two, clamped to [256, 8192]). */
    void setFftSize(int size);

    //@}

    //==============================================================================
    /** @name Analysis */
    //@{

    /** Extract spectral features from the currently loaded style audio.
        @return StyleFeatures descriptor with all per-frame and aggregate data.
        @note  Results are cached internally — repeated calls are cheap. */
    StyleFeatures extractStyle();

    /** Return the features extracted from the most recent content audio.
        @warning Returns an empty/default structure until extractStyle() or
                 process() has been called. */
    StyleFeatures getContentFeatures() const;

    //@}

    //==============================================================================
    /** @name Processing */
    //@{

    /** Apply style transfer to the loaded content audio.
        @return Processed audio buffer (floating-point mono samples). */
    std::vector<float> process();

    /** Apply style transfer directly to additive synthesis partials.
        Synthesises a short frame from the partials, runs the full-band
        transfer, and maps the result back onto partial amplitudes.
        @param partials  Partial data — amplitude values are modified in-place. */
    void process(PartialDataSIMD& partials);

    //@}

    //==============================================================================
    /** @name State */
    //@{

    /** Full reset: clears all audio, features, and cached state. */
    void reset();

    //@}

private:
    //==============================================================================
    /** @name Feature Extraction */
    //@{

    /** Extract all spectral features from an audio buffer.
        Fills the provided StyleFeatures struct with per-frame and aggregate data. */
    void extractFeatures(const std::vector<float>& audio,
                         double sampleRate,
                         StyleFeatures& features);

    /** Match the distribution of one vector to another (reserved for future use). */
    std::vector<float> matchFeatureDistribution(
        const std::vector<float>& content,
        const std::vector<float>& style);

    //@}

    //==============================================================================
    /** @name STFT Analysis / Synthesis */
    //@{

    /** Forward STFT: decompose audio into magnitude and phase per frame.
        @param audio   Input time-domain signal.
        @param mag     Output magnitude matrix [numFrames][numBins].
        @param phase   Output phase   matrix [numFrames][numBins]. */
    void stftAnalysis(const std::vector<float>& audio,
                      std::vector<std::vector<float>>& mag,
                      std::vector<std::vector<float>>& phase);

    /** Inverse STFT: reconstruct time-domain audio from magnitude/phase.
        @param mag             Magnitude matrix.
        @param phase           Phase matrix.
        @param expectedLength  Hint for the output buffer length.
        @return Reconstructed audio. */
    std::vector<float> stftSynthesis(
        const std::vector<std::vector<float>>& mag,
        const std::vector<std::vector<float>>& phase,
        int expectedLength);

    //@}

    //==============================================================================
    /** @name Spectral Processing */
    //@{

    /** Histogram match content magnitudes to style magnitudes.
        Ensures the output has the same statistical distribution as the style. */
    void applyHistogramMatch(std::vector<float>& content,
                             const std::vector<float>& style);

    /** Extract smooth spectral envelope via cepstral liftering.
        @param mag              Magnitude matrix.
        @param envelope         Output envelope matrix (same dimensions).
        @param numCepstralBins  Number of cepstral coefficients to retain. */
    void extractSpectralEnvelope(const std::vector<std::vector<float>>& mag,
                                  std::vector<std::vector<float>>& envelope,
                                  int numCepstralBins);

    /** Precompute the DCT-II/DCT-III cos table for the current FFT size. */
    void updateDCTTable();

    /** Build a mel-spaced triangular filterbank.
        @param numBins      Number of FFT magnitude bins.
        @param numMelBands  Number of mel bands (e.g. 26).
        @param filterbank   Output filterbank matrix [numMelBands][numBins]. */
    void buildMelFilterbank(int numBins, int numMelBands,
                            std::vector<std::vector<float>>& filterbank);

    // Mel scale helpers
    static float hzToMel(float hz);
    static float melToHz(float mel);

    //@}

    //==============================================================================
    /** @name Members */
    //@{

    // Content audio
    std::vector<float> contentAudio_;
    double contentSampleRate_ = 44100.0;

    // Style audio
    std::vector<float> styleAudio_;
    double styleSampleRate_ = 44100.0;

    // Parameters
    float strength_              = 0.5f;
    bool   preserveTransients_   = true;
    int    iterations_           = 3;
    float  spectralSmoothness_   = 0.3f;
    float  temporalSmoothness_   = 0.3f;
    double sampleRate_           = 44100.0;
    int    fftSize_              = 2048;
    int    hopSize_              = 512;

    // Cached features
    StyleFeatures contentFeatures_;
    StyleFeatures styleFeatures_;
    bool contentAnalyzed_ = false;
    bool styleAnalyzed_   = false;

    // FFT engine and window
    std::unique_ptr<juce::dsp::FFT> fft_;
    std::vector<float> windowTable_;

    // Precomputed DCT cos table (N×N, for cepstral envelope extraction)
    std::vector<float> dctCosTab_;
    int dctCosTabNumBins_ = 0;

    // Precomputed Mel filterbank cache (flat, numMelBands × numBins)
    std::vector<float> melFilterbank_;
    int melFilterbankNumBins_ = 0;
    double melFilterbankSampleRate_ = 0.0;

    // Per-frame scratch buffers (reused to avoid repeated allocation)
    std::vector<float> logMag_;
    std::vector<float> melEnergies_;
    std::vector<float> contentFlat_;
    std::vector<float> magFrame_;
    std::vector<float> phaseFrame_;

    //@}

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeuralStyleTransfer)
};

} // namespace ana
