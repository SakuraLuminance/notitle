# P6 Round 1 — Additive Synth Engine + SYNTH Mode + Editor Wiring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a loaded sample's harmonic content playable and editable in real time: a new additive synth voices the analysed partials, a SYNTH mode toggle switches the plugin from sample playback to additive playback, and spectrum edits become audible.

**Architecture:** New `ana::AdditiveSynth` (`juce::MPESynthesiser` subclass) with `AdditiveVoice` (per-partial recursive phasors, ADSR) and a SpinLock-guarded harmonic-set snapshot published by the message thread and stabilised on the audio thread once per sub-block. `PluginProcessor` holds the engine-derived source frame and the edited set, renders `AdditiveSynth` into `voiceBuffer` when SYNTH mode is on (skipping resynth-buffer playback), and forwards root note. `PluginEditor` stops clobbering the editor canvas each timer tick and wires `SpectrumEditorCanvas::onPartialEdited` to the processor.

**Tech Stack:** C++20, JUCE 8.0.17 (MPESynthesiser, SpinLock), Catch2 tests, CMake, MSVC /MT. **No local toolchain** — verification only via GitHub Actions (~20 min/round).

## Global Constraints

- Any file compiled into `AnaPlugTests` MUST NOT include `PluginProcessor.h` (clap trap). This includes `AdditiveSynth.h/.cpp` and `test_additive_synth.cpp`.
- JUCE 8 MPE naming: per-voice rate is `setCurrentSampleRate` / `getSampleRate`; instrument-level is `setCurrentPlaybackSampleRate` (calls `turnOffAllVoices`). Only call the instrument-level setter from `prepare()`.
- `AudioBuffer(nCh, nSamples)` does NOT clear memory — never assume a fresh buffer is silent.
- All DSP numerics must be guarded: `std::isfinite`, frequency clamp to `[0, Nyquist)`, amplitude clamp `[0,1]`.
- Preserve the existing golden path: 557 test cases must stay green and pluginval strictness 3 must keep passing.
- Commit after each task; push via CI only when a coherent batch is ready.
- Config is read at startup only; no opencode restart needed for this work.

---

### Task 1: `AdditiveSynth` engine

**Files:**
- Create: `src/dsp/AdditiveSynth.h`
- Create: `src/dsp/AdditiveSynth.cpp`
- Modify: `CMakeLists.txt` (add `src/dsp/AdditiveSynth.cpp` to `target_sources(AnaPlug ...)`)
- Modify: `tests/CMakeLists.txt` (add `../src/dsp/AdditiveSynth.cpp` to the test target sources)

**Interfaces (produced, consumed by Tasks 2–4):**
- `struct ana::AdditiveSnapshot { static constexpr int kMaxPartials = 512; float frequency[kMaxPartials]; float amplitude[kMaxPartials]; float phase[kMaxPartials]; int count; float rootHz; };`
- `class ana::AdditiveVoice : public juce::MPESynthesiserVoice` — public `const AdditiveSnapshot* snapshot`, ADSR floats.
- `class ana::AdditiveSynth : public juce::MPESynthesiser`:
  - `void prepare(double sampleRate);`
  - `void setPartials(const PartialDataSIMD&); void clearPartials();`
  - `void setMaxPartials(int); void setMaxVoices(int);`
  - `void setRootNote(int); void setRootFineTune(float);`
  - `void setMPEEnabled(bool);`
  - `int getActivePartialCount() const; bool hasPartials() const; int getNumActiveVoices() const;`

- [ ] **Step 1: Write `src/dsp/AdditiveSynth.h`**

```cpp
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
```

- [ ] **Step 2: Write `src/dsp/AdditiveSynth.cpp`**

