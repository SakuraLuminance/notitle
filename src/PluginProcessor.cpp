#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include "dsp/effects/DelayEffect.h"
#include "dsp/effects/ReverbEffect.h"
#include "dsp/effects/EQEffect.h"
#include "dsp/effects/ChorusEffect.h"
#include "dsp/effects/DistortionEffect.h"
#include "dsp/effects/SaturationEffect.h"
#include "dsp/effects/BitcrusherEffect.h"
#include "dsp/effects/CompressorEffect.h"
#include "dsp/effects/AutoTuneEffect.h"
#include "dsp/effects/FlangerEffect.h"
#include "dsp/effects/PhaserEffect.h"
#include "dsp/effects/RingModulatorEffect.h"
#include "dsp/effects/StereoWidenerEffect.h"
#include "dsp/WavLoader.h"
#include "dsp/STFTAnalyzer.h"
#include "dsp/ProcessorStore.h"
#include "dsp/EffectAdapters.h"
#include "dsp/PeakDetector.h"

// SSE intrinsics for FTZ/DAZ denormal handling
#if JUCE_INTEL
#include <xmmintrin.h>
#endif

//==============================================================================
// Effect adapters — wrap concrete effect classes into the EffectBase interface
//==============================================================================
namespace {

class BitcrusherEffectAdapter : public ana::EffectBase {
    ana::BitcrusherEffect effect;
public:
    ana::BitcrusherEffect* getEffect() { return &effect; }
    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
    juce::ValueTree getState() const override                 { return effect.getState(); }
    void setState(const juce::ValueTree& s) override          { effect.setState(s); }
};

class CompressorEffectAdapter : public ana::EffectBase {
    ana::CompressorEffect effect;
public:
    ana::CompressorEffect* getEffect() { return &effect; }
    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
    juce::ValueTree getState() const override                 { return effect.getState(); }
    void setState(const juce::ValueTree& s) override          { effect.setState(s); }
};

class PhaserEffectAdapter : public ana::EffectBase {
    ana::PhaserEffect effect;
public:
    ana::PhaserEffect* getEffect() { return &effect; }
    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
    juce::ValueTree getState() const override                 { return effect.getState(); }
    void setState(const juce::ValueTree& s) override          { effect.setState(s); }
};

class LimiterEffectAdapter : public ana::EffectBase {
    ana::LimiterEffect& effect;
public:
    explicit LimiterEffectAdapter(ana::LimiterEffect& e) : effect(e) {}
    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
    juce::ValueTree getState() const override                 { return effect.getState(); }
    void setState(const juce::ValueTree& s) override          { effect.setState(s); }
};

class FlangerEffectAdapter : public ana::EffectBase {
    ana::FlangerEffect& effect;
public:
    explicit FlangerEffectAdapter(ana::FlangerEffect& e) : effect(e) {}
    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
    juce::ValueTree getState() const override                 { return effect.getState(); }
    void setState(const juce::ValueTree& s) override          { effect.setState(s); }
};

class StereoWidenerEffectAdapter : public ana::EffectBase {
    ana::StereoWidenerEffect effect;
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
    juce::ValueTree getState() const override                 { return effect.getState(); }
    void setState(const juce::ValueTree& s) override          { effect.setState(s); }
    ana::StereoWidenerEffect* getEffect() { return &effect; }
};

class RingModulatorEffectAdapter : public ana::EffectBase {
    ana::RingModulatorEffect effect;
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
    juce::ValueTree getState() const override                 { return effect.getState(); }
    void setState(const juce::ValueTree& s) override          { effect.setState(s); }
    ana::RingModulatorEffect* getEffect() { return &effect; }
};

} // namespace

AnaPlugAudioProcessor::AnaPlugAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // Flush-to-Zero and Denormals-Are-Zero for SSE
    // Prevents denormal numbers from crippling DSP performance
    // (resonant IIR filters, comb filters with feedback)
#if JUCE_INTEL
    _mm_setcsr(_mm_getcsr() | 0x8040);  // FTZ (bit 15) + DAZ (bit 6)
#elif JUCE_ARM
    #if defined(__aarch64__)
    {
        uint64_t fpcr;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        fpcr |= (1ULL << 24);  // FZ (Flush-to-Zero, bit 24)
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
    }
    #else
    {
        uint32_t fpscr;
        __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
        fpscr |= (1 << 24);  // FZ (Flush-to-Zero, bit 24)
        __asm__ __volatile__("vmsr fpscr, %0" : : "r"(fpscr));
    }
    #endif
