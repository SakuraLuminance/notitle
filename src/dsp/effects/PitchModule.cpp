#include "PitchModule.h"
#include <cmath>
#include <algorithm>

namespace ana {

PitchModule::PitchModule()
{
    scale_.assign(12, true);
}

//==============================================================================
void PitchModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;

    pitchCorrector_.setSampleRate(sampleRate_);
    pitchCorrector_.reset();

    for (auto& v : harmonyVoices_)
    {
        v->setSampleRate(sampleRate_);
        v->reset();
    }

    wetHPF_.prepare(spec);
    wetLPF_.prepare(spec);

    auto hpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate_, wetLowCut_, 0.707);
    auto lpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate_, wetHighCut_, 0.707);
    *wetHPF_.state = *hpfCoeffs;
    *wetLPF_.state = *lpfCoeffs;

    dryBuffer_.setSize(static_cast<int>(spec.numChannels),
                       static_cast<int>(spec.maximumBlockSize),
                       false, false, true);
    tempBuffer_.setSize(static_cast<int>(spec.numChannels),
                        static_cast<int>(spec.maximumBlockSize),
                        false, false, true);
}

void PitchModule::reset()
{
    pitchCorrector_.reset();
    for (auto& v : harmonyVoices_)
        v->reset();
    wetHPF_.reset();
    wetLPF_.reset();
    currentShiftSemitones_ = 0.0f;
}

//==============================================================================
void PitchModule::process(juce::AudioBuffer<float>& buffer)
{
    const auto numSamples  = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    if (numSamples == 0)
        return;

    const bool needDry    = mix_ < 1.0f;
    const bool needFilter = wetLowCut_ > 20.0f || wetHighCut_ < 20000.0f;
    const bool needBlend  = needDry || needFilter;

    // Always capture dry signal (Harmonize mode reads it for voice processing)
    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    // Dispatch to mode-specific processor
    switch (mode_)
    {
        case PitchMode::AutoTune:   processAutoTune(buffer);   break;
        case PitchMode::PitchShift: processPitchShift(buffer); break;
        case PitchMode::Harmonize:  processHarmonize(buffer);  break;
        case PitchMode::Formant:    processFormant(buffer);    break;
    }

    // Apply wet HPF/LPF and blend with dry
    if (needBlend)
    {
        if (needFilter)
        {
            juce::dsp::AudioBlock<float> wetBlock(buffer);
            juce::dsp::ProcessContextReplacing<float> ctx(wetBlock);
            wetHPF_.process(ctx);
            wetLPF_.process(ctx);
        }

        if (needDry)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* dst = buffer.getWritePointer(ch);
                const auto* dry = dryBuffer_.getReadPointer(ch);
                for (int s = 0; s < numSamples; ++s)
                    dst[s] = dry[s] * (1.0f - mix_) + dst[s] * mix_;
            }
        }
    }
}

//==============================================================================
// Mode switching
void PitchModule::setMode(PitchMode mode)
{
    mode_ = mode;
    reset();

    switch (mode)
    {
        case PitchMode::AutoTune:
            pitchCorrector_.setAlgorithm(PitchAlgorithm::PhaseVocoder);
            pitchCorrector_.setCorrectionAmount(amount_);
            break;

        case PitchMode::PitchShift:
            pitchCorrector_.setAlgorithm(PitchAlgorithm::Spectral);
            pitchCorrector_.setCorrectionAmount(1.0f);
            pitchCorrector_.setPitchShift(semitones_ + cents_ / 100.0f);
            break;

        case PitchMode::Harmonize:
            pitchCorrector_.setAlgorithm(PitchAlgorithm::Spectral);
            pitchCorrector_.setCorrectionAmount(1.0f);
            ensureHarmonyVoices(voices_);
            break;

        case PitchMode::Formant:
            pitchCorrector_.setAlgorithm(PitchAlgorithm::Formant);
            pitchCorrector_.setFormantPreservation(formantPreserve_);
            pitchCorrector_.setCorrectionAmount(1.0f);
            pitchCorrector_.setPitchShift(formantShift_);
            break;
    }
}

