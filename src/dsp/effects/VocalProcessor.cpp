#include "VocalProcessor.h"
#include <cmath>

namespace ana {

//==============================================================================
// Mode display names
//==============================================================================
static const char* modeNames[] = {
    "Chest",
    "Head",
    "Breathy",
    "Telephone",
    "Choir",
    "Megaphone",
    "Whisper"
};

const char* VocalProcessor::getModeName(VocalCharacter mode)
{
    const int idx = static_cast<int>(mode);
    return (idx >= 0 && idx < getNumModes()) ? modeNames[idx] : "Unknown";
}

//==============================================================================
// Construction
//==============================================================================
VocalProcessor::VocalProcessor()
{
    applyMode(VocalCharacter::Chest);
}

//==============================================================================
// Prepare
//==============================================================================
void VocalProcessor::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate_  = spec.sampleRate;
    blockSize_   = static_cast<int>(spec.maximumBlockSize);
    numChannels_ = static_cast<int>(spec.numChannels);

    pitch_.prepare(spec);
    eq_.prepare(spec);
    drive_.prepare(spec);
    deEsser_.prepare(spec);
    breath_.prepare(spec);
    formant_.prepare(spec);
    widener_.prepare(spec);
    compressor_.prepare(spec);
    space_.prepare(spec);

    // Prepare inline IIR filters
    lpf_.prepare(spec);
    hpf_.prepare(spec);

    // Prepare noise gate
    noiseGate_.prepare(sampleRate_);

    // Re-apply current mode to refresh states with the new sample rate
    applyMode(currentMode_);
}

//==============================================================================
// Process — route audio through the active mode's chain
//==============================================================================
void VocalProcessor::process(juce::AudioBuffer<float>& buffer)
{
    // Snapshot parameters once per block (message thread vs audio thread safety)
    const auto currentMode   = currentMode_;
    const int  numChannels   = numChannels_;
    int        rampLeft      = rampSamplesLeft_.load(std::memory_order_relaxed);

    switch (currentMode)
    {
    case VocalCharacter::Chest:
        // Sub-octave → EQ low shelf → LP 4 kHz → Soft drive
        pitch_.process(buffer);
        eq_.process(buffer);
        {
            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> context(block);
            lpf_.process(context);
        }
        drive_.process(buffer);
        break;

    case VocalCharacter::Head:
        // Formant shift +3 → high shelf +4 dB @ 4 kHz → stereo widen 30 %
        formant_.process(buffer);
        eq_.process(buffer);
        widener_.process(buffer);
        break;

    case VocalCharacter::Breathy:
        // Breath noise → de-esser
        breath_.process(buffer);
        deEsser_.process(buffer);
        break;

    case VocalCharacter::Telephone:
        // BPF 300–3400 Hz (HPF + LPF) → Crush drive → notch -6 dB @ 400 Hz
        {
            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> context(block);
            hpf_.process(context);
            lpf_.process(context);
        }
        drive_.process(buffer);
        eq_.process(buffer);
        break;

    case VocalCharacter::Choir:
        // Harmony 4 voices → formant spread → hall reverb
        pitch_.process(buffer);
        formant_.process(buffer);
        space_.process(buffer);
        break;

    case VocalCharacter::Megaphone:
        // Heavy compression → BPF 800–4000 Hz (HPF + LPF) → Hard drive → mid boost
        compressor_.process(buffer);
        {
            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> context(block);
            hpf_.process(context);
            lpf_.process(context);
        }
        drive_.process(buffer);
        eq_.process(buffer);
        break;

    case VocalCharacter::Whisper:
        // Noise gate → HPF 500 Hz → presence cut → de-ess
        noiseGate_.process(buffer);
        {
            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> context(block);
            hpf_.process(context);
        }
        eq_.process(buffer);
        deEsser_.process(buffer);
        break;
    }

    // Anti-zipper gain ramp: fade in over 5 ms after mode change
    if (rampLeft > 0)
    {
        const int totalSamples = buffer.getNumSamples();
        const int rampInBlock  = std::min(rampLeft, totalSamples);
        const float rampStep   = 1.0f / static_cast<float>(rampLeft);
        float gain = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* d = buffer.getWritePointer(ch);
            for (int i = 0; i < rampInBlock; ++i)
            {
                d[i] *= gain;
                gain += rampStep;
            }
        }
        rampLeft -= rampInBlock;
        rampSamplesLeft_.store(rampLeft, std::memory_order_relaxed);
    }
}

