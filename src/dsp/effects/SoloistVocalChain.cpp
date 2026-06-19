#include "SoloistVocalChain.h"
#include <algorithm>
#include <cmath>

namespace ana {

//==============================================================================
SoloistVocalChain::SoloistVocalChain()
{
    preset();
}

//==============================================================================
void SoloistVocalChain::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    blockSize_  = static_cast<int>(spec.maximumBlockSize);

    deEsser_.prepare(spec);
    compressor_.prepare(spec);
    eq_.prepare(spec);
    drive_.prepare(spec);
    driftLfo_.prepare(sampleRate_);
    pitch_.prepare(spec);
    widener_.prepare(spec);
    reverb_.prepare(spec);
}

void SoloistVocalChain::process(juce::AudioBuffer<float>& buffer)
{
    // Snapshot parameters once per block (message thread vs audio thread safety)
    const float pitchDriftCents = pitchDriftCents_;

    // ---- 1. DeEsser ----
    deEsser_.process(buffer);

    // ---- 2. Compressor ----
    compressor_.process(buffer);

    // ---- 3. EQ ----
    eq_.process(buffer);

    // ---- 4. Drive (Tube saturation) ----
    drive_.process(buffer);

    // ---- 5. Pitch Drift (LFO → pitch shift) ----
    {
        // Advance the drift LFO by the actual block size and compute offset
        const float lfoValue = driftLfo_.process(buffer.getNumSamples()); // bipolar -1..1
        const float drift    = lfoValue * pitchDriftCents;                // -max..+max cents
        pitch_.setCents(drift);

        pitch_.process(buffer);
    }

    // ---- 6. Stereo Widener ----
    widener_.process(buffer);

    // ---- 7. SpaceModule (Reverb) ----
    reverb_.process(buffer);
}

void SoloistVocalChain::reset()
{
    deEsser_.reset();
    compressor_.reset();
    eq_.reset();
    drive_.reset();
    driftLfo_.reset();
    pitch_.reset();
    widener_.reset();
    reverb_.reset();
}

//==============================================================================
void SoloistVocalChain::setPresence(float db)
{
    presenceDb_ = juce::jlimit(0.0f, 6.0f, db);
    // 5-Band mode: high mid band at 4 kHz
    eq_.setHighGain5(presenceDb_);
    eq_.setHighFreq5(4000.0f);
}

void SoloistVocalChain::setAir(float db)
{
    airDb_ = juce::jlimit(0.0f, 4.0f, db);
    // Air shelf at 10 kHz
    eq_.setAirGain(airDb_);
    eq_.setAirFreq(10000.0f);
}

void SoloistVocalChain::setCompression(float ratio)
{
    compression_ = juce::jlimit(2.0f, 8.0f, ratio);
    compressor_.setRatio(compression_);
    compressor_.setAttack(3.0f);
}

void SoloistVocalChain::setSaturation(float drive)
{
    saturation_ = juce::jlimit(0.0f, 0.5f, drive);
    drive_.setDrive(saturation_);
}

void SoloistVocalChain::setPitchDrift(float cents)
{
    pitchDriftCents_ = juce::jlimit(0.0f, 10.0f, cents);
}

void SoloistVocalChain::setDriftRate(float hz)
{
    driftRateHz_ = juce::jlimit(0.5f, 2.0f, hz);
    driftLfo_.setRate(driftRateHz_);
}

void SoloistVocalChain::setReverbWet(float wet)
{
    reverbWet_ = juce::jlimit(0.0f, 1.0f, wet);
    reverb_.setMix(reverbWet_);
}

void SoloistVocalChain::setWidth(float percent)
{
    widthPercent_ = juce::jlimit(0.5f, 1.0f, percent);
    // StereoWidener: 0.0=0%, 0.5=100%, 1.0=200%
    // Map 50-100% → 0.25-0.5
    const float widenerNorm = widthPercent_ * 0.5f;
    widener_.setWidth(widenerNorm);
}