//==============================================================================
// AutoTune setters
void PitchModule::setRetuneSpeed(float ms)
{
    retuneSpeed_ = juce::jlimit(0.01f, 200.0f, ms);
}

void PitchModule::setAmount(float v)
{
    amount_ = juce::jlimit(0.0f, 1.0f, v);
    pitchCorrector_.setCorrectionAmount(amount_);
}

void PitchModule::setScale(const std::vector<bool>& scale)
{
    if (scale.size() == 12)
        scale_ = scale;
    else
        scale_.assign(12, true);
}

//==============================================================================
// PitchShift setters
void PitchModule::setSemitones(float st)
{
    semitones_ = juce::jlimit(-24.0f, 24.0f, st);
}

void PitchModule::setCents(float c)
{
    cents_ = juce::jlimit(-99.0f, 99.0f, c);
}

void PitchModule::setWindow(int size)
{
    window_ = juce::jlimit(256, 8192, size);
    if (mode_ == PitchMode::PitchShift)
        pitchCorrector_.setFftSize(window_);
}

//==============================================================================
// Harmonize setters
void PitchModule::setInterval(float st)
{
    interval_ = juce::jlimit(-24.0f, 24.0f, st);
}

void PitchModule::setHarmonyMix(float v)
{
    harmonyMix_ = juce::jlimit(0.0f, 1.0f, v);
}

void PitchModule::setVoices(int count)
{
    voices_ = juce::jlimit(1, 4, count);
    if (mode_ == PitchMode::Harmonize)
        ensureHarmonyVoices(voices_);
}

//==============================================================================
// Formant setters
void PitchModule::setFormantShift(float st)
{
    formantShift_ = juce::jlimit(-12.0f, 12.0f, st);
    if (mode_ == PitchMode::Formant)
        pitchCorrector_.setPitchShift(formantShift_);
}

void PitchModule::setFormantPreserve(float v)
{
    formantPreserve_ = juce::jlimit(0.0f, 1.0f, v);
    if (mode_ == PitchMode::Formant)
        pitchCorrector_.setFormantPreservation(formantPreserve_);
}

//==============================================================================
// Shared setters
void PitchModule::setMix(float v)          { mix_        = juce::jlimit(0.0f, 1.0f, v); }
void PitchModule::setWetLowCut(float hz)   { wetLowCut_  = juce::jlimit(10.0f, 20000.0f, hz); }
void PitchModule::setWetHighCut(float hz)  { wetHighCut_ = juce::jlimit(10.0f, 20000.0f, hz); }

//==============================================================================
// Mode processors

void PitchModule::processAutoTune(juce::AudioBuffer<float>& buffer)
{
    if (amount_ < 0.01f)
        return;

    const auto numSamples  = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    // Extract mono mix for pitch detection
    monoBuffer_.resize(static_cast<size_t>(numSamples));
    std::fill(monoBuffer_.begin(), monoBuffer_.end(), 0.0f);

    const float invChannels = 1.0f / static_cast<float>(numChannels);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* in = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            monoBuffer_[static_cast<size_t>(i)] += in[i] * invChannels;
    }

    // Detect pitch
    float detectedMidi = pitchCorrector_.detectPitch(monoBuffer_, sampleRate_);

    float targetShift = 0.0f;
    if (detectedMidi > 1.0f)
    {
        float targetNote = getNearestNote(detectedMidi);
        targetShift = targetNote - detectedMidi;
    }

    // Apply retune smoothing
    if (retuneSpeed_ <= 0.1f)
    {
        currentShiftSemitones_ = targetShift;
    }
    else
    {
        float blockDurationMs = 1000.0f * static_cast<float>(numSamples)
                              / static_cast<float>(sampleRate_);
        float alpha = std::exp(-blockDurationMs / retuneSpeed_);
        currentShiftSemitones_ = currentShiftSemitones_ * alpha
                               + targetShift * (1.0f - alpha);
    }

    if (std::abs(currentShiftSemitones_) < 0.01f)
        return;

    pitchCorrector_.setPitchShift(currentShiftSemitones_);
    pitchCorrector_.process(buffer);
}

