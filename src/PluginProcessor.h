#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/AnaPlugEngine.h"
#include "dsp/VoiceManager.h"
#include "dsp/SubHarmonicGenerator.h"
#include "dsp/PresetManager.h"

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

    // Preset Manager
    ana::PresetManager& getPresetManager() { return presetManager; }

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

    // Reusable voice buffer (allocated in prepareToPlay to avoid heap in audio callback)
    juce::AudioBuffer<float> voiceBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnaPlugAudioProcessor)
};
