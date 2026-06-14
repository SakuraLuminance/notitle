#include "VocoderMode.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

namespace ana {

//==============================================================================
// Constants
//==============================================================================
namespace {

constexpr float  kMinFreq       = 20.0f;
constexpr float  kMaxFreq       = 20000.0f;
constexpr float  kAmpThreshold  = 1e-6f;
constexpr int    kMinFftSize    = 512;
constexpr int    kMaxFftSize    = 2048;

/** Compute a Q factor suitable for the given number of bands so that
    adjacent bandpass filters have reasonable overlap (≈50 % at -3 dB). */
inline float bandQ(int numBands) noexcept
{
    return 0.8f + static_cast<float>(numBands - 8) * 0.175f;
    //   8 bands → Q ≈ 0.8   (1+ octave BW)
    //  16 bands → Q ≈ 2.2   (≈2/3 octave BW)
    //  32 bands → Q ≈ 5.0   (≈1/3 octave BW)
}

/** Clamp a value to [lo, hi]. */
template <typename T>
inline T clamp(T v, T lo, T hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace

//==============================================================================
VocoderMode::VocoderMode()
{
    bandEnvelopes_.resize(numBands_, 0.0f);
    bandFrequencies_.resize(numBands_, 0.0f);
}

//==============================================================================
// Configuration
//==============================================================================

void VocoderMode::setNumBands(int bands)
{
    bands = clamp(bands, 8, 32);
    if (bands == numBands_)
        return;

    numBands_ = bands;
    bandEnvelopes_.assign(static_cast<size_t>(numBands_), 0.0f);
    bandFrequencies_.resize(static_cast<size_t>(numBands_));

    // Resize scratch buffers that depend on numBands
    scratch_energies_.resize(static_cast<size_t>(numBands_));
    scratch_binCount_.resize(static_cast<size_t>(numBands_));

    filtersDirty_        = true;
    envelopeCoeffsDirty_ = true;
}

void VocoderMode::setMix(float mix)
{
    mix_ = clamp(mix, 0.0f, 1.0f);
}

void VocoderMode::setFormantShift(float semitones)
{
    formantShift_ = clamp(semitones, -12.0f, 12.0f);
}

void VocoderMode::setAttack(float ms)
{
    attackMs_ = clamp(ms, 1.0f, 100.0f);
    envelopeCoeffsDirty_ = true;
}

void VocoderMode::setRelease(float ms)
{
    releaseMs_ = clamp(ms, 1.0f, 500.0f);
    envelopeCoeffsDirty_ = true;
}

void VocoderMode::setSampleRate(double sr)
{
    sampleRate_ = sr > 0.0 ? sr : 44100.0;
    filtersDirty_        = true;
    envelopeCoeffsDirty_ = true;
}

void VocoderMode::setFftSize(int size)
{
    const int s = clamp(size, kMinFftSize, kMaxFftSize);

    // Snap to nearest valid power of two
    int valid = kMinFftSize;
    while (valid * 2 <= s && valid * 2 <= kMaxFftSize)
        valid *= 2;

    if (valid == fftSize_)
        return;

    fftSize_    = valid;
    fftHopSize_ = fftSize_ / 4;

    fft_ = std::make_unique<juce::dsp::FFT>(static_cast<int>(std::log2(fftSize_)));

    buildWindow();

    // Overlap buffer for the carrier output (one channel initially)
    overlapBuffer_.assign(static_cast<size_t>(fftSize_), 0.0f);

    // Resize FFT scratch buffers
    scratch_fftIn_.resize(static_cast<size_t>(fftSize_));
    scratch_magnitude_.resize(static_cast<size_t>(fftSize_ / 2 + 1));
}

//==============================================================================
// Modulator
//==============================================================================

void VocoderMode::setModulator(const std::vector<float>& audio, double sampleRate)
{
    modulatorBuffer_       = audio;
    modulatorSampleRate_   = sampleRate > 0.0 ? sampleRate : 44100.0;
}

//==============================================================================
// Processing
//==============================================================================

void VocoderMode::process(PartialDataSIMD& carrierPartials)
{
    if (mix_ <= 0.0f || modulatorBuffer_.empty())
        return;

    if (modulatorBuffer_.size() < 2)
        return;

    // 1. Analyse modulator into band energies (pre-allocated scratch buffer)
    scratch_energies_.assign(static_cast<size_t>(numBands_), 0.0f);

    switch (selectMode())
    {
        case Mode::FFT:
            analyseFFT(modulatorBuffer_, scratch_energies_);
            break;

        case Mode::FilterBank:
        default:
            analyseFilterBank(modulatorBuffer_, scratch_energies_);
            break;
    }

    // 2. Optionally apply formant shift
    if (std::abs(formantShift_) > 0.01f)
        shiftFormant(scratch_energies_, scratch_energies_);

    // 3. Apply to partials
    applyToPartials(carrierPartials, scratch_energies_);
}

void VocoderMode::processAudio(juce::AudioBuffer<float>& carrier,
                                const juce::AudioBuffer<float>& modulator)
{
    if (mix_ <= 0.0f)
        return;

    const int numChannels = std::min(carrier.getNumChannels(),
                                     modulator.getNumChannels());
    if (numChannels <= 0)
        return;

    const int numSamples = std::min(carrier.getNumSamples(),
                                    modulator.getNumSamples());
    if (numSamples < 2)
        return;

    // --- Analyse modulator into band energies ---
    // Use the first channel of the modulator for analysis (typical vocoder behaviour)
    const float* modData = modulator.getReadPointer(0);

    // Copy modulator frame into pre-allocated scratch buffer
    scratch_modFrame_.resize(static_cast<size_t>(numSamples));
    std::copy(modData, modData + numSamples, scratch_modFrame_.begin());

    scratch_energies_.assign(static_cast<size_t>(numBands_), 0.0f);

    switch (selectMode())
    {
        case Mode::FFT:
            analyseFFT(scratch_modFrame_, scratch_energies_);
            break;

        case Mode::FilterBank:
        default:
            analyseFilterBank(scratch_modFrame_, scratch_energies_);
            break;
    }

    // Optionally apply formant shift
    if (std::abs(formantShift_) > 0.01f)
        shiftFormant(scratch_energies_, scratch_energies_);

    // --- Apply to carrier via filter bank ---
    // If the filter bank has been rebuilt, recreate carrier filter state
    if (filtersDirty_)
        buildFilterBank();

    const int nBands = numBands_;

    // Prepare carrier filters if needed
    if (static_cast<int>(carrierFilters_.size()) != nBands * numChannels)
    {
        carrierFilters_.resize(static_cast<size_t>(nBands * numChannels));
        for (auto& f : carrierFilters_)
            f.reset();
    }

    // For each channel, for each band: filter the carrier through the
    // bandpass filter, multiply by the band envelope, accumulate into a
    // wet mix buffer, then blend with dry.
    //
    // We process each channel independently.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* src = carrier.getReadPointer(ch);
        float*       dst = carrier.getWritePointer(ch);

        // Wet mix buffer for this channel (pre-allocated scratch)
        scratch_wet_.assign(static_cast<size_t>(numSamples), 0.0f);

        for (int band = 0; band < nBands; ++band)
        {
            const float env = scratch_energies_[static_cast<size_t>(band)];

            if (env < kAmpThreshold)
                continue;

            juce::dsp::IIR::Filter<float>& filter =
                carrierFilters_[static_cast<size_t>(band * numChannels + ch)];
            filter.coefficients = bandFilterCoeffs_[static_cast<size_t>(band)];

            // Block-process the entire buffer through the IIR filter
            // (enables JUCE's internal SIMD-friendly loops for the biquad)
            {
                float* filterBuf = scratch_modFrame_.data();
                float* srcPtrs[] = { const_cast<float*>(src) };
                float* dstPtrs[] = { filterBuf };
                auto srcBlock = juce::dsp::AudioBlock<float>(srcPtrs, 1, static_cast<size_t>(numSamples));
                auto dstBlock = juce::dsp::AudioBlock<float>(dstPtrs, 1, static_cast<size_t>(numSamples));
                juce::dsp::ProcessContextNonReplacing<float> ctx(srcBlock, dstBlock);
                filter.process(ctx);
            }

            for (int s = 0; s < numSamples; ++s)
                scratch_wet_[static_cast<size_t>(s)] += scratch_modFrame_[static_cast<size_t>(s)] * env;
        }

        // Wet / dry mix: output = dry * (1 - mix) + wet * mix
        if (mix_ >= 1.0f)
        {
            for (int s = 0; s < numSamples; ++s)
                dst[s] = scratch_wet_[static_cast<size_t>(s)];
        }
        else
        {
            const float dryGain = 1.0f - mix_;
            for (int s = 0; s < numSamples; ++s)
                dst[s] = src[s] * dryGain + scratch_wet_[static_cast<size_t>(s)] * mix_;
        }
    }
}

//==============================================================================
void VocoderMode::reset()
{
    std::fill(bandEnvelopes_.begin(), bandEnvelopes_.end(), 0.0f);

    for (auto& f : carrierFilters_)
        f.reset();

    std::fill(overlapBuffer_.begin(), overlapBuffer_.end(), 0.0f);
    modulatorBuffer_.clear();
    filtersDirty_        = true;
    envelopeCoeffsDirty_ = true;
}

//==============================================================================
// Mode selection
//==============================================================================

VocoderMode::Mode VocoderMode::selectMode() const noexcept
{
    // FFT mode is enabled when the user has explicitly set fftSize > 0
    // and we have enough samples for a meaningful transform.
    return (fftSize_ >= kMinFftSize) ? Mode::FFT : Mode::FilterBank;
}

//==============================================================================
// Filter bank construction
//==============================================================================

void VocoderMode::buildFilterBank()
{
    const size_t n = static_cast<size_t>(numBands_);

    bandFilterCoeffs_.clear();
    bandFilterCoeffs_.reserve(n);
    bandFrequencies_.resize(n);

    const float logMin  = std::log(kMinFreq);
    const float logMax  = std::log(kMaxFreq);
    const float logStep = (logMax - logMin) / static_cast<float>(numBands_ - 1);
    const float Q       = bandQ(numBands_);

    for (int i = 0; i < numBands_; ++i)
    {
        const float freq = std::exp(logMin + logStep * static_cast<float>(i));
        bandFrequencies_[static_cast<size_t>(i)] = freq;

        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass(
            sampleRate_, freq, Q);

        bandFilterCoeffs_.push_back(coeffs);
    }

    // Resize carrier filter state
    // (actual channel count not known here — resized in processAudio)
    carrierFilters_.clear();

    // Precompute bin-to-band LUT for FFT mode
    if (fftSize_ >= kMinFftSize)
    {
        const int halfFFT = fftSize_ / 2;
        const int numBins = halfFFT + 1;
        binToBandLUT_.resize(static_cast<size_t>(numBins));
        const float binFreq = static_cast<float>(sampleRate_) / static_cast<float>(fftSize_);

        for (int bin = 0; bin < numBins; ++bin)
        {
            const float freq = static_cast<float>(bin) * binFreq;
            if (freq >= bandFrequencies_.back())
            {
                binToBandLUT_[static_cast<size_t>(bin)] = numBands_ - 1;
            }
            else
            {
                auto it = std::upper_bound(bandFrequencies_.begin() + 1,
                                           bandFrequencies_.end(), freq);
                const int band = static_cast<int>(it - bandFrequencies_.begin()) - 1;
                binToBandLUT_[static_cast<size_t>(bin)] = std::max(0, band);
            }
        }
    }

    filtersDirty_ = false;
}

//==============================================================================
// Envelope follower coefficients
//==============================================================================

void VocoderMode::updateEnvelopeCoeffs()
{
    const float attackSamples  = attackMs_  * 0.001f * static_cast<float>(sampleRate_);
    const float releaseSamples = releaseMs_ * 0.001f * static_cast<float>(sampleRate_);

    // One-pole smoothing coefficient: alpha = 1 - exp(-1 / tau_samples)
    const float aCoeff = 1.0f - std::exp(-1.0f / attackSamples);
    const float rCoeff = 1.0f - std::exp(-1.0f / releaseSamples);

    attackCoeffs_.assign(static_cast<size_t>(numBands_), aCoeff);
    releaseCoeffs_.assign(static_cast<size_t>(numBands_), rCoeff);

    envelopeCoeffsDirty_ = false;
}

//==============================================================================
// Modulator analysis — Filter Bank
//==============================================================================

void VocoderMode::analyseFilterBank(const std::vector<float>& modulator,
                                     std::vector<float>& energies)
{
    if (filtersDirty_)
        buildFilterBank();
    if (envelopeCoeffsDirty_)
        updateEnvelopeCoeffs();

    // Ensure envelope storage matches
    if (static_cast<int>(bandEnvelopes_.size()) != numBands_)
        bandEnvelopes_.assign(static_cast<size_t>(numBands_), 0.0f);

    const size_t nBands = static_cast<size_t>(numBands_);

    energies.assign(nBands, 0.0f);

    const size_t numSamples = modulator.size();

    for (size_t band = 0; band < nBands; ++band)
    {
        // Create a IIR filter for this band analysis
        juce::dsp::IIR::Filter<float> filter;
        filter.coefficients = bandFilterCoeffs_[band];
        filter.reset();

        float env = bandEnvelopes_[band];
        const float aCoeff = attackCoeffs_[band];
        const float rCoeff = releaseCoeffs_[band];

        // Process every sample: filter -> rectify -> envelope follower
        for (size_t s = 0; s < numSamples; ++s)
        {
            const float filtered = filter.processSample(modulator[s]);
            const float absVal   = std::abs(filtered);

            // Envelope follower (classic analog-style)
            if (absVal > env)
                env += aCoeff * (absVal - env);
            else
                env += rCoeff * (absVal - env);
        }

        energies[band]      = env;
        bandEnvelopes_[band] = env;
    }
}

