#include "MultiFilter.h"
#include <cmath>
#include <algorithm>
#include <mutex>

namespace ana {

//==============================================================================
// Precomputed LUTs for gain staging
//==============================================================================
namespace {
constexpr int LUT_SIZE = 256;
float driveGainLUT[LUT_SIZE];
float resQLUT[LUT_SIZE];

void ensureLUTs() noexcept
{
    static std::once_flag lutInitFlag;
    std::call_once(lutInitFlag, [&]()
    {
        for (int i = 0; i < LUT_SIZE; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(LUT_SIZE - 1);
            driveGainLUT[i] = juce::Decibels::decibelsToGain(t * 24.0f);
            resQLUT[i]      = 0.707f + t * 19.293f;
        }
    });
}
} // namespace

//==============================================================================
// Resonance-to-Q mapping (LUT)
// 0% resonance → Q=0.707 (Butterworth), 100% resonance → Q≈20 (near self-oscillation)
static float resonanceToQ(float resonance) noexcept
{
    return resQLUT[juce::jlimit(0, LUT_SIZE - 1,
                                static_cast<int>(resonance * static_cast<float>(LUT_SIZE - 1)))];
}

//==============================================================================
// Decibels-to-gain for drive (LUT): 0% → 0dB, 100% → +24dB
static float driveToGain(float drive) noexcept
{
    return driveGainLUT[juce::jlimit(0, LUT_SIZE - 1,
                                     static_cast<int>(drive * static_cast<float>(LUT_SIZE - 1)))];
}

//==============================================================================
MultiFilter::MultiFilter()  { ensureLUTs(); }
MultiFilter::~MultiFilter() {}

//==============================================================================
void MultiFilter::prepare(const juce::dsp::ProcessSpec& newSpec)
{
    spec = newSpec;
    currentSampleRate = spec.sampleRate;

    for (auto& slot : slots)
    {
        slot.iirFilter.prepare(spec);
        slot.delayLine.prepare(spec);
        slot.lastDelayed[0] = 0.0f;
        slot.lastDelayed[1] = 0.0f;

        for (auto& ff : slot.formantFilters)
            ff.prepare(spec);

        updateCoefficients(slot);
        slot.coefficientsDirty = false;
        slot.lastParams = slot.params;
    }

    const int numChannels = static_cast<int>(spec.numChannels);
    const int blockSize   = static_cast<int>(spec.maximumBlockSize);

    parallelScratch.setSize(numChannels, blockSize);
    dryScratch.setSize(numChannels, blockSize);
    formantScratch.setSize(numChannels, blockSize);
    formantAccum.setSize(numChannels, blockSize);
}

//==============================================================================
void MultiFilter::reset()
{
    for (auto& slot : slots)
    {
        slot.iirFilter.reset();
        slot.delayLine.reset();
        slot.lastDelayed[0] = 0.0f;
        slot.lastDelayed[1] = 0.0f;

        for (auto& ff : slot.formantFilters)
            ff.reset();
    }

    parallelScratch.clear();
    dryScratch.clear();
    formantAccum.clear();
}

//==============================================================================
int MultiFilter::addSlot(FilterType type, const FilterParams& params)
{
    FilterSlot slot;
    slot.type   = type;
    slot.params = params;
    slot.lastParams = params; // initial snapshot for change detection

    // Prepare if spec is valid
    if (spec.sampleRate > 0 && spec.maximumBlockSize > 0)
    {
        slot.iirFilter.prepare(spec);
        slot.delayLine.prepare(spec);

        if (type == FilterType::Formant)
        {
            // 5 formant filters (matching FormantFilterBank)
            slot.formantFilters.resize(5);
            for (auto& ff : slot.formantFilters)
                ff.prepare(spec);
        }

        updateCoefficients(slot);
        slot.coefficientsDirty = false;
    }

    invalidateFreqResponseCache();
    slots.push_back(std::move(slot));
    return static_cast<int>(slots.size()) - 1;
}

//==============================================================================
void MultiFilter::removeSlot(int index)
{
    if (index >= 0 && index < static_cast<int>(slots.size()))
        slots.erase(slots.begin() + index);
    invalidateFreqResponseCache();
}

void MultiFilter::clearSlots()
{
    slots.clear();
    invalidateFreqResponseCache();
}

FilterSlot& MultiFilter::getSlot(int index) { return slots[index]; }
int MultiFilter::getNumSlots() const noexcept { return static_cast<int>(slots.size()); }

//==============================================================================
void MultiFilter::setRoutingMode(RoutingMode mode) noexcept
{
    if (currentRouting != mode)
    {
        currentRouting = mode;
        invalidateFreqResponseCache();
    }
}
RoutingMode MultiFilter::getRoutingMode() const noexcept    { return currentRouting; }

//==============================================================================
void MultiFilter::setMasterGain(float gain) noexcept { masterGainValue = gain; }
float MultiFilter::getMasterGain() const noexcept    { return masterGainValue; }

//==============================================================================
void MultiFilter::markCoefficientsDirty() noexcept
{
    for (auto& slot : slots)
        slot.coefficientsDirty = true;
    invalidateFreqResponseCache();
}

void MultiFilter::invalidateFreqResponseCache() const noexcept
{
    freqResponseCacheValid_ = false;
}

//==============================================================================
//  Coefficient updates
//==============================================================================
void MultiFilter::updateCoefficients(FilterSlot& slot)
{
    const auto& p = slot.params;
    const float q = resonanceToQ(p.resonance);

    switch (slot.type)
    {
        case FilterType::LowPass:
            slot.iirFilter.coefficients =
                juce::dsp::IIR::Coefficients<float>::makeLowPass(
                    currentSampleRate, p.cutoff, q);
            break;

        case FilterType::HighPass:
            slot.iirFilter.coefficients =
                juce::dsp::IIR::Coefficients<float>::makeHighPass(
                    currentSampleRate, p.cutoff, q);
            break;

        case FilterType::BandPass:
            slot.iirFilter.coefficients =
                juce::dsp::IIR::Coefficients<float>::makeBandPass(
                    currentSampleRate, p.cutoff, q);
            break;

        case FilterType::Notch:
            slot.iirFilter.coefficients =
                juce::dsp::IIR::Coefficients<float>::makeNotch(
                    currentSampleRate, p.cutoff, q);
            break;

        case FilterType::AllPass:
            slot.iirFilter.coefficients =
                juce::dsp::IIR::Coefficients<float>::makeAllPass(
                    currentSampleRate, p.cutoff, q);
            break;

        case FilterType::Comb:
            // Comb uses delay line; coefficients handled indirectly
            break;

        case FilterType::Formant:
            updateFormantCoefficients(slot);
            break;

        case FilterType::Morph:
            updateMorphCoefficients(slot);
            break;
    }
}

//==============================================================================
void MultiFilter::updateFormantCoefficients(FilterSlot& slot)
{
    // Standard 'ah' vowel formant frequencies shifted by cutoff ratio
    const double base        = 1000.0;
    const double shiftFactor = std::max(0.1, slot.params.cutoff / base);

    const float q = resonanceToQ(slot.params.resonance);
    // Broaden bandwidth as resonance decreases
    const float bandwidth = 50.0f + (1.0f - slot.params.resonance) * 150.0f;

    // 5 formants: F1-F5 scaled by the shift factor (matches FormantFilterBank)
    const double formantFreqs[5] =
    {
        700.0  * shiftFactor,
        1100.0 * shiftFactor,
        2500.0 * shiftFactor,
        3500.0 * shiftFactor,
        4500.0 * shiftFactor
    };

    // Ensure we have 5 formant filters
    if (slot.formantFilters.size() != 5)
    {
        for (auto& ff : slot.formantFilters)
            ff.reset();
        slot.formantFilters.resize(5);
        for (auto& ff : slot.formantFilters)
            ff.prepare(spec);
    }

    for (int i = 0; i < 5; ++i)
    {
        const double fc = juce::jlimit(20.0, currentSampleRate * 0.48,
                                       formantFreqs[i]);
        slot.formantFilters[i].coefficients =
            juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                currentSampleRate, fc, bandwidth, q);
    }
}

