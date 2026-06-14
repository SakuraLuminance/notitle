#pragma once

#include <vector>
#include <cstdint>
#include <juce_core/juce_core.h>
#include "PartialDataSIMD.h"

namespace ana {

//==============================================================================
/**
    SpectralSculptor — real-time spectrum painting tool for partial data.

    Provides direct frequency-domain manipulation of PartialDataSIMD via
    eight distinct tools: Brush, Eraser, Smudge, Sharpen, Warp, Clone,
    Mirror, and Fractal.  All operations work on the partial array directly
    (not on raw audio) and include temporal smoothing to prevent zipper noise.

    @see PartialDataSIMD, SIMDSupport
*/
class SpectralSculptor
{
public:
    //==============================================================================
    enum class Tool
    {
        Brush,      /**< Increase energy at cursor position (Gaussian falloff).      */
        Eraser,     /**< Decrease energy at cursor position (Gaussian falloff).      */
        Smudge,     /**< Blend neighboring partials (low-pass in frequency domain).  */
        Sharpen,    /**< Enhance peaks, suppress valleys (high-pass).               */
        Warp,       /**< Warp frequency axis around a centre (stretch/compress).    */
        Clone,      /**< Copy spectrum region from source to target position.       */
        Mirror,     /**< Reflect spectrum around a centre frequency.                */
        Fractal     /**< Apply recursive self-similar amplitude pattern.            */
    };

    //==============================================================================
    SpectralSculptor();
    ~SpectralSculptor() = default;

    //==============================================================================
    /** @name Tool configuration */
    //@{
    void setActiveTool(Tool tool);
    void setBrushSize(float normalizedSize);   /**< 0.01 to 1.0 (fraction of spectrum) */
    void setBrushStrength(float strength);     /**< 0.0 to 1.0 */
    void setPosition(float normalizedPos);     /**< 0.0 to 1.0 (cursor in spectrum) */
    void setCenterFrequency(float freqHz);     /**< Mirror / Clone centre frequency */
    //@}

    //==============================================================================
    /** @name Clone tool specific */
    //@{
    void setSourcePosition(float normPos);     /**< Sample source for clone stamp */
    void cloneToPosition(float normPos);        /**< Perform one-shot clone */
    //@}

    //==============================================================================
    /** @name Warp tool specific */
    //@{
    void setWarpFactor(float factor);          /**< 0.25 to 4.0 */
    void setWarpCenter(float normPos);         /**< 0.0 to 1.0 centre of warping */
    //@}

    //==============================================================================
    /** @name Fractal tool specific */
    //@{
    void setFractalIterations(int iterations); /**< 1–5 */
    void setFractalSeed(float seed);           /**< Pattern variation */
    //@}

    //==============================================================================
    /** Main processing — applies the active tool to the given partial set. */
    void process(PartialDataSIMD& partials);

    /** Resets all internal state (parameters, clone buffer, smoothing). */
    void reset();

private:
    //==============================================================================
    // Tool implementations
    void applyBrush   (PartialDataSIMD& partials);
    void applyEraser  (PartialDataSIMD& partials);
    void applySmudge  (PartialDataSIMD& partials);
    void applySharpen (PartialDataSIMD& partials);
    void applyWarp    (PartialDataSIMD& partials);
    void applyClone   (PartialDataSIMD& partials);
    void applyMirror  (PartialDataSIMD& partials);
    void applyFractal (PartialDataSIMD& partials);

    //==============================================================================
    // Helpers
    float gaussianFalloff(int idx, int center, float sigma) const noexcept;

    /** Builds a list of active partial indices sorted by frequency. */
    void sortPartialsByFrequency(const PartialDataSIMD& partials,
                                 std::vector<int>& order) const;

    /** Captures the source region into cloneBuffer_ from live partials. */
    void captureCloneSource(const PartialDataSIMD& partials);

    /** Pastes cloneBuffer_ contents at the target position. */
    void pasteCloneSource(PartialDataSIMD& partials) const;

    //==============================================================================
    // Parameters
    Tool    activeTool_        = Tool::Brush;
    float   brushSize_         = 0.1f;
    float   brushStrength_     = 0.5f;
    float   position_          = 0.5f;
    float   centerFrequency_   = 1000.0f;
    float   sourcePosition_    = 0.0f;
    float   warpFactor_        = 1.0f;
    float   warpCenter_        = 0.5f;
    int     fractalIterations_ = 3;
    float   fractalSeed_       = 0.5f;

    //==============================================================================
    // Internal state
    PartialDataSIMD cloneBuffer_;           /**< Frozen clone-source data.             */
    bool            cloneNeedsCapture_ = true; /**< setSourcePosition() marks dirty.   */
    std::vector<float> prevGains_;          /**< Previous-frame amplitudes (smoothing).*/
    float           smoothingFactor_ = 0.1f;/**< 0 = no smoothing, 1 = fully held.     */

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralSculptor)
};

} // namespace ana