```cpp
#include "AdditiveSynth.h"

namespace ana
{

//==============================================================================
// AdditiveVoice
//==============================================================================

bool AdditiveVoice::isActive() const
{
    return state_.load(std::memory_order_relaxed) != State::free;
}

void AdditiveVoice::noteStarted()
{
    const auto& mpe = getCurrentlyPlayingNote();

    baseFreq  = juce::jmax(1.0f, mpe.getFrequencyInHertz());
    bendRatio = 1.0f;
    velocity  = juce::jlimit(0.0f, 1.0f, mpe.noteOnVelocity.asUnsignedFloat());

    envelopeLevel = 0.0f;
    releaseStart  = 0.0f;

    const int n = snapshot != nullptr ? snapshot->count : 0;
    for (int k = 0; k < AdditiveSnapshot::kMaxPartials; ++k)
    {
        const float ph = (k < n) ? snapshot->phase[k] : 0.0f;
        phasorRe[k] = std::cos(ph);
        phasorIm[k] = std::sin(ph);
    }

    state_.store(State::attack, std::memory_order_release);
}

void AdditiveVoice::noteStopped(bool allowTailOff)
{
    if (allowTailOff)
    {
        State expected = state_.load(std::memory_order_relaxed);
        while (expected >= State::attack && expected <= State::sustain)
        {
            if (state_.compare_exchange_weak(expected, State::release,
                                             std::memory_order_release))
            {
                releaseStart = envelopeLevel;
                break;
            }
        }
    }
    else
    {
        clearCurrentNote();
        state_.store(State::free, std::memory_order_release);
    }
}

void AdditiveVoice::notePressureChanged()    { /* MVP: pressure not yet routed */ }
void AdditiveVoice::noteTimbreChanged()      { /* MVP: timbre not yet routed */ }
void AdditiveVoice::noteKeyStateChanged()    { /* handled by MPESynthesiser */ }

void AdditiveVoice::notePitchbendChanged()
{
    const float f = getCurrentlyPlayingNote().getFrequencyInHertz();
    if (baseFreq > 0.0f)
        bendRatio = f / baseFreq;
}

void AdditiveVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                    int startSample, int numSamples)
{
    State currentState = state_.load(std::memory_order_relaxed);
    if (currentState == State::free || snapshot == nullptr || snapshot->count <= 0)
        return;

    const double srD = getSampleRate();
    if (srD <= 0.0)
        return;

    const float sr = static_cast<float>(srD);
    const float dt = 1.0f / sr;

    const int n = juce::jmin(snapshot->count, AdditiveSnapshot::kMaxPartials);
    const float ratio = bendRatio * (baseFreq / juce::jmax(1.0f, snapshot->rootHz));

    // Per-partial rotation coefficients (block rate).
    float cosD[AdditiveSnapshot::kMaxPartials];
    float sinD[AdditiveSnapshot::kMaxPartials];
    for (int k = 0; k < n; ++k)
    {
        const float f = juce::jlimit(0.0f, sr * 0.49f, snapshot->frequency[k] * ratio);
        const float d = juce::MathConstants<float>::twoPi * f * dt;
        cosD[k] = std::cos(d);
        sinD[k] = std::sin(d);
    }

    const float attackDt   = attackSeconds  > 0.0f ? dt / attackSeconds  : 1.0f;
    const float decayRange = 1.0f - sustainLevel;
    const float decayDt    = decaySeconds   > 0.0f ? dt * decayRange / decaySeconds : 1.0f;
    const float releaseDt  = releaseSeconds > 0.0f ? dt / releaseSeconds : 1.0f;

    constexpr float kOutputGain = 0.25f;

    const int numChannels = outputBuffer.getNumChannels();
    auto* const* chData = outputBuffer.getArrayOfWritePointers();

    for (int s = 0; s < numSamples; ++s)
    {
        float acc = 0.0f;
        for (int k = 0; k < n; ++k)
        {
            acc += phasorIm[k] * snapshot->amplitude[k];
            const float re = phasorRe[k] * cosD[k] - phasorIm[k] * sinD[k];
            const float im = phasorRe[k] * sinD[k] + phasorIm[k] * cosD[k];
            phasorRe[k] = re;
            phasorIm[k] = im;
        }

        switch (currentState)
        {
            case State::attack:
            {
                const float env = envelopeLevel + attackDt;
                envelopeLevel = env;
                if (env >= 0.999f)
                {
                    envelopeLevel = 1.0f;
                    currentState = State::decay;
                    state_.store(State::decay, std::memory_order_relaxed);
                }
                break;
            }
            case State::decay:
            {
                const float env = envelopeLevel - decayDt;
                envelopeLevel = env;
                if (env <= sustainLevel)
                {
                    envelopeLevel = sustainLevel;
                    currentState = State::sustain;
                    state_.store(State::sustain, std::memory_order_relaxed);
                }
                break;
            }
            case State::sustain:
                envelopeLevel = sustainLevel;
                break;
            case State::release:
            {
                const float rsl = releaseStart;
                if (rsl <= 0.0f)
                {
                    envelopeLevel = 0.0f;
                    currentState = State::idle;
                }
                else
                {
                    const float env = envelopeLevel - rsl * releaseDt;
                    envelopeLevel = env;
                    if (env <= 0.0f)
                    {
                        envelopeLevel = 0.0f;
                        currentState = State::idle;
                    }
                }
                break;
            }
            default:
                break;
        }

        const float sample = acc * envelopeLevel * velocity * kOutputGain;
        const int wi = startSample + s;
        for (int ch = 0; ch < numChannels; ++ch)
            chData[ch][wi] += sample;
    }

    if (currentState == State::idle)
    {
        state_.store(State::idle, std::memory_order_relaxed);
        clearCurrentNote();
    }
}

//==============================================================================
// AdditiveSynth
//==============================================================================

namespace
{
constexpr int kInitialVoices = 16;
}

AdditiveSynth::AdditiveSynth()
{
    for (int i = 0; i < kInitialVoices; ++i)
        addVoice(new AdditiveVoice());

    setVoiceStealingEnabled(true);
    enableLegacyMode(12, { 1, 17 });   // plain MIDI by default (MPE via setMPEEnabled)
}

float AdditiveSynth::rootHzFrom(int midiNote, float cents)
{
    return 440.0f * std::pow(2.0f,
        (static_cast<float>(midiNote) - 69.0f + cents / 100.0f) / 12.0f);
}

void AdditiveSynth::prepare(double sampleRate)
{
    setCurrentPlaybackSampleRate(sampleRate > 0.0 ? sampleRate : 44100.0);
}

void AdditiveSynth::setPartials(const PartialDataSIMD& src)
{
    const int maxP = juce::jlimit(1, kMaxPartials, maxPartials_.load(std::memory_order_relaxed));
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const float nyq = static_cast<float>(sr * 0.5);

    struct Cand { float f, a, ph; };
    std::vector<Cand> cands;
    cands.reserve(static_cast<std::size_t>(maxP));

    for (int i = 0; i < kMaxPartials; ++i)
    {
        const float a = src.amplitude[i];
        const float f = src.frequency[i];
        if (! std::isfinite(a) || ! std::isfinite(f)) continue;
        if (a <= 1.0e-6f) continue;
        if (f <= 0.0f || f >= nyq) continue;
        cands.push_back({ f,
                          juce::jlimit(0.0f, 1.0f, a),
                          std::isfinite(src.phase[i]) ? src.phase[i] : 0.0f });
    }

    std::sort(cands.begin(), cands.end(),
              [](const Cand& x, const Cand& y) { return x.a > y.a; });

    const int n = static_cast<int>(juce::jmin(static_cast<std::size_t>(maxP), cands.size()));

    AdditiveSnapshot next;
    next.count = n;
    for (int i = 0; i < n; ++i)
    {
        next.frequency[i] = cands[static_cast<std::size_t>(i)].f;
        next.amplitude[i] = cands[static_cast<std::size_t>(i)].a;
        next.phase[i]     = cands[static_cast<std::size_t>(i)].ph;
    }

    {
        const juce::SpinLock::ScopedLockType sl(partialLock_);
        published_ = next;
    }
    publishedCount_.store(n, std::memory_order_relaxed);
}

void AdditiveSynth::clearPartials()
{
    setPartials(PartialDataSIMD{});
}

void AdditiveSynth::setMaxPartials(int n)
{
    maxPartials_.store(juce::jlimit(1, kMaxPartials, n), std::memory_order_relaxed);
}

void AdditiveSynth::setMaxVoices(int n)
{
    n = juce::jlimit(1, 32, n);
    while (getNumVoices() < n)
        addVoice(new AdditiveVoice());
    if (getNumVoices() > n)
        reduceNumVoices(n);
}

void AdditiveSynth::setRootNote(int midiNote)
{
    rootNote_.store(juce::jlimit(0, 127, midiNote), std::memory_order_relaxed);
}

void AdditiveSynth::setRootFineTune(float cents)
{
    rootFineTune_.store(juce::jlimit(-50.0f, 50.0f, cents), std::memory_order_relaxed);
}

void AdditiveSynth::setMPEEnabled(bool enabled)
{
    if (enabled)
        setZoneLayout(juce::MPEZoneLayout());
    else
        enableLegacyMode(12, { 1, 17 });
}

int AdditiveSynth::getNumActiveVoices() const
{
    int count = 0;
    for (int i = 0; i < getNumVoices(); ++i)
        if (getVoice(i) != nullptr && getVoice(i)->isActive())
            ++count;
    return count;
}

void AdditiveSynth::renderNextSubBlock(juce::AudioBuffer<float>& outputAudio,
                                       int startSample, int numSamples)
{
    {
        // Non-blocking: if the message thread is mid-write, keep last good copy.
        const juce::SpinLock::ScopedTryLockType lock(partialLock_);
        if (lock.isLocked())
            active_ = published_;
    }

    active_.rootHz = rootHzFrom(rootNote_.load(std::memory_order_relaxed),
                                rootFineTune_.load(std::memory_order_relaxed));

    for (int i = 0; i < getNumVoices(); ++i)
        if (auto* v = static_cast<AdditiveVoice*>(getVoice(i)))
            v->snapshot = &active_;

    for (int ch = 0; ch < outputAudio.getNumChannels(); ++ch)
        outputAudio.clear(ch, startSample, numSamples);

    MPESynthesiser::renderNextSubBlock(outputAudio, startSample, numSamples);
}

} // namespace ana
```