//==============================================================================
// Reset
//==============================================================================
void VocalProcessor::reset()
{
    pitch_.reset();
    eq_.reset();
    drive_.reset();
    deEsser_.reset();
    breath_.reset();
    formant_.reset();
    widener_.reset();
    compressor_.reset();
    space_.reset();

    lpf_.reset();
    hpf_.reset();

    noiseGate_.envelope = 0.0f;
}

//==============================================================================
// applyMode — re-configure all sub-modules for the given vocal character
//
// Each mode is a preset.  The method first bypasses / disables every module,
// then configures and enables only the modules needed for the selected mode.
//==============================================================================
void VocalProcessor::applyMode(VocalCharacter mode)
{
    currentMode_ = mode;

    // Start 5 ms gain ramp to prevent zipper noise on filter coeff snap
    rampSamplesLeft_.store(static_cast<int>(sampleRate_ * 0.005), std::memory_order_relaxed);

    // ---- BYPASS ALL MODULES (default dead state) ----
    pitch_.setMix(0.0f);
    eq_.setMix(0.0f);
    drive_.setBypass(true);
    deEsser_.setBypass(true);
    breath_.setMix(0.0f);
    formant_.setMix(0.0f);
    widener_.setBypass(true);
    compressor_.setBypass(true);
    space_.setBypass(true);

    // ---- CONFIGURE THE SELECTED MODE ----
    switch (mode)
    {
    case VocalCharacter::Chest:
    {
        // Pitch: sub-octave (-12 st), mix 0.3 → level 0.3 sub
        pitch_.setMode(PitchMode::PitchShift);
        pitch_.setSemitones(-12.0f);
        pitch_.setCents(0.0f);
        pitch_.setMix(0.3f);

        // EQ: Band5 → low shelf (sub) +3 dB @ 200 Hz
        eq_.setMode(EQMode::Band5);
        eq_.setSubGain(3.0f);
        eq_.setSubFreq(200.0f);
        eq_.setLowGain5(0.0f);
        eq_.setMidGain5(0.0f);
        eq_.setHighGain5(0.0f);
        eq_.setAirGain(0.0f);
        eq_.setMix(1.0f);

        // LP: 4 kHz
        *lpf_.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(
            sampleRate_, 4000.0f);

        // Drive: Soft, 0.2
        drive_.setMode(DriveMode::Soft);
        drive_.setDrive(0.2f);
        drive_.setTone(0.5f);
        drive_.setMix(1.0f);
        drive_.setGain(1.0f);
        drive_.setBypass(false);
        break;
    }

    case VocalCharacter::Head:
    {
        // Formant: +3 semitones
        formant_.setFormantShift(3.0f);
        formant_.setFormantSpread(1.0f);
        formant_.setGender(0.5f);
        formant_.setMix(1.0f);

        // EQ: Band5 → high shelf +4 dB @ 4 kHz
        eq_.setMode(EQMode::Band5);
        eq_.setSubGain(0.0f);
        eq_.setLowGain5(0.0f);
        eq_.setMidGain5(0.0f);
        eq_.setHighGain5(4.0f);
        eq_.setHighFreq5(4000.0f);
        eq_.setAirGain(0.0f);
        eq_.setMix(1.0f);

        // Widener: 30 % width (0.5 = 100 %, so 0.15 = 30 %)
        widener_.setWidth(0.15f);
        widener_.setMode(StereoWidenerMode::Stereo);
        widener_.setMix(1.0f);
        widener_.setBypass(false);
        break;
    }

    case VocalCharacter::Breathy:
    {
        // Breath: 60 %, mid color (50 %), mix 15 %
        breath_.setBreathiness(60.0f);
        breath_.setNoiseColor(50.0f);
        breath_.setMix(15.0f);

        // DeEsser: threshold -25 dB
        deEsser_.setThreshold(-25.0f);
        deEsser_.setFrequency(6000.0f);
        deEsser_.setReduction(-10.0f);
        deEsser_.setBypass(false);
        break;
    }

    case VocalCharacter::Telephone:
    {
        // BPF = HPF 300 Hz + LPF 3400 Hz
        *hpf_.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(
            sampleRate_, 300.0f);
        *lpf_.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(
            sampleRate_, 3400.0f);

        // Drive: Crush (~8 bits, downsample ≈4)
        drive_.setMode(DriveMode::Crush);
        drive_.setDrive(0.7f);
        drive_.setTone(0.5f);
        drive_.setMix(1.0f);
        drive_.setGain(1.0f);
        drive_.setBypass(false);

        // EQ: Band3 → mid notch -6 dB @ 400 Hz
        eq_.setMode(EQMode::Band3);
        eq_.setLowGain(0.0f);
        eq_.setMidGain(-6.0f);
        eq_.setMidFreq(400.0f);
        eq_.setHighGain(0.0f);
        eq_.setMix(1.0f);
        break;
    }

    case VocalCharacter::Choir:
    {
        // Pitch: Harmonize, 4 voices, near-unison interval (≈12 cents)
        pitch_.setMode(PitchMode::Harmonize);
        pitch_.setInterval(0.12f);
        pitch_.setHarmonyMix(0.6f);
        pitch_.setVoices(4);
        pitch_.setWindow(2048);
        pitch_.setMix(1.0f);

        // Formant: spread variance (1.0 + 0.3)
        formant_.setFormantShift(0.0f);
        formant_.setFormantSpread(1.3f);
        formant_.setGender(0.5f);
        formant_.setMix(0.7f);

        // Space: Hall reverb
        space_.setMode(SpaceMode::Hall);
        space_.setReverbSize(0.7f);
        space_.setReverbDamping(0.4f);
        space_.setReverbWidth(0.8f);
        space_.setMix(0.4f);
        space_.setBypass(false);
        break;
    }

    case VocalCharacter::Megaphone:
    {
        // Compressor: 8:1, 1 ms attack
        compressor_.setRatio(8.0f);
        compressor_.setThreshold(-18.0f);
        compressor_.setAttack(1.0f);
        compressor_.setRelease(20.0f);
        compressor_.setKnee(3.0f);
        compressor_.setMakeupGain(6.0f);
        compressor_.setMix(1.0f);
        compressor_.setBypass(false);

        // BPF = HPF 800 Hz + LPF 4000 Hz
        *hpf_.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(
            sampleRate_, 800.0f);
        *lpf_.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(
            sampleRate_, 4000.0f);

        // Drive: Hard, 0.7
        drive_.setMode(DriveMode::Hard);
        drive_.setDrive(0.7f);
        drive_.setTone(0.3f);
        drive_.setMix(1.0f);
        drive_.setGain(1.0f);
        drive_.setBypass(false);

        // EQ: Band3 → mid +6 dB @ 1.5 kHz
        eq_.setMode(EQMode::Band3);
        eq_.setLowGain(0.0f);
        eq_.setMidGain(6.0f);
        eq_.setMidFreq(1500.0f);
        eq_.setHighGain(0.0f);
        eq_.setMix(1.0f);
        break;
    }

    case VocalCharacter::Whisper:
    {
        // Noise gate: threshold -40 dB (configured, active in process())
        noiseGate_.setThreshold(-40.0f);

        // HPF: 500 Hz
        *hpf_.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(
            sampleRate_, 500.0f);

        // EQ: Band3 → high shelf (presence) -6 dB @ 5 kHz
        eq_.setMode(EQMode::Band3);
        eq_.setLowGain(0.0f);
        eq_.setMidGain(0.0f);
        eq_.setHighGain(-6.0f);
        eq_.setMix(1.0f);

        // DeEsser: reduction -15 dB
        deEsser_.setThreshold(-30.0f);
        deEsser_.setFrequency(6000.0f);
        deEsser_.setReduction(-15.0f);
        deEsser_.setBypass(false);
        break;
    }
    }
}