#endif

    presetManager.setStateReferences(nullptr, &multiFilter_, &envPool_[0], &lfoPool_[0], nullptr, &unisonEngine_, &voiceManager, nullptr);

    // Wire new modulation/LFO/ENV/volume ADSR references (v1.2)
    presetManager.setModulationSlotsRef(&modSlots_);
    presetManager.setLfoPoolRef(&lfoPool_);
    presetManager.setEnvPoolRef(&envPool_);
    presetManager.setVolumeAdsrRef(&volumeAdsr_);
    presetManager.setRandomizerRef(&randomizer_);
    presetManager.setMidiLearnRef(&midiLearn_);

    // Initialise the engine partials ref so morphPresets can read
    // partial data from the engine after loading each preset.
    enginePartials_ = ana::PartialDataSIMD::fromPartialData(engine.getPartialData());
    presetManager.setEnginePartialsRef(&enginePartials_);

    // Initialise MacroController with 4 macros
    macroController_.setNumMacros(4);

    // Configure first envelope with default ADSR
    envPool_[0].prepare(44100.0);
    envPool_[0].rebuildADSR();

    // Configure independent Volume ADSR (VCA multiplier, NOT in modulation bus)
    // Uses Sustain loop mode so the envelope holds at sustain level until note-off.
    // Default sustain=1.0 means immediate full volume (no attenuation).
    volumeAdsr_.prepare(44100.0);
    volumeAdsr_.setSustain(1.0f);     // default: full volume during sustain
    volumeAdsr_.setLoopMode(ana::LoopMode::Sustain);
    volumeAdsr_.setLoopEnd(2);        // hold at sustain breakpoint (index 2)

    //==============================================================================
    // --- Flat-array modulation slots initialization ---
    // Each slot is wired to the corresponding modTarget* atomic as its base value.
    // Modulation is applied at block-rate in processBlock() via a single flat loop.
    modSlots_[0]  = { ana::ModulationConnection(), &modTargetFilterCutoff_,     0.0f, "filter_cutoff" };
    modSlots_[1]  = { ana::ModulationConnection(), &modTargetFilterRes_,        0.0f, "filter_res" };
    modSlots_[2]  = { ana::ModulationConnection(), &modTargetDelayTime_,        0.0f, "delay_time" };
    modSlots_[3]  = { ana::ModulationConnection(), &modTargetReverbWet_,        0.0f, "reverb_wet" };
    modSlots_[4]  = { ana::ModulationConnection(), &modTargetChorusDepth_,      0.0f, "chorus_depth" };
    modSlots_[5]  = { ana::ModulationConnection(), &modTargetPhaserFeedback_,   0.0f, "phaser_feedback" };
    modSlots_[6]  = { ana::ModulationConnection(), &modTargetDistDrive_,        0.0f, "dist_drive" };
    modSlots_[7]  = { ana::ModulationConnection(), &modTargetSaturationDrive_,  0.0f, "saturation_drive" };
    modSlots_[8]  = { ana::ModulationConnection(), &modTargetBitDepth_,         0.0f, "bit_depth" };
    modSlots_[9]  = { ana::ModulationConnection(), &modTargetRingFreq_,         0.0f, "ring_freq" };
    modSlots_[10] = { ana::ModulationConnection(), &modTargetWidenerWidth_,     0.0f, "widener_width" };
    modSlots_[11] = { ana::ModulationConnection(), &modTargetTimbreABright_,    0.0f, "timbre_a_brightness" };
    modSlots_[12] = { ana::ModulationConnection(), &modTargetTimbreBBlur_,      0.0f, "timbre_b_blur" };
    modSlots_[13] = { ana::ModulationConnection(), &modTargetMasterVol_,        0.0f, "master_vol" };
    modSlots_[14] = { ana::ModulationConnection(), &modTargetMasterPan_,        0.0f, "master_pan" };
    modSlots_[15] = { ana::ModulationConnection(), &modTargetSpare_,            0.0f, "spare" };

    // Register all effect factories and initialise the default effect rack
    ProcessorStore::registerAll();
    initializeDefaultEffects();
}

//==============================================================================
// initializeDefaultEffects — one-time default rack construction
//
// Creates the default 14-effect rack preset (StereoWidener → Delay → Flanger
// → Reverb → EQ → Chorus → Distortion → Saturation → Bitcrusher → Compressor
// → AutoTune → Phaser → RingModulator → Limiter).
//
// Called once from the constructor.  Guarded by effectsInitialized_ to prevent
// double initialisation if the host calls any preparation methods early.
//
// Effects that need direct pointer extraction for UI access use the manual
// adapter pattern (pointer-tracked).  Simpler effects use ProcessorStore
// factories.  Both approaches produce the same runtime chain.
//==============================================================================
void AnaPlugAudioProcessor::initializeDefaultEffects()
{
    if (effectsInitialized_)
        return;
    effectsInitialized_ = true;

    // Effects order (14):
    // StereoWidener → Delay → Flanger → Reverb → EQ → Chorus → Distortion
    // → Saturation → Bitcrusher → Compressor → AutoTune → Phaser
    // → RingModulator → Limiter

    // StereoWidener (no pointer extraction needed)
    effectsChain_.addEffect(ProcessorStore::create("StereoWidener"), "StereoWidener");

    // Delay (pointer-tracked via delayEffect_)
    {
        auto adapter = std::make_unique<ana::DelayEffectAdapter>();
        delayEffect_ = adapter->getEffect();
        effectsChain_.addEffect(std::move(adapter), "Delay");
    }

    // Flanger (reference to direct member flangerEffect_)
    effectsChain_.addEffect(std::make_unique<FlangerEffectAdapter>(flangerEffect_), "Flanger");

    // Reverb (pointer-tracked via reverbEffect_)
    {
        auto adapter = std::make_unique<ana::ReverbEffectAdapter>();
        reverbEffect_ = adapter->getEffect();
        effectsChain_.addEffect(std::move(adapter), "Reverb");
    }

    // EQ (pointer-tracked via eqEffect_)
    {
        auto adapter = std::make_unique<ana::EQEffectAdapter>();
        eqEffect_ = adapter->getEffect();
        effectsChain_.addEffect(std::move(adapter), "EQ");
    }

    // Chorus (pointer-tracked via chorusEffect_)
    {
        auto adapter = std::make_unique<ana::ChorusEffectAdapter>();
        chorusEffect_ = adapter->getEffect();
        effectsChain_.addEffect(std::move(adapter), "Chorus");
    }

    // Distortion (pointer-tracked via distortionEffect_)
    {
        auto adapter = std::make_unique<ana::DistortionEffectAdapter>();
        distortionEffect_ = adapter->getEffect();
        effectsChain_.addEffect(std::move(adapter), "Distortion");
    }

    // Saturation (reference to direct member saturationEffect_)
    effectsChain_.addEffect(std::make_unique<SaturationEffectAdapter>(saturationEffect_), "Saturation");

    // Bitcrusher (pointer-tracked via bitcrusherEffect_)
    {
        auto bc = std::make_unique<BitcrusherEffectAdapter>();
        bitcrusherEffect_ = bc->getEffect();
        bitcrusherSlotIndex_ = effectsChain_.addEffect(std::move(bc), "Bitcrusher");
    }

    // Compressor (pointer-tracked via compressorEffect_)
    {
        auto comp = std::make_unique<CompressorEffectAdapter>();
        compressorEffect_ = comp->getEffect();
        effectsChain_.addEffect(std::move(comp), "Compressor");
    }

    // AutoTune (pointer-tracked via autoTuneEffect_)
    {
        auto adapter = std::make_unique<ana::AutoTuneEffectAdapter>();
        autoTuneEffect_ = adapter->getEffect();
        effectsChain_.addEffect(std::move(adapter), "AutoTune");
    }

    // Phaser (pointer-tracked via phaserEffect_)
    {
        auto adapter = std::make_unique<PhaserEffectAdapter>();
        phaserEffect_ = adapter->getEffect();
        effectsChain_.addEffect(std::move(adapter), "Phaser");
    }

    // RingModulator (no pointer extraction needed)
    effectsChain_.addEffect(ProcessorStore::create("RingModulator"), "RingModulator");

    // Limiter (reference to direct member limiterEffect_)
    effectsChain_.addEffect(std::make_unique<LimiterEffectAdapter>(limiterEffect_), "Limiter");
}

