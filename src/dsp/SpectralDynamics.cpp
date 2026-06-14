#include "SpectralDynamics.h"
#include "SIMDSupport.h"

#include <cmath>
#include <cstring>

namespace ana {

//==============================================================================
//  Internal helpers
//==============================================================================
namespace {

constexpr float kMinLevel     = 1e-10f;   /**< -200 dB floor — avoid log(0) / div-by-zero. */
constexpr float kAmpThreshold = 1e-6f;    /**< Activity threshold for partials.             */

inline float msToSec(float ms) noexcept
{
    return ms * 0.001f;
}

// Smoothstep: Hermite interpolation in [0, 1].
inline float smoothstep(float t) noexcept
{
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

//==============================================================================
//  Construction / reset
//==============================================================================

SpectralDynamics::SpectralDynamics()
{
    envelopeState_.resize(static_cast<size_t>(PartialDataSIMD::kMaxPartials), 0.0f);
    gainState_.resize(static_cast<size_t>(PartialDataSIMD::kMaxPartials), 1.0f);
    std::memset(scAmplitude_, 0, sizeof(scAmplitude_));
    updateCoefficients();
}

void SpectralDynamics::reset()
{
    mode_               = Mode::Compressor;
    detection_          = Detection::RMS;
    threshold_          = -24.0f;
    ratio_              = 4.0f;
    attackMs_           = 10.0f;
    releaseMs_          = 50.0f;
    knee_               = 6.0f;
    makeupGain_         = 0.0f;
    mix_                = 1.0f;

    bandFocusEnabled_   = false;
    bandCenterHz_       = 1000.0f;
    bandWidthOctaves_   = 3.0f;
    bandBypass_         = false;

    sidechainActive_    = false;
    std::memset(scAmplitude_, 0, sizeof(scAmplitude_));

    sampleRate_         = 44100.0;

    SIMDKernels::vectorFill(envelopeState_.data(), 0.0f,
                            static_cast<int>(envelopeState_.size()));
    SIMDKernels::vectorFill(gainState_.data(), 1.0f,
                            static_cast<int>(gainState_.size()));

    updateCoefficients();
}

//==============================================================================
//  Setters
//==============================================================================

void SpectralDynamics::setMode(Mode mode) noexcept
{
    mode_ = mode;
}

void SpectralDynamics::setDetection(Detection detection) noexcept
{
    detection_ = detection;

    // Reset envelope state when switching away from Envelope mode
    // so that a later switch back starts from a clean slate.
    if (detection_ != Detection::Envelope)
    {
        SIMDKernels::vectorFill(envelopeState_.data(), 0.0f,
                                static_cast<int>(envelopeState_.size()));
    }
}

void SpectralDynamics::setThreshold(float dB)
{
    threshold_ = juce::jlimit(-80.0f, 0.0f, dB);
}

void SpectralDynamics::setRatio(float ratio)
{
    // Compressor: 1:1 to 20:1;  Expander: 1:1 to 0.2:1
    // Accept the full range and rely on the mode to interpret.
    ratio_ = juce::jlimit(0.2f, 20.0f, ratio);
}

void SpectralDynamics::setAttack(float ms)
{
    attackMs_ = juce::jlimit(0.1f, 100.0f, ms);
    updateCoefficients();
}

void SpectralDynamics::setRelease(float ms)
{
    releaseMs_ = juce::jlimit(1.0f, 500.0f, ms);
    updateCoefficients();
}

void SpectralDynamics::setKnee(float dB)
{
    knee_ = juce::jlimit(0.0f, 20.0f, dB);
}

void SpectralDynamics::setMakeupGain(float dB)
{
    makeupGain_ = juce::jlimit(0.0f, 24.0f, dB);
}

void SpectralDynamics::setMix(float mix)
{
    mix_ = juce::jlimit(0.0f, 1.0f, mix);
}

void SpectralDynamics::setBandFocus(float centerHz, float widthOctaves)
{
    bandCenterHz_     = juce::jlimit(20.0f, 20000.0f, centerHz);
    bandWidthOctaves_ = juce::jmax(0.25f, widthOctaves);
    bandFocusEnabled_ = true;
}

void SpectralDynamics::setBandBypass(bool bypass) noexcept
{
    bandBypass_ = bypass;
}

void SpectralDynamics::setSidechain(const PartialDataSIMD& sidechainPartials)
{
    std::memcpy(scAmplitude_, sidechainPartials.amplitude, sizeof(scAmplitude_));
    sidechainActive_ = true;
}

//==============================================================================
//  Coefficient update
//==============================================================================

void SpectralDynamics::updateCoefficients() noexcept
{
    const float attackSec  = msToSec(attackMs_);
    const float releaseSec = msToSec(releaseMs_);
    const float fs         = static_cast<float>(sampleRate_);

    // Standard one-pole time-constant coefficient:
    //   coeff = exp(-1 / (tau * fs))
    // The +epsilon prevents division by zero at extreme sample rates.
    constexpr float kEps = 1e-30f;
    attackCoeff_  = std::exp(-1.0f / (attackSec  * fs + kEps));
    releaseCoeff_ = std::exp(-1.0f / (releaseSec * fs + kEps));
}

//==============================================================================
//  Band focus query
//==============================================================================

bool SpectralDynamics::isInBand(float freqHz) const noexcept
{
    if (!bandFocusEnabled_)
        return true;

    // Band = [center / 2^(width/2), center * 2^(width/2)]
    const float halfOct = bandWidthOctaves_ * 0.5f;
    const float lower   = bandCenterHz_ * std::pow(2.0f, -halfOct);
    const float upper   = bandCenterHz_ * std::pow(2.0f,  halfOct);

    return freqHz >= lower && freqHz <= upper;
}

//==============================================================================
//  Envelope follower
//==============================================================================

void SpectralDynamics::updateEnvelope(const PartialDataSIMD& partials)
{
    const int N = partials.maxPartials;

    if (detection_ != Detection::Envelope)
    {
        // In RMS/Peak mode the envelope state is unused — decay it silently
        // so that if the user switches to Envelope mid-flight the state is
        // near zero rather than stale.
        for (int i = 0; i < N; ++i)
            envelopeState_[i] *= releaseCoeff_;
        return;
    }

    // Envelope detection: one-pole follower per partial
    for (int i = 0; i < N; ++i)
    {
        if (!partials.isActive(i))
        {
            envelopeState_[i] *= releaseCoeff_;
            continue;
        }

        const float input = std::abs(partials.amplitude[i]);
        const float coeff = (input > envelopeState_[i]) ? attackCoeff_ : releaseCoeff_;

        // y[n] = (1 - a) * x[n] + a * y[n-1]
        envelopeState_[i] = (1.0f - coeff) * input + coeff * envelopeState_[i];
    }
}

//==============================================================================
//  Gain computer
//==============================================================================

float SpectralDynamics::computeGain(float levelLinear) const noexcept
{
    // Silence floor — avoid log(0), pow(NaN).
    if (levelLinear <= kMinLevel)
        return 1.0f;

    const float levelDB = 20.0f * std::log10(levelLinear);
    const float T       = threshold_;
    const float R       = ratio_;
    const float W       = knee_;
    const float W2      = W * 0.5f;

    float gainDB = 0.0f;  // positive = boost, negative = reduction

    switch (mode_)
    {
        //======================================================================
        //  Compressor  —  attenuate when level > threshold
        //======================================================================
        case Mode::Compressor:
        {
            const float overshoot = levelDB - T;

            if (W > 0.0f && overshoot > -W2 && overshoot < W2)
            {
                // Soft-knee transition
                const float x      = (overshoot + W2) / W;        // [0, 1]
                const float smooth = smoothstep(x);
                gainDB = -overshoot * (1.0f - 1.0f / R) * smooth;
            }
            else if (overshoot >= W2)
            {
                // Full compression
                gainDB = -overshoot * (1.0f - 1.0f / R);
            }
            // else: below threshold (+ below knee region) → unity gain
            break;
        }

        //======================================================================
        //  Expander  —  attenuate when level < threshold
        //  The stored ratio is < 1.  Effective expansion factor = 1 / R.
        //======================================================================
        case Mode::Expander:
        {
            const float deficit  = T - levelDB;  // positive below threshold
            const float effRatio = 1.0f / R;     // > 1 when R < 1

            if (W > 0.0f && deficit > -W2 && deficit < W2)
            {
                const float x      = (deficit + W2) / W;
                const float smooth = smoothstep(x);
                gainDB = -deficit * (effRatio - 1.0f) * smooth;
            }
            else if (deficit >= W2)
            {
                gainDB = -deficit * (effRatio - 1.0f);
            }
            break;
        }

        //======================================================================
        //  Limiter  —  hard clip to threshold (infinite ratio)
        //======================================================================
        case Mode::Limiter:
        {
            const float overshoot = levelDB - T;
            if (overshoot > 0.0f)
                gainDB = -overshoot;
            break;
        }

        //======================================================================
        //  Upward compressor  —  boost when level < threshold
        //======================================================================
        case Mode::UpwardCompressor:
        {
            const float deficit = T - levelDB;  // positive below threshold

            if (W > 0.0f && deficit > -W2 && deficit < W2)
            {
                const float x      = (deficit + W2) / W;
                const float smooth = smoothstep(x);
                gainDB = deficit * (1.0f - 1.0f / R) * smooth;
            }
            else if (deficit >= W2)
            {
                gainDB = deficit * (1.0f - 1.0f / R);
            }
            break;
        }
    }

    return std::pow(10.0f, gainDB / 20.0f);
}

//==============================================================================
//  Main processing
//==============================================================================

void SpectralDynamics::process(PartialDataSIMD& partials)
{
    // -----------------------------------------------------------------------
    //  Early-outs
    // -----------------------------------------------------------------------
    if (partials.activeCount == 0 || partials.maxPartials <= 0 || bandBypass_)
        return;

    // -----------------------------------------------------------------------
    //  Resample-rate guard — keep coefficients in sync
    // -----------------------------------------------------------------------
    if (partials.sampleRate != sampleRate_)
    {
        sampleRate_ = partials.sampleRate;
        updateCoefficients();
    }

    const int N = partials.maxPartials;

    // -----------------------------------------------------------------------
    //  Ensure state vectors are sized
    // -----------------------------------------------------------------------
    if (static_cast<int>(envelopeState_.size()) != N)
    {
        envelopeState_.resize(static_cast<size_t>(N), 0.0f);
        gainState_.resize(static_cast<size_t>(N), 1.0f);
    }

    // -----------------------------------------------------------------------
    //  Envelope follower update (for Envelope detection mode)
    // -----------------------------------------------------------------------
    updateEnvelope(partials);

    // -----------------------------------------------------------------------
    //  Pre-compute makeup gain (linear)
    // -----------------------------------------------------------------------
    const float makeupLinear = std::pow(10.0f, makeupGain_ / 20.0f);

    // -----------------------------------------------------------------------
    //  Per-partial dynamics processing
    // -----------------------------------------------------------------------
    // We use a scratch gain buffer so we can apply the final gains in one
    // vectorized pass, but the gain computation itself remains per-partial
    // since each partial has a unique level (non-linear cost is dominated
    // by log10/pow, not by the vector width).
    //
    // For inactive / band-bypassed partials the gain is held at unity so
    // they pass through untouched.
    // -----------------------------------------------------------------------

    // Scratch buffer for effective gains (512 floats fits on the stack)
    float effectiveGain[PartialDataSIMD::kMaxPartials];

    for (int i = 0; i < N; ++i)
    {
        if (!partials.isActive(i))
        {
            // Decay states toward rest position
            envelopeState_[i] *= releaseCoeff_;
            gainState_[i] = (1.0f - releaseCoeff_) * 1.0f
                          + releaseCoeff_ * gainState_[i];
            effectiveGain[i] = 1.0f;  // pass-through
            continue;
        }

        // Skip partials outside the focused frequency band
        if (!isInBand(partials.frequency[i]))
        {
            effectiveGain[i] = 1.0f;
            continue;
        }

        // ---- 1. Level detection ----
        float detectedLevel;
        if (sidechainActive_)
        {
            detectedLevel = std::abs(scAmplitude_[i]);
        }
        else if (detection_ == Detection::Envelope)
        {
            detectedLevel = envelopeState_[i];
        }
        else
        {
            // RMS / Peak — instantaneous
            detectedLevel = std::abs(partials.amplitude[i]);
        }

        // ---- 2. Gain computation ----
        const float targetGain = computeGain(detectedLevel);

        // ---- 3. Attack / release smoothing ----
        // Use attack coefficient when gain is decreasing (compressor engaging),
        // release coefficient when gain is increasing (compressor releasing).
        const float coeff = (targetGain < gainState_[i])
                                ? attackCoeff_
                                : releaseCoeff_;

        gainState_[i] = (1.0f - coeff) * targetGain + coeff * gainState_[i];

        // ---- 4. Build effective gain (with makeup and mix) ----
        //   processed = raw * linearGain * makeupLinear
        //   output    = raw * (1 - mix) + processed * mix
        //   effectiveGain = (1 - mix) + mix * linearGain * makeupLinear
        const float wetGain = gainState_[i] * makeupLinear;
        effectiveGain[i] = 1.0f - mix_ + mix_ * wetGain;
    }

    // -----------------------------------------------------------------------
    //  Apply effective gains (vectorized)
    // -----------------------------------------------------------------------
    SIMDKernels::vectorMul(partials.amplitude,
                           partials.amplitude,
                           effectiveGain,
                           N);

    // Clamp to non-negative (defensive — gain should never be negative)
    for (int i = 0; i < N; ++i)
        if (partials.amplitude[i] < 0.0f)
            partials.amplitude[i] = 0.0f;

    // -----------------------------------------------------------------------
    //  Update active mask
    // -----------------------------------------------------------------------
    partials.updateActiveMask();
}

} // namespace ana
