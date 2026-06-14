#pragma once

#include <vector>
#include <complex>
#include <cmath>
#include <juce_dsp/juce_dsp.h>
#include "SIMDSupport.h"

namespace ana {

//==============================================================================
/**
    A point on a time-stretch speed curve.

    @p time is the normalized source position [0, 1].
    @p speed is the desired playback speed at that position [0.1, 10.0].
*/
struct StretchCurvePoint
{
    double time  = 0.0;  ///< Normalized time in source [0, 1]
    double speed = 1.0;  ///< Playback speed at this point [0.1, 10.0]
};

//==============================================================================
/**
    A speed curve made of control points used by TimeStretchEngine.

    The curve defines playback speed as a function of the normalized source
    position.  Between points the speed is linearly interpolated; outside
    the defined range the nearest endpoint speed is used.

    When no curve is set, the engine uses a constant speed equal to
    @ref stretchRatio.
*/
struct StretchCurve
{
    std::vector<StretchCurvePoint> points;

    //----------------------------------------------------------------------
    /** Add a control point.  @p time and @p speed are clamped to valid ranges. */
    void addPoint(double time, double speed);

    /** Remove all points. */
    void clear();

    /** Return the interpolated speed at a given normalized source time [0, 1].
        Clamps to the first/last point when @p normalizedTime is outside the
        defined range.  Returns 1.0 when no points exist. */
    double getSpeedAt(double normalizedTime) const;
};

//==============================================================================
/**
    Phase-vocoder-based time stretch engine with non-linear speed curves and
    MIDI-fit mode.

    Primary algorithm is a standard phase vocoder (STFT analysis, instantaneous
    frequency phase propagation, IFFT overlap-add synthesis).  For extreme
    stretch ratios (> 3.0 or < 0.33) a granular fallback is used instead.

    Speed curves map source position to instantaneous playback speed, allowing
    non-linear time manipulation (e.g.  ramp up / slow down effects).

    MIDI-fit mode calculates the required stretch ratio to fit audio into a
    specified target duration, with note velocity affecting attack preservation.
*/
class TimeStretchEngine
{
public:
    TimeStretchEngine();
    ~TimeStretchEngine() = default;

    //==============================================================================
    /** @name Core Stretch */
    //@{

    /** Set the global stretch ratio (playback speed).
        0.25 = quarter speed (4x output length)
        1.0  = normal speed (no stretch)
        4.0  = quadruple speed (0.25x output length)
    */
    void setStretchRatio(float ratio);

    /** Set a non-linear speed curve that overrides the constant ratio.
        Pass an empty curve to revert to constant-speed mode. */
    void setSpeedCurve(const StretchCurve& curve);

    /** Set the default playback speed used when no stretch ratio or curve
        has been explicitly set.  Range [0.1, 10.0]. */
    void setDefaultSpeed(float speed);

    //@}

    //==============================================================================
    /** @name MIDI-Fit Mode */
    //@{

    /** Enable MIDI-fit mode where the stretch ratio is calculated automatically
        to fit the input into the target duration. */
    void setMidiFitMode(bool enabled);

    /** Set the target duration in seconds for MIDI-fit mode. */
    void setTargetDuration(double seconds);

    /** Set MIDI note velocity [0, 1] which affects stretch character:
        higher values preserve transients / attacks more aggressively. */
    void setNoteVelocity(float velocity);

    //@}

    //==============================================================================
    /** @name Sample Rate */
    //@{
    void setSampleRate(double sr);
    //@}

    //==============================================================================
    /** @name Processing */
    //@{

    /** Stretch the input audio buffer.
        @param input      Mono floating-point samples.
        @param numSamples Number of samples in @p input (may be less than
                          the vector size; excess is ignored).
        @return Stretched audio buffer.
    */
    std::vector<float> process(const std::vector<float>& input, int numSamples);

    /** Convenience: stretch to an exact output duration.
        Calculates the required ratio from the input length and the given
        duration, then calls process().

        @param input           Mono floating-point samples.
        @param durationSeconds Desired output duration in seconds.
        @return Stretched audio buffer.
    */
    std::vector<float> processToDuration(const std::vector<float>& input,
                                          double durationSeconds);

    /** Reset all internal state (analysis buffers, phase accumulators). */
    void reset();

    //@}

private:
    //==============================================================================
    /** @name Internal processing engines */
    //@{

    /** Phase vocoder time stretch.
        Builds an STFT analysis of the input, propagates phase via the
        instantaneous-frequency method, and overlap-adds the result. */
    std::vector<float> processPhaseVocoder(const std::vector<float>& input,
                                            double stretchRatio);

    /** Granular time stretch used for extreme ratios.
        Overlaps windowed grains from the source at calculated output
        positions, then normalizes to compensate for overlap density. */
    std::vector<float> processGranular(const std::vector<float>& input,
                                        double stretchRatio);

    /** WSOLA (Waveform Similarity Overlap-Add) time stretch.
        Time-domain stretching algorithm that preserves transients better
        than phase vocoder for percussive sounds. */
    std::vector<float> processWSOLA(const std::vector<float>& input,
                                    double stretchRatio);

    /** For a given source sample position, return the effective playback
        speed (considering curve, ratio, and default). */
    double getEffectiveSpeed(double normalizedSourcePos) const;

    /** Pre-compute a mapping from output positions to source positions
        based on the current speed curve and input length.
        The mapping is a series of (outputPos, sourcePos) pairs that
        can be interpolated during synthesis. */
    void precomputeMapping(int numInputSamples);

    /** Given an output sample position, find the corresponding source
        sample position via the pre-computed mapping.  Falls back to
        linear mapping when no curve is active. */
    double getSourceFromOutput(double outputPos) const;

    /** (Re)compute the Hann window table. */
    void recomputeWindow();

    //@}

    //==============================================================================
    /** @name Per-frame analysis storage */
    struct AnalysisFrame
    {
        std::vector<float> magnitude;  ///< Magnitudes for bins 0..fftSize/2
        std::vector<float> phase;      ///< Phases    for bins 0..fftSize/2
    };

    /** Source-output mapping entry used when a speed curve is active. */
    struct MappingPoint
    {
        double outputPos = 0.0;  ///< Cumulative output sample position
        double sourcePos = 0.0;  ///< Corresponding source sample position
    };

    //==============================================================================
    /** @name Members */
    //@{

    float stretchRatio_    = 1.0f;
    float defaultSpeed_    = 1.0f;

    bool   midiFitMode_    = false;
    double targetDuration_ = 1.0;
    float  noteVelocity_   = 0.7f;

    double sampleRate_ = 44100.0;

    int fftSize_ = 2048;
    int hopSize_ = 512;

    StretchCurve curve_;
    bool hasCurve_ = false;

    // Pre-computed Hann window coefficients (size = fftSize_)
    std::vector<float> windowTable_;

    // Phase vocoder analysis data
    std::vector<AnalysisFrame> analysisData_;

    // Speed-curve mapping
    std::vector<MappingPoint> mapping_;

    // Per-bin phase accumulator for the current synthesis pass
    std::vector<float> synPhase_;

    // JUCE FFT engine
    std::unique_ptr<juce::dsp::FFT> fft_;

    //@}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimeStretchEngine)
};

} // namespace ana