//==============================================================================
void MultiFilter::updateMorphCoefficients(FilterSlot& slot)
{
    const auto& p = slot.params;
    const float q = resonanceToQ(p.resonance);

    // Build coefficients for a given filter type + cutoff + Q
    auto makeCoeffs = [&](FilterType ft, double cutoff, float qVal)
        -> juce::dsp::IIR::Coefficients<float>::Ptr
    {
        switch (ft)
        {
            case FilterType::LowPass:  return juce::dsp::IIR::Coefficients<float>::makeLowPass(currentSampleRate, cutoff, qVal);
            case FilterType::HighPass: return juce::dsp::IIR::Coefficients<float>::makeHighPass(currentSampleRate, cutoff, qVal);
            case FilterType::BandPass: return juce::dsp::IIR::Coefficients<float>::makeBandPass(currentSampleRate, cutoff, qVal);
            case FilterType::Notch:    return juce::dsp::IIR::Coefficients<float>::makeNotch(currentSampleRate, cutoff, qVal);
            case FilterType::AllPass:  return juce::dsp::IIR::Coefficients<float>::makeAllPass(currentSampleRate, cutoff, qVal);
            default:                   return juce::dsp::IIR::Coefficients<float>::makeLowPass(currentSampleRate, cutoff, qVal);
        }
    };

    // Source uses morphSource type, target uses morphTarget type
    auto sourceCoeffs = makeCoeffs(p.morphSource, p.cutoff, q);
    auto targetCoeffs = makeCoeffs(p.morphTarget, p.cutoff, std::max(0.707f, q * 0.5f));

    slot.morphCoeffs = targetCoeffs;

    // Linear interpolation of coefficients
    const float t = juce::jlimit(0.0f, 1.0f, p.morphAmount);
    slot.iirFilter.coefficients = juce::dsp::IIR::Coefficients<float>::Ptr(
        new juce::dsp::IIR::Coefficients<float>(
            sourceCoeffs->b0 + t * (targetCoeffs->b0 - sourceCoeffs->b0),
            sourceCoeffs->b1 + t * (targetCoeffs->b1 - sourceCoeffs->b1),
            sourceCoeffs->b2 + t * (targetCoeffs->b2 - sourceCoeffs->b2),
            sourceCoeffs->a1 + t * (targetCoeffs->a1 - sourceCoeffs->a1),
            sourceCoeffs->a2 + t * (targetCoeffs->a2 - sourceCoeffs->a2)));
}

