#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "PartialDataSIMD.h"

namespace ana
{

//==============================================================================
/** Immutable harmonic set consumed by AdditiveVoice (SoA, bounded arrays). */
struct AdditiveSnapshot
{
    static constexpr int kMaxPartials = 512;

    float frequency[kMaxPartials] = {};
    float amplitude[kMaxPartials] = {};
    float phase[kMaxPartials]     = {};
    int   count  = 0;
    float rootHz = 261.625565f;   // C4
};

//==============================================================================
/** One additive voice: sums up to kMaxPartials recursive phasors, transposed by
    note/root and shaped by a classic ADSR. Lock-free w.r.t. the message thread:
    it only reads the snapshot pointer handed to it each sub-block. */
class AdditiveVoice : public juce::MPESynthesiserVoice
{
public:
    AdditiveVoice() = default;

    bool isActive() const override;

    void noteStarted() override;
    void noteStopped(bool allowTailOff) override;
    void notePressureChanged() override;
    void notePitchbendChanged() override;
    void noteTimbreChanged() override;
    void noteKeyStateChanged() override;

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                         int startSample, int numSamples) override;

    /** ADSR in seconds / [0,1]. */
    float attackSeconds  = 0.01f;
    float decaySeconds   = 0.20f;
    float sustainLevel   = 0.70f;
    float releaseSeconds = 0.30f;

    /** Set by AdditiveSynth::renderNextSubBlock on the audio thread. */
    const AdditiveSnapshot* snapshot = nullptr;

private:
    enum class State : std::uint8_t { free = 0, attack, decay, sustain, release, idle };

    std::atomic<State> state_{ State::free };
    float envelopeLevel = 0.0f;
    float releaseStart  = 0.0f;
    float baseFreq      = 0.0f;   // Hz at note-on (includes initial bend)
    float bendRatio     = 1.0f;   // applied on top of baseFreq
    float velocity      = 0.0f;

    float phasorRe[AdditiveSnapshot::kMaxPartials] = {};
    float phasorIm[AdditiveSnapshot::kMaxPartials] = {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdditiveVoice)
};

//==============================================================================
/** Real-time additive synthesiser driven by an analysed harmonic set.
    Message thread publishes; audio thread stabilises once per sub-block. */
class AdditiveSynth : public juce::MPESynthesiser
{
public:
    static constexpr int kMaxPartials = AdditiveSnapshot::kMaxPartials;

    AdditiveSynth();

    /** Message thread: sets the playback rate (instrument-level only). */
    void prepare(double sampleRate);

    /** Message thread: sanitise (finite, in-range) + top-N by amplitude + publish. */
    void setPartials(const PartialDataSIMD& partials);
    void clearPartials();

    void setMaxPartials(int n);      // 1..kMaxPartials, default 128
    void setMaxVoices(int n);        // message thread only, 1..32
    void setRootNote(int midiNote);  // default 60
    void setRootFineTune(float cents);
    void setMPEEnabled(bool enabled); // mirrors VoiceManager semantics

    int  getActivePartialCount() const { return publishedCount_.load(std::memory_order_relaxed); }
    bool hasPartials() const           { return getActivePartialCount() > 0; }
    int  getNumActiveVoices() const;

protected:
    void renderNextSubBlock(juce::AudioBuffer<float>& outputAudio,
                            int startSample, int numSamples) override;

private:
    AdditiveSnapshot published_;   // written by message thread under partialLock_
    AdditiveSnapshot active_;      // audio-thread stable copy (never shared)
    juce::SpinLock   partialLock_;

    std::atomic<int>   publishedCount_{ 0 };
    std::atomic<int>   maxPartials_{ 128 };
    std::atomic<int>   rootNote_{ 60 };
    std::atomic<float> rootFineTune_{ 0.0f };

    static float rootHzFrom(int midiNote, float cents);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdditiveSynth)
};

} // namespace ana
