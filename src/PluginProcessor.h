#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/AnaPlugEngine.h"
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
#include "dsp/MultibandProcessor.h"
#include "dsp/ModulationBus.h"
#include "dsp/PartialModulator.h"
#include <array>

class AnaPlugAudioProcessor : public juce::AudioProcessor
{
public:
    AnaPlugAudioProcessor();
    ~AnaPlugAudioProcessor() override;

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
    void setMPEEnabled(bool enabled) { mpeEnabled_.store(enabled); voiceManager.enableMPE(enabled); }
    bool isMPEEnabled() const { return mpeEnabled_.load(); }
    void setMPEMasterChannel(int channel) { mpeMasterChannel_.store(channel); voiceManager.setMPEMasterChannel(channel); }
    int getMPEMasterChannel() const { return mpeMasterChannel_.load(); }
    ana::VoiceManager& getVoiceManager() { return voiceManager; }

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

    // Wavetable engine access
    ana::WavetableEngine& getWavetableEngine() { return wavetableEngine_; }
    std::atomic<float>& getWavetablePositionRef() { return wavetablePosition_; }
    float getWavetablePosition() const { return wavetablePosition_.load(); }
    void setWavetablePosition(float pos) { wavetablePosition_.store(pos); wavetableEngine_.setPosition(pos); }
    bool isWavetableEnabled() const { return wavetableEnabled_.load(); }
    void setWavetableEnabled(bool enabled) { wavetableEnabled_.store(enabled); }
    std::atomic<bool>& getWavetableEnabledRef() { return wavetableEnabled_; }

private:
    ana::PresetManager presetManager;
    ana::AnaPlugEngine engine;
    mutable std::atomic<int> currentResynthBuffer_{0};
    std::vector<float> resynthBuffer_[2];  // double buffer: one for read, one for write
    std::atomic<bool> resynthBufferReady_{false};
    std::atomic<int> resynthBufferSize_{0};

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
    std::atomic<bool>  mpeEnabled_{false};   /**< Whether MPE mode is active. */
    std::atomic<int>   mpeMasterChannel_{0}; /**< MPE master MIDI channel (0-15). */

    // --- Sub-harmonic generator ---
    ana::SubHarmonicGenerator subHarmonicGen_;
    std::atomic<float> subHarmonicLevel_{0.0f};

    // --- Dual signal chain (partial-domain spectral shaping) ---
    ana::DualSignalChain dualChain_;
    std::atomic<bool> chainEnabled_{false};

    // --- Effects chain (post-synth audio processing) ---
    ana::EffectsChain effectsChain_;
    std::atomic<bool> effectsEnabled_{false};

    // --- VoiceManager partial data bridge (for SubHarmonic/DualChain) ---
    ana::PartialDataSIMD synthPartials_;

    // Per-voice sub-oscillator phase tracking (32 voices × 3 sub-harmonics)
    float subOscPhase_[96] = {};

    // Reusable voice buffer (allocated in prepareToPlay to avoid heap in audio callback)
    juce::AudioBuffer<float> voiceBuffer;

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

    // --- MIDI Learn ---
    ana::MidiLearn midiLearn_;

    // --- Macro Controller ---
    ana::MacroController macroController_;

    // Multiband processor access
    ana::MultibandProcessor& getMultibandProcessor() { return multibandProcessor_; }
    const ana::MultibandProcessor& getMultibandProcessor() const { return multibandProcessor_; }

    // --- Modulation bus (audio-rate modulation routing) ---
    ana::ModulationBus modBus_;

    // --- Per-partial LFO/envelope modulator ---
    ana::PartialModulator partialMod_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnaPlugAudioProcessor)
};