//==============================================================================
//  Main processing
//==============================================================================
void MultiFilter::process(juce::AudioBuffer<float>& buffer)
{
    if (slots.empty())
        return;

    // Clamp cutoff and update coefficients only for dirty slots
    bool anyDirty = false;
    for (auto& slot : slots)
    {
        slot.params.cutoff = juce::jlimit(20.0, currentSampleRate * 0.48,
                                          slot.params.cutoff);

        // Auto-detect param changes from external modification (via getSlot())
        if (slot.params != slot.lastParams)
        {
            slot.coefficientsDirty = true;
            anyDirty = true;
        }

        if (slot.coefficientsDirty)
        {
            updateCoefficients(slot);
            slot.coefficientsDirty = false;
            slot.lastParams = slot.params;
        }
    }

    if (anyDirty)
        invalidateFreqResponseCache();

    switch (currentRouting)
    {
        case RoutingMode::Serial:
        {
            juce::dsp::AudioBlock<float> block(buffer);
            processSerial(block);
            break;
        }
        case RoutingMode::Parallel:
            processParallel(buffer);
            break;
        case RoutingMode::Split:
        {
            juce::dsp::AudioBlock<float> block(buffer);
            processSplit(block);
            break;
        }
    }

    // Master gain
    if (masterGainValue != 1.0f)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        block.multiplyBy(masterGainValue);
    }
}