- [ ] **Step 3: Wire both CMake targets**

In `CMakeLists.txt`, add after `src/dsp/BlurEffect.cpp` (line ~151):
```cmake
    src/dsp/AdditiveSynth.cpp
```
In `tests/CMakeLists.txt`, add `test_additive_synth.cpp` near line 27 (replace the commented `#test_partial_editor_canvas.cpp` region by adding a new line — do not uncomment that one) and add `../src/dsp/AdditiveSynth.cpp` near line 110.

- [ ] **Step 4: Commit**

```bash
git add src/dsp/AdditiveSynth.h src/dsp/AdditiveSynth.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(p6): AdditiveSynth engine - MPE additive voices over a lock-free harmonic snapshot"
```

---

### Task 2: Unit tests for `AdditiveSynth`

**Files:**
- Create: `tests/test_additive_synth.cpp`
- Modify: `tests/CMakeLists.txt` (already listed in Task 1)

**Interfaces consumed:** all of `ana::AdditiveSynth` / `ana::AdditiveVoice` from Task 1.

- [ ] **Step 1: Write the failing tests**

```cpp
#include <catch2/catch_all.hpp>
#include "dsp/AdditiveSynth.h"

#include <cmath>
#include <vector>

namespace
{
constexpr double kSr = 44100.0;

ana::PartialDataSIMD makeSet(std::initializer_list<std::pair<float, float>> partials)
{
    ana::PartialDataSIMD d;
    d.sampleRate = kSr;
    int i = 0;
    for (const auto& p : partials)
    {
        if (i >= ana::PartialDataSIMD::kMaxPartials) break;
        d.frequency[i] = p.first;
        d.amplitude[i] = p.second;
        d.phase[i]     = 0.0f;
        ++i;
    }
    d.updateActiveMask();
    return d;
}

// Goertzel magnitude at `freq` over x[0..n).
float goertzel(const float* x, int n, float freq)
{
    const float w  = juce::MathConstants<float>::twoPi * freq / static_cast<float>(kSr);
    const float c  = 2.0f * std::cos(w);
    float s1 = 0.0f, s2 = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(std::max(0.0f, s1 * s1 + s2 * s2 - c * s1 * s2));
}

float estimateFreq(const float* x, int n)
{
    int crossings = 0;
    for (int i = 1; i < n; ++i)
        if ((x[i - 1] <= 0.0f) != (x[i] <= 0.0f))
            ++crossings;
    const float dur = static_cast<float>(n) / static_cast<float>(kSr);
    return static_cast<float>(crossings) / (2.0f * dur);
}

struct Rendered { std::vector<float> mono; float rms = 0.0f; };

Rendered render(ana::AdditiveSynth& synth, int note, int numSamples)
{
    juce::AudioBuffer<float> buf(1, numSamples);
    buf.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), 0);
    synth.renderNextBlock(buf, midi, 0, numSamples);

    Rendered r;
    r.mono.assign(buf.getReadPointer(0), buf.getReadPointer(0) + numSamples);
    r.rms = buf.getRMSLevel(0, 0, numSamples);
    return r;
}
}

TEST_CASE("AdditiveSynth renders a partial at the note frequency", "[additive]")
{
    ana::AdditiveSynth synth;
    synth.prepare(kSr);
    synth.setRootNote(60);
    synth.setPartials(makeSet({ { 440.0f, 1.0f } }));

    const auto r = render(synth, 60, 8192);
    REQUIRE(r.rms > 0.01f);

    const auto f = estimateFreq(r.mono.data() + 2048, 4096);
    REQUIRE(f == Catch::Approx(440.0f).margin(15.0f));
}

TEST_CASE("AdditiveSynth transposes with the note", "[additive]")
{
    ana::AdditiveSynth synth;
    synth.prepare(kSr);
    synth.setRootNote(60);
    synth.setPartials(makeSet({ { 440.0f, 1.0f } }));

    const auto r = render(synth, 72, 8192);   // one octave up
    const auto f = estimateFreq(r.mono.data() + 2048, 4096);
    REQUIRE(f == Catch::Approx(880.0f).margin(30.0f));
}

TEST_CASE("Removing a partial removes its energy (subtractive semantics)", "[additive]")
{
    ana::AdditiveSynth synth;
    synth.prepare(kSr);
    synth.setRootNote(60);

    synth.setPartials(makeSet({ { 440.0f, 1.0f }, { 660.0f, 0.9f } }));
    const auto both = render(synth, 60, 8192);
    const float magBoth = goertzel(both.mono.data(), 8192, 660.0f);

    ana::AdditiveSynth synth2;
    synth2.prepare(kSr);
    synth2.setRootNote(60);
    synth2.setPartials(makeSet({ { 440.0f, 1.0f } }));
    const auto only440 = render(synth2, 60, 8192);
    const float magAfter = goertzel(only440.mono.data(), 8192, 660.0f);

    REQUIRE(magBoth > magAfter * 8.0f);
}

TEST_CASE("AdditiveSynth is silent with no partials", "[additive]")
{
    ana::AdditiveSynth synth;
    synth.prepare(kSr);
    synth.clearPartials();

    const auto r = render(synth, 60, 2048);
    REQUIRE(r.rms < 1.0e-5f);
}

TEST_CASE("setMaxPartials keeps only the loudest partials", "[additive]")
{
    ana::AdditiveSynth synth;
    synth.prepare(kSr);
    synth.setMaxPartials(4);

    ana::PartialDataSIMD d;
    d.sampleRate = kSr;
    for (int i = 0; i < 20; ++i)
    {
        d.frequency[i] = 200.0f + static_cast<float>(i) * 50.0f;
        d.amplitude[i] = static_cast<float>(i + 1) / 20.0f;
    }
    d.updateActiveMask();
    synth.setPartials(d);

    REQUIRE(synth.getActivePartialCount() == 4);
}

TEST_CASE("AdditiveSynth ignores out-of-range and non-finite partials", "[additive]")
{
    ana::AdditiveSynth synth;
    synth.prepare(kSr);

    ana::PartialDataSIMD d;
    d.sampleRate = kSr;
    d.frequency[0] = 440.0f;  d.amplitude[0] = 1.0f;
    d.frequency[1] = -10.0f;  d.amplitude[1] = 1.0f;
    d.frequency[2] = 1.0e9f;  d.amplitude[2] = 1.0f;
    d.frequency[3] = 500.0f;  d.amplitude[3] = std::nanf("");
    d.updateActiveMask();
    synth.setPartials(d);

    REQUIRE(synth.getActivePartialCount() == 1);
}

TEST_CASE("AdditiveSynth voice lifecycle survives note on/off", "[additive]")
{
    ana::AdditiveSynth synth;
    synth.prepare(kSr);
    synth.setRootNote(60);
    synth.setPartials(makeSet({ { 440.0f, 1.0f } }));

    juce::AudioBuffer<float> buf(1, 512);
    buf.clear();
    juce::MidiBuffer on;
    on.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    synth.renderNextBlock(buf, on, 0, 512);
    REQUIRE(synth.getNumActiveVoices() >= 1);

    for (int block = 0; block < 200; ++block)
    {
        buf.clear();
        juce::MidiBuffer off;
        if (block == 0)
            off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        synth.renderNextBlock(buf, off, 0, 512);
    }
    REQUIRE(synth.getNumActiveVoices() == 0);
}

TEST_CASE("setPartials under interleaved rendering does not tear or crash", "[additive]")
{
    ana::AdditiveSynth synth;
    synth.prepare(kSr);
    synth.setRootNote(60);
    synth.setPartials(makeSet({ { 440.0f, 1.0f } }));

    juce::AudioBuffer<float> buf(1, 256);
    for (int i = 0; i < 200; ++i)
    {
        synth.setPartials((i % 2 == 0) ? makeSet({ { 220.0f, 1.0f }, { 440.0f, 0.5f } })
                                       : makeSet({ { 440.0f, 1.0f } }));
        buf.clear();
        juce::MidiBuffer midi;
        if (i == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        synth.renderNextBlock(buf, midi, 0, 256);
    }
    REQUIRE(true);
}
```

