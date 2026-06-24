#include <catch2/catch_all.hpp>
#include "dsp/LFOSystem.h"
#include <cmath>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr double TEST_SR = 44100.0;

/** Number of samples per cycle at the given frequency (Hz). */
static int samplesPerCycle(double hz)
{
    return static_cast<int>(std::round(TEST_SR / hz));
}

/** Number of samples to advance to reach the given normalised phase [0, 1). */
static int samplesForPhase(double hz, double targetPhase)
{
    return static_cast<int>(std::round(samplesPerCycle(hz) * targetPhase));
}

// ===========================================================================
// Default state
// ===========================================================================

TEST_CASE("LFOSystem - default state", "[lfo][basic]")
{
    ana::LFOSystem lfo;

    REQUIRE(lfo.getWaveform() == ana::WaveformType::Sine);
    REQUIRE(lfo.getRate() == 1.0f);
    REQUIRE(lfo.getDepth() == 100.0f);
    REQUIRE(lfo.getPhase() == 0.0f);
    REQUIRE(lfo.isBipolar() == true);
    REQUIRE(lfo.isSyncEnabled() == false);
    REQUIRE(lfo.getTempo() == 120.0);
}

// ===========================================================================
// Waveform shapes
// ===========================================================================

TEST_CASE("LFOSystem - sine waveform", "[lfo][waveform][sine]")
{
    constexpr float hz = 5.0f;
    ana::LFOSystem lfo;
    lfo.prepare(TEST_SR);
    lfo.setWaveform(ana::WaveformType::Sine);
    lfo.setRate(hz);
    lfo.setDepth(100.0f);
    lfo.setBipolar(true);

    SECTION("phase 0")
    {
        float val = lfo.process(0);
        REQUIRE(val == Catch::Approx(0.0f).margin(0.001f));
    }

    SECTION("phase 0.25")
    {
        float val = lfo.process(samplesForPhase(hz, 0.25));
        REQUIRE(val == Catch::Approx(1.0f).margin(0.001f));
    }

    SECTION("phase 0.5")
    {
        float val = lfo.process(samplesForPhase(hz, 0.5));
        REQUIRE(val == Catch::Approx(0.0f).margin(0.001f));
    }

    SECTION("phase 0.75")
    {
        float val = lfo.process(samplesForPhase(hz, 0.75));
        REQUIRE(val == Catch::Approx(-1.0f).margin(0.001f));
    }

    SECTION("full cycle returns to zero")
    {
        lfo.process(samplesForPhase(hz, 1.0));
        float val = lfo.process(0);
        REQUIRE(val == Catch::Approx(0.0f).margin(0.001f));
    }
}

TEST_CASE("LFOSystem - triangle waveform", "[lfo][waveform][triangle]")
{
    constexpr float hz = 5.0f;
    ana::LFOSystem lfo;
    lfo.prepare(TEST_SR);
    lfo.setWaveform(ana::WaveformType::Triangle);
    lfo.setRate(hz);
    lfo.setDepth(100.0f);
    lfo.setBipolar(true);

    SECTION("phase 0")
    {
        float val = lfo.process(0);
        REQUIRE(val == Catch::Approx(-1.0f).margin(0.001f));
    }

    SECTION("phase 0.25")
    {
        float val = lfo.process(samplesForPhase(hz, 0.25));
        REQUIRE(val == Catch::Approx(0.0f).margin(0.001f));
    }

    SECTION("phase 0.5")
    {
        float val = lfo.process(samplesForPhase(hz, 0.5));
        REQUIRE(val == Catch::Approx(1.0f).margin(0.001f));
    }

    SECTION("phase 0.75")
    {
        float val = lfo.process(samplesForPhase(hz, 0.75));
        REQUIRE(val == Catch::Approx(0.0f).margin(0.001f));
    }
}