//==============================================================================
// State management
//==============================================================================
juce::ValueTree VocalProcessor::getState() const
{
    juce::ValueTree tree("VocalProcessor");
    tree.setProperty("mode", static_cast<int>(currentMode_), nullptr);

    tree.addChild(pitch_.getState(),      -1, nullptr);
    tree.addChild(eq_.getState(),          -1, nullptr);
    tree.addChild(drive_.getState(),       -1, nullptr);
    tree.addChild(deEsser_.getState(),     -1, nullptr);
    tree.addChild(breath_.getState(),      -1, nullptr);
    tree.addChild(formant_.getState(),     -1, nullptr);
    tree.addChild(widener_.getState(),     -1, nullptr);
    tree.addChild(compressor_.getState(),  -1, nullptr);
    tree.addChild(space_.getState(),       -1, nullptr);

    return tree;
}

void VocalProcessor::setState(const juce::ValueTree& state)
{
    if (!state.hasType("VocalProcessor"))
        return;

    const int modeIdx = state.getProperty("mode", static_cast<int>(VocalCharacter::Chest));
    const auto mode = static_cast<VocalCharacter>(
        juce::jlimit(0, getNumModes() - 1, modeIdx));
    applyMode(mode);

    // Restore sub-module states from child trees
    for (const auto& child : state)
    {
        const auto tag = child.getType().toString();
        if      (tag == "PitchModule")         pitch_.setState(child);
        else if (tag == "EQModule")            eq_.setState(child);
        else if (tag == "DriveModule")         drive_.setState(child);
        else if (tag == "DeEsserModule")       deEsser_.setState(child);
        else if (tag == "BreathNoiseGenerator") breath_.setState(child);
        else if (tag == "FormantTuner")        formant_.setState(child);
        else if (tag == "StereoWidenerEffect") widener_.setState(child);
        else if (tag == "CompressorEffect")    compressor_.setState(child);
        else if (tag == "SpaceModule")         space_.setState(child);
    }
}