- [ ] **Step 2: Verify via CI (no local toolchain)**

After pushing (Task 5), in the CI run confirm the `AnaPlugTests` step lists and passes all `[additive]` cases.

- [ ] **Step 3: Commit**

```bash
git add tests/test_additive_synth.cpp
git commit -m "test(p6): AdditiveSynth unit tests (freq, subtractive energy, caps, lifecycle)"
```

---

### Task 3: Processor integration (SYNTH mode, source frame, edited set)

**Files:**
- Modify: `src/PluginProcessor.h` (include + public API + private members + private helper)
- Modify: `src/PluginProcessor.cpp` (`prepareToPlay`, `processBlock`, new methods, `loadFile`)

**Interfaces consumed:** `ana::AdditiveSynth` (Task 1); existing `engine.getPartialData()`, `rootNoteParam_`, `rootFineTuneParam_`, `voiceBuffer`, `resynthBuffer_`, `isPlaying`, `resynthBufferReady_`.
**Interfaces produced (consumed by Task 4):** `bool isSynthMode() const; void setSynthMode(bool); void setEditedPartials(const ana::PartialDataSIMD&); const ana::PartialDataSIMD& getEditedPartials() const; void resetPartialsFromEngine(); int getActivePartialCount() const;`

- [ ] **Step 1: Edit `src/PluginProcessor.h`**