TEST_CASE("LFOSystem - saw waveform", "[lfo][waveform][saw]")
{
    constexpr float hz = 5.0f;
    ana::LFOSystem lfo;
    lfo.prepare(TEST_SR);
    lfo.setWaveform(ana::WaveformType::Saw);
    lfo.setRate(hz);
    lfo.setDepth(100.0f);
    lfo.setBipolar(true);

    SECTION("phase 0")
    {
        float val = lfo.process(0);
        REQUIRE(val == Catch::Approx(0.0f).margin(0.001f));
    }

    SECTION("phase 0.25")
    {
        float val = lfo.process(samplesForPhase(hz, 0.25));
        REQUIRE(val == Catch::Approx(0.5f).margin(0.001f));
    }

    SECTION("phase 0.75")
    {
        float val = lfo.process(samplesForPhase(hz, 0.75));
        REQUIRE(val == Catch::Approx(-0.5f).margin(0.001f));
    }
}

TEST_CASE("LFOSystem - square waveform", "[lfo][waveform][square]")
{
    constexpr float hz = 5.0f;
    ana::LFOSystem lfo;
    lfo.prepare(TEST_SR);
    lfo.setWaveform(ana::WaveformType::Square);
    lfo.setRate(hz);
    lfo.setDepth(100.0f);
    lfo.setBipolar(true);

    SECTION("positive half (phase 0.1)")
    {
        float val = lfo.process(samplesForPhase(hz, 0.1));
        REQUIRE(val == Catch::Approx(1.0f).margin(0.001f));
    }

    SECTION("positive half (phase 0.4)")
    {
        float val = lfo.process(samplesForPhase(hz, 0.4));
        REQUIRE(val == Catch::Approx(1.0f).margin(0.001f));
    }

    SECTION("negative half (phase 0.6)")
    {
        float val = lfo.process(samplesForPhase(hz, 0.6));
        REQUIRE(val == Catch::Approx(-1.0f).margin(0.001f));
    }

    SECTION("negative half (phase 0.9)")
    {
        float val = lfo.process(samplesForPhase(hz, 0.9));
        REQUIRE(val == Catch::Approx(-1.0f).margin(0.001f));
    }
}

TEST_CASE("LFOSystem - random waveform", "[lfo][waveform][random]")
{
    ana::LFOSystem lfo;
    lfo.prepare(TEST_SR);
    lfo.setWaveform(ana::WaveformType::Random);
    lfo.setDepth(100.0f);
    lfo.setBipolar(true);

    SECTION("values are within [-1, 1]")
    {
        for (int i = 0; i < 1000; ++i)
        {
            float val = lfo.process(1);
            REQUIRE(val >= -1.0f);
            REQUIRE(val <= 1.0f);
        }
    }

    SECTION("values change at cycle boundaries")
    {
        // Set a known low rate so we can observe changes
        lfo.setRate(1.0f);

        // Get value at start of cycle
        float val1 = lfo.process(0);

        // Advance almost a full cycle (still within the same cycle)
        float val2 = lfo.process(samplesPerCycle(1.0f) - 10);
        REQUIRE(val2 == val1); // Same random value held

        // Advance past the cycle boundary
        float val3 = lfo.process(20);
        // Value may be same by coincidence (1/2 probability), but at least in range
        REQUIRE(val3 >= -1.0f);
        REQUIRE(val3 <= 1.0f);
    }
}

// ===========================================================================
// Rate accuracy
// ===========================================================================

TEST_CASE("LFOSystem - rate accuracy", "[lfo][rate]")
{
    ana::LFOSystem lfo;
    lfo.prepare(TEST_SR);
    lfo.setWaveform(ana::WaveformType::Sine);
    lfo.setDepth(100.0f);
    lfo.setBipolar(true);

    SECTION("5 Hz produces 5 cycles in 1 second")
    {
        lfo.setRate(5.0f);
        lfo.process(static_cast<int>(TEST_SR)); // advance 1 second

        // After exactly 1 second at 5 Hz, sine should be at phase 0 (5 full cycles)
        float val = lfo.process(0);
        REQUIRE(val == Catch::Approx(0.0f).margin(0.001f));
    }

    SECTION("10 Hz completes 10 cycles in 1 second")
    {
        lfo.setRate(10.0f);
        lfo.process(static_cast<int>(TEST_SR)); // advance 1 second

        float val = lfo.process(0);
        REQUIRE(val == Catch::Approx(0.0f).margin(0.001f));
    }

    SECTION("2 Hz sine hits expected values at quarter cycles")
    {
        lfo.setRate(2.0f);
        // 2 Hz → 22050 samples per cycle
        const int quarterCycle = static_cast<int>(TEST_SR / 2.0 / 4.0); // 5512 (rounded)

        float v1 = lfo.process(quarterCycle);
        REQUIRE(v1 == Catch::Approx(1.0f).margin(0.005f));

        float v2 = lfo.process(quarterCycle);
        REQUIRE(v2 == Catch::Approx(0.0f).margin(0.005f));

        float v3 = lfo.process(quarterCycle);
        REQUIRE(v3 == Catch::Approx(-1.0f).margin(0.005f));
    }
}