AnaPlugAudioProcessor::~AnaPlugAudioProcessor()
{
}

const juce::String AnaPlugAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AnaPlugAudioProcessor::acceptsMidi() const
{
    return true;
}

bool AnaPlugAudioProcessor::producesMidi() const
{
    return false;
}

bool AnaPlugAudioProcessor::isMidiEffect() const
{
    return false;
}

double AnaPlugAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AnaPlugAudioProcessor::getNumPrograms()
{
    return 1;
}

int AnaPlugAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AnaPlugAudioProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String AnaPlugAudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void AnaPlugAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void AnaPlugAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);

    // Ensure FTZ/DAZ is active (some hosts may reset FP control state
    // between constructor and prepareToPlay or between successive calls)
#if JUCE_INTEL
    _mm_setcsr(_mm_getcsr() | 0x8040);  // FTZ + DAZ
#elif JUCE_ARM
    #if defined(__aarch64__)
    {
        uint64_t fpcr;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        fpcr |= (1ULL << 24);  // FZ (Flush-to-Zero, bit 24)
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
    }
    #else
    {
        uint32_t fpscr;
        __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
        fpscr |= (1 << 24);  // FZ (Flush-to-Zero, bit 24)
        __asm__ __volatile__("vmsr fpscr, %0" : : "r"(fpscr));
    }
    #endif
#endif

    // Prepare the polyphonic voice engine
    if (sampleRate > 0.0)
    {
        voiceManager.prepare(sampleRate);
        partialMod_.prepare(sampleRate);
        subHarmonicGen_.setSampleRate(sampleRate);

        // Prepare modulation source pool (4 LFOs + 3 ENVs)
        for (auto& lfo : lfoPool_)
            lfo.prepare(sampleRate);
        for (auto& env : envPool_)
            env.prepare(sampleRate);

        // Prepare independent Volume ADSR (VCA multiplier)
        volumeAdsr_.prepare(sampleRate);

        // Prepare Step Sequencer
        stepSequencer_.prepare(sampleRate);
        stepSequencer_.setBpm(sampleRate > 0.0 ? 120.0 : 120.0);

        // Prepare unison engine
        unisonEngine_.prepare(sampleRate, samplesPerBlock);

        // Pre-allocate voiceBuffer and unisonBuffer to avoid heap allocations in the audio callback
        voiceBuffer.setSize(juce::jmax(getTotalNumOutputChannels(), 1), samplesPerBlock, false, false, true);
        unisonBuffer.setSize(juce::jmax(getTotalNumOutputChannels(), 1), samplesPerBlock, false, false, true);

        // Prepare the effects chain (effects already initialised in constructor)
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = static_cast<juce::uint32>(juce::jmax(getTotalNumOutputChannels(), 1));

        effectsChain_.prepare(spec);

        // Prepare the vocal character processor
        vocalProcessor_.prepare(spec);

        // Prepare the post-effects master multi-filter with a default LP slot
        multiFilter_.clearSlots();
        multiFilter_.addSlot(ana::FilterType::LowPass);
        multiFilter_.prepare(spec);

        // Prepare LUFS output metering
        meteringEngine_.prepare(sampleRate,
                                spec.numChannels,
                                spec.maximumBlockSize);
    }

    // Initialise Spectral DNA evolver population if not already done
    if (!dnaEvolver_.getPopulationSize())
        dnaEvolver_.init(16);
}

void AnaPlugAudioProcessor::releaseResources()
{
    vocalProcessor_.reset();
}

void AnaPlugAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Flush-to-Zero and Denormals-Are-Zero for SSE
    // Reset here because some hosts reset the FP control word between prepareToPlay and processBlock
#if JUCE_INTEL
    _mm_setcsr(_mm_getcsr() | 0x8040);
#elif JUCE_ARM
    #if defined(__aarch64__)
    {
        uint64_t fpcr;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        fpcr |= (1ULL << 24);  // FZ (Flush-to-Zero, bit 24)
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
    }
    #else
    {
        uint32_t fpscr;
        __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
        fpscr |= (1 << 24);  // FZ (Flush-to-Zero, bit 24)
        __asm__ __volatile__("vmsr fpscr, %0" : : "r"(fpscr));
    }
    #endif
