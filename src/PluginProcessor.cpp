#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include "dsp/effects/DelayEffect.h"
#include "dsp/effects/ReverbEffect.h"
#include "dsp/effects/EQEffect.h"
#include "dsp/effects/ChorusEffect.h"
#include "dsp/effects/DistortionEffect.h"
#include "dsp/effects/AutoTuneEffect.h"
#include "dsp/WavLoader.h"
#include "dsp/STFTAnalyzer.h"
#include "dsp/PeakDetector.h"

// SSE intrinsics for FTZ/DAZ denormal handling
#if JUCE_INTEL
#include <xmmintrin.h>
#endif

//==============================================================================
// Effect adapters — wrap concrete effect classes into the EffectBase interface
//==============================================================================
namespace {

class DelayEffectAdapter : public ana::EffectBase {
    ana::DelayEffect effect;
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
};

class ReverbEffectAdapter : public ana::EffectBase {
    ana::ReverbEffect effect;
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
};

class EQEffectAdapter : public ana::EffectBase {
    ana::EQEffect effect;
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
};

class ChorusEffectAdapter : public ana::EffectBase {
    ana::ChorusEffect effect;
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
};

class DistortionEffectAdapter : public ana::EffectBase {
    ana::DistortionEffect effect;
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { effect.process(buffer); }
    void reset() override                                      { effect.reset(); }
};

class AutoTuneEffectAdapter : public ana::EffectBase {
    ana::AutoTuneEffect effect;
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override {
        effect.setSampleRate(spec.sampleRate);
    }
    void process(juce::AudioBuffer<float>& buffer) override { effect.processBlock(buffer); }
    void reset() override                                    { effect.reset(); }
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
#endif

    presetManager.setStateReferences(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &voiceManager, nullptr);

    // Initialise the engine partials ref so morphPresets can read
    // partial data from the engine after loading each preset.
    enginePartials_ = ana::PartialDataSIMD::fromPartialData(engine.getPartialData());
    presetManager.setEnginePartialsRef(&enginePartials_);
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
#endif

    // Prepare the polyphonic voice engine
    if (sampleRate > 0.0)
    {
        voiceManager.prepare(sampleRate);
        subHarmonicGen_.setSampleRate(sampleRate);

        // Pre-allocate voiceBuffer to avoid heap allocations in the audio callback
        voiceBuffer.setSize(juce::jmax(getTotalNumOutputChannels(), 1), samplesPerBlock, false, false, true);

        // Prepare the effects chain with default effects
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = static_cast<juce::uint32>(juce::jmax(getTotalNumOutputChannels(), 1));

        effectsChain_.clear();
        // Effects order: Delay → Reverb → EQ → Chorus → Distortion → AutoTune
        effectsChain_.addEffect(std::make_unique<DelayEffectAdapter>(), "Delay");
        effectsChain_.addEffect(std::make_unique<ReverbEffectAdapter>(), "Reverb");
        effectsChain_.addEffect(std::make_unique<EQEffectAdapter>(), "EQ");
        effectsChain_.addEffect(std::make_unique<ChorusEffectAdapter>(), "Chorus");
        effectsChain_.addEffect(std::make_unique<DistortionEffectAdapter>(), "Distortion");
        effectsChain_.addEffect(std::make_unique<AutoTuneEffectAdapter>(), "AutoTune");
        effectsChain_.prepare(spec);
    }

    // Initialise Spectral DNA evolver population if not already done
    if (!dnaEvolver_.getPopulationSize())
        dnaEvolver_.init(16);
}

void AnaPlugAudioProcessor::releaseResources()
{
}

void AnaPlugAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Flush-to-Zero and Denormals-Are-Zero for SSE
    // Reset here because some hosts reset the FP control word between prepareToPlay and processBlock
#if JUCE_INTEL
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const double sr       = getSampleRate();

    // Handle flatten trigger on message thread
    if (flattenPending_.exchange(false))
    {
        // Flatten is triggered from the UI, we just acknowledge it here
        // The actual flatten processing happens on the message thread
    }

    // --- MPE / MIDI routing ---
    const bool mpe = mpeEnabled_.load();
    const int masterCh = mpeMasterChannel_.load();