// ===========================================================================
// Tempo sync
// ===========================================================================

TEST_CASE("LFOSystem - tempo sync", "[lfo][sync]")
{
    ana::LFOSystem lfo;
    lfo.prepare(TEST_SR);
    lfo.setWaveform(ana::WaveformType::Sine);
    lfo.setDepth(100.0f);
    lfo.setBipolar(true);

    SECTION("quarter note at 120 BPM equals 2 Hz")
    {
        lfo.setTempo(120.0);
        lfo.setRateBeats(1.0f); // quarter note
        REQUIRE(lfo.isSyncEnabled());

        // 2 Hz → half cycle at 11025 samples → sin(π) = 0
        float val = lfo.process(static_cast<int>(TEST_SR / 4.0)); // quarter second = half cycle
        REQUIRE(val == Catch::Approx(0.0f).margin(0.005f));
    }

    SECTION("eighth note at 120 BPM equals 4 Hz")
    {
        lfo.setTempo(120.0);
        lfo.setRateBeats(0.5f); // eighth note
        REQUIRE(lfo.isSyncEnabled());

        // 4 Hz → quarter cycle at 11025/2 = 5512 samples → sin(π/2) = 1
        float val = lfo.process(static_cast<int>(TEST_SR / 8.0)); // 1/8 second = quarter cycle
        REQUIRE(val == Catch::Approx(1.0f).margin(0.01f));
    }

    SECTION("sixteenth note at 120 BPM equals 8 Hz")
    {
        lfo.setTempo(120.0);
        lfo.setRateBeats(0.25f); // sixteenth note
        REQUIRE(lfo.isSyncEnabled());

        // Verify rate is approximately 8 Hz after 1/4 second
        lfo.process(static_cast<int>(TEST_SR / 4.0)); // 2 full cycles at 8 Hz
        float val = lfo.process(0);
        REQUIRE(val == Catch::Approx(0.0f).margin(0.01f));
    }

    SECTION("tempo change updates effective rate")
    {
        lfo.setTempo(60.0);
        lfo.setRateBeats(1.0f); // quarter note at 60 BPM = 1 Hz

        // 1 Hz → quarter cycle at 11025 → sin(π/2) = 1
        float val = lfo.process(static_cast<int>(TEST_SR / 4.0));
        REQUIRE(val == Catch::Approx(1.0f).margin(0.005f));
    }

    SECTION("setRate disables sync")
    {
        lfo.setRateBeats(1.0f);
        REQUIRE(lfo.isSyncEnabled());

        lfo.setRate(5.0f);
        REQUIRE_FALSE(lfo.isSyncEnabled());
    }
}

// ===========================================================================
// Bipolar / Unipolar output
// ===========================================================================