#endif

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const double sr       = getSampleRate();

    // Sync step sequencer BPM from host transport
    if (auto* ph = getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo pos;
        if (ph->getCurrentPosition(pos))
            stepSequencer_.setBpm(pos.bpm);
    }

    // Handle flatten trigger on message thread
    if (flattenPending_.exchange(false))
    {
        // Flatten is triggered from the UI, we just acknowledge it here
        // The actual flatten processing happens on the message thread
    }

    // --- MIDI side effects ---
    // MPESynthesiser processes all MIDI internally inside renderNextBlock.
    // This loop only handles non-MPE side effects that PluginProcessor needs.
    for (const auto& msg : midiMessages)
    {
        const auto& m = msg.getMessage();

        if (m.isNoteOn())
        {
            // Track most recent note number for UI
            lastMidiNote_.store(m.getNoteNumber());

            // Trigger unison engine note-on
            const float unisonFreq = 440.0f * std::pow(2.0f, (m.getNoteNumber() - 69.0f) / 12.0f);
            unisonEngine_.setFrequency(unisonFreq);
            unisonEngine_.noteOn();

            // Trigger all envelopes on each note-on
            for (auto& env : envPool_)
                env.trigger();

            // Trigger Volume ADSR on each note-on
            volumeAdsr_.trigger();
        }

        if (m.isNoteOff())
        {
            // Release Volume ADSR on note-off
            volumeAdsr_.release();
        }

        // Route ALL controller messages through the MIDI Learn system
        if (m.isController())
            midiLearn_.processMidi(m);
    }

    // --- LFO modulation pool ---
    // Advance all 4 LFO phases and store output values for flat-array modulation pass.
    for (int i = 0; i < 4; ++i)
        lfoModValues_[i] = lfoPool_[i].process(numSamples);

    // --- Envelope modulation pool ---
    // Advance all 3 envelopes and cache output values for flat-array modulation pass.
    for (int i = 0; i < 3; ++i)
        envValues_[i] = envPool_[i].process(numSamples);

    // --- Step Sequencer ---
    // Advance sequencer and cache its current CV value for the modulation pass.
    stepSequencer_.process(numSamples);
    sequencerValue_ = stepSequencer_.getCurrentValue();

    // --- Independent Volume ADSR (VCA multiplier, NOT in modulation bus) ---
    volumeAdsrValue_.store(volumeAdsr_.process(numSamples), std::memory_order_relaxed);

    // --- Flat-array modulation pass (Surge XT pattern) ---
    // Block-rate single pass: read LFO/ENV source values, apply depth per slot,
    // write modulatedValue. No per-sample branching, no virtual calls, no string
    // lookups.
    {
        // Grab LFO and ENV values once per block
        const float lfoVals[4] = {
            lfoModValues_[0], lfoModValues_[1],
            lfoModValues_[2], lfoModValues_[3]
        };
        const float envVals[3] = {
            envValues_[0], envValues_[1], envValues_[2]
        };

        for (auto& slot : modSlots_)
        {
            if (slot.baseValuePtr == nullptr) continue;
            const float baseVal = slot.baseValuePtr->load(std::memory_order_relaxed);
            float modVal = 0.0f;
            const int src = static_cast<int>(slot.mod.source);

            if (src >= static_cast<int>(ana::ModSource::LFO1) && src <= static_cast<int>(ana::ModSource::LFO4))
                modVal = lfoVals[static_cast<size_t>(src - static_cast<int>(ana::ModSource::LFO1))];
            else if (src >= static_cast<int>(ana::ModSource::ENV1) && src <= static_cast<int>(ana::ModSource::ENV3))
                modVal = envVals[static_cast<size_t>(src - static_cast<int>(ana::ModSource::ENV1))];
            else if (src == static_cast<int>(ana::ModSource::Sequencer))
                modVal = sequencerValue_;

            // Apply depth and write modulated output
            slot.modulatedValue = baseVal + modVal * slot.mod.depth;
        }
    }

    // --- Process VoiceManager audio ---
    // Use a temporary buffer for VoiceManager output, then mix into the main buffer
    // renderNextBlock processes MIDI (MPE) internally and renders audio
    voiceBuffer.setSize(numChannels, numSamples, false, false, true);
    voiceBuffer.clear();
    voiceManager.renderNextBlock(voiceBuffer, midiMessages, 0, numSamples);

    // --- Unison engine ---
    // Generates detuned stereo-spread unison voices on top of the VoiceManager
    // output. Only active when voice count > 1 (voices=1 bypasses).
    if (unisonEngine_.getVoiceCount() > 1)
    {
        unisonBuffer.setSize(numChannels, numSamples, false, false, true);
        unisonEngine_.process(unisonBuffer);

        // Mix: scale VoiceManager dry signal to make room, add unison wet
        voiceBuffer.applyGain(0.5f);
        for (int ch = 0; ch < numChannels; ++ch)
            voiceBuffer.addFrom(ch, 0, unisonBuffer, ch, 0, numSamples, 0.5f);
    }

    // --- Sub-harmonic generator ---
    // Generates sub-harmonic sine waves for each active voice and mixes them
    // into voiceBuffer using a recursive phasor (avoids std::sin per sample).
    {
        const float subLevel = subHarmonicLevel_.load();
        if (subLevel > 0.0f && sr > 0.0)
        {
            constexpr float twoPi = 6.283185307179586f;
            const float dT = 1.0f / static_cast<float>(sr);

            // Hoist channel write pointers outside all loops (Fix B)
            float* const chData[2] = {
                voiceBuffer.getWritePointer(0),
                numChannels > 1 ? voiceBuffer.getWritePointer(1) : nullptr
            };

            for (int v = 0; v < ana::VoiceManager::maxVoices; ++v)
            {
                if (!voiceManager.isVoiceActive(v))
                    continue;

                const auto* voice = voiceManager.getVoice(v);
                const float fundFreq = voice->pitchHz.load()
                                     * voice->pitchBend.load();
                if (fundFreq <= 0.0f)
                    continue;

                float subFreqs[3] = {}, subAmps[3] = {};
                const int nSubs = subHarmonicGen_.generate(fundFreq, subFreqs, subAmps, 3);

                const float envLevel = voice->envelopeLevel;
                const float voiceAmp = voice->amplitude.load();

                for (int s = 0; s < nSubs; ++s)
                {
                    if (subAmps[s] <= 1e-6f)
                        continue;

                    const int idx = v * 3 + s;
                    const float totalAmp = subAmps[s] * subLevel * envLevel * voiceAmp;

                    // Precompute phasor rotation coefficients for this sub (Fix A)
                    const float delta = twoPi * subFreqs[s] * dT;
                    const float cosDelta = std::cos(delta);
                    const float sinDelta = std::sin(delta);

                    // Load persistent phasor state (cos, sin) per sub-osc
                    float cosVal = subOscState_[idx].cos;
                    float sinVal = subOscState_[idx].sin;

                    // Inner sample loop: recursive phasor + direct pointer writes
                    for (int i = 0; i < numSamples; ++i)
                    {
                        // Recursive phasor output (no std::sin in inner loop)
                        const float sample = sinVal * totalAmp;
                        chData[0][i] += sample;
                        if (chData[1] != nullptr)
                            chData[1][i] += sample;

                        // Rotate phasor by delta (no phase wrap needed — Fix C)
                        const float newCos = cosVal * cosDelta - sinVal * sinDelta;
                        const float newSin = sinVal * cosDelta + cosVal * sinDelta;
                        cosVal = newCos;
                        sinVal = newSin;
                    }

                    // Store persistent phasor state
                    subOscState_[idx].cos = cosVal;
                    subOscState_[idx].sin = sinVal;
                }
            }
        }
    }

    // --- Preset morphing ---
    // If morph is enabled and both caches have data, blend the cached
    // partial data in real-time and write the result to synthPartials_
    // for the downstream synth / dual-chain to consume.
    if (morphEnabled_.load())
    {
        const float t = morphAmount_.load();
        if (morphCacheA_.activeCount > 0 && morphCacheB_.activeCount > 0)
        {
            ana::PartialDataSIMD morphed;
            ana::SpectralMorpher::morphLinear(morphed,
                                               morphCacheA_,
                                               morphCacheB_,
                                               t);
            // Store into synthPartials_ so it can be picked up by the
            // dual signal chain or synthesis engine when wired.
            synthPartials_ = std::move(morphed);
        }
    }

    // --- Wavetable engine (oscillator source) ---
    // When wavetable mode is enabled and the engine has loaded frames,
    // populate synthPartials_ with the interpolated wavetable frame.
    // This feeds into the partial modulator and multiband processor
    // downstream, acting as a spectral oscillator source.
    // Wavetable takes priority over morphing when both are enabled,
    // since it runs after the morph block above.
    if (wavetableEnabled_.load() && wavetableEngine_.isLoaded())
    {
        wavetableEngine_.getCurrentFrame(synthPartials_);
    }

    // --- Per-partial LFO/envelope modulation ---
    // Modulates synthPartials_ amplitudes using independent per-partial
    // LFO phases and ADSR envelopes.  Wired after voice generation and
    // sub-harmonics, before effects and dual-chain processing.
    {
        ana::PartialModulator::Config modConfig;
        modConfig.lfoRate  = 1.0f;
        modConfig.lfoDepth = 0.0f;   // default: no LFO modulation
        modConfig.attack   = 0.01f;
        modConfig.decay    = 0.1f;
        modConfig.sustain  = 0.7f;
        modConfig.release  = 0.3f;
        modConfig.perPartialPhase = true;

        partialMod_.process(synthPartials_, modConfig, numSamples);
    }

    // --- Dual signal chain (partial-domain spectral shaping) ---
    // Currently operates on PartialDataSIMD/TimbrePart data.  Once the synth is
    // refactored to produce partials (via synthPartials_), this will apply the
    // A/B spectral filter chains and blend modes.  For now it is a placeholder.
    // 
    // When chainEnabled_ is true and synthPartials_ contains voice partials:
    //   dualChain_.setInputA(partialsFromFilterA);
    //   dualChain_.setInputB(partialsFromFilterB);
    //   TimbrePart blended;
    //   dualChain_.process(blended);
    //   // synthesise 'blended' back into voiceBuffer
    if (chainEnabled_.load())
    {
        // Placeholder: chain requires partial-domain synth output.
        // Wire when synth is refactored to populate synthPartials_.
    }

    // --- Spectral effects (partial-domain: Prism → Blur → Harmonizer) ---
    // These operate on synthPartials_ in the partial/spectral domain, not on
    // the audio buffer.  They are controlled by the PRISM/BLUR/HARM buttons.
    if (synthPartials_.activeCount > 0)
    {
        if (prismEnabled_.load())
            prismEffect_.process(synthPartials_);
        if (blurEnabled_.load())
            blurEffect_.process(synthPartials_);
        if (harmEnabled_.load())
            harmonizer_.process(synthPartials_);
    }

    // --- Multiband Processor (frequency-band partial processing) ---
    if (synthPartials_.activeCount > 0)
        multibandProcessor_.process(synthPartials_);

    // --- Effects chain ---
    // Sync limiter parameters from atomics before processing
    limiterEffect_.setThreshold(limiterThreshold_.load());
    limiterEffect_.setRelease(limiterRelease_.load());
    limiterEffect_.setGain(limiterCeiling_.load());
    if (effectsEnabled_.load())
        effectsChain_.process(voiceBuffer);

    // --- Vocal character processor (post-effects, pre-filter) ---
    // Applies one of 7 vocal-character modes to the processed audio.
    vocalProcessor_.process(voiceBuffer);

    // --- Post-effects master multi-filter ---
    // Applies filter type/style/cutoff/resonance after all other effects
    // running in serial mode with a single slot by default
    multiFilter_.process(voiceBuffer);

    // --- Spectral DNA evolution audio path ---
    if (dnaEnabled_.load() && dnaBufferValid_.load())
    {
        // Atomic double-buffer contains the current best DNA's partial data
        // (freq, amp, phase interleaved per-partial, 512 partials max).
        // Full audio synthesis from partials will be implemented in Phase 2.
    }

    const int rootNote = rootNoteParam_.load();
    const int midiNote = lastMidiNote_.load();
    const float fineTune = rootFineTuneParam_.load();

    // Pitch ratio: 2^((midiNote - rootNote + fineTune/100) / 12)
    const float pitchRatio = std::pow(2.0f, (static_cast<float>(midiNote - rootNote) + fineTune / 100.0f) / 12.0f);

    if (isPlaying.load() && resynthBufferReady_.load())
    {
        const juce::SpinLock::ScopedTryLockType lock(resynthLock_);
        if (lock.isLocked())
        {
            const int readIdx = currentResynthBuffer_.load();
            const auto& readBuf = resynthBuffer_[readIdx];
            const int totalSamples = resynthBufferSize_.load();

            if (totalSamples > 0)
            {
                float pos = static_cast<float>(playbackPosition.load());

                for (int i = 0; i < numSamples; ++i)
                {
                    // Wrap around
                    while (pos >= static_cast<float>(totalSamples))
                        pos -= static_cast<float>(totalSamples);
                    while (pos < 0.0f)
                        pos += static_cast<float>(totalSamples);

                    // Linear interpolation
                    const int idx = static_cast<int>(pos);
                    const float frac = pos - static_cast<float>(idx);
                    const int nextIdx = (idx + 1 < totalSamples) ? idx + 1 : 0;

                    const float sample = readBuf[idx] * (1.0f - frac) + readBuf[nextIdx] * frac;

                    for (int ch = 0; ch < numChannels; ++ch)
                        buffer.setSample(ch, i, sample);

                    pos += pitchRatio;
                }

                playbackPosition.store(static_cast<int>(pos));

                // Mix VoiceManager output on top of resynthesis
                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.addFrom(ch, 0, voiceBuffer, ch, 0, numSamples, 0.5f);

                // Apply master volume and pan (output stage)
                {
                    const float vol = masterVol_.load();
                    const float pan = masterPan_.load();
                    const float panL = (pan <= 0.0f) ? 1.0f : 1.0f - pan;
                    const float panR = (pan >= 0.0f) ? 1.0f : 1.0f + pan;
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        float* d = buffer.getWritePointer(ch);
                        const float panG = (ch == 0) ? panL : panR;
                        for (int i = 0; i < numSamples; ++i)
                            d[i] = juce::jlimit(-1.0f, 1.0f, d[i] * vol * panG);
                    }
                }

                // Apply Volume ADSR (VCA multiplier, after master volume/pan)
                {
                    const float amp = volumeAdsrValue_.load(std::memory_order_relaxed);
                    buffer.applyGain(amp);
                }

                // LUFS output metering (read-only, after master gain)
                meteringEngine_.process(buffer);

                return;
            }
        }
    }

    // No resynthesis: just copy VoiceManager output to main buffer
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.copyFrom(ch, 0, voiceBuffer, ch, 0, numSamples);

    // Apply master volume and pan (output stage)
    {
        const float vol = masterVol_.load();
        const float pan = masterPan_.load();
        const float panL = (pan <= 0.0f) ? 1.0f : 1.0f - pan;
        const float panR = (pan >= 0.0f) ? 1.0f : 1.0f + pan;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* d = buffer.getWritePointer(ch);
            const float panG = (ch == 0) ? panL : panR;
            for (int i = 0; i < numSamples; ++i)
                d[i] = juce::jlimit(-1.0f, 1.0f, d[i] * vol * panG);
        }
    }

    // Apply Volume ADSR (VCA multiplier, after master volume/pan)
    {
        const float amp = volumeAdsrValue_.load(std::memory_order_relaxed);
        buffer.applyGain(amp);
    }

    // LUFS output metering (read-only, after master gain)
    meteringEngine_.process(buffer);
}