Add near the other dsp includes (after `#include "dsp/BlurEffect.h"`):
```cpp
#include "dsp/AdditiveSynth.h"
```

Add to the public API after the engine access block (`ana::AnaPlugEngine& getEngine();`):
```cpp
    // --- Additive synth mode (P6) ---
    bool isSynthMode() const { return synthMode_.load(); }
    void setSynthMode(bool enabled);
    void setEditedPartials(const ana::PartialDataSIMD& partials);
    const ana::PartialDataSIMD& getEditedPartials() const { return editedPartials_; }
    void resetPartialsFromEngine();
    int getActivePartialCount() const { return additiveSynth_.getActivePartialCount(); }
```

Add to the private members near `ana::AnaPlugEngine engine;`:
```cpp
    // --- Additive synth (P6) ---
    ana::AdditiveSynth additiveSynth_;
    std::atomic<bool>  synthMode_{ false };
    ana::PartialDataSIMD sourcePartials_;
    ana::PartialDataSIMD editedPartials_;
    void refreshPartialsFromEngine();   // message thread: engine -> source/edited -> synth
```

- [ ] **Step 2: Implement in `src/PluginProcessor.cpp`**

After `void AnaPlugAudioProcessor::setSubHarmonicLevel(float level)` block, add:
```cpp
//==============================================================================
// Additive synth mode (P6)
//==============================================================================

void AnaPlugAudioProcessor::refreshPartialsFromEngine()
{
    sourcePartials_ = ana::PartialDataSIMD{};

    const auto& pd = engine.getPartialData();
    if (! pd.frames.empty())
    {
        std::size_t best = 0;
        double bestEnergy = -1.0;
        for (std::size_t f = 0; f < pd.frames.size(); ++f)
        {
            double e = 0.0;
            for (const auto& p : pd.frames[f].partials)
                e += static_cast<double>(p.amplitude) * static_cast<double>(p.amplitude);
            if (e > bestEnergy) { bestEnergy = e; best = f; }
        }

        sourcePartials_.maxPartials = pd.maxPartials;
        sourcePartials_.sampleRate  = pd.sampleRate;
        sourcePartials_.hopSize     = pd.hopSize;

        const auto& frame = pd.frames[best];
        const int cnt = juce::jmin(static_cast<int>(frame.partials.size()),
                                   ana::PartialDataSIMD::kMaxPartials);
        for (int i = 0; i < cnt; ++i)
        {
            sourcePartials_.frequency[i] = frame.partials[static_cast<std::size_t>(i)].frequency;
            sourcePartials_.amplitude[i] = frame.partials[static_cast<std::size_t>(i)].amplitude;
            sourcePartials_.phase[i]     = frame.partials[static_cast<std::size_t>(i)].phase;
        }
        sourcePartials_.updateActiveMask();
    }

    editedPartials_ = sourcePartials_;
    additiveSynth_.setPartials(editedPartials_);
}

void AnaPlugAudioProcessor::setSynthMode(bool enabled)
{
    synthMode_.store(enabled);
    if (enabled)
        additiveSynth_.setPartials(editedPartials_);
}

void AnaPlugAudioProcessor::setEditedPartials(const ana::PartialDataSIMD& partials)
{
    editedPartials_ = partials;
    editedPartials_.updateActiveMask();
    additiveSynth_.setPartials(editedPartials_);
}

void AnaPlugAudioProcessor::resetPartialsFromEngine()
{
    refreshPartialsFromEngine();
}
```

