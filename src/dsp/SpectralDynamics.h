#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include <juce_core/juce_core.h>
#include "PartialDataSIMD.h"

namespace ana {

//==============================================================================
/**
    SpectralDynamics — a 512-band spectral dynamics processor.

    Operates on individual partials (frequency bins) from PartialDataSIMD,
    applying classic dynamics processing — compression, expansion, limiting,
    or upward compression — on each partial independently.

    Features per-band focus (target specific frequency regions), external
    sidechain modulation, soft-knee curves, attack/release smoothing for
    zipper-noise suppression, and dry/wet mix.

    @see PartialDataSIMD, SIMDSupport
*/
class SpectralDynamics
{
public:
    //==============================================================================
    /** Dynamics mode.                                                */
    enum class Mode
    {
        Compressor,         /**< Attenuate partials above threshold.              */
        Expander,           /**< Attenuate partials below threshold.              */
        Limiter,            /**< Hard-knee limiting at threshold (infinite ratio).*/
        UpwardCompressor    /**< Boost partials below threshold.                  */
    };

    //==============================================================================
    /** Level detection method.                                         */
    enum class Detection
    {
        RMS,        /**< Root-mean-square (instantaneous for a single bin). */
        Peak,       /**< Peak detection (instantaneous).                    */
        Envelope    /**< Envelope follower with attack/release smoothing.   */
    };

    //==============================================================================
    SpectralDynamics();
    ~SpectralDynamics() = default;

    //==============================================================================
    /** @name Core dynamics controls */
    //@{
    void setMode(Mode mode) noexcept;
    void setDetection(Detection detection) noexcept;

    void setThreshold(float dB);          /**< -80 to 0 dB  */
    void setRatio(float ratio);           /**< 1:1 to 20:1 (compressor); 1:1 to 0.2:1 (expander) */
    void setAttack(float ms);             /**< 0.1 to 100 ms */
    void setRelease(float ms);            /**< 1 to 500 ms   */
    void setKnee(float dB);               /**< 0 to 20 dB (soft-knee transition width) */
    void setMakeupGain(float dB);         /**< 0 to 24 dB post-processing gain */
    void setMix(float mix);               /**< 0.0 (dry) to 1.0 (fully wet) */
    //@}

    //==============================================================================
    /** @name Band-focus (optional — target a specific frequency region) */
    //@{
    void setBandFocus(float centerHz, float widthOctaves);
    void setBandBypass(bool bypass) noexcept;
    //@}

    //==============================================================================
    /** @name Sidechain — use external partials for level detection */
    //@{
    void setSidechain(const PartialDataSIMD& sidechainPartials);
    //@}

    //==============================================================================
    /** Main processing entry point. */
    void process(PartialDataSIMD& partials);

    /** Reset all internal state to defaults. */
    void reset();

private:
    //==============================================================================
    /** Update the per-partial envelope follower (for Envelope detection mode).  */
    void updateEnvelope(const PartialDataSIMD& partials);

    /** Compute linear gain from a detected level (linear amplitude).           */
    float computeGain(float levelLinear) const noexcept;

    /** Recompute attack/release one-pole coefficients from current times.      */
    void updateCoefficients() noexcept;

    /** Check whether a frequency falls inside the focused band.                */
    bool isInBand(float freqHz) const noexcept;

    //==============================================================================
    // Parameter state
    Mode        mode_        = Mode::Compressor;
    Detection   detection_   = Detection::RMS;
    float       threshold_   = -24.0f;
    float       ratio_       = 4.0f;
    float       attackMs_    = 10.0f;
    float       releaseMs_   = 50.0f;
    float       knee_        = 6.0f;
    float       makeupGain_    = 0.0f;
    float       makeupLinear_  = 1.0f;
    float       mix_           = 1.0f;

    //==============================================================================
    // Band focus
    bool        bandFocusEnabled_   = false;
    float       bandCenterHz_       = 1000.0f;
    float       bandWidthOctaves_   = 3.0f;
    float       bandLowerHz_        = 20.0f;
    float       bandUpperHz_        = 20000.0f;
    bool        bandBypass_         = false;

    //==============================================================================
    // Sidechain
    bool        sidechainActive_ = false;
    float       scAmplitude_[PartialDataSIMD::kMaxPartials] = {};

    //==============================================================================
    // Runtime state
    double      sampleRate_         = 44100.0;

    /** Per-partial envelope state (used in Envelope detection mode).            */
    std::vector<float> envelopeState_;

    /** Per-partial smoothed gain state (prevents zipper noise).                 */
    std::vector<float> gainState_;

    /** One-pole filter coefficients computed from attack/release time.          */
    float       attackCoeff_        = 0.0f;
    float       releaseCoeff_       = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralDynamics)
};

} // namespace ana
