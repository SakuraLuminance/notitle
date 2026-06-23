#include <catch2/catch_all.hpp>
#include <cmath>
#include <vector>

#include "dsp/WavetableEngine.h"

using namespace ana;

//==============================================================================
// Helper: generate a synthetic audio buffer with a frequency sweep.
// The frequency increases linearly from freqStart to freqEnd over the
// duration, ensuring different time regions produce different spectra.
//==============================================================================
static std::vector<float> generateSweep(double sampleRate,
                                         float freqStart,
                                         float freqEnd,
                                         float durationSec)
{
    const int numSamples = static_cast<int>(sampleRate * durationSec);
    std::vector<float> audio(static_cast<size_t>(numSamples), 0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float instFreq = freqStart + (freqEnd - freqStart) * t / durationSec;
        audio[static_cast<size_t>(i)] = std::sin(2.0f * 3.14159265f * instFreq * t);
    }

    return audio;
}

//==============================================================================
// Test: loadWavetable from raw audio
//==============================================================================
TEST_CASE("WavetableEngine - load from raw audio", "[wavetable]")
{
    WavetableEngine wte;
    REQUIRE_FALSE(wte.isLoaded());
    REQUIRE(wte.getNumFrames() == 0);

    const double sr = 44100.0;
    auto audio = generateSweep(sr, 200.0f, 800.0f, 2.0f);

    REQUIRE(wte.loadWavetable(audio, sr));
    CHECK(wte.isLoaded());
    CHECK(wte.getNumFrames() > 1);

    // getCurrentFrame should return a valid frame
    PartialDataSIMD frame;
    wte.getCurrentFrame(frame);
    CHECK(frame.activeCount > 0);
    CHECK(frame.sampleRate == sr);

    // getFrame at valid index
    auto f0 = wte.getFrame(0);
    CHECK(f0.activeCount > 0);

    // getFrame at invalid index returns empty
    auto empty = wte.getFrame(999999);
    CHECK(empty.activeCount == 0);
}

//==============================================================================
// Test: position 0 and 1 return different frames (frequency sweep ensures this)
//==============================================================================
TEST_CASE("WavetableEngine - position 0 and 1 differ", "[wavetable]")
{
    WavetableEngine wte;
    const double sr = 44100.0;
    auto audio = generateSweep(sr, 100.0f, 1000.0f, 3.0f);
    REQUIRE(wte.loadWavetable(audio, sr));
    REQUIRE(wte.getNumFrames() >= 4);

    // Position 0
    wte.setPosition(0.0f);
    PartialDataSIMD frame0;
    wte.getCurrentFrame(frame0);

    // Position 1
    wte.setPosition(1.0f);
    PartialDataSIMD frame1;
    wte.getCurrentFrame(frame1);

    // The spectrum should differ at beginning vs end of sweep
    // Check that some frequency bins differ meaningfully
    bool anyDiff = false;
    for (int i = 0; i < static_cast<int>(PartialDataSIMD::kMaxPartials); ++i)
    {
        if (std::abs(frame0.frequency[i] - frame1.frequency[i]) > 1.0f ||
            std::abs(frame0.amplitude[i] - frame1.amplitude[i]) > 0.001f)
        {
            anyDiff = true;
            break;
        }
    }
    CHECK(anyDiff);

    // Check position readback
    CHECK(wte.getPosition() == Catch::Approx(1.0f));
}

