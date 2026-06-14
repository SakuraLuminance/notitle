#pragma once
#include "PartialDataSIMD.h"
#include <vector>
#include <cmath>

namespace ana {

//==============================================================================
/**
    Spectral-domain prism effect modes.

    Each mode transforms partial data in a distinct way before IFFT resynthesis,
    similar to Harmor's Prism effect.
*/
enum class PrismMode
{
    FrequencyShift,   ///< Shift all partial frequencies by a constant offset
    SpectralBlur,     ///< Smooth amplitudes across neighboring partials
    HarmonicRotation, ///< Rotate partial assignments (Nth → N+1)
    SpectralMirror,   ///< Mirror frequency spectrum around a center frequency
    CombSweep         ///< Apply a sweeping comb-filter pattern to amplitudes
};

//==============================================================================
/**
    Prism spectral-domain processor.

    Operates on PartialData in-place, transforming frequency/amplitude/phase
    values before IFFT resynthesis. Supports five distinct modes, each
    independently testable.

    Parameters:
        Amount:   0-100% effect intensity (mode-specific interpretation)
        Feedback: 0-100% previous-output feedback blending
        Mix:      0-100% wet/dry blend after processing
        Mode:     Selects the active spectral transformation

    Usage:
        PrismEffect prism;
        prism.setMode(PrismMode::FrequencyShift);
        prism.setAmount(0.5f);
        prism.setMix(0.8f);

        PartialDataSIMD data = ...;
        prism.process(data);
*/
class PrismEffect
{
public:
    /** Default max shift for FrequencyShift mode (Hz). */
    static constexpr float kDefaultMaxShift = 2000.0f;

    /** Default center frequency for SpectralMirror mode (Hz). */
    static constexpr float kDefaultCenterFreq = 1000.0f;

    /** Minimum sweep frequency for CombSweep mode (Hz). */
    static constexpr float kCombSweepMin = 50.0f;

    /** Maximum sweep frequency for CombSweep mode (Hz). */
    static constexpr float kCombSweepMax = 20000.0f;

    //==============================================================================
    PrismEffect();
    ~PrismEffect() = default;

    //==============================================================================
    /** Process partial data in-place with the selected mode and parameters.
        Applies the spectral transformation, then blends with original using
        Mix and stores output for Feedback on subsequent calls.
    */
    void process(PartialDataSIMD& data);

    //==============================================================================
    /** @name Parameter Setters */
    //@{
    void setAmount(float amount);            ///< 0.0 to 1.0
    void setFeedback(float feedback);        ///< 0.0 to 1.0
    void setMix(float mix);                  ///< 0.0 to 1.0
    void setMode(PrismMode mode);
    void setMaxShift(float maxShiftHz);      ///< Frequency shift range in Hz
    void setCenterFreq(float centerFreqHz);  ///< Mirror center frequency in Hz
    //@}

    //==============================================================================
    /** @name Parameter Getters */
    //@{
    float getAmount() const;
    float getFeedback() const;
    float getMix() const;
    PrismMode getMode() const;
    float getMaxShift() const;
    float getCenterFreq() const;
    //@}

    //==============================================================================
    /** Reset internal state (feedback buffer). */
    void reset();

private:
    //==============================================================================
    /** @name Mode Application Methods (each operates on data in-place) */
    //@{
    void applyFrequencyShift(PartialDataSIMD& data);
    void applySpectralBlur(PartialDataSIMD& data);
    void applyHarmonicRotation(PartialDataSIMD& data);
    void applySpectralMirror(PartialDataSIMD& data);
    void applyCombSweep(PartialDataSIMD& data);
    //@}

    /** Apply wet/dry mix between original and processed frames. */
    void applyMix(const PartialDataSIMD& original, PartialDataSIMD& processed);

    /** Apply feedback by blending previous output into the current data. */
    void applyFeedback(PartialDataSIMD& data);

    //==============================================================================
    float amount         = 0.0f;
    float feedback       = 0.0f;
    float mix            = 1.0f;
    PrismMode mode       = PrismMode::FrequencyShift;
    float maxShift       = kDefaultMaxShift;
    float centerFreq     = kDefaultCenterFreq;

    /** Stored previous output for feedback blending. */
    PartialDataSIMD feedbackBuffer;
    bool hasFeedback_ = false;

    /** Reusable scratch buffers for sorting/blur. */
    int scratchIndices_[PartialDataSIMD::kMaxPartials];
    float scratchAmps_[PartialDataSIMD::kMaxPartials];

    //==============================================================================
    static float clamp01(float v);
    static bool isValid(float v);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PrismEffect)
};

} // namespace ana