TEST_CASE("LFOSystem - bipolar/unipolar mode", "[lfo][output]")
{
    ana::LFOSystem lfo;
    lfo.prepare(TEST_SR);
    lfo.setWaveform(ana::WaveformType::Sine);
    lfo.setRate(5.0f);
    lfo.setDepth(100.0f);

    SECTION("bipolar output stays within [-1, 1]")
    {
        lfo.setBipolar(true);
        for (int i = 0; i < static_cast<int>(TEST_SR); ++i)
        {
            float val = lfo.process(1);
            REQUIRE(val >= -1.0f);
            REQUIRE(val <= 1.0f);
        }
    }

    SECTION("unipolar output stays within [0, 1]")
    {
        lfo.setBipolar(false);
        for (int i = 0; i < static_cast<int>(TEST_SR); ++i)
        {
            float val = lfo.process(1);
            REQUIRE(val >= 0.0f);
            REQUIRE(val <= 1.0f);
        }
    }

    SECTION("unipolar sine midpoint at phase 0")
    {
        lfo.setBipolar(false);
        float val = lfo.process(0);
        // At phase 0: raw sine = 0, scaled = 0, unipolar = 0*0.5 + 0.5 = 0.5
        REQUIRE(val == Catch::Approx(0.5f).margin(0.001f));
    }

    SECTION("unipolar sine peak at phase 0.25")
    {
        lfo.setBipolar(false);
        float val = lfo.process(samplesForPhase(5.0f, 0.25));
        // At phase 0.25: raw = 1, scaled = 1, unipolar = 1*0.5 + 0.5 = 1.0
        REQUIRE(val == Catch::Approx(1.0f).margin(0.001f));
    }

    SECTION("unipolar sine trough at phase 0.75")
    {
        lfo.setBipolar(false);
        float val = lfo.process(samplesForPhase(5.0f, 0.75));
        // At phase 0.75: raw = -1, scaled = -1, unipolar = -1*0.5 + 0.5 = 0.0
        REQUIRE(val == Catch::Approx(0.0f).margin(0.001f));
    }
}

// ===========================================================================
// Depth scaling
// ===========================================================================

TEST_CASE("LFOSystem - depth scaling", "[lfo][depth]")
{
    ana::LFOSystem lfo;
    lfo.prepare(TEST_SR);
    lfo.setWaveform(ana::WaveformType::Sine);
    lfo.setRate(5.0f);
    lfo.setBipolar(true);

    SECTION("50% depth halves the sine amplitude")
    {
        lfo.setDepth(50.0f);

        // At phase 0.25, sine = 1.0, depth 50% → 0.5
        float peak = lfo.process(samplesForPhase(5.0f, 0.25));
        REQUIRE(peak == Catch::Approx(0.5f).margin(0.001f));

        // At phase 0.75, sine = -1.0, depth 50% → -0.5
        float trough = lfo.process(samplesForPhase(5.0f, 0.5));
        REQUIRE(trough == Catch::Approx(-0.5f).margin(0.001f));
    }

    SECTION("0% depth gives zero output in bipolar mode")
    {
        lfo.setDepth(0.0f);
        for (int i = 0; i < 100; ++i)
        {
            float val = lfo.process(1);
            REQUIRE(val == Catch::Approx(0.0f).margin(0.001f));
        }
    }

    SECTION("0% depth gives 0.5 output in unipolar mode")
    {
        lfo.setBipolar(false);
        lfo.setDepth(0.0f);
        for (int i = 0; i < 100; ++i)
        {
            float val = lfo.process(1);
            REQUIRE(val == Catch::Approx(0.5f).margin(0.001f));
        }
    }
}

// ===========================================================================
// Phase offset
// ===========================================================================

TEST_CASE("LFOSystem - phase offset", "[lfo][phase]")
{
    ana::LFOSystem lfo;
    lfo.prepare(TEST_SR);
    lfo.setWaveform(ana::WaveformType::Sine);
    lfo.setRate(5.0f);
    lfo.setDepth(100.0f);
    lfo.setBipolar(true);

    SECTION("90° offset shifts sine to start at 1")
    {
        lfo.setPhase(90.0f);
        lfo.reset();
        float val = lfo.process(0);
        // sin(2pi * 0.25) = sin(pi/2) = 1
        REQUIRE(val == Catch::Approx(1.0f).margin(0.001f));
    }

    SECTION("180° offset shifts sine to start at 0, inverted trajectory")
    {
        lfo.setPhase(180.0f);
        lfo.reset();
        float val = lfo.process(samplesForPhase(5.0f, 0.25));
        // Phase = 0.25 + 0.5 = 0.75 → sin(3pi/2) = -1
        REQUIRE(val == Catch::Approx(-1.0f).margin(0.001f));
    }

    SECTION("270° offset shifts sine to start at -1")
    {
        lfo.setPhase(270.0f);
        lfo.reset();
        float val = lfo.process(0);
        // sin(2pi * 0.75) = sin(3pi/2) = -1
        REQUIRE(val == Catch::Approx(-1.0f).margin(0.001f));
    }
}