void PitchModule::processPitchShift(juce::AudioBuffer<float>& buffer)
{
    float totalShift = semitones_ + cents_ / 100.0f;
    if (std::abs(totalShift) < 0.01f)
        return;

    pitchCorrector_.setPitchShift(totalShift);
    pitchCorrector_.process(buffer);
}

void PitchModule::processHarmonize(juce::AudioBuffer<float>& buffer)
{
    const auto numSamples  = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();
    const int numVoices    = static_cast<int>(harmonyVoices_.size());

    if (numVoices == 0 || std::abs(interval_) < 0.01f)
        return;

    // dryBuffer_ already has the original clean signal from outer process()
    // Use tempBuffer_ as scratch for per-voice processing
    tempBuffer_.clear();

    // Process each harmony voice
    for (int v = 0; v < numVoices; ++v)
    {
        // Compute interval offset with alternating +/- for musical spread
        // v=0->+interval, v=1->-interval, v=2->+interval*2, v=3->-interval*2
        int voicePair = v / 2;
        float voiceShift = interval_ * static_cast<float>(voicePair + 1);
        if ((v % 2) == 1)
            voiceShift = -voiceShift;

        // Copy original to scratch and process
        tempBuffer_.makeCopyOf(dryBuffer_, true);

        harmonyVoices_[v]->setPitchShift(voiceShift);
        harmonyVoices_[v]->process(tempBuffer_);

        // Accumulate into output buffer with gain scaling
        // Voices sum to 0.5x original to avoid clipping
        float gain = 0.5f / static_cast<float>(numVoices);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* src = tempBuffer_.getReadPointer(ch);
            auto* dst = buffer.getWritePointer(ch);
            for (int s = 0; s < numSamples; ++s)
                dst[s] += src[s] * gain;
        }
    }

    // Blend original with harmony sum using harmonyMix_
    // buffer = original + harmonyVoices
    // We want: original*(1-harmonyMix) + (original + harmonyVoices)*harmonyMix
    //        = original + harmonyVoices*harmonyMix
    if (harmonyMix_ < 1.0f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* dst = buffer.getWritePointer(ch);
            const auto* orig = dryBuffer_.getReadPointer(ch);
            for (int s = 0; s < numSamples; ++s)
                dst[s] = orig[s] + (dst[s] - orig[s]) * harmonyMix_;
        }
    }
}

void PitchModule::processFormant(juce::AudioBuffer<float>& buffer)
{
    if (std::abs(formantShift_) < 0.01f)
        return;

    pitchCorrector_.setPitchShift(formantShift_);
    pitchCorrector_.setFormantPreservation(formantPreserve_);
    pitchCorrector_.process(buffer);
}

//==============================================================================
void PitchModule::ensureHarmonyVoices(int count)
{
    while (static_cast<int>(harmonyVoices_.size()) < count)
    {
        auto voice = std::make_unique<PitchCorrector>();
        voice->setAlgorithm(PitchAlgorithm::Spectral);
        voice->setCorrectionAmount(1.0f);
        if (sampleRate_ > 0.0)
            voice->setSampleRate(sampleRate_);
        voice->setFftSize(window_);
        harmonyVoices_.push_back(std::move(voice));
    }

    while (static_cast<int>(harmonyVoices_.size()) > count)
        harmonyVoices_.pop_back();
}