bool AnaPlugAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* AnaPlugAudioProcessor::createEditor()
{
    return new AnaPlugAudioProcessorEditor(*this);
}

void AnaPlugAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("AnaPlugState");
    state.setProperty("fftSize", fftSize.load(), nullptr);
    state.setProperty("hopSize", hopSize.load(), nullptr);
    state.setProperty("peakThreshold", peakThreshold.load(), nullptr);
    state.setProperty("rootNote", rootNoteParam_.load(), nullptr);
    state.setProperty("rootFineTune", rootFineTuneParam_.load(), nullptr);
    state.setProperty("mpeEnabled", voiceManager.isMPEEnabled(), nullptr);
    state.setProperty("mpeMasterChannel", voiceManager.getMPEMasterChannel(), nullptr);
    state.setProperty("subHarmonicLevel", subHarmonicLevel_.load(), nullptr);

    auto presetState = presetManager.serialiseState();
    state.addChild(presetState, -1, nullptr);

    // MIDI Learn mappings (all — global + per-preset)
    auto midiLearnState = midiLearn_.saveProcessorState();
    if (midiLearnState.isValid())
        state.addChild(midiLearnState, -1, nullptr);

    // Macro Controller state
    if (auto macroXml = macroController_.createXml())
    {
        juce::ValueTree macroTree = juce::ValueTree::fromXml(*macroXml);
        if (macroTree.isValid())
            state.addChild(macroTree, -1, nullptr);
    }

    juce::MemoryOutputStream stream(destData, true);
    state.writeToStream(stream);
}

void AnaPlugAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));
    if (!state.isValid() || !state.hasType("AnaPlugState"))
        return;

    if (state.hasProperty("fftSize")) fftSize.store(juce::jlimit(256, 65536, (int)state.getProperty("fftSize")));
    if (state.hasProperty("hopSize")) hopSize.store(juce::jlimit(64, 8192, (int)state.getProperty("hopSize")));
    if (state.hasProperty("peakThreshold")) peakThreshold.store(juce::jlimit(-120.0f, 0.0f, (float)state.getProperty("peakThreshold", -60.0f)));
    
    if (state.hasProperty("rootNote")) { int note = (int)state.getProperty("rootNote", 60); setRootNote(juce::jlimit(0, 127, note)); }
    if (state.hasProperty("rootFineTune")) { float fine = (float)state.getProperty("rootFineTune", 0.0f); setRootFineTune(juce::jlimit(-50.0f, 50.0f, fine)); }
    if (state.hasProperty("subHarmonicLevel")) { float subLevel = (float)state.getProperty("subHarmonicLevel", 0.0f); setSubHarmonicLevel(juce::jlimit(0.0f, 1.0f, subLevel)); }
    
    if (state.hasProperty("mpeEnabled")) setMPEEnabled(state.getProperty("mpeEnabled"));
    if (state.hasProperty("mpeMasterChannel")) setMPEMasterChannel(juce::jlimit(0, 15, (int)state.getProperty("mpeMasterChannel")));

    auto presetState = state.getChildWithName("Parameters");
    if (presetState.isValid())
        presetManager.deserialiseState(presetState);

    // MIDI Learn mappings (all — global + per-preset)
    auto midiLearnState = state.getChildWithName("MidiLearn");
    if (midiLearnState.isValid())
        midiLearn_.loadProcessorState(midiLearnState);

    // Macro Controller state
    auto macroState = state.getChildWithName("macrocontroller");
    if (macroState.isValid())
    {
        auto macroXml = macroState.toXml();
        if (macroXml != nullptr)
        {
            macroController_.loadFromXml(*macroXml);
        }
    }
}

bool AnaPlugAudioProcessor::loadFile(const juce::File& file)
{
    bool success = engine.loadSample(file);
    if (success)
    {
        // Auto-analyze after loading
        engine.analyze();
        // Sync the engine partials SIMD cache for morphing
        enginePartials_ = ana::PartialDataSIMD::fromPartialData(engine.getPartialData());
        // Auto-resynthesize
        auto result = engine.resynthesize();
        {
            const juce::SpinLock::ScopedLockType lock(resynthLock_);
            const int writeIdx = 1 - currentResynthBuffer_.load();
            resynthBuffer_[writeIdx] = std::move(result);
            resynthBufferSize_.store(static_cast<int>(resynthBuffer_[writeIdx].size()));
            currentResynthBuffer_.store(writeIdx);
            resynthBufferReady_.store(true);
            playbackPosition.store(0);
        }
    }
    return success;
}