//==============================================================================
//  Serial processing
//==============================================================================
void MultiFilter::processSerial(juce::dsp::AudioBlock<float>& block)
{
    for (auto& slot : slots)
    {
        if (slot.bypassed)
            continue;

        // Save dry
        dryScratch.copyFrom(0, 0,
            const_cast<const float**>(block.getArrayOfChannels()),
            static_cast<int>(block.getNumChannels()),
            0, 0, static_cast<int>(block.getNumSamples()));

        const float driveGain = driveToGain(slot.params.drive);

        // Apply drive if > 0
        if (driveGain > 1.0001f)
        {
            for (int ch = 0; ch < static_cast<int>(block.getNumChannels()); ++ch)
                juce::FloatVectorOperations::multiply(
                    block.getChannelPointer(ch),
                    driveGain,
                    static_cast<int>(block.getNumSamples()));
        }

        // Apply filter
        switch (slot.type)
        {
            case FilterType::Comb:
                processCombSlot(slot, block);
                break;
            case FilterType::Formant:
                processFormantSlot(slot, block);
                break;
            default:
            {
                juce::dsp::ProcessContextReplacing<float> ctx(block);
                slot.iirFilter.process(ctx);
                break;
            }
        }

        // Wet/dry mix (SIMD-accelerated via FloatVectorOperations FMA pipeline)
        const float mix = juce::jlimit(0.0f, 1.0f, slot.params.mix);
        if (mix < 0.999f)
        {
            for (int ch = 0; ch < static_cast<int>(block.getNumChannels()); ++ch)
            {
                auto* dest = block.getChannelPointer(ch);
                const auto* dry = dryScratch.getReadPointer(ch);
                const int numSamples = static_cast<int>(block.getNumSamples());
                // dest = dest * mix + dry * (1.0f - mix) — single FMA pipeline
                juce::FloatVectorOperations::multiply(dest, mix, numSamples);
                juce::FloatVectorOperations::addWithMultiply(dest, dry, 1.0f - mix, numSamples);
            }
        }
    }
}

//==============================================================================
//  Parallel processing
//==============================================================================
void MultiFilter::processParallel(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // Save dry and clear output
    dryScratch.makeCopyOf(buffer, true);
    buffer.clear();

    for (auto& slot : slots)
    {
        if (slot.bypassed)
            continue;

        // Copy dry → scratch (per-channel copyFrom avoids makeCopyOf size check overhead)
        for (int ch = 0; ch < numChannels; ++ch)
            parallelScratch.copyFrom(ch, 0, dryScratch, ch, 0, numSamples);

        const float driveGain = driveToGain(slot.params.drive);
        if (driveGain > 1.0001f)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::multiply(
                    parallelScratch.getWritePointer(ch),
                    driveGain,
                    numSamples);
        }

        // Process scratch
        {
            juce::dsp::AudioBlock<float> scratchBlock(parallelScratch);

            switch (slot.type)
            {
                case FilterType::Comb:
                    processCombSlot(slot, scratchBlock);
                    break;
                case FilterType::Formant:
                    processFormantSlot(slot, scratchBlock);
                    break;
                default:
                {
                    juce::dsp::ProcessContextReplacing<float> ctx(scratchBlock);
                    slot.iirFilter.process(ctx);
                    break;
                }
            }
        }

        // Add weighted to output
        const float mix = juce::jlimit(0.0f, 1.0f, slot.params.mix);
        if (mix > 0.001f)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::addWithMultiply(
                    buffer.getWritePointer(ch),
                    parallelScratch.getReadPointer(ch),
                    mix,
                    numSamples);
        }
    }

    // Blend in dry signal — if total mix sum is < 1, fill remainder with dry
    float totalMix = 0.0f;
    for (const auto& slot : slots)
        if (!slot.bypassed)
            totalMix += juce::jlimit(0.0f, 1.0f, slot.params.mix);

    const float dryBlend = juce::jmax(0.0f, 1.0f - totalMix);
    if (dryBlend > 0.001f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::addWithMultiply(
                buffer.getWritePointer(ch),
                dryScratch.getReadPointer(ch),
                dryBlend,
                numSamples);
    }
}

