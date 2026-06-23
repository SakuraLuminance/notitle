#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <clap-juce-extensions/clap-juce-extensions.h>
#include "dsp/AnaPlugEngine.h"

//==============================================================================
/**
    Mixin adapter for JUCE AudioProcessorParameters that should advertise
    CLAP polyphonic modulation capability.

    Use as a base class (alongside juce::AudioParameterFloat etc.) so the
    clap-juce-extensions wrapper routes per-voice modulation to your param:

        class PolyParam : public juce::AudioParameterFloat,
                          public clap_juce_extensions::clap_juce_parameter_capabilities
        {
            bool supportsPolyphonicModulation() override { return true; }
            void applyPolyphonicModulation(int32_t /*noteId*/,
                                           int16_t /*portIndex*/,
                                           int16_t /*channel*/,
                                           int16_t /*key*/,
                                           double amount) override
            {
                // route amount to your modulation bus target
            }
        };

    The processor-level `supportsNoteExpressions()` must also return true
    for the host to send per-note modulation events.
*/
class ClapPolyphonicParam : public clap_juce_extensions::clap_juce_parameter_capabilities
{
public:
    bool supportsPolyphonicModulation() override { return true; }

    /** Override to route per-note modulation to your target parameter.
        @param noteId    CLAP note ID (unique per active note)
        @param portIndex MIDI port index
        @param channel   MIDI channel
        @param key       MIDI key (0-127)
        @param amount    Normalised modulation amount [0..1]
    */
    void applyPolyphonicModulation(int32_t noteId,
                                   int16_t portIndex,
                                   int16_t channel,
                                   int16_t key,
                                   double amount) override
    {
        juce::ignoreUnused(noteId, portIndex, channel, key, amount);
        // Subclass should forward `amount` to the corresponding
        // modSlots_ target atomic via setModDepth / applyModulation.
    }
};
#include "dsp/VoiceManager.h"
#include "dsp/SubHarmonicGenerator.h"
#include "dsp/PresetManager.h"
#include "dsp/DualSignalChain.h"
#include "dsp/EffectsChain.h"
#include "dsp/PartialDataSIMD.h"
#include "dsp/SpectralDNA.h"
#include "dsp/SpectralMorpher.h"
#include "dsp/WavetableEngine.h"
#include "dsp/MidiLearn.h"
#include "dsp/MacroController.h"
#include "dsp/ProcessorStore.h"
#include "dsp/MultibandProcessor.h"
#include "dsp/MultiFilter.h"
#include "dsp/ModulationBus.h"
#include "dsp/MultiPointEnvelope.h"
#include "dsp/LFOSystem.h"
#include "dsp/StepSequencer.h"
#include "dsp/ModulationEngine.h"
#include "dsp/PartialModulator.h"
#include "dsp/UnisonEngine.h"
#include "dsp/MeteringEngine.h"
#include "dsp/effects/LimiterEffect.h"
#include "dsp/effects/FlangerEffect.h"
#include "dsp/effects/SaturationEffect.h"
#include "dsp/effects/VocalProcessor.h"
#include "dsp/BlurEffect.h"
#include "dsp/PrismEffect.h"
#include "dsp/Harmonizer.h"
#include "dsp/Randomizer.h"
#include <array>
#include <atomic>

namespace ana { class CompressorEffect; }
namespace ana { class BitcrusherEffect; }
namespace ana { class DelayEffect; }
namespace ana { class ReverbEffect; }
namespace ana { class EQEffect; }
namespace ana { class ChorusEffect; }
namespace ana { class DistortionEffect; }
namespace ana { class AutoTuneEffect; }
namespace ana { class PhaserEffect; }