In `prepareToPlay` (next to `voiceManager.prepare(sampleRate);`, line ~427) add:
```cpp
    additiveSynth_.prepare(sampleRate);
```

In `loadFile`, after the `enginePartials_ = ...` line (~1024) add:
```cpp
        refreshPartialsFromEngine();
```
(Call it inside the `if (success)` block so partials follow a successful load.)

In `processBlock`:
1. Replace the VoiceManager render block (lines ~621–623):
```cpp
    voiceBuffer.setSize(numChannels, numSamples, false, false, true);
    voiceBuffer.clear();
    voiceManager.renderNextBlock(voiceBuffer, midiMessages, 0, numSamples);
```
with:
```cpp
    voiceBuffer.setSize(numChannels, numSamples, false, false, true);
    voiceBuffer.clear();
    if (synthMode_.load())
    {
        additiveSynth_.setRootNote(rootNoteParam_.load());
        additiveSynth_.setRootFineTune(rootFineTuneParam_.load());
        additiveSynth_.renderNextBlock(voiceBuffer, midiMessages, 0, numSamples);
    }
    else
    {
        voiceManager.renderNextBlock(voiceBuffer, midiMessages, 0, numSamples);
    }
```
Do NOT wrap the unison/sub-harmonic blocks — they keep running (they are driven by the existing note side effects and are independent).