//==============================================================================
void SoloistVocalChain::preset()
{
    // --- DeEsser ---
    deEsser_.setThreshold(-30.0f);
    deEsser_.setFrequency(6000.0f);
    deEsser_.setReduction(-10.0f);
    deEsser_.setBypass(false);

    // --- Compressor ---
    compression_ = 4.0f;
    compressor_.setRatio(compression_);
    compressor_.setThreshold(-18.0f);
    compressor_.setAttack(3.0f);
    compressor_.setRelease(80.0f);
    compressor_.setKnee(3.0f);
    compressor_.setMakeupGain(0.0f);
    compressor_.setMix(1.0f);
    compressor_.setBypass(false);

    // --- EQ (5-Band mode) ---
    eq_.setMode(EQMode::Band5);
    eq_.setSubGain(0.0f);
    eq_.setLowGain5(0.0f);
    eq_.setMidGain5(0.0f);
    presenceDb_ = 3.0f;
    eq_.setHighGain5(presenceDb_);
    eq_.setHighFreq5(4000.0f);
    airDb_ = 2.0f;
    eq_.setAirGain(airDb_);
    eq_.setAirFreq(10000.0f);
    eq_.setMix(1.0f);

    // --- Drive (Tube saturation) ---
    drive_.setMode(DriveMode::Tube);
    saturation_ = 0.15f;
    drive_.setDrive(saturation_);
    drive_.setTone(0.6f);
    drive_.setMix(0.5f);
    drive_.setGain(1.0f);
    drive_.setBypass(false);

    // --- Pitch Drift LFO ---
    driftLfo_.setWaveform(WaveformType::Sine);
    driftRateHz_ = 1.2f;
    driftLfo_.setRate(driftRateHz_);
    driftLfo_.setDepth(100.0f);
    driftLfo_.setBipolar(true);

    pitchDriftCents_ = 5.0f;
    pitch_.setMode(PitchMode::PitchShift);
    pitch_.setSemitones(0.0f);
    pitch_.setCents(0.0f);
    pitch_.setMix(1.0f);

    // --- Stereo Widener ---
    widthPercent_ = 0.8f;
    {
        const float widenerNorm = widthPercent_ * 0.5f;
        widener_.setWidth(widenerNorm);
    }
    widener_.setMode(StereoWidenerMode::Stereo);
    widener_.setMix(1.0f);
    widener_.setBypass(false);

    // --- SpaceModule (Reverb) ---
    reverb_.setMode(SpaceMode::Room);
    reverbWet_ = 0.25f;
    reverb_.setMix(reverbWet_);
    reverb_.setReverbSize(0.3f);
    reverb_.setReverbDamping(0.5f);
    reverb_.setReverbWidth(0.5f);
    reverb_.setBypass(false);
}

//==============================================================================
juce::ValueTree SoloistVocalChain::getState() const
{
    juce::ValueTree tree("SoloistVocalChain");

    tree.setProperty("presenceDb",     presenceDb_,     nullptr);
    tree.setProperty("airDb",          airDb_,          nullptr);
    tree.setProperty("compression",    compression_,    nullptr);
    tree.setProperty("saturation",     saturation_,     nullptr);
    tree.setProperty("pitchDriftCents", pitchDriftCents_, nullptr);
    tree.setProperty("driftRateHz",    driftRateHz_,    nullptr);
    tree.setProperty("reverbWet",      reverbWet_,      nullptr);
    tree.setProperty("widthPercent",   widthPercent_,   nullptr);

    // Persist sub-module states for full recall
    tree.addChild(deEsser_.getState(),     -1, nullptr);
    tree.addChild(compressor_.getState(),  -1, nullptr);
    tree.addChild(eq_.getState(),          -1, nullptr);
    tree.addChild(drive_.getState(),       -1, nullptr);
    tree.addChild(pitch_.getState(),       -1, nullptr);
    tree.addChild(widener_.getState(),     -1, nullptr);
    tree.addChild(reverb_.getState(),      -1, nullptr);

    return tree;
}

void SoloistVocalChain::setState(const juce::ValueTree& state)
{
    // Restore high-level parameters
    setPresence(state.getProperty("presenceDb", 3.0f));
    setAir(state.getProperty("airDb", 2.0f));
    setCompression(state.getProperty("compression", 4.0f));
    setSaturation(state.getProperty("saturation", 0.15f));
    setPitchDrift(state.getProperty("pitchDriftCents", 5.0f));
    setDriftRate(state.getProperty("driftRateHz", 1.2f));
    setReverbWet(state.getProperty("reverbWet", 0.25f));
    setWidth(state.getProperty("widthPercent", 0.8f));

    // Restore sub-module states from child trees
    for (const auto& child : state)
    {
        const auto tag = child.getType().toString();
        if      (tag == "DeEsserModule")       deEsser_.setState(child);
        else if (tag == "CompressorEffect")    compressor_.setState(child);
        else if (tag == "EQModule")            eq_.setState(child);
        else if (tag == "DriveModule")         drive_.setState(child);
        else if (tag == "PitchModule")         pitch_.setState(child);
        else if (tag == "StereoWidenerEffect") widener_.setState(child);
        else if (tag == "SpaceModule")         reverb_.setState(child);
    }
}

} // namespace ana