float PitchModule::getNearestNote(float midiNote) const
{
    int baseMidi = static_cast<int>(std::round(midiNote));

    // Quick path: full chromatic = just round
    bool allTrue = true;
    for (bool b : scale_) if (!b) { allTrue = false; break; }
    if (allTrue)
        return static_cast<float>(baseMidi);

    // Find nearest active note
    int bestNote = baseMidi;
    for (int searchDist = 0; searchDist <= 12; ++searchDist)
    {
        int upNote = baseMidi + searchDist;
        if (upNote >= 0 && upNote <= 127 && scale_[static_cast<size_t>(upNote % 12)])
        {
            bestNote = upNote;
            break;
        }
        int downNote = baseMidi - searchDist;
        if (downNote >= 0 && downNote <= 127 && scale_[static_cast<size_t>(downNote % 12)])
        {
            bestNote = downNote;
            break;
        }
    }

    return static_cast<float>(bestNote);
}

//==============================================================================
juce::ValueTree PitchModule::getState() const
{
    juce::ValueTree tree("PitchModule");
    tree.setProperty("mode", static_cast<int>(mode_), nullptr);

    // AutoTune
    tree.setProperty("retuneSpeed", retuneSpeed_, nullptr);
    tree.setProperty("amount", amount_, nullptr);

    // Encode scale as bitmask
    int scaleMask = 0;
    for (int i = 0; i < 12 && i < static_cast<int>(scale_.size()); ++i)
        if (scale_[static_cast<size_t>(i)]) scaleMask |= (1 << i);
    tree.setProperty("scaleMask", scaleMask, nullptr);

    // PitchShift
    tree.setProperty("semitones", semitones_, nullptr);
    tree.setProperty("cents", cents_, nullptr);
    tree.setProperty("window", window_, nullptr);

    // Harmonize
    tree.setProperty("interval", interval_, nullptr);
    tree.setProperty("harmonyMix", harmonyMix_, nullptr);
    tree.setProperty("voices", voices_, nullptr);

    // Formant
    tree.setProperty("formantShift", formantShift_, nullptr);
    tree.setProperty("formantPreserve", formantPreserve_, nullptr);

    // Shared
    tree.setProperty("mix", mix_, nullptr);
    tree.setProperty("wetLowCut", wetLowCut_, nullptr);
    tree.setProperty("wetHighCut", wetHighCut_, nullptr);

    return tree;
}

void PitchModule::setState(const juce::ValueTree& state)
{
    // Mode first
    int modeInt = static_cast<int>(state.getProperty("mode", static_cast<int>(PitchMode::AutoTune)));
    setMode(static_cast<PitchMode>(juce::jlimit(0, 3, modeInt)));

    // AutoTune
    setRetuneSpeed(state.getProperty("retuneSpeed", 50.0f));
    setAmount(state.getProperty("amount", 1.0f));

    // Decode scale from bitmask
    int scaleMask = state.getProperty("scaleMask", 0x0FFF); // all 12 bits on by default
    std::vector<bool> scale(12, false);
    for (int i = 0; i < 12; ++i)
        scale[static_cast<size_t>(i)] = (scaleMask & (1 << i)) != 0;
    setScale(scale);

    // PitchShift
    setSemitones(state.getProperty("semitones", 0.0f));
    setCents(state.getProperty("cents", 0.0f));
    setWindow(state.getProperty("window", 2048));

    // Harmonize
    setInterval(state.getProperty("interval", 7.0f));
    setHarmonyMix(state.getProperty("harmonyMix", 0.5f));
    setVoices(state.getProperty("voices", 2));

    // Formant
    setFormantShift(state.getProperty("formantShift", 0.0f));
    setFormantPreserve(state.getProperty("formantPreserve", 1.0f));

    // Shared
    setMix(state.getProperty("mix", 1.0f));
    setWetLowCut(state.getProperty("wetLowCut", 20.0f));
    setWetHighCut(state.getProperty("wetHighCut", 20000.0f));
}

} // namespace ana