2. Gate the resynth playback so SYNTH mode falls through to the "copy voiceBuffer" path. Change:
```cpp
    if (isPlaying.load() && resynthBufferReady_.load())
```
to:
```cpp
    if (! synthMode_.load() && isPlaying.load() && resynthBufferReady_.load())
```

- [ ] **Step 3: Commit**

```bash
git add src/PluginProcessor.h src/PluginProcessor.cpp
git commit -m "feat(p6): SYNTH mode processor integration - source frame, edited set, additive render path"
```

---

### Task 4: Editor wiring (SYNTH toggle + stop clobbering edits)

**Files:**
- Modify: `src/PluginEditor.h` (add button member)
- Modify: `src/PluginEditor.cpp` (ctor toggle + onPartialEdited, title-bar bounds, timer guard)

**Interfaces consumed:** processor API from Task 3; existing `spectrumEditorCanvas_`, `titleRect`, `timerCallback`.

- [ ] **Step 1: `src/PluginEditor.h`** — add after `juce::TextButton importButton_{"IMPORT"};`:
```cpp
    juce::TextButton synthModeButton_{"SYNTH"};
```

- [ ] **Step 2: `src/PluginEditor.cpp` ctor** — after the IMPORT button setup (line ~42 `addAndMakeVisible(importButton_);`) add:
```cpp
    // SYNTH mode toggle (P6): switch between sample playback and live additive synth
    addCyberButton(synthModeButton_);
    synthModeButton_.setClickingTogglesState(true);
    synthModeButton_.setTooltip("Additive SYNTH mode: play the sample's partials live. Edit them in the spectrum EDITOR view.");
    synthModeButton_.onClick = [this]()
    {
        const bool on = synthModeButton_.getToggleState();
        audioProcessor.setSynthMode(on);
        if (on)
            spectrumEditorCanvas_.setPartials(audioProcessor.getEditedPartials());
    };
    addAndMakeVisible(synthModeButton_);
```