bool AnaPlugAudioProcessor::loadSampleAsParent(const juce::File& audioFile)
{
    using namespace ana;

    if (!audioFile.existsAsFile()) return false;

    // 1. Load audio file via WavLoader
    WavLoader loader;
    auto audioDataOpt = loader.loadWav(audioFile);
    if (!audioDataOpt.has_value()) return false;

    const auto& audioData = audioDataOpt.value();
    const double sampleRate = audioData.sampleRate;

    // 2. STFT analysis
    STFTConfig config;
    config.fftSize = 2048;
    config.hopSize = 512;

    STFTAnalyzer analyzer;
    auto spectrumFrames = analyzer.analyze(audioData, config);
    if (spectrumFrames.empty()) return false;

    // 3. Detect spectral peaks from the last analysis frame
    PeakDetector peakDetector;
    const auto& lastSpectrum = spectrumFrames.back();
    auto peaks = peakDetector.detectPeaks(lastSpectrum, config, sampleRate);
    if (peaks.empty()) return false;

    // 4. Build a SpectralDNA from the detected peaks
    SpectralDNA parent;
    const size_t peakCount = std::min(peaks.size(), static_cast<size_t>(SpectralDNA::kMaxPartials));
    for (size_t i = 0; i < peakCount; ++i)
    {
        parent.frequency[i] = peaks[i].frequency;
        parent.amplitude[i] = peaks[i].amplitude;
        parent.phase[i]     = peaks[i].phase;
    }
    parent.clamp();
    parent.updateActiveMask();

    // 5. Ensure evolver has a population
    if (!dnaEvolver_.getPopulationSize())
        dnaEvolver_.init(16);

    // 6. Insert the analysed parent (replaces the worst individual)
    dnaEvolver_.replaceWorst(parent);

    // 7. Refresh the atomic audio buffer with the current fittest DNA
    //    Interleave freq, amp, phase for audio-thread safe reading.
    const auto& best = dnaEvolver_.getFittest();
    for (int i = 0; i < SpectralDNA::kMaxPartials; ++i)
    {
        dnaAudioBuffer_[static_cast<size_t>(i) * 3 + 0] = best.frequency[i];
        dnaAudioBuffer_[static_cast<size_t>(i) * 3 + 1] = best.amplitude[i];
        dnaAudioBuffer_[static_cast<size_t>(i) * 3 + 2] = best.phase[i];
    }
    dnaBufferValid_.store(true);

    lastSampleFile_ = audioFile;
    dnaSampleLoaded_.store(true);

    return true;
}

void AnaPlugAudioProcessor::startPlayback()
{
    playbackPosition.store(0);
    isPlaying.store(true);
}

void AnaPlugAudioProcessor::stopPlayback()
{
    isPlaying.store(false);
}

bool AnaPlugAudioProcessor::isEngineLoaded() const
{
    return engine.isLoaded();
}

bool AnaPlugAudioProcessor::isEngineAnalyzed() const
{
    return engine.isAnalyzed();
}

ana::AnaPlugEngine& AnaPlugAudioProcessor::getEngine()
{
    return engine;
}

const ana::AnaPlugEngine& AnaPlugAudioProcessor::getEngine() const
{
    return engine;
}

const std::vector<float>& AnaPlugAudioProcessor::getResynthesizedBuffer() const
{
    return resynthBuffer_[currentResynthBuffer_.load()];
}

void AnaPlugAudioProcessor::setResynthesizedBuffer(std::vector<float> buffer)
{
    const juce::SpinLock::ScopedLockType lock(resynthLock_);
    const int writeIdx = 1 - currentResynthBuffer_.load();
    resynthBuffer_[writeIdx] = std::move(buffer);
    resynthBufferSize_.store(static_cast<int>(resynthBuffer_[writeIdx].size()));
    currentResynthBuffer_.store(writeIdx);
    resynthBufferReady_.store(true);
    playbackPosition.store(0);
}

ana::STFTConfig AnaPlugAudioProcessor::getSTFTConfig() const
{
    ana::STFTConfig config;
    config.fftSize = fftSize.load();
    config.hopSize = hopSize.load();
    config.peakThresholdDB = peakThreshold.load();
    return config;
}

void AnaPlugAudioProcessor::setSTFTConfig(const ana::STFTConfig& config)
{
    fftSize.store(config.fftSize);
    hopSize.store(config.hopSize);
    peakThreshold.store(config.peakThresholdDB);
}

int AnaPlugAudioProcessor::getPartialCount() const
{
    const auto& partialData = engine.getPartialData();
    int count = 0;
    for (const auto& frame : partialData.frames)
        count += static_cast<int>(frame.partials.size());
    return count;
}

//==============================================================================
// Root note control
//==============================================================================

void AnaPlugAudioProcessor::setRootNote(int note)
{
    note = juce::jlimit(0, 127, note);
    rootNoteParam_.store(note);
    engine.setRootNote(note);
}

int AnaPlugAudioProcessor::getRootNote() const
{
    return rootNoteParam_.load();
}

void AnaPlugAudioProcessor::setRootFineTune(float cents)
{
    cents = juce::jlimit(-50.0f, 50.0f, cents);
    rootFineTuneParam_.store(cents);
    engine.setRootFineTune(cents);
}

float AnaPlugAudioProcessor::getRootFineTune() const
{
    return rootFineTuneParam_.load();
}

//==============================================================================
// Sub-harmonic generator control
//==============================================================================

void AnaPlugAudioProcessor::setSubHarmonicLevel(float level)
{
    level = juce::jlimit(0.0f, 1.0f, level);
    subHarmonicLevel_.store(level);
    subHarmonicGen_.setSubLevel(0, level);
}

float AnaPlugAudioProcessor::getSubHarmonicLevel() const
{
    return subHarmonicLevel_.load();
}

//==============================================================================
// Preset morphing
//==============================================================================

bool AnaPlugAudioProcessor::morphPresets(const juce::String& presetA,
                                          const juce::String& presetB,
                                          float t)
{
    // Update the engine partials snapshot before morphing
    enginePartials_ = ana::PartialDataSIMD::fromPartialData(engine.getPartialData());

    // Cache the current preset name so we can restore it after morph
    const juce::String restorePreset = presetManager.getCurrentPresetName();

    // Let PresetManager load both presets and morph their partial data
    ana::PartialDataSIMD morphedOutput;
    if (!presetManager.morphPresets(presetA, presetB, t, morphedOutput))
        return false;

    // Cache the pre-loaded partials for audio-thread-safe access
    morphCacheA_ = presetManager.getMorphCacheA();
    morphCacheB_ = presetManager.getMorphCacheB();

    // Store the morph configuration
    morphPresetA_ = presetA;
    morphPresetB_ = presetB;
    morphAmount_.store(t);
    morphEnabled_.store(true);

    // Restore the original preset so the engine continues with the user's
    // current sound.  The cached partials are used for morphing instead.
    if (restorePreset.isNotEmpty())
        presetManager.loadPreset(restorePreset);

    return true;
}

