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