After `spectrumEditorCanvas_.setVisible(false);` (line ~63) add:
```cpp
    spectrumEditorCanvas_.onPartialEdited = [this](const ana::PartialDataSIMD& edited)
    {
        audioProcessor.setEditedPartials(edited);
    };
```

- [ ] **Step 3: `resized()` title bar** — insert before `importButton_.setBounds(...)` (line ~515):
```cpp
    synthModeButton_.setBounds(titleRect.removeFromRight(70).reduced(0, 3));
```

- [ ] **Step 4: `timerCallback()`** — guard the canvas push (line ~747):
```cpp
            if (! audioProcessor.isSynthMode())
                spectrumEditorCanvas_.setPartials(simd);
```

- [ ] **Step 5: Commit**

```bash
git add src/PluginEditor.h src/PluginEditor.cpp
git commit -m "feat(p6): SYNTH toggle + spectrum editor wiring (edits are audible; timer no longer clobbers)"
```

---

### Task 5: Push, CI verification, forensics

**Files:** none.

- [ ] **Step 1: Push** (PAT supplied in session; not written to disk)
```bash
git push https://x-access-token:<PAT>@github.com/SakuraLuminance/notitle.git main
git fetch origin main
```

- [ ] **Step 2: Read the CI run** via authenticated API:
```
GET /repos/SakuraLuminance/notitle/actions/runs?branch=main&per_page=1
GET /repos/SakuraLuminance/notitle/actions/runs/{id}/jobs
GET /repos/SakuraLuminance/notitle/actions/jobs/{job_id}/logs
```
Expected: build succeeds; `AnaPlugTests` reports the previous 557 cases + the new `[additive]` cases, 0 failures; pluginval strictness 3 passes.

- [ ] **Step 3: If red, pull the failing job log first** and fix the specific error before changing anything else (no speculative rewrites).

---

## Self-Review

**Spec coverage**
- AdditiveSynth engine (MPE voices, lock-free snapshot, caps) → Task 1.
- SYNTH mode toggle preserving sample playback → Tasks 3, 4.
- Single-frame harmonic set from max-energy analysis frame → Task 3 (`refreshPartialsFromEngine`).
- SpectrumEditorCanvas wired so edits are audible + timer no longer clobbers → Task 4.
- Unit tests (freq/RMS, subtractive energy, caps, sanitisation, lifecycle, interleaving) → Task 2.
- Round 2 (bright/blur/hpf/BLEND/DualTimbre, strictness 4) is intentionally out of this plan per spec staging.

**Placeholder scan:** none — all code shown; the only deferred item is explicitly scoped to Round 2.

**Type consistency:** `AdditiveSnapshot::kMaxPartials` (512) aliases `PartialDataSIMD::kMaxPartials` (512); `snapshot` member name consistent across header/impl/test; processor method names match editor call sites; `synthMode_` used consistently.