//==============================================================================
// NoiseGate implementation
//==============================================================================
void VocalProcessor::NoiseGate::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    // Time constants: 1 ms attack, 50 ms release
    attackCoeff_  = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.001));
    releaseCoeff_ = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.050));
    envelope = 0.0f;
}

void VocalProcessor::NoiseGate::setThreshold(float db)
{
    threshold_ = db;
}

void VocalProcessor::NoiseGate::process(juce::AudioBuffer<float>& buffer)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Compute RMS for this block
    double sumSq = 0.0;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* d = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            sumSq += static_cast<double>(d[i]) * static_cast<double>(d[i]);
    }
    const float rms = static_cast<float>(
        std::sqrt(sumSq / (numChannels * numSamples)));

    // Envelope follower (one-pole)
    const float coeff = (rms > envelope) ? attackCoeff_ : releaseCoeff_;
            envelope = envelope + coeff * (rms - envelope);
            envelope += 1e-15f; // denormal protection

    // Gate: smooth gain reduction (linear fade below threshold)
    const float threshLin = juce::Decibels::decibelsToGain(threshold_);
    float gateGain = 1.0f;

    if (envelope < threshLin)
    {
        // Below threshold: apply fade proportional to how far below
        if (threshLin > 1e-10f)
            gateGain = std::max(0.0f, envelope / threshLin);
        else
            gateGain = 0.0f;

        // Apply a gentle floor to avoid complete silence (whisper residue)
        gateGain = juce::jmap(gateGain, 0.0f, 1.0f, 0.05f, 1.0f);
    }

    buffer.applyGain(gateGain);
}

} // namespace ana