        energies[band]      = env;
        bandEnvelopes_[band] = env;
    }
}

//==============================================================================
// Modulator analysis — FFT
//==============================================================================

void VocoderMode::analyseFFT(const std::vector<float>& modulator,
                              std::vector<float>& energies)
{
    if (envelopeCoeffsDirty_)
        updateEnvelopeCoeffs();

    if (fft_ == nullptr || fftSize_ < kMinFftSize)
    {
        // Fallback to filter bank if FFT not configured
        analyseFilterBank(modulator, energies);
        return;
    }

    const size_t nBands = static_cast<size_t>(numBands_);

    // Ensure envelope storage matches
    if (static_cast<int>(bandEnvelopes_.size()) != numBands_)
        bandEnvelopes_.assign(nBands, 0.0f);

    // Prepare FFT input buffer (windowed) — pre-allocated scratch
    const int fftSize = fftSize_;
    const int halfFFT = fftSize / 2;

    // Copy modulator samples (with clamping) and apply window
    const size_t copyLen = std::min(modulator.size(), static_cast<size_t>(fftSize));
    if (copyLen < 2)
    {
        energies.assign(nBands, 0.0f);
        return;
    }

    // Zero the FFT input buffer, then copy and window
    // (use pre-allocated scratch_fftIn_, sized to fftSize_)
    scratch_fftIn_.assign(static_cast<size_t>(fftSize), 0.0f);

    // Use the most recent fftSize samples
    const size_t offset = modulator.size() - copyLen;
    SIMDKernels::vectorMul(scratch_fftIn_.data(),
                           modulator.data() + offset,
                           fftWindow_.data(),
                           static_cast<int>(copyLen));

    // Perform FFT
    fft_->performRealOnlyForwardTransform(scratch_fftIn_.data());

    // Extract magnitude spectrum (scratch_fftIn_ now contains packed complex data)
    // JUCE performRealOnlyForwardTransform packs output as:
    //   data[0] = DC (real)
    //   data[1] = Nyquist (real)
    //   data[2], data[3] = bin 1 (real, imag)
    //   data[4], data[5] = bin 2 (real, imag)
    //   ...
    scratch_magnitude_.assign(static_cast<size_t>(halfFFT + 1), 0.0f);
    scratch_magnitude_[0] = std::abs(scratch_fftIn_[0]);  // |DC|
    scratch_magnitude_[static_cast<size_t>(halfFFT)] = std::abs(scratch_fftIn_[1]);  // |Nyquist| at index 1
    for (int k = 1; k < halfFFT; ++k)
    {
        const float re = scratch_fftIn_[static_cast<size_t>(k * 2)];
        const float im = scratch_fftIn_[static_cast<size_t>(k * 2 + 1)];
        scratch_magnitude_[static_cast<size_t>(k)] = std::sqrt(re * re + im * im);
    }

    // Map FFT bins to bands using precomputed LUT
    // Rebuild LUT if stale (e.g., FFT size changed after buildFilterBank)
    const size_t expectedLutSize = static_cast<size_t>(halfFFT + 1);
    if (binToBandLUT_.size() != expectedLutSize)
    {
        binToBandLUT_.resize(expectedLutSize);
        const float binFreq = static_cast<float>(sampleRate_) / static_cast<float>(fftSize);
        for (int bin = 0; bin <= halfFFT; ++bin)
        {
            const float freq = static_cast<float>(bin) * binFreq;
            if (freq >= bandFrequencies_.back())
            {
                binToBandLUT_[static_cast<size_t>(bin)] = numBands_ - 1;
            }
            else
            {
                auto it = std::upper_bound(bandFrequencies_.begin() + 1,
                                           bandFrequencies_.end(), freq);
                const int band = static_cast<int>(it - bandFrequencies_.begin()) - 1;
                binToBandLUT_[static_cast<size_t>(bin)] = std::max(0, band);
            }
        }
    }

    // Use pre-allocated scratch buffers for band accumulation
    energies.assign(nBands, 0.0f);
    scratch_binCount_.assign(nBands, 0);

    for (int bin = 0; bin <= halfFFT; ++bin)
    {
        const int band = binToBandLUT_[static_cast<size_t>(bin)];
        if (band >= 0 && band < numBands_)
        {
            energies[static_cast<size_t>(band)] +=
                scratch_magnitude_[static_cast<size_t>(bin)];
            ++scratch_binCount_[static_cast<size_t>(band)];
        }
    }

    // Average energies per band
    for (size_t i = 0; i < nBands; ++i)
    {
        if (scratch_binCount_[i] > 0)
            energies[i] /= static_cast<float>(scratch_binCount_[i]);
    }

    // Apply envelope followers to smooth the FFT-derived energies
    // (so they behave consistently with the filter-bank analysis)
    for (size_t band = 0; band < nBands; ++band)
    {
        float env = bandEnvelopes_[band];
        const float val = energies[band];
        const float aCoeff = attackCoeffs_[band];
        const float rCoeff = releaseCoeffs_[band];

        if (val > env)
            env += aCoeff * (val - env);
        else
            env += rCoeff * (val - env);

        energies[band]       = env;
        bandEnvelopes_[band] = env;
    }
}