//==============================================================================
//  Split processing
//==============================================================================
void MultiFilter::processSplit(juce::dsp::AudioBlock<float>& block)
{
    // Determine crossover frequency from slot 0's params (or default)
    const double xoverLow  = slots.empty() ? 200.0  : slots[0].params.crossoverLow;
    const double xoverHigh = slots.empty() ? 2000.0 : slots[0].params.crossoverHigh;

    // Create Linkwitz-Riley crossover filters (4th order = 2 biquads each)
    const double sr = currentSampleRate;
    const float  q  = 0.5f; // Linkwitz-Riley Q

    auto lpCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, xoverLow, q);
    auto hpCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, xoverLow, q);

    juce::dsp::IIR::Filter<float> lpFilter1, lpFilter2;
    juce::dsp::IIR::Filter<float> hpFilter1, hpFilter2;

    lpFilter1.coefficients = lpFilter2.coefficients = lpCoeffs;
    hpFilter1.coefficients = hpFilter2.coefficients = hpCoeffs;

    lpFilter1.prepare(spec);
    lpFilter2.prepare(spec);
    hpFilter1.prepare(spec);
    hpFilter2.prepare(spec);

    // Copy block → both scratch buffers
    dryScratch.clear();
    parallelScratch.clear();
    {
        juce::dsp::AudioBlock<float> dryBlock(dryScratch);
        dryBlock.copyFrom(block);
    }
    {
        juce::dsp::AudioBlock<float> parBlock(parallelScratch);
        parBlock.copyFrom(block);
    }

    // Low-pass branch (first half of slots) - processes dryScratch
    {
        juce::dsp::AudioBlock<float> lowBand(dryScratch);
        juce::dsp::ProcessContextReplacing<float> ctxLP1(lowBand);
        lpFilter1.process(ctxLP1);
        lpFilter2.process(ctxLP1);

        const int lowSlotCount = juce::jmax(1, static_cast<int>(slots.size()) / 2);
        for (int i = 0; i < lowSlotCount && i < static_cast<int>(slots.size()); ++i)
        {
            auto& slot = slots[i];
            if (slot.bypassed) continue;

            const float driveGain = driveToGain(slot.params.drive);
            if (driveGain > 1.0001f)
                lowBand.multiplyBy(driveGain);

            switch (slot.type)
            {
                case FilterType::Comb:
                    processCombSlot(slot, lowBand);
                    break;
                case FilterType::Formant:
                    processFormantSlot(slot, lowBand);
                    break;
                default:
                {
                    juce::dsp::ProcessContextReplacing<float> ctx(lowBand);
                    slot.iirFilter.process(ctx);
                    break;
                }
            }
        }

        block.copyFrom(lowBand);
    }

    // High-pass branch (remaining slots) - uses parallelScratch (original copy)
    {
        juce::dsp::AudioBlock<float> highBand(parallelScratch);
        juce::dsp::ProcessContextReplacing<float> ctxHP1(highBand);
        hpFilter1.process(ctxHP1);
        hpFilter2.process(ctxHP1);

        const int startSlot = juce::jmax(1, static_cast<int>(slots.size()) / 2);
        for (int i = startSlot; i < static_cast<int>(slots.size()); ++i)
        {
            auto& slot = slots[i];
            if (slot.bypassed) continue;

            const float driveGain = driveToGain(slot.params.drive);
            if (driveGain > 1.0001f)
                highBand.multiplyBy(driveGain);

            switch (slot.type)
            {
                case FilterType::Comb:
                    processCombSlot(slot, highBand);
                    break;
                case FilterType::Formant:
                    processFormantSlot(slot, highBand);
                    break;
                default:
                {
                    juce::dsp::ProcessContextReplacing<float> ctx(highBand);
                    slot.iirFilter.process(ctx);
                    break;
                }
            }
        }

        // Sum high band into output
        for (int ch = 0; ch < static_cast<int>(block.getNumChannels()); ++ch)
            juce::FloatVectorOperations::add(
                block.getChannelPointer(ch),
                highBand.getChannelPointer(ch),
                static_cast<int>(block.getNumSamples()));
    }
}

//==============================================================================
//  Comb filter processor
//==============================================================================
void MultiFilter::processCombSlot(FilterSlot& slot, juce::dsp::AudioBlock<float>& block)
{
    // cutoff = frequency (Hz), convert to delay samples: delay = sr / freq
    const int delaySamples = juce::jmax(1,
        static_cast<int>(std::round(currentSampleRate / slot.params.cutoff)));

    // resonance maps to feedback gain: 0..1 → 0..0.95
    const float feedback = juce::jlimit(-0.95f, 0.95f, slot.params.resonance * 0.95f);

    // drive maps to damping: 0..1 → 0.5..1.0 (higher drive = more damping)
    const float damping = 0.5f + slot.params.drive * 0.5f;

    auto& delay = slot.delayLine;

    for (int ch = 0; ch < static_cast<int>(block.getNumChannels()); ++ch)
    {
        auto* samples = block.getChannelPointer(ch);
        const int numSamples = static_cast<int>(block.getNumSamples());

        for (int i = 0; i < numSamples; ++i)
        {
            const float input   = samples[i];
            const float delayed = delay.popSample(ch, delaySamples);

            // Apply damping (low-pass filter on feedback path)
            float damped = delayed;
            if (ch < 2) // max 2 channels for damping state
            {
                damped = slot.lastDelayed[ch] * damping + delayed * (1.0f - damping);
                slot.lastDelayed[ch] = damped;
            }

            const float output = input + feedback * damped;
            delay.pushSample(ch, output);

            // Wet/dry mix
            const float mix = juce::jlimit(0.0f, 1.0f, slot.params.mix);
            samples[i] = input * (1.0f - mix) + output * mix;
        }
    }
}