//==============================================================================
// Limiter control
//==============================================================================

void AnaPlugAudioProcessor::setBitcrusherWetLowCut(float hz)
{
    hz = juce::jlimit(20.0f, 500.0f, hz);
    if (bitcrusherSlotIndex_ >= 0)
        effectsChain_.setWetLowCut(bitcrusherSlotIndex_, hz);
}

void AnaPlugAudioProcessor::setBitcrusherWetHighCut(float hz)
{
    hz = juce::jlimit(500.0f, 20000.0f, hz);
    if (bitcrusherSlotIndex_ >= 0)
        effectsChain_.setWetHighCut(bitcrusherSlotIndex_, hz);
}

void AnaPlugAudioProcessor::setLimiterThreshold(float db)
{
    db = juce::jlimit(-30.0f, 0.0f, db);
    limiterThreshold_.store(db);
}

void AnaPlugAudioProcessor::setLimiterRelease(float ms)
{
    ms = juce::jlimit(1.0f, 100.0f, ms);
    limiterRelease_.store(ms);
}

void AnaPlugAudioProcessor::setLimiterCeiling(float ceiling)
{
    // Never exceed 0dBFS (gain = 1.0)
    ceiling = juce::jlimit(0.0f, 1.0f, ceiling);
    limiterCeiling_.store(ceiling);
}

//==============================================================================
// Pitch flatten
//==============================================================================

void AnaPlugAudioProcessor::triggerFlattenPitch()
{
    flattenPending_.store(true);
}

bool AnaPlugAudioProcessor::flattenPending() const
{
    return flattenPending_.load();
}

void AnaPlugAudioProcessor::clearFlattenPending()
{
    flattenPending_.store(false);
}

//==============================================================================
// LFO target control
//==============================================================================

void AnaPlugAudioProcessor::updateLFOTarget(int /*targetId*/)
{
    // Routing now handled per-parameter in Task 2.
    // Target combo box preserved for backward compat until UI replacement in Task 4.
}

//==============================================================================
// Envelope target control
//==============================================================================

void AnaPlugAudioProcessor::setEnvelopeTarget(int targetId)
{
    envelopeTargetId_ = targetId;
}

void AnaPlugAudioProcessor::rebuildEnvelopeRoute()
{
    // Routing now handled per-parameter in Task 2.
    // Target combo box preserved for backward compat until UI replacement in Task 4.
}

//==============================================================================
// --- Flat-array modulation slots methods ---
//==============================================================================

void AnaPlugAudioProcessor::setModSource(int slotIndex, ana::ModSource src)
{
    if (slotIndex >= 0 && slotIndex < static_cast<int>(modSlots_.size()))
        modSlots_[static_cast<size_t>(slotIndex)].mod.source = src;
}

void AnaPlugAudioProcessor::setModDepth(int slotIndex, float depth)
{
    if (slotIndex >= 0 && slotIndex < static_cast<int>(modSlots_.size()))
        modSlots_[static_cast<size_t>(slotIndex)].mod.depth = juce::jlimit(-1.0f, 1.0f, depth);
}

float AnaPlugAudioProcessor::getModulatedValue(int slotIndex) const
{
    if (slotIndex >= 0 && slotIndex < static_cast<int>(modSlots_.size()))
        return modSlots_[static_cast<size_t>(slotIndex)].modulatedValue;
    return 0.0f;
}

//==============================================================================
// Randomizer
//==============================================================================
void AnaPlugAudioProcessor::randomizeAllParameters()
{
    auto& rng = randomizer_;

    // Master volume / pan
    masterVol_.store(rng.apply(masterVol_.load(), 0.0f, 2.0f));
    masterPan_.store(rng.apply(masterPan_.load(), -1.0f, 1.0f));

    // Sub-harmonic level
    subHarmonicLevel_.store(rng.apply(subHarmonicLevel_.load(), 0.0f, 1.0f));

    // MultiFilter slot 0 (main filter)
    if (multiFilter_.getNumSlots() > 0)
    {
        auto& slot = multiFilter_.getSlot(0);
        slot.params.cutoff = static_cast<double>(
            rng.apply(static_cast<float>(slot.params.cutoff), 20.0f, 20000.0f));
        slot.params.resonance = rng.apply(static_cast<float>(slot.params.resonance), 0.0f, 1.0f);
        slot.params.drive = rng.apply(static_cast<float>(slot.params.drive), 0.0f, 1.0f);
        slot.params.mix = rng.apply(static_cast<float>(slot.params.mix), 0.0f, 1.0f);
        multiFilter_.markCoefficientsDirty();
    }

    // Unison engine
    unisonEngine_.setDetune(rng.apply(unisonEngine_.getDetune(), 0.0f, 50.0f));
    unisonEngine_.setStereoSpread(rng.apply(unisonEngine_.getStereoSpread(), 0.0f, 100.0f));

    // Modulation target atomics (sound parameters)
    modTargetFilterCutoff_.store(
        rng.apply(modTargetFilterCutoff_.load(), 20.0f, 20000.0f));
    modTargetFilterRes_.store(
        rng.apply(modTargetFilterRes_.load(), 0.0f, 1.0f));
    modTargetDelayTime_.store(
        rng.apply(modTargetDelayTime_.load(), 1.0f, 2000.0f));
    modTargetReverbWet_.store(
        rng.apply(modTargetReverbWet_.load(), 0.0f, 1.0f));
    modTargetChorusDepth_.store(
        rng.apply(modTargetChorusDepth_.load(), 0.0f, 1.0f));
    modTargetDistDrive_.store(
        rng.apply(modTargetDistDrive_.load(), 0.0f, 1.0f));
    modTargetSaturationDrive_.store(
        rng.apply(modTargetSaturationDrive_.load(), 0.0f, 1.0f));
    modTargetBitDepth_.store(
        rng.apply(modTargetBitDepth_.load(), 1.0f, 24.0f));
    modTargetMasterVol_.store(
        rng.apply(modTargetMasterVol_.load(), 0.0f, 2.0f));
    modTargetMasterPan_.store(
        rng.apply(modTargetMasterPan_.load(), -1.0f, 1.0f));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnaPlugAudioProcessor();
}
