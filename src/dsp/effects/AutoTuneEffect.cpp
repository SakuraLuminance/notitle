#include "AutoTuneEffect.h"
#include <cmath>
#include <algorithm>

namespace ana {

AutoTuneEffect::AutoTuneEffect()
{
    // Default to chromatic scale
    scaleNotes_.assign(12, true);
    pitchCorrector_.setAlgorithm(PitchAlgorithm::Formant); // Formant is best for vocals
}

void AutoTuneEffect::setSampleRate(double sr)
{
    sampleRate_ = sr;
    pitchCorrector_.setSampleRate(sr);
}

void AutoTuneEffect::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    pitchCorrector_.setSampleRate(spec.sampleRate);
    pitchCorrector_.prepare(static_cast<int>(spec.numChannels),
                            static_cast<int>(spec.maximumBlockSize));
    
    // Pre-allocate mono buffer for worst-case block size (grow-only)
    monoBuffer_.resize(static_cast<size_t>(spec.maximumBlockSize));
}

void AutoTuneEffect::setRetuneSpeed(float ms)
{
    retuneSpeed_ = juce::jlimit(0.01f, 20.0f, ms);
}

void AutoTuneEffect::setScale(const std::vector<bool>& activeNotesInOctave)
{
    if (activeNotesInOctave.size() == 12)
        scaleNotes_ = activeNotesInOctave;
    else
        scaleNotes_.assign(12, true);
}

void AutoTuneEffect::setEnabled(bool e)
{
    enabled_ = e;
}

void AutoTuneEffect::setAmount(float amount)
{
    amount_ = std::max(0.0f, std::min(1.0f, amount));
    pitchCorrector_.setCorrectionAmount(amount_);
}

void AutoTuneEffect::setAlgorithm(PitchAlgorithm algo)
{
    pitchCorrector_.setAlgorithm(algo);
}

void AutoTuneEffect::reset()
{
    pitchCorrector_.reset();
    currentShiftSemitones_ = 0.0f;
}

float AutoTuneEffect::getNearestNote(float midiNote) const
{
    int baseMidi = static_cast<int>(std::round(midiNote));
    
    // Quick path: if chromatic scale, just round
    bool allTrue = true;
    for (bool b : scaleNotes_) if (!b) { allTrue = false; break; }
    if (allTrue) return static_cast<float>(baseMidi);
    
    // Find nearest active note in scale
    int bestNote = baseMidi;
    int minDist = 1000;
    
    // Search up and down
    for (int searchDist = 0; searchDist <= 12; ++searchDist)
    {
        // Check up
        int upNote = baseMidi + searchDist;
        if (upNote >= 0 && upNote <= 127 && scaleNotes_[upNote % 12])
        {
            minDist = searchDist;
            bestNote = upNote;
            break; // found nearest
        }
        
        // Check down
        int downNote = baseMidi - searchDist;
        if (downNote >= 0 && downNote <= 127 && scaleNotes_[downNote % 12])
        {
            minDist = searchDist;
            bestNote = downNote;
            break; // found nearest
        }
    }
    
    return static_cast<float>(bestNote);
}

void AutoTuneEffect::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!enabled_ || amount_ < 0.01f || buffer.getNumSamples() == 0)
        return;

    const int numSamples = buffer.getNumSamples();
    
    // Extract mono mix for pitch detection (grow-only, pre-allocated in prepare())
    if (monoBuffer_.size() < static_cast<size_t>(numSamples))
        monoBuffer_.resize(static_cast<size_t>(numSamples));
    std::fill(monoBuffer_.begin(), monoBuffer_.begin() + static_cast<ptrdiff_t>(numSamples), 0.0f);
    
    const int numChannels = buffer.getNumChannels();
    float invChannels = 1.0f / static_cast<float>(numChannels);
    
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* in = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
        {
            monoBuffer_[static_cast<size_t>(i)] += in[i] * invChannels;
        }
    }

    // Detect pitch using PitchCorrector's internal ACF/LPC method
    float detectedMidi = pitchCorrector_.detectPitch(monoBuffer_, sampleRate_);
    
    float targetShift = 0.0f;
    
    if (detectedMidi > 1.0f) // If pitch is successfully detected
    {
        float targetNote = getNearestNote(detectedMidi);
        targetShift = targetNote - detectedMidi; // Difference required to hit target
    }
    else
    {
        // No pitch detected, gradually return to 0 shift
        targetShift = 0.0f;
    }

    // Apply smoothing to the shift amount (retune speed)
    // 0 ms = instant, e.g. T-Pain effect
    // 50 ms = more natural
    if (retuneSpeed_ <= 0.1f)
    {
        currentShiftSemitones_ = targetShift;
    }
    else
    {
        // Simple 1-pole lowpass filter for the pitch shift target
        // Calculate alpha based on retune speed and block duration
        float blockDurationMs = 1000.0f * static_cast<float>(numSamples) / static_cast<float>(sampleRate_);
        float alpha = std::exp(-blockDurationMs / retuneSpeed_);
        currentShiftSemitones_ = currentShiftSemitones_ * alpha + targetShift * (1.0f - alpha);
    }
    
    // If shift is very small, we can just skip shifting
    if (std::abs(currentShiftSemitones_) < 0.01f)
    {
        return;
    }

    // Apply pitch shift to the buffer
    pitchCorrector_.setPitchShift(currentShiftSemitones_);
    pitchCorrector_.process(buffer);
}

} // namespace ana