//==============================================================================
//  Formant filter processor
//==============================================================================
void MultiFilter::processFormantSlot(FilterSlot& slot, juce::dsp::AudioBlock<float>& block)
{
    const int numChannels = static_cast<int>(block.getNumChannels());
    const int numSamples  = static_cast<int>(block.getNumSamples());

    // Ensure scratch buffers are correctly sized (should be from prepare())
    if (formantScratch.getNumChannels() != numChannels
        || formantScratch.getNumSamples() < numSamples)
        formantScratch.setSize(numChannels, numSamples);
    if (formantAccum.getNumChannels() != numChannels
        || formantAccum.getNumSamples() < numSamples)
        formantAccum.setSize(numChannels, numSamples, false, false, true);

    formantAccum.clear();

    // Process each formant: copy input → scratch, process, accumulate into formantAccum
    for (auto& ff : slot.formantFilters)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            formantScratch.copyFrom(ch, 0, block.getChannelPointer(ch), numSamples);

        auto scratchBlock = juce::dsp::AudioBlock<float>(formantScratch);
        ff.process(juce::dsp::ProcessContextReplacing<float>(scratchBlock));

        for (int ch = 0; ch < numChannels; ++ch)
            formantAccum.addFrom(ch, 0, formantScratch, ch, 0, numSamples);
    }

    // Write back with wet/dry mix (SIMD-accelerated FMA pipeline)
    const float mix = juce::jlimit(0.0f, 1.0f, slot.params.mix);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* dest = block.getChannelPointer(ch);
        const auto* wet = formantAccum.getReadPointer(ch);
        // dest = dest * (1.0f - mix) + wet * mix — single FMA pipeline
        juce::FloatVectorOperations::multiply(dest, 1.0f - mix, numSamples);
        juce::FloatVectorOperations::addWithMultiply(dest, wet, mix, numSamples);
    }
}

//==============================================================================
//  Frequency response
//==============================================================================
float MultiFilter::evalBiquadResponse(
    float frequency, double sampleRate,
    const juce::dsp::IIR::Coefficients<float>& coeffs)
{
    if (frequency <= 0.0f || frequency >= static_cast<float>(sampleRate * 0.5))
        return 1.0f;

    const float w = juce::MathConstants<float>::twoPi * frequency
                    / static_cast<float>(sampleRate);
    const float cosW  = std::cos(w);
    const float sinW  = std::sin(w);
    // Double-angle formulas: cos(2w) = 2cos²w - 1, sin(2w) = 2 sin w cos w
    const float cos2W = 2.0f * cosW * cosW - 1.0f;
    const float sin2W = 2.0f * sinW * cosW;

    // Numerator:  b0 + b1*z^-1 + b2*z^-2,   z = e^(jw)
    const float numReal = coeffs.b0
                          + coeffs.b1 * cosW
                          + coeffs.b2 * cos2W;
    const float numImag = -coeffs.b1 * sinW
                          - coeffs.b2 * sin2W;

    // Denominator: 1 + a1*z^-1 + a2*z^-2
    const float denReal = 1.0f
                          + coeffs.a1 * cosW
                          + coeffs.a2 * cos2W;
    const float denImag = -coeffs.a1 * sinW
                          - coeffs.a2 * sin2W;

    const float magNum = std::sqrt(numReal * numReal + numImag * numImag);
    const float magDen = std::sqrt(denReal * denReal + denImag * denImag);

    return (magDen > 1e-10f) ? (magNum / magDen) : 0.0f;
}