class AnaPlugAudioProcessor : public juce::AudioProcessor,
                              public clap_juce_extensions::clap_properties,
                              public clap_juce_extensions::clap_juce_audio_processor_capabilities
{
public:
    AnaPlugAudioProcessor();
    ~AnaPlugAudioProcessor() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // One-time default effects initialisation (called from constructor)
    void initializeDefaultEffects();
    bool areEffectsInitialized() const { return effectsInitialized_; }

    // Engine access
    bool loadFile(const juce::File& file);
    void startPlayback();
    void stopPlayback();
    bool isEngineLoaded() const;
    bool isEngineAnalyzed() const;
    ana::AnaPlugEngine& getEngine();
    const ana::AnaPlugEngine& getEngine() const;

    // Resynthesized buffer access
    const std::vector<float>& getResynthesizedBuffer() const;
    void setResynthesizedBuffer(std::vector<float> buffer);

    // Playback access
    int getPlaybackPosition() const { return playbackPosition.load(); }

    // STFT configuration
    void setFftSize(int size) { fftSize.store(size); }
    void setHopSize(int size) { hopSize.store(size); }
    void setPeakThreshold(float dB) { peakThreshold.store(dB); }
    int getFftSize() const { return fftSize.load(); }
    int getHopSize() const { return hopSize.load(); }
    float getPeakThreshold() const { return peakThreshold.load(); }

    ana::STFTConfig getSTFTConfig() const;
    void setSTFTConfig(const ana::STFTConfig& config);
    int getPartialCount() const;

    // Root note control
    void setRootNote(int note);
    int getRootNote() const;
    void setRootFineTune(float cents);
    float getRootFineTune() const;

    // Pitch flatten
    void triggerFlattenPitch();
    bool flattenPending() const;
    void clearFlattenPending();

    // MIDI note tracking
    int getLastMidiNote() const { return lastMidiNote_.load(); }
    void setLastMidiNote(int note) { lastMidiNote_.store(note); }

    // MPE access
    void setMPEEnabled(bool enabled) { voiceManager.enableMPE(enabled); }
    bool isMPEEnabled() const { return voiceManager.isMPEEnabled(); }
    void setMPEMasterChannel(int channel) { voiceManager.setMPEMasterChannel(channel); }
    int getMPEMasterChannel() const { return voiceManager.getMPEMasterChannel(); }
    ana::VoiceManager& getVoiceManager() { return voiceManager; }

    // CLAP note expressions (clap-juce-extensions)
    // Tells the CLAP wrapper that this plugin wants to receive per-note
    // expression events (pitch, pan, volume, brightness, expression).
    bool supportsNoteExpressions() override { return true; }

    /** Enables CLAP polyphonic modulation advertisement at the plugin level.
        Per-parameter polyphonic modulation is implemented by deriving
        parameters from ClapPolyphonicParam (defined above), which overrides
        clap_juce_parameter_capabilities::supportsPolyphonicModulation().
        @see ClapPolyphonicParam
    */
    bool supportsPolyphonicModulation() { return true; }

    //==============================================================================
    // CLAP note expression → clapNoteExprBus_ routing (clap-juce-extensions)
    // Map the five standard CLAP per-note expression parameters to the dedicated
    // CLAP modulation bus so the DSP engine can react per-voice.
    bool supportsDirectEvent(uint16_t spaceId, uint16_t type) override;
    void handleDirectEvent(const clap_event_header_t* event, int sampleOffset) override;

    /** Access the ModulationBus for CLAP note expression routing. */
    ana::ModulationBus& getClapNoteExprBus() { return clapNoteExprBus_; }

    // Sub-harmonic generator control
    void setSubHarmonicLevel(float level);
    float getSubHarmonicLevel() const;
    ana::SubHarmonicGenerator& getSubHarmonicGenerator() { return subHarmonicGen_; }

    // Dual signal chain access
    ana::DualSignalChain& getDualChain() { return dualChain_; }
    bool isChainEnabled() const { return chainEnabled_.load(); }
    void setChainEnabled(bool enabled) { chainEnabled_.store(enabled); }

    // Effects chain access
    ana::EffectsChain& getEffectsChain() { return effectsChain_; }
    bool isEffectsEnabled() const { return effectsEnabled_.load(); }
    void setEffectsEnabled(bool enabled) { effectsEnabled_.store(enabled); }

    // Multi-filter access
    ana::MultiFilter& getMultiFilter() { return multiFilter_; }

    // Vocal processor access
    ana::VocalProcessor& getVocalProcessor() { return vocalProcessor_; }
    const ana::VocalProcessor& getVocalProcessor() const { return vocalProcessor_; }

    // Limiter parameter access
    void setLimiterThreshold(float db);
    float getLimiterThreshold() const { return limiterThreshold_.load(); }
    std::atomic<float>& getLimiterThresholdRef() { return limiterThreshold_; }
    void setLimiterRelease(float ms);
    float getLimiterRelease() const { return limiterRelease_.load(); }
    std::atomic<float>& getLimiterReleaseRef() { return limiterRelease_; }
    void setLimiterCeiling(float ceiling);
    float getLimiterCeiling() const { return limiterCeiling_.load(); }
    std::atomic<float>& getLimiterCeilingRef() { return limiterCeiling_; }

    // Master volume / pan (output stage)
    void setMasterVol(float v) { masterVol_.store(v); }
    float getMasterVol() const { return masterVol_.load(); }
    void setMasterPan(float v) { masterPan_.store(v); }
    float getMasterPan() const { return masterPan_.load(); }

    // Compressor access (for UI controls)
    ana::CompressorEffect* getCompressorEffect() { return compressorEffect_; }

    // Flanger effect access (for UI controls)
    ana::FlangerEffect& getFlangerEffect() { return flangerEffect_; }

    // Saturation effect access (for UI controls)
    ana::SaturationEffect& getSaturationEffect() { return saturationEffect_; }

    // Bitcrusher effect access (for UI controls)
    ana::BitcrusherEffect* getBitcrusherEffect() { return bitcrusherEffect_; }
    int getBitcrusherSlotIndex() const { return bitcrusherSlotIndex_; }
    void setBitcrusherWetLowCut(float hz);
    void setBitcrusherWetHighCut(float hz);

    // EffectsChain-owned effect access (for UI controls)
    ana::DelayEffect&      getDelayEffect()      { return *delayEffect_; }
    ana::ReverbEffect&     getReverbEffect()     { return *reverbEffect_; }
    ana::EQEffect&         getEQEffect()         { return *eqEffect_; }
    ana::ChorusEffect&     getChorusEffect()     { return *chorusEffect_; }
    ana::DistortionEffect& getDistortionEffect() { return *distortionEffect_; }
    ana::AutoTuneEffect&   getAutoTuneEffect()   { return *autoTuneEffect_; }
    ana::PhaserEffect&     getPhaserEffect()     { return *phaserEffect_; }

    // Spectral effect toggles (for UI buttons)
    bool isPrismEnabled() const { return prismEnabled_.load(); }
    void setPrismEnabled(bool enabled) { prismEnabled_.store(enabled); }
    ana::PrismEffect& getPrismEffect() { return prismEffect_; }

    bool isBlurEnabled() const { return blurEnabled_.load(); }
    void setBlurEnabled(bool enabled) { blurEnabled_.store(enabled); }
    ana::BlurEffect& getBlurEffect() { return blurEffect_; }

    bool isHarmEnabled() const { return harmEnabled_.load(); }
    void setHarmEnabled(bool enabled) { harmEnabled_.store(enabled); }
    ana::Harmonizer& getHarmonizer() { return harmonizer_; }

    // Preset Manager
    ana::PresetManager& getPresetManager() { return presetManager; }

    // --- Spectral DNA evolver ---
    ana::SpectralDNAEvolver& getDNAEvolver() { return dnaEvolver_; }
    bool loadSampleAsParent(const juce::File& audioFile);
    bool isDNAEnabled() const { return dnaEnabled_.load(); }
    void setDNAEnabled(bool enabled) { dnaEnabled_.store(enabled); }
    const juce::File& getLastSampleFile() const { return lastSampleFile_; }

    //==============================================================================
    // --- Preset morphing ---
    /** Loads two presets, caches their partial data, and stores the morph
        configuration.  The actual per-block morph is applied in processBlock
        using the cached data + current morphAmount_.

        @param presetA  First morph source (t=0)
        @param presetB  Second morph source (t=1)
        @param t        Initial morph amount [0, 1]
        @return true if both presets were loaded and cached successfully
    */
    bool morphPresets(const juce::String& presetA,
                      const juce::String& presetB,
                      float t);

    bool isMorphEnabled() const             { return morphEnabled_.load(); }
    void setMorphEnabled(bool enabled)      { morphEnabled_.store(enabled); }
    float getMorphAmount() const            { return morphAmount_.load(); }
    void setMorphAmount(float amount)       { morphAmount_.store(amount); }
    juce::String getMorphPresetA() const    { return morphPresetA_; }
    juce::String getMorphPresetB() const    { return morphPresetB_; }

    // --- Randomizer ---
    ana::Randomizer& getRandomizer() { return randomizer_; }
    /** Applies the current Randomizer seed + range to all sound parameters. */
    void randomizeAllParameters();

    // --- MIDI Learn ---
    ana::MidiLearn& getMidiLearn() { return midiLearn_; }
    const ana::MidiLearn& getMidiLearn() const { return midiLearn_; }

    // --- Macro Controller ---
    ana::MacroController& getMacroController() { return macroController_; }
    const ana::MacroController& getMacroController() const { return macroController_; }

    // Atomic references for MIDI Learn targets
    std::atomic<float>& getSubHarmonicLevelRef() { return subHarmonicLevel_; }
    std::atomic<int>& getRootNoteRef() { return rootNoteParam_; }
    std::atomic<float>& getRootFineTuneRef() { return rootFineTuneParam_; }
    std::atomic<float>& getMorphAmountRef() { return morphAmount_; }

    // Unison engine access
    ana::UnisonEngine& getUnisonEngine() { return unisonEngine_; }

    // Metering engine access
    ana::MeteringEngine& getMeteringEngine() { return meteringEngine_; }
    const ana::MeteringEngine& getMeteringEngine() const { return meteringEngine_; }

    //==============================================================================
    // --- Oscilloscope scope buffer (captures last N samples of output) ---
    static constexpr int kScopeBufferSize = 2048;
    void captureScopeOutput(const juce::AudioBuffer<float>& buffer);
    bool getScopeOutput(std::vector<float>& dest) const;

    // Wavetable engine access
    ana::WavetableEngine& getWavetableEngine() { return wavetableEngine_; }
    std::atomic<float>& getWavetablePositionRef() { return wavetablePosition_; }
    float getWavetablePosition() const { return wavetablePosition_.load(); }
    void setWavetablePosition(float pos) { wavetablePosition_.store(pos); wavetableEngine_.setPosition(pos); }
    bool isWavetableEnabled() const { return wavetableEnabled_.load(); }
    void setWavetableEnabled(bool enabled) { wavetableEnabled_.store(enabled); }
    std::atomic<bool>& getWavetableEnabledRef() { return wavetableEnabled_; }

    //==============================================================================
    // --- Envelope ADSR control (operates on envPool_[0]) ---
    void setEnvelopeAttack(float v)  { envPool_[0].setAttack(v); }
    float getEnvelopeAttack() const  { return envPool_[0].getAttack(); }
    void setEnvelopeDecay(float v)   { envPool_[0].setDecay(v); }
    float getEnvelopeDecay() const   { return envPool_[0].getDecay(); }
    void setEnvelopeSustain(float v) { envPool_[0].setSustain(v); }
    float getEnvelopeSustain() const { return envPool_[0].getSustain(); }
    void setEnvelopeRelease(float v) { envPool_[0].setRelease(v); }
    float getEnvelopeRelease() const { return envPool_[0].getRelease(); }

    /** Stores envelope target selection; routing handled by modSlots_ in Task 2. */
    void setEnvelopeTarget(int targetId);
    int getEnvelopeTarget() const { return envelopeTargetId_; }

    /** Preserved for backward compat; no-op since Task 1 (routing by modSlots_). */
    void rebuildEnvelopeRoute();

    ana::MultiPointEnvelope& getEnvelope() { return envPool_[0]; }

    //==============================================================================
    // --- Flat-array modulation slots (16 params, Surge XT pattern) ---
    /** Sets the modulation source for the given slot index.
        @param slotIndex  0-15
        @param src        ModSource (OFF, LFO1-4, ENV1-3)
    */
    void setModSource(int slotIndex, ana::ModSource src);

    /** Sets the modulation depth for the given slot index.
        @param slotIndex  0-15
        @param depth      Modulation amount in [-1.0, 1.0]
    */
    void setModDepth(int slotIndex, float depth);

    /** Returns the current modulated value for the given slot index.
        Updated once per block in processBlock().
        @param slotIndex  0-15
        @return Modulated parameter value (base + modulation)
    */
    float getModulatedValue(int slotIndex) const;

    /** Access the modulation slot configuration (for UI sync). */
    const ana::ModulationSlot& getModSlot(int index) const { return modSlots_[index]; }
    ana::ModulationSlot& getModSlot(int index) { return modSlots_[index]; }

    //==============================================================================
    // --- Step Sequencer ---
    ana::StepSequencer& getStepSequencer() { return stepSequencer_; }
    const ana::StepSequencer& getStepSequencer() const { return stepSequencer_; }
    const float* getSequencerValuePtr() const { return &sequencerValue_; }

    //==============================================================================
    // --- Independent Volume ADSR (VCA multiplier, NOT in modulation bus) ---
    void setVolumeAttack(float v)   { volumeAdsr_.setAttack(v); }
    float getVolumeAttack() const   { return volumeAdsr_.getAttack(); }
    void setVolumeDecay(float v)    { volumeAdsr_.setDecay(v); }
    float getVolumeDecay() const    { return volumeAdsr_.getDecay(); }
    void setVolumeSustain(float v)  { volumeAdsr_.setSustain(v); }
    float getVolumeSustain() const  { return volumeAdsr_.getSustain(); }
    void setVolumeRelease(float v)  { volumeAdsr_.setRelease(v); }
    float getVolumeRelease() const  { return volumeAdsr_.getRelease(); }

private:
    ana::PresetManager presetManager;
    ana::AnaPlugEngine engine;
    mutable std::atomic<int> currentResynthBuffer_{0};
    std::vector<float> resynthBuffer_[2];  // double buffer: one for read, one for write
    std::atomic<bool> resynthBufferReady_{false};
    std::atomic<int> resynthBufferSize_{0};
    juce::SpinLock resynthLock_;           // protects swap of index/size — audio uses try-lock (non-blocking)

    std::atomic<bool> isPlaying{false};
    std::atomic<int> playbackPosition{0};
    std::atomic<int> fftSize{2048};
    std::atomic<int> hopSize{512};
    std::atomic<float> peakThreshold{-60.0f};

    // Root note / pitch flatten
    std::atomic<int> rootNoteParam_{60};
    std::atomic<float> rootFineTuneParam_{0.0f};
    std::atomic<int> lastMidiNote_{60};  // most recent MIDI note received
    std::atomic<bool> flattenPending_{false};

    // --- MPE (MIDI Polyphonic Expression) ---
    ana::VoiceManager voiceManager;          /**< Polyphonic voice engine with MPE support. */

    // --- Unison engine ---
    ana::UnisonEngine unisonEngine_;

    // --- LUFS output metering (read-only, post master gain) ---
    ana::MeteringEngine meteringEngine_;

    // --- Oscilloscope double-buffered output capture ---
    std::array<float, 2 * kScopeBufferSize> scopeBuffer_[2];  // stereo-capable double buffer
    std::atomic<int> scopeReadIdx_{0};

    // --- Sub-harmonic generator ---
    ana::SubHarmonicGenerator subHarmonicGen_;
    std::atomic<float> subHarmonicLevel_{0.0f};

    // --- Dual signal chain (partial-domain spectral shaping) ---
    ana::DualSignalChain dualChain_;
    std::atomic<bool> chainEnabled_{false};

    // --- Effects chain (post-synth audio processing) ---
    ana::EffectsChain effectsChain_;
    std::atomic<bool> effectsEnabled_{false};
    bool effectsInitialized_ = false;

    // --- Post-effects master multi-filter ---
    ana::MultiFilter multiFilter_;

    // --- Master volume / pan (output stage) ---
    std::atomic<float> masterVol_{0.8f};   // 0..2, default 0.8
    std::atomic<float> masterPan_{0.0f};   // -1..1, default center

    // --- Limiter (brickwall, always last in chain) ---
    ana::LimiterEffect limiterEffect_;
    std::atomic<float> limiterThreshold_{-6.0f};  // dB, [-30, 0]
    std::atomic<float> limiterRelease_{20.0f};    // ms, [1, 100]
    std::atomic<float> limiterCeiling_{1.0f};     // gain, [0, 1] (0dBFS max)
    ana::CompressorEffect* compressorEffect_ = nullptr;
    ana::BitcrusherEffect* bitcrusherEffect_ = nullptr;
    int bitcrusherSlotIndex_ = -1;
    ana::FlangerEffect flangerEffect_;  // owned separately for UI access; adapter wraps into chain
    ana::SaturationEffect saturationEffect_;  // owned separately for UI access; adapter wraps into chain

    // EffectsChain-owned effect pointers (for UI access via adapters)
    ana::DelayEffect*      delayEffect_      = nullptr;
    ana::ReverbEffect*     reverbEffect_     = nullptr;
    ana::EQEffect*         eqEffect_         = nullptr;
    ana::ChorusEffect*     chorusEffect_     = nullptr;
    ana::DistortionEffect* distortionEffect_ = nullptr;
    ana::AutoTuneEffect*   autoTuneEffect_   = nullptr;
    ana::PhaserEffect*     phaserEffect_     = nullptr;

    // --- Vocal character processor (post-effects chain) ---
    ana::VocalProcessor vocalProcessor_;

    // --- Spectral effects (partial-domain, owned by processor) ---
    ana::PrismEffect prismEffect_;
    ana::BlurEffect blurEffect_;
    ana::Harmonizer harmonizer_;
    std::atomic<bool> prismEnabled_{false};
    std::atomic<bool> blurEnabled_{false};
    std::atomic<bool> harmEnabled_{false};

    // --- VoiceManager partial data bridge (for SubHarmonic/DualChain) ---
    ana::PartialDataSIMD synthPartials_;

    // Per-voice sub-oscillator phasor state (32 voices × 3 sub-harmonics)
    // Uses recursive phasor (cos, sin) instead of phase angle + std::sin.
    struct SubOscPhasor { float cos = 1.0f; float sin = 0.0f; };
    SubOscPhasor subOscState_[96];

    // Reusable voice buffer (allocated in prepareToPlay to avoid heap in audio callback)
    juce::AudioBuffer<float> voiceBuffer;

    // Reusable unison buffer (pre-allocated to avoid heap in audio callback)
    juce::AudioBuffer<float> unisonBuffer;

    // --- Spectral DNA evolver (genetic algorithm timbre engine) ---
    ana::SpectralDNAEvolver dnaEvolver_;
    std::atomic<bool> dnaEnabled_{false};
    std::atomic<bool> dnaSampleLoaded_{false};
    juce::File lastSampleFile_;

    // Atomic double-buffer — audio-thread safe read of the current best DNA
    std::array<float, 512 * 3> dnaAudioBuffer_{};  // freq, amp, phase interleaved
    std::atomic<bool> dnaBufferValid_{false};

    // --- Wavetable engine ---
    ana::WavetableEngine wavetableEngine_;
    std::atomic<float> wavetablePosition_{0.0f};
    std::atomic<bool> wavetableEnabled_{false};

    //==============================================================================
    // --- Preset morphing state ---
    // Engine partial data in SIMD format (mirrors engine.getPartialData())
    ana::PartialDataSIMD enginePartials_;
    // Cached partials for audio-thread-safe morphing
    ana::PartialDataSIMD morphCacheA_;
    ana::PartialDataSIMD morphCacheB_;

    std::atomic<bool>   morphEnabled_{false};
    std::atomic<float>  morphAmount_{0.0f};
    juce::String        morphPresetA_;   // message-thread only
    juce::String        morphPresetB_;   // message-thread only

    // --- Multiband Processor (frequency-band partial processing) ---
    ana::MultibandProcessor multibandProcessor_;

    // --- Randomizer ---
    ana::Randomizer randomizer_;

    // --- MIDI Learn ---
    ana::MidiLearn midiLearn_;

    // --- Macro Controller ---
    ana::MacroController macroController_;

    // Multiband processor access
    ana::MultibandProcessor& getMultibandProcessor() { return multibandProcessor_; }
    const ana::MultibandProcessor& getMultibandProcessor() const { return multibandProcessor_; }

    // --- LFO system (pool of 4) ---
    ana::LFOSystem& getLFO() { return lfoPool_[0]; }
    ana::LFOSystem& getLFO(int index) { return lfoPool_[index]; }

    /** 
        @param targetId  1=Cutoff, 2=Volume, 3=Pan, 4=Pitch
    */
    void updateLFOTarget(int targetId);

    // --- CLAP note expression modulation bus ---
    // Receives per-note expression events (volume, pan, tuning, brightness,
    // expression) from the CLAP host and routes them into the modulation system.
    ana::ModulationBus clapNoteExprBus_;

    // --- Modulation source pool (4 LFOs + 3 ENVs + 1 Sequencer) ---
    std::array<ana::LFOSystem, 4> lfoPool_;
    std::array<float, 4> lfoModValues_{};
    std::array<ana::MultiPointEnvelope, 3> envPool_;
    std::array<float, 3> envValues_{};
    ana::StepSequencer stepSequencer_;
    float sequencerValue_ = 0.0f;
    int envelopeTargetId_ = 1;  // default: VOLUME

    // --- Independent Volume ADSR (VCA multiplier, NOT in modulation bus) ---
    ana::MultiPointEnvelope volumeAdsr_;
    std::atomic<float> volumeAdsrValue_{1.0f};

    // --- Per-parameter modulation targets (Task 2) ---
    std::atomic<float> modTargetFilterCutoff_{1000.0f};
    std::atomic<float> modTargetFilterRes_{0.3f};
    std::atomic<float> modTargetDelayTime_{200.0f};
    std::atomic<float> modTargetReverbWet_{0.3f};
    std::atomic<float> modTargetChorusDepth_{0.0f};
    std::atomic<float> modTargetPhaserFeedback_{0.3f};
    std::atomic<float> modTargetDistDrive_{0.5f};
    std::atomic<float> modTargetSaturationDrive_{0.3f};
    std::atomic<float> modTargetBitDepth_{12.0f};
    std::atomic<float> modTargetRingFreq_{440.0f};
    std::atomic<float> modTargetWidenerWidth_{100.0f};
    std::atomic<float> modTargetTimbreABright_{0.5f};
    std::atomic<float> modTargetTimbreBBlur_{0.0f};
    std::atomic<float> modTargetMasterVol_{0.8f};
    std::atomic<float> modTargetMasterPan_{0.0f};
    std::atomic<float> modTargetSpare_{0.0f};

    // --- Flat-array modulation slots (16 params, Surge XT pattern) ---
    // Each slot holds a ModulationConnection (source + depth), a pointer to
    // its base value atomic, and the computed modulatedValue output.
    // The block-rate modulation pass in processBlock() applies all 16 slots
    // in a single flat loop — no per-sample branching, no virtual calls.
    std::array<ana::ModulationSlot, 16> modSlots_;

    // --- Per-partial LFO/envelope modulator ---
    ana::PartialModulator partialMod_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnaPlugAudioProcessor)
};