    for (const auto& msg : midiMessages)
    {
        const auto& m = msg.getMessage();
        const int channel = m.getChannel();

        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            const float vel = m.getFloatVelocity();

            if (mpe)
            {
                if (channel == masterCh)
                {
                    // Master channel note → legacy behaviour (e.g. bass channel on some controllers)
                    voiceManager.noteOnWithChannel(channel, note, vel, sr);
                }
                else
                {
                    // Per-note channel → MPE note
                    voiceManager.noteOnWithChannel(channel, note, vel, sr);
                }
                // Track globally for UI
                lastMidiNote_.store(note);
            }
            else
            {
                // Non-MPE: standard note-on
                voiceManager.noteOn(note, vel);
                lastMidiNote_.store(note);
            }
        }
        else if (m.isNoteOff())
        {
            const int note = m.getNoteNumber();

            if (mpe)
            {
                // In MPE mode, use per-channel note-off
                voiceManager.noteOffWithChannel(channel, note);
            }
            else
            {
                voiceManager.noteOff(note);
            }
        }
        else if (m.isPitchWheel())
        {
            // Pitch bend: [-1, 1] normalized
            const float bend = static_cast<float>(m.getPitchWheelValue() - 8192) / 8192.0f;

            if (mpe && channel != masterCh)
            {
                // Per-note channel pitch bend → find voices on this channel
                for (int v = 0; v < ana::VoiceManager::maxVoices; ++v)
                {
                    if (voiceManager.getVoice(v).midiChannel == channel)
                        voiceManager.setVoicePitchBend(v, bend);
                }
            }
            else
            {
                // Master channel or non-MPE: apply to all active voices
                for (int v = 0; v < ana::VoiceManager::maxVoices; ++v)
                {
                    if (voiceManager.getVoice(v).state.load(std::memory_order_relaxed) != ana::VoiceState::free)
                        voiceManager.setVoicePitchBend(v, bend);
                }
            }
        }
        else if (m.isAftertouch())
        {
            // Channel Pressure (monophonic aftertouch)
            const float pressure = m.getChannelPressureValue() / 127.0f;

            if (mpe && channel != masterCh)
            {
                // Per-note channel pressure → MPE pressure on this channel's voices
                for (int v = 0; v < ana::VoiceManager::maxVoices; ++v)
                {
                    if (voiceManager.getVoice(v).midiChannel == channel)
                        voiceManager.setPressure(v, pressure);
                }
            }
            else if (!mpe)
            {
                // Non-MPE: standard aftertouch on all active voices
                for (int v = 0; v < ana::VoiceManager::maxVoices; ++v)
                {
                    voiceManager.setVoiceAftertouch(v, pressure);
                }
            }
        }
        else if (m.isController())
        {
            const int ccNum = m.getControllerNumber();
            const float value = m.getControllerValue() / 127.0f;

            if (ccNum == 74)
            {
                // CC74 = MPE Timbre (also used as Slide on some controllers)
                if (mpe && channel != masterCh)
                {
                    // Per-note channel CC74 → MPE timbre
                    for (int v = 0; v < ana::VoiceManager::maxVoices; ++v)
                    {
                        if (voiceManager.getVoice(v).midiChannel == channel)
                            voiceManager.setTimbre(v, value);
                    }
                }
                else if (mpe && channel == masterCh)
                {
                    // Master channel CC74 → global slide (apply to all MPE voices)
                    for (int v = 0; v < ana::VoiceManager::maxVoices; ++v)
                    {
                        voiceManager.setSlide(v, value);
                    }
                }
            }

            // Route ALL controller messages through the MIDI Learn system
            // so that learned CC mappings are applied regardless of channel
            midiLearn_.processMidi(m);
        }
    }

    // --- Process VoiceManager audio ---
    // Use a temporary buffer for VoiceManager output, then mix into the main buffer
    voiceBuffer.setSize(numChannels, numSamples, false, false, true);
    voiceManager.process(voiceBuffer);

    // --- Sub-harmonic generator ---
    // Generates sub-harmonic sine waves for each active voice and mixes them
    // into voiceBuffer. Works in the frequency domain using per-voice pitch.
    {
        const float subLevel = subHarmonicLevel_.load();
        if (subLevel > 0.0f && sr > 0.0)
        {
            const float dT = 1.0f / static_cast<float>(sr);
            constexpr float twoPi = 6.283185307179586f;

            for (int v = 0; v < ana::VoiceManager::maxVoices; ++v)
            {
                if (!voiceManager.isVoiceActive(v))
                    continue;

                const auto& voice = voiceManager.getVoice(v);
                const float fundFreq = voice.pitchHz.load()
                                     * voice.pitchBend.load();
                if (fundFreq <= 0.0f)
                    continue;

                float subFreqs[3] = {}, subAmps[3] = {};
                const int nSubs = subHarmonicGen_.generate(fundFreq, subFreqs, subAmps, 3);

                const float envLevel = voice.envelopeLevel.load();
                const float voiceAmp = voice.amplitude.load();

                for (int s = 0; s < nSubs; ++s)
                {
                    if (subAmps[s] <= 1e-6f)
                        continue;

                    const int phaseIdx = v * 3 + s;
                    const float totalAmp = subAmps[s] * subLevel * envLevel * voiceAmp;
                    float phase = subOscPhase_[phaseIdx];

                    for (int i = 0; i < numSamples; ++i)
                    {
                        const float sample = std::sin(phase) * totalAmp;
                        for (int ch = 0; ch < numChannels; ++ch)
                            voiceBuffer.addSample(ch, i, sample);

                        phase += twoPi * subFreqs[s] * dT;
                        if (phase >= twoPi)
                            phase -= twoPi;
                        if (phase < 0.0f)
                            phase += twoPi;
                    }

                    subOscPhase_[phaseIdx] = phase;
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

    // --- Effects chain ---
    if (effectsEnabled_.load())
        effectsChain_.process(voiceBuffer);

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

            return;
        }
    }

    // No resynthesis: just copy VoiceManager output to main buffer
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.copyFrom(ch, 0, voiceBuffer, ch, 0, numSamples);
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
    state.setProperty("mpeEnabled", mpeEnabled_.load(), nullptr);
    state.setProperty("mpeMasterChannel", mpeMasterChannel_.load(), nullptr);
    state.setProperty("subHarmonicLevel", subHarmonicLevel_.load(), nullptr);

    auto presetState = presetManager.serialiseState();
    state.addChild(presetState, -1, nullptr);

    // MIDI Learn mappings
    auto midiLearnState = midiLearn_.saveState();
    if (midiLearnState.isValid())
        state.addChild(midiLearnState, -1, nullptr);

    juce::MemoryOutputStream stream(destData, true);
    state.writeToStream(stream);
}

void AnaPlugAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));
    if (!state.isValid() || !state.hasType("AnaPlugState"))
        return;

    if (state.hasProperty("fftSize")) fftSize.store(state.getProperty("fftSize"));
    if (state.hasProperty("hopSize")) hopSize.store(state.getProperty("hopSize"));
    if (state.hasProperty("peakThreshold")) peakThreshold.store(state.getProperty("peakThreshold"));
    
    if (state.hasProperty("rootNote")) setRootNote(state.getProperty("rootNote"));
    if (state.hasProperty("rootFineTune")) setRootFineTune(state.getProperty("rootFineTune"));
    if (state.hasProperty("subHarmonicLevel")) setSubHarmonicLevel(state.getProperty("subHarmonicLevel"));
    
    if (state.hasProperty("mpeEnabled")) setMPEEnabled(state.getProperty("mpeEnabled"));
    if (state.hasProperty("mpeMasterChannel")) setMPEMasterChannel(state.getProperty("mpeMasterChannel"));

    auto presetState = state.getChildWithName("Parameters");
    if (presetState.isValid())
        presetManager.deserialiseState(presetState);

    // MIDI Learn mappings
    auto midiLearnState = state.getChildWithName("MidiLearn");
    if (midiLearnState.isValid())
        midiLearn_.loadState(midiLearnState);
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
        const int writeIdx = 1 - currentResynthBuffer_.load();
        resynthBuffer_[writeIdx] = std::move(result);
        resynthBufferSize_.store(static_cast<int>(resynthBuffer_[writeIdx].size()));
        currentResynthBuffer_.store(writeIdx);
        resynthBufferReady_.store(true);
        playbackPosition.store(0);
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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnaPlugAudioProcessor();
}
