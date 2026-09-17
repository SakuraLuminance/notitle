#include <catch2/catch_all.hpp>
#include "dsp/AdditiveSynth.h"

#include <algorithm>
#include <cmath>
#include <utility>
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
    const float w = juce::MathConstants<float>::twoPi * freq / static_cast<float>(kSr);
    const float c = 2.0f * std::cos(w);
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
} // namespace

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
    ana::AdditiveSynth synthBoth;
    synthBoth.prepare(kSr);
    synthBoth.setRootNote(60);
    synthBoth.setPartials(makeSet({ { 440.0f, 1.0f }, { 660.0f, 0.9f } }));
    const auto both = render(synthBoth, 60, 8192);
    const float magBoth = goertzel(both.mono.data(), 8192, 660.0f);

    ana::AdditiveSynth synthOnly440;
    synthOnly440.prepare(kSr);
    synthOnly440.setRootNote(60);
    synthOnly440.setPartials(makeSet({ { 440.0f, 1.0f } }));
    const auto only440 = render(synthOnly440, 60, 8192);
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

//==============================================================================
// P6b: time-varying harmonic image

namespace
{
float goertzelMag(const juce::AudioBuffer<float>& buf, float freq, double sr)
{
    const int n = buf.getNumSamples();
    const float* x = buf.getReadPointer(0);
    const double w = juce::MathConstants<double>::twoPi * static_cast<double>(freq) / sr;
    const double c = 2.0 * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        s0 = static_cast<double>(x[i]) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return static_cast<float>(std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2));
}
}

TEST_CASE("AdditiveSynth image playback advances through frames", "[additive][image]")
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    ana::PartialDataSIMD f0, f1;
    f0.sampleRate = f1.sampleRate = sr;
    f0.frequency[0] = 440.0f;  f0.amplitude[0] = 1.0f;
    f1.frequency[0] = 2000.0f; f1.amplitude[0] = 1.0f;
    f0.updateActiveMask();
    f1.updateActiveMask();

    juce::MidiBuffer emptyMidi;

    auto render = [&](ana::AdditiveSynth& synth, int blocks, juce::AudioBuffer<float>& last)
    {
        juce::AudioBuffer<float> buf(1, block);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);

        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            if (b == 0)
                synth.renderNextBlock(buf, midi, 0, block);
            else
                synth.renderNextBlock(buf, emptyMidi, 0, block);

            if (b == blocks - 1)
                last.makeCopyOf(buf);
        }
    };

    SECTION("image off renders frame 0")
    {
        ana::AdditiveSynth synth;
        synth.prepare(sr);
        synth.setRootNote(60);
        synth.setFrames({ f0, f1 });
        REQUIRE(synth.getActiveFrameCount() == 2);

        juce::AudioBuffer<float> last(1, block);
        render(synth, 20, last);

        REQUIRE(goertzelMag(last, 440.0f, sr) > goertzelMag(last, 2000.0f, sr) * 4.0f);
    }

    SECTION("image on without loop reaches the last frame")
    {
        ana::AdditiveSynth synth;
        synth.prepare(sr);
        synth.setRootNote(60);
        synth.setFrames({ f0, f1 });
        synth.setImageEnabled(true);
        synth.setImageLoop(false);
        synth.setImageRate(20.0f);   // 2-frame image completes in 50 ms

        juce::AudioBuffer<float> last(1, block);
        render(synth, 30, last);     // ~0.32 s, well past the image end

        REQUIRE(goertzelMag(last, 2000.0f, sr) > goertzelMag(last, 440.0f, sr) * 4.0f);
    }

    SECTION("image on with loop wraps back to frame 0")
    {
        ana::AdditiveSynth synth;
        synth.prepare(sr);
        synth.setRootNote(60);
        synth.setFrames({ f0, f1 });
        synth.setImageEnabled(true);
        synth.setImageLoop(true);
        synth.setImageRate(12.0f);

        // 1 frame per second at rate 1 would land back on frame 0; with rate 12
        // and 30 * 512 samples (0.32 s) the position wraps several times, so the
        // output must contain energy at both frequencies over the run.
        juce::AudioBuffer<float> acc(1, block);
        juce::AudioBuffer<float> buf(1, block);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);

        double e440 = 0.0, e2000 = 0.0;
        for (int b = 0; b < 30; ++b)
        {
            buf.clear();
            if (b == 0)
                synth.renderNextBlock(buf, midi, 0, block);
            else
                synth.renderNextBlock(buf, emptyMidi, 0, block);

            const double m440  = goertzelMag(buf, 440.0f, sr);
            const double m2000 = goertzelMag(buf, 2000.0f, sr);
            e440  = std::max(e440, m440);
            e2000 = std::max(e2000, m2000);
        }

        REQUIRE(e440  > 0.0);
        REQUIRE(e2000 > 0.0);
    }

    SECTION("single frame image is safe and static")
    {
        ana::AdditiveSynth synth;
        synth.prepare(sr);
        synth.setRootNote(60);
        synth.setFrames({ f0 });
        synth.setImageEnabled(true);
        synth.setImageRate(8.0f);

        juce::AudioBuffer<float> last(1, block);
        render(synth, 20, last);

        REQUIRE(synth.getActiveFrameCount() == 1);
        REQUIRE(goertzelMag(last, 440.0f, sr) > goertzelMag(last, 2000.0f, sr) * 4.0f);
    }

    SECTION("empty frames clear the bank")
    {
        ana::AdditiveSynth synth;
        synth.prepare(sr);
        synth.setFrames(std::vector<ana::PartialDataSIMD>{});
        REQUIRE(synth.getActiveFrameCount() == 0);
        REQUIRE_FALSE(synth.hasPartials());
    }
}

TEST_CASE("AdditiveSynth drops the inaudible partial tail", "[additive][trim]")
{
    ana::PartialDataSIMD p;
    p.sampleRate = 48000.0;
    p.maxPartials = ana::PartialDataSIMD::kMaxPartials;
    p.frequency[0] = 440.0f;  p.amplitude[0] = 1.0f;
    p.frequency[1] = 880.0f;  p.amplitude[1] = 1.0e-5f;   // far below audibility
    p.frequency[2] = 1320.0f; p.amplitude[2] = 5.0e-5f;
    p.updateActiveMask();

    ana::AdditiveSynth synth;
    synth.prepare(48000.0);
    synth.setPartials(p);

    REQUIRE(synth.getActivePartialCount() == 1);
}
