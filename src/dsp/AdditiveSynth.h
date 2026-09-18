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
/** One frame of the harmonic image: up to kMaxPartials partials (SoA, bounded). */
struct AdditiveFrame
{
    static constexpr int kMaxPartials = 128;

    float frequency[kMaxPartials] = {};
    float amplitude[kMaxPartials] = {};
    float phase[kMaxPartials]     = {};
    int   count = 0;
};

//==============================================================================
/** A bounded image of harmonic frames plus the root pitch they are relative to.
    Frame index advances over time (AdditiveSynth::setImageRate) so the timbre
    evolves during a held note, like a Harmor image. */
struct AdditiveBank
{
    static constexpr int kMaxFrames = 32;

    AdditiveFrame frames[kMaxFrames];
    int   frameCount = 0;
    float rootHz     = 261.625565f;   // C4
};

//==============================================================================
/** One additive voice: sums up to kMaxPartials recursive phasors, transposed by
    note/root and shaped by a classic ADSR. Lock-free w.r.t. the message thread:
    it only reads the bank pointer handed to it each sub-block. */
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
    const AdditiveBank* bank = nullptr;
    float framesPerBlock = 0.0f;   // image advance per render call (0 = static)
    bool  imageLoop      = true;
    int   imageCurve     = 0;      // 0 = linear, 1 = smooth, 2 = step (see shapeFrameMix)

private:
    enum class State : std::uint8_t { free = 0, attack, decay, sustain, release, idle };

    std::atomic<State> state_{ State::free };
    float envelopeLevel = 0.0f;
    float releaseStart  = 0.0f;
    float baseFreq      = 0.0f;   // Hz at note-on (includes initial bend)
    float bendRatio     = 1.0f;   // applied on top of baseFreq
    float velocity      = 0.0f;
    float framePos      = 0.0f;   // image playback position, in frames

    float phasorRe[AdditiveFrame::kMaxPartials] = {};
    float phasorIm[AdditiveFrame::kMaxPartials] = {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdditiveVoice)
};

//==============================================================================
/** Real-time additive synthesiser driven by an analysed harmonic set or by a
    time-varying harmonic image. Message thread publishes; the audio thread
    stabilises the published bank once per sub-block. */
class AdditiveSynth : public juce::MPESynthesiser
{
public:
    static constexpr int kMaxPartials = AdditiveFrame::kMaxPartials;
    static constexpr int kMaxFrames   = AdditiveBank::kMaxFrames;

    AdditiveSynth();

    /** Message thread: sets the playback rate (instrument-level only). */
    void prepare(double sampleRate);

    /** Message thread: sanitise (finite, in-range) + top-N by amplitude +
        publish as a single-frame bank. */
    void setPartials(const PartialDataSIMD& partials);

    /** Message thread: publish an image of frames. Each frame is sanitised and
        truncated to N partials, ordered by ascending frequency so that partial
        index i means the same thing across frames (smooth interpolation). */
    void setFrames(const std::vector<PartialDataSIMD>& frames);

    void clearPartials();

    void setMaxPartials(int n);      // 1..kMaxPartials, default 128
    void setMaxVoices(int n);        // message thread only, 1..32
    void setRootNote(int midiNote);  // default 60
    void setRootFineTune(float cents);
    void setMPEEnabled(bool enabled); // mirrors VoiceManager semantics

    //--- Image playback (time-varying harmonic set) ---
    void  setImageEnabled(bool enabled) noexcept { imageEnabled_.store(enabled, std::memory_order_relaxed); }
    bool  isImageEnabled() const noexcept        { return imageEnabled_.load(std::memory_order_relaxed); }
    void  setImageRate(float framesPerSecond) noexcept
    {
        imageRate_.store(std::isfinite(framesPerSecond) ? std::max(0.0f, framesPerSecond) : 0.0f,
                         std::memory_order_relaxed);
    }
    float getImageRate() const noexcept   { return imageRate_.load(std::memory_order_relaxed); }
    void  setImageLoop(bool shouldLoop) noexcept { imageLoop_.store(shouldLoop, std::memory_order_relaxed); }
    bool  isImageLoop() const noexcept    { return imageLoop_.load(std::memory_order_relaxed); }

    /** Blend shape between two image frames: 0 = linear, 1 = smooth (ease in
        and out), 2 = step (hold frame A, then jump - the image "sequences"
        instead of morphing). See shapeFrameMix(). */
    void  setImageCurve(int curve) noexcept { imageCurve_.store(juce::jlimit(0, 2, curve), std::memory_order_relaxed); }
    int   getImageCurve() const noexcept    { return imageCurve_.load(std::memory_order_relaxed); }

    /** Shapes a linear frame blend: the same warp AdditiveVoice applies before
        cross-fading two frames.  Exposed for tests and callers that want the
        identical curve outside the render loop. */
    static float shapeFrameMix(float linearMix, int curve) noexcept
    {
        const float m = juce::jlimit(0.0f, 1.0f, linearMix);
        if (curve == 1)
            return m * m * (3.0f - 2.0f * m);          // smoothstep
        if (curve == 2)
            return (m < 0.5f) ? 0.0f : 1.0f;           // sample and hold
        return m;
    }

    int  getActivePartialCount() const { return publishedCount_.load(std::memory_order_relaxed); }
    int  getActiveFrameCount() const   { return publishedFrames_.load(std::memory_order_relaxed); }
    bool hasPartials() const           { return getActivePartialCount() > 0; }
    int  getNumActiveVoices() const;

protected:
    void renderNextSubBlock(juce::AudioBuffer<float>& outputAudio,
                            int startSample, int numSamples) override;

private:
    void publishBank(const AdditiveBank& next);

    AdditiveBank published_;   // written by message thread under partialLock_
    AdditiveBank active_;      // audio-thread stable copy (never shared)
    juce::SpinLock partialLock_;
    std::atomic<int> publishedGeneration_{ 0 };
    int              activeGeneration_    { -1 };

    std::atomic<int>   publishedCount_{ 0 };
    std::atomic<int>   publishedFrames_{ 0 };
    std::atomic<int>   maxPartials_{ 128 };
    std::atomic<int>   rootNote_{ 60 };
    std::atomic<float> rootFineTune_{ 0.0f };

    std::atomic<bool>  imageEnabled_{ false };
    std::atomic<float> imageRate_{ 2.0f };
    std::atomic<bool>  imageLoop_{ true };
    std::atomic<int>   imageCurve_{ 0 };

    static float rootHzFrom(int midiNote, float cents);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdditiveSynth)
};

} // namespace ana
