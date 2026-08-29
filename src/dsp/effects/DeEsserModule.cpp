#include "DeEsserModule.h"
#include <cmath>

namespace ana {

//==============================================================================
// Construction
DeEsserModule::DeEsserModule() {}

//==============================================================================
// Preparation
void DeEsserModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    blockSize_  = static_cast<int>(spec.maximumBlockSize);
    const int numCh = static_cast<int>(spec.numChannels);

    // --- HPF + LPF for split-band processing ---
    hpf_.prepare(spec);
    lpf_.prepare(spec);
    filtersDirty_ = true;
    updateFilters();

    // --- Envelope follower time constants ---
    // Fast attack (1 ms) catches sibilance onset quickly.
    // Medium release (20 ms) allows natural HF content between sibilance events.
    const float attackMs  = 1.0f;
    const float releaseMs = 20.0f;
    attackCoeff_  = std::exp(-1.0f / (attackMs  * static_cast<float>(sampleRate_) / 1000.0f));
    releaseCoeff_ = std::exp(-1.0f / (releaseMs * static_cast<float>(sampleRate_) / 1000.0f));

    // --- Reset state ---
    envelope_ = 0.0f;

    // --- Pre-allocate processing buffers ---
    highBand_.setSize(numCh, blockSize_, false, false, true);
    lowBand_.setSize(numCh, blockSize_, false, false, true);
    dryBuffer_.setSize(numCh, blockSize_, false, false, true);
}

//==============================================================================
// Reset
void DeEsserModule::reset()
{
    envelope_ = 0.0f;
    hpf_.reset();
    lpf_.reset();
}

//==============================================================================
// Main processing
void DeEsserModule::process(juce::AudioBuffer<float>& buffer)
{
    // Snapshot parameters once per block (message thread vs audio thread safety)
    const bool     bypassed     = bypassed_;
    const float    threshold    = threshold_;
    const float    reduction    = reduction_;
    const bool     listen       = listen_;
    const bool     filtersDirty = filtersDirty_;

    if (bypassed)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numCh      = buffer.getNumChannels();
    if (numSamples == 0)
        return;

    // Re-compute HPF coefficients if frequency changed
    if (filtersDirty)
    {
        updateFilters();
        filtersDirty_ = false;
    }

    //--- Step 1: Save dry (full-range) signal for later reconstruction ---
    dryBuffer_.makeCopyOf(buffer, true);

    //--- Step 2: Extract the sibilance band via HPF, and the low band via a
    // matched LPF.  The old reconstruction (dry - high) assumed the high band
    // was in phase with the dry signal; an IIR HPF shifts phase, so at the
    // crossover the "reduction" actually ADDED energy (+0.9 dB on a 6 kHz
    // sine).  A Butterworth LP/HP pair with the same cutoff sums flat.
    highBand_.makeCopyOf(buffer, true);
    lowBand_.makeCopyOf(buffer, true);
    {
        juce::dsp::AudioBlock<float> highBlock(highBand_);
        juce::dsp::ProcessContextReplacing<float> highCtx(highBlock);
        hpf_.process(highCtx);

        juce::dsp::AudioBlock<float> lowBlock(lowBand_);
        juce::dsp::ProcessContextReplacing<float> lowCtx(lowBlock);
        lpf_.process(lowCtx);
    }

    //--- Step 3: Envelope detection + gain reduction on the high band ---
    for (int s = 0; s < numSamples; ++s)
    {
        // Stereo-linked peak detection (max absolute value across channels)
        float absMax = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float sample = std::abs(highBand_.getSample(ch, s));
            if (sample > absMax)
                absMax = sample;
        }

        // One-pole smoothing (attack/release)
        if (absMax > envelope_)
            envelope_ = envelope_ + (1.0f - attackCoeff_) * (absMax - envelope_);
        else
            envelope_ = envelope_ + (1.0f - releaseCoeff_) * (absMax - envelope_);
        if (!std::isfinite(envelope_))
            envelope_ = 0.0f;
        envelope_ += 1e-15f; // denormal protection

        // Envelope level in dB
        const float envDb = 20.0f * std::log10(std::max(envelope_, 1e-10f));

        // Compute gain reduction: threshold_crossing overage, clamped to max reduction
        float gainDb = 0.0f;
        if (envDb > threshold)
        {
            gainDb = threshold - envDb;
            // Clamp to the user-specified maximum reduction
            if (gainDb < reduction)
                gainDb = reduction;
        }

        const float gainLin = juce::Decibels::decibelsToGain(gainDb);

        //--- Step 4: Reconstruction ---
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float highSample = highBand_.getSample(ch, s);
            float* out = buffer.getWritePointer(ch);

            if (listen)
            {
                // Listen mode: solo the processed band for tuning
                out[s] = highSample * gainLin;
            }
            else
            {
                // Split-band reconstruction:
                //   output = lowBand + highBand * gainLin
                // with both bands produced by a matched (power-complementary)
                // Butterworth LP/HP pair, so gainLin == 1 reconstructs the
                // input exactly.
                out[s] = lowBand_.getSample(ch, s) + highSample * gainLin;
            }
        }
    }
}

//==============================================================================
// Filter coefficient update
void DeEsserModule::updateFilters()
{
    if (sampleRate_ <= 0.0)
        return;

    auto hpCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        sampleRate_, frequency_, 0.707);  // Butterworth Q
    *hpf_.state = *hpCoeffs;

    auto lpCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
        sampleRate_, frequency_, 0.707);  // matched Butterworth pair
    *lpf_.state = *lpCoeffs;
}

//==============================================================================
// Parameter setters (all clamped via jlimit)
void DeEsserModule::setThreshold(float db)
{
    threshold_ = juce::jlimit(-40.0f, 0.0f, db);
}

void DeEsserModule::setFrequency(float hz)
{
    frequency_ = juce::jlimit(4000.0f, 10000.0f, hz);
    filtersDirty_ = true;
}

void DeEsserModule::setReduction(float db)
{
    reduction_ = juce::jlimit(-30.0f, 0.0f, db);
}

void DeEsserModule::setListen(bool listen)
{
    listen_ = listen;
}

void DeEsserModule::setBypass(bool b)
{
    bypassed_ = b;
}

//==============================================================================
// State serialization
juce::ValueTree DeEsserModule::getState() const
{
    juce::ValueTree tree("DeEsserModule");
    tree.setProperty("threshold", static_cast<double>(threshold_), nullptr);
    tree.setProperty("frequency", static_cast<double>(frequency_), nullptr);
    tree.setProperty("reduction", static_cast<double>(reduction_), nullptr);
    tree.setProperty("listen",    listen_,  nullptr);
    tree.setProperty("bypass",    bypassed_, nullptr);
    return tree;
}

void DeEsserModule::setState(const juce::ValueTree& tree)
{
    setThreshold(static_cast<float>(tree.getProperty("threshold", -30.0)));
    setFrequency(static_cast<float>(tree.getProperty("frequency", 6000.0)));
    setReduction(static_cast<float>(tree.getProperty("reduction", -10.0)));
    setListen(tree.getProperty("listen", false));
    setBypass(tree.getProperty("bypass", false));
}

} // namespace ana