//==============================================================================
// Formant shift
//==============================================================================

void VocoderMode::shiftFormant(
    const std::vector<float>& bandEnergies,
    std::vector<float>& shifted) const
{
    const size_t n = static_cast<size_t>(numBands_);

    // Shift factor in band-index space.
    // Positive formantShift → envelope is stretched upward by 2^(shift/12).
    // We read the source envelope at srcIdx = i / factor so that the
    // envelope appears shifted upward on the carrier.
    const float factor = std::pow(2.0f, formantShift_ / 12.0f);

    // Use stack array for safe aliasing (bandEnergies may == shifted, and n <= 32)
    float tmp[32];
    for (size_t i = 0; i < n; ++i)
    {
        const float srcIdx = static_cast<float>(i) / factor;

        if (srcIdx <= 0.0f)
        {
            tmp[i] = bandEnergies[0];
        }
        else if (srcIdx >= static_cast<float>(n - 1))
        {
            tmp[i] = bandEnergies[n - 1];
        }
        else
        {
            const size_t idxLo = static_cast<size_t>(srcIdx);
            const size_t idxHi = idxLo + 1;
            const float  frac  = srcIdx - static_cast<float>(idxLo);

            tmp[i] = bandEnergies[idxLo] * (1.0f - frac)
                   + bandEnergies[idxHi] * frac;
        }
    }

    shifted.resize(n);
    std::copy(tmp, tmp + n, shifted.begin());
}