//==============================================================================
// Test: interpolation between frames
//==============================================================================
TEST_CASE("WavetableEngine - interpolation between frames", "[wavetable]")
{
    WavetableEngine wte;
    const double sr = 44100.0;
    auto audio = generateSweep(sr, 200.0f, 600.0f, 2.0f);
    REQUIRE(wte.loadWavetable(audio, sr));
    REQUIRE(wte.getNumFrames() >= 4);

    // Get frame at position 0 → pure first frame
    wte.setPosition(0.0f);
    PartialDataSIMD frame0;
    wte.getCurrentFrame(frame0);

    // Get frame at position 0.5 → should be halfway between two adjacent frames
    wte.setPosition(0.5f);
    PartialDataSIMD frameMid;
    wte.getCurrentFrame(frameMid);

    // The mid frame should be different from the first frame
    bool differsFrom0 = false;
    for (int i = 0; i < static_cast<int>(PartialDataSIMD::kMaxPartials); ++i)
    {
        if (std::abs(frame0.amplitude[i] - frameMid.amplitude[i]) > 0.001f)
        {
            differsFrom0 = true;
            break;
        }
    }
    CHECK(differsFrom0);

    // Loading 2 frames via loadFromPartials guarantees only 2 frames
    // so we can test exact 50/50 interpolation
    {
        std::vector<PartialDataSIMD> twoFrames(2);
        twoFrames[0].sampleRate = sr;
        twoFrames[0].hopSize = 512;
        twoFrames[0].frequency[0] = 100.0f;
        twoFrames[0].amplitude[0] = 0.5f;
        twoFrames[0].phase[0] = 0.0f;
        twoFrames[0].updateActiveMask();

        twoFrames[1].sampleRate = sr;
        twoFrames[1].hopSize = 512;
        twoFrames[1].frequency[0] = 300.0f;
        twoFrames[1].amplitude[0] = 1.0f;
        twoFrames[1].phase[0] = 1.0f;
        twoFrames[1].updateActiveMask();

        REQUIRE(wte.loadFromPartials(twoFrames));
        CHECK(wte.getNumFrames() == 2);

        // Position 0 → pure frame A
        wte.setPosition(0.0f);
        PartialDataSIMD pureA;
        wte.getCurrentFrame(pureA);
        CHECK(pureA.frequency[0] == Catch::Approx(100.0f));
        CHECK(pureA.amplitude[0] == Catch::Approx(0.5f));
        CHECK(pureA.phase[0] == Catch::Approx(0.0f));

        // Position 1 → pure frame B
        wte.setPosition(1.0f);
        PartialDataSIMD pureB;
        wte.getCurrentFrame(pureB);
        CHECK(pureB.frequency[0] == Catch::Approx(300.0f));
        CHECK(pureB.amplitude[0] == Catch::Approx(1.0f));
        CHECK(pureB.phase[0] == Catch::Approx(1.0f));

        // Position 0.5 → 50/50 blend
        wte.setPosition(0.5f);
        PartialDataSIMD half;
        wte.getCurrentFrame(half);
        CHECK(half.frequency[0] == Catch::Approx(200.0f).margin(0.1f));
        CHECK(half.amplitude[0] == Catch::Approx(0.75f).margin(0.01f));
        CHECK(half.phase[0] == Catch::Approx(0.5f).margin(0.01f));
    }
}

//==============================================================================
// Test: clear resets state
//==============================================================================
TEST_CASE("WavetableEngine - clear resets state", "[wavetable]")
{
    WavetableEngine wte;
    const double sr = 44100.0;
    auto audio = generateSweep(sr, 200.0f, 800.0f, 1.0f);
    REQUIRE(wte.loadWavetable(audio, sr));
    CHECK(wte.isLoaded());
    CHECK(wte.getNumFrames() > 0);

    wte.clear();
    CHECK_FALSE(wte.isLoaded());
    CHECK(wte.getNumFrames() == 0);
    CHECK(wte.getPosition() == Catch::Approx(0.0f));

    // getCurrentFrame on cleared engine returns empty
    PartialDataSIMD frame;
    wte.getCurrentFrame(frame);
    CHECK(frame.activeCount == 0);
}