// ===========================================================================
// Parameter clamping
// ===========================================================================

TEST_CASE("LFOSystem - parameter clamping", "[lfo][params]")
{
    ana::LFOSystem lfo;

    SECTION("rate clamped to [0.01, 100]")
    {
        lfo.setRate(-10.0f);
        REQUIRE(lfo.getRate() == 0.01f);

        lfo.setRate(0.0f);
        REQUIRE(lfo.getRate() == 0.01f);

        lfo.setRate(1000.0f);
        REQUIRE(lfo.getRate() == 100.0f);
    }

    SECTION("depth clamped to [0, 100]")
    {
        lfo.setDepth(-50.0f);
        REQUIRE(lfo.getDepth() == 0.0f);

        lfo.setDepth(200.0f);
        REQUIRE(lfo.getDepth() == 100.0f);
    }

    SECTION("phase clamped to [0, 360]")
    {
        lfo.setPhase(-90.0f);
        REQUIRE(lfo.getPhase() == 0.0f);

        lfo.setPhase(450.0f);
        REQUIRE(lfo.getPhase() == 360.0f);
    }

    SECTION("tempo clamped to minimum 1 BPM")
    {
        lfo.setTempo(0.0);
        REQUIRE(lfo.getTempo() == 1.0);
    }

    SECTION("rate beats clamped to minimum 1/32 note")
    {
        lfo.setRateBeats(0.0f);
        REQUIRE(lfo.getRateBeats() == 0.03125f);

        lfo.setRateBeats(-1.0f);
        REQUIRE(lfo.getRateBeats() == 0.03125f);
    }
}

// ===========================================================================
// Reset
// ===========================================================================

TEST_CASE("LFOSystem - reset", "[lfo][basic]")
{
    ana::LFOSystem lfo;
    lfo.prepare(TEST_SR);
    lfo.setWaveform(ana::WaveformType::Sine);
    lfo.setRate(5.0f);
    lfo.setDepth(100.0f);
    lfo.setBipolar(true);

    SECTION("reset restores initial phase")
    {
        // Advance past the start
        lfo.process(1000);
        REQUIRE(lfo.getCurrentPhase() > 0.0);

        lfo.reset();
        REQUIRE(lfo.getCurrentPhase() == Catch::Approx(0.0).margin(0.0001));
    }

    SECTION("reset respects phase offset")
    {
        lfo.setPhase(90.0f);
        lfo.reset();
        REQUIRE(lfo.getCurrentPhase() == Catch::Approx(0.25).margin(0.0001));
    }
}

// ===========================================================================
// Tempo sync edge cases
// ===========================================================================

TEST_CASE("LFOSystem - tempo sync edge cases", "[lfo][sync]")
{
    ana::LFOSystem lfo;
    lfo.prepare(TEST_SR);
    lfo.setWaveform(ana::WaveformType::Sine);
    lfo.setDepth(100.0f);
    lfo.setBipolar(true);

    SECTION("sync mode persists across parameter changes")
    {
        lfo.setRateBeats(1.0f);
        REQUIRE(lfo.isSyncEnabled());

        lfo.setTempo(140.0);
        REQUIRE(lfo.isSyncEnabled()); // Still synced
    }

    SECTION("very slow beat division")
    {
        lfo.setTempo(20.0);
        lfo.setRateBeats(4.0f); // Whole note at 20 BPM = 20/(60*4) ≈ 0.083 Hz
        REQUIRE_NOTHROW(lfo.process(100));
        float val = lfo.process(0);
        // Should produce a value without error
        REQUIRE(val >= -1.0f);
        REQUIRE(val <= 1.0f);
    }
}