//==============================================================================
// Apply to partials
//==============================================================================

void VocoderMode::applyToPartials(PartialDataSIMD& carrier,
                                   const std::vector<float>& bandEnergies)
{
    const size_t nBands = static_cast<size_t>(numBands_);
    const float  dryGain = 1.0f - mix_;

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        if (! carrier.isActive(i))
            continue;

        const float freq = carrier.frequency[i];
        if (freq <= 0.0f)
            continue;

        // --- Find the band for this frequency ---
        int band = numBands_ - 1;
        for (int b = 0; b < numBands_ - 1; ++b)
        {
            if (freq < bandFrequencies_[static_cast<size_t>(b + 1)])
            {
                band = b;
                break;
            }
        }

        // Interpolate between adjacent bands for smoother cross-fading
        float interpEnergy;
        if (band < numBands_ - 1)
        {
            const float fLo = bandFrequencies_[static_cast<size_t>(band)];
            const float fHi = bandFrequencies_[static_cast<size_t>(band + 1)];
            const float frac = (fHi > fLo)
                ? clamp((freq - fLo) / (fHi - fLo), 0.0f, 1.0f)
                : 0.0f;

            interpEnergy = bandEnergies[static_cast<size_t>(band)] * (1.0f - frac)
                         + bandEnergies[static_cast<size_t>(
                               std::min(band + 1, numBands_ - 1))] * frac;
        }
        else
        {
            interpEnergy = bandEnergies[static_cast<size_t>(band)];
        }

        // Clamp energy to a reasonable range to avoid blow-ups
        interpEnergy = clamp(interpEnergy, 0.0f, 10.0f);

        // Wet/dry mix on amplitude
        carrier.amplitude[i] *= dryGain + mix_ * interpEnergy;
    }
}