//==============================================================================
// Test: getFrame direct access
//==============================================================================
TEST_CASE("WavetableEngine - getFrame direct access", "[wavetable]")
{
    std::vector<PartialDataSIMD> threeFrames(3);
    for (int i = 0; i < 3; ++i)
    {
        threeFrames[static_cast<size_t>(i)].sampleRate = 44100.0;
        threeFrames[static_cast<size_t>(i)].hopSize = 512;
        threeFrames[static_cast<size_t>(i)].frequency[0] = 100.0f + static_cast<float>(i) * 50.0f;
        threeFrames[static_cast<size_t>(i)].amplitude[0] = 0.5f + static_cast<float>(i) * 0.1f;
        threeFrames[static_cast<size_t>(i)].phase[0] = 0.0f + static_cast<float>(i) * 0.5f;
        threeFrames[static_cast<size_t>(i)].updateActiveMask();
    }

    WavetableEngine wte;
    REQUIRE(wte.loadFromPartials(threeFrames));

    // Direct index access
    auto f0 = wte.getFrame(0);
    CHECK(f0.frequency[0] == Catch::Approx(100.0f));
    CHECK(f0.amplitude[0] == Catch::Approx(0.5f));

    auto f2 = wte.getFrame(2);
    CHECK(f2.frequency[0] == Catch::Approx(200.0f));
    CHECK(f2.amplitude[0] == Catch::Approx(0.7f));

    // Out of bounds returns empty
    auto ob = wte.getFrame(-1);
    CHECK(ob.activeCount == 0);
    auto ob2 = wte.getFrame(10);
    CHECK(ob2.activeCount == 0);
}

//==============================================================================
// Test: loadFromPartials with empty vector fails and clears
//==============================================================================
TEST_CASE("WavetableEngine - loadFromPartials empty fails", "[wavetable]")
{
    WavetableEngine wte;
    CHECK_FALSE(wte.loadFromPartials({}));
    CHECK_FALSE(wte.isLoaded());
}

//==============================================================================
// Test: default position is 0
//==============================================================================
TEST_CASE("WavetableEngine - default position is 0", "[wavetable]")
{
    WavetableEngine wte;
    CHECK(wte.getPosition() == Catch::Approx(0.0f));
}

//==============================================================================
// Test: setPosition clamps to [0, 1]
//==============================================================================
TEST_CASE("WavetableEngine - setPosition clamps", "[wavetable]")
{
    WavetableEngine wte;
    wte.setPosition(-0.5f);
    CHECK(wte.getPosition() == Catch::Approx(0.0f));

    wte.setPosition(1.5f);
    CHECK(wte.getPosition() == Catch::Approx(1.0f));

    wte.setPosition(0.3f);
    CHECK(wte.getPosition() == Catch::Approx(0.3f));
}

//==============================================================================
// Test: getCurrentFrame on empty engine returns empty frame
//==============================================================================
TEST_CASE("WavetableEngine - getCurrentFrame on empty engine", "[wavetable]")
{
    WavetableEngine wte;
    PartialDataSIMD frame;
    wte.getCurrentFrame(frame);
    CHECK(frame.activeCount == 0);
    CHECK(frame.sampleRate == 44100.0);
}

//==============================================================================
// Test: single frame loaded returns that frame regardless of position
//==============================================================================
TEST_CASE("WavetableEngine - single frame at any position", "[wavetable]")
{
    std::vector<PartialDataSIMD> single(1);
    single[0].sampleRate = 48000.0;
    single[0].hopSize = 1024;
    single[0].frequency[0] = 440.0f;
    single[0].amplitude[0] = 0.9f;
    single[0].phase[0] = 0.5f;
    single[0].updateActiveMask();

    WavetableEngine wte;
    REQUIRE(wte.loadFromPartials(single));
    CHECK(wte.getNumFrames() == 1);

    wte.setPosition(0.0f);
    PartialDataSIMD f0;
    wte.getCurrentFrame(f0);
    wte.setPosition(0.7f);
    PartialDataSIMD f1;
    wte.getCurrentFrame(f1);
    wte.setPosition(1.0f);
    PartialDataSIMD f2;
    wte.getCurrentFrame(f2);

    CHECK(f0.frequency[0] == Catch::Approx(440.0f));
    CHECK(f1.amplitude[0] == Catch::Approx(0.9f));
    CHECK(f2.phase[0] == Catch::Approx(0.5f));
}