//==============================================================================
float MultiFilter::evalSlotResponse(const FilterSlot& slot, float frequency) const
{
    if (slot.bypassed)
        return 1.0f;

    switch (slot.type)
    {
        case FilterType::Comb:
        {
            // Approximate comb magnitude: |1 + g * z^(-D)|
            const float d = static_cast<float>(currentSampleRate / slot.params.cutoff);
            const float w = juce::MathConstants<float>::twoPi * frequency
                            / static_cast<float>(currentSampleRate);
            const float phase = w * d;
            const float g = slot.params.resonance * 0.95f;
            return std::sqrt(1.0f + g * g + 2.0f * g * std::cos(phase));
        }

        case FilterType::Formant:
        {
            float mag = 1.0f;
            for (const auto& ff : slot.formantFilters)
                if (ff.coefficients != nullptr)
                    mag *= evalBiquadResponse(frequency, currentSampleRate, *ff.coefficients);
            return mag;
        }

        case FilterType::Morph:
        {
            if (slot.iirFilter.coefficients != nullptr)
                return evalBiquadResponse(frequency, currentSampleRate,
                                           *slot.iirFilter.coefficients);
            return 1.0f;
        }

        default:
        {
            if (slot.iirFilter.coefficients != nullptr)
                return evalBiquadResponse(frequency, currentSampleRate,
                                           *slot.iirFilter.coefficients);
            return 1.0f;
        }
    }
}

//==============================================================================
std::vector<float> MultiFilter::getFrequencyResponse(
    const std::vector<float>& frequencies) const
{
    // Return cached response if valid and frequencies match
    if (freqResponseCacheValid_ && freqResponseFrequencies_ == frequencies)
        return freqResponseCache_;

    std::vector<float> magnitudes(frequencies.size(), 1.0f);

    if (! slots.empty())
    {
        switch (currentRouting)
        {
            case RoutingMode::Serial:
            {
                // Multiply magnitudes of each slot in series
                std::fill(magnitudes.begin(), magnitudes.end(), 1.0f);
                for (const auto& slot : slots)
                {
                    if (slot.bypassed) continue;
                    for (size_t i = 0; i < frequencies.size(); ++i)
                        magnitudes[i] *= evalSlotResponse(slot, frequencies[i]);
                }
                break;
            }

            case RoutingMode::Parallel:
            {
                // Sum magnitudes weighted by mix
                std::fill(magnitudes.begin(), magnitudes.end(), 0.0f);
                float totalMix = 0.0f;
                for (const auto& slot : slots)
                    if (!slot.bypassed)
                        totalMix += juce::jlimit(0.0f, 1.0f, slot.params.mix);

                if (totalMix > 0.001f)
                {
                    for (const auto& slot : slots)
                    {
                        if (slot.bypassed) continue;
                        const float weight = juce::jlimit(0.0f, 1.0f, slot.params.mix) / totalMix;
                        for (size_t i = 0; i < frequencies.size(); ++i)
                            magnitudes[i] += weight * evalSlotResponse(slot, frequencies[i]);
                    }
                }
                else
                {
                    std::fill(magnitudes.begin(), magnitudes.end(), 1.0f);
                }
                break;
            }

            case RoutingMode::Split:
            {
                std::fill(magnitudes.begin(), magnitudes.end(), 0.0f);

                const double xover = slots[0].params.crossoverLow;
                const int lowCount = juce::jmax(1, static_cast<int>(slots.size()) / 2);

                for (size_t i = 0; i < frequencies.size(); ++i)
                {
                    const float f = frequencies[i];
                    // Simple LR crossover response approximation
                    const float actualLp = (f <= xover) ? 1.0f
                                           : (f >= static_cast<float>(xover * 2.0)) ? 0.0f
                                           : 1.0f - (f - static_cast<float>(xover)) / static_cast<float>(xover);
                    const float actualHp = 1.0f - actualLp;

                    float lowMag = 1.0f;
                    for (int si = 0; si < lowCount && si < static_cast<int>(slots.size()); ++si)
                        if (!slots[si].bypassed)
                            lowMag *= evalSlotResponse(slots[si], f);

                    float highMag = 1.0f;
                    for (int si = lowCount; si < static_cast<int>(slots.size()); ++si)
                        if (!slots[si].bypassed)
                            highMag *= evalSlotResponse(slots[si], f);

                    magnitudes[i] = actualLp * lowMag + actualHp * highMag;
                }
                break;
            }
        }
    }

    // Cache result
    freqResponseCache_ = magnitudes;
    freqResponseFrequencies_ = frequencies;
    freqResponseCacheValid_ = true;

    return magnitudes;
}

} // namespace ana