//==============================================================================
// Apply filter bank to audio
//==============================================================================

void VocoderMode::applyFilterBankToAudio(
    juce::AudioBuffer<float>& /*carrier*/,
    const std::vector<float>& /*bandEnergies*/)
{
    // This logic is inlined in processAudio() above to avoid an extra
    // buffer copy in the hot path.  The method exists for potential
    // future FFT-mode audio processing or testing.
}

//==============================================================================
// Window
//==============================================================================

void VocoderMode::buildWindow()
{
    if (fftSize_ <= 0)
        return;

    fftWindow_.resize(static_cast<size_t>(fftSize_));

    // Hann window
    for (int i = 0; i < fftSize_; ++i)
    {
        fftWindow_[static_cast<size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi
                       * static_cast<float>(i)
                       / static_cast<float>(fftSize_ - 1)));
    }
}

//==============================================================================
// Utility
//==============================================================================

float VocoderMode::channelEnergy(const float* data, int numSamples) noexcept
{
    float sum = 0.0f;

#if defined(__AVX2__)
    {
        __m256 acc = _mm256_setzero_ps();
        int i = 0;
        for (; i + 8 <= numSamples; i += 8)
        {
            const __m256 v = _mm256_loadu_ps(data + i);
            acc = _mm256_add_ps(acc, _mm256_mul_ps(v, v));
        }

        alignas(32) float tmp[8];
        _mm256_store_ps(tmp, acc);
        for (int j = 0; j < 8; ++j)
            sum += tmp[j];

        for (; i < numSamples; ++i)
            sum += data[i] * data[i];
    }
#elif defined(__SSE2__)
    {
        __m128 acc = _mm_setzero_ps();
        int i = 0;
        for (; i + 4 <= numSamples; i += 4)
        {
            const __m128 v = _mm_loadu_ps(data + i);
            acc = _mm_add_ps(acc, _mm_mul_ps(v, v));
        }

        alignas(16) float tmp[4];
        _mm_store_ps(tmp, acc);
        for (int j = 0; j < 4; ++j)
            sum += tmp[j];

        for (; i < numSamples; ++i)
            sum += data[i] * data[i];
    }
#else
    {
        for (int i = 0; i < numSamples; ++i)
            sum += data[i] * data[i];
    }
#endif

    return sum;
}

} // namespace ana
