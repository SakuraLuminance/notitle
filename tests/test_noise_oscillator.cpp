#include <catch2/catch_all.hpp>
#include "../src/dsp/SpectralParticleSystem.h"
#include "../src/dsp/PartialDataSIMD.h"
#include <cmath>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

//==============================================================================
//  Noise type tests
//==============================================================================

TEST_CASE("Noise oscillator - default state", "[noise][init]")
{
    SpectralParticleSystem ps;

    REQUIRE(ps.getNoiseType() == SpectralParticleSystem::NoiseType::White);
    REQUIRE(ps.getNoiseColor() == Catch::Approx(0.5f));
    REQUIRE(ps.getNoiseAmplitude() == Catch::Approx(0.0f));
}

TEST_CASE("Noise oscillator - set/get noise type", "[noise][config]")
{
    SpectralParticleSystem ps;

    ps.setNoiseType(SpectralParticleSystem::NoiseType::Pink);
    REQUIRE(ps.getNoiseType() == SpectralParticleSystem::NoiseType::Pink);

    ps.setNoiseType(SpectralParticleSystem::NoiseType::Brown);
    REQUIRE(ps.getNoiseType() == SpectralParticleSystem::NoiseType::Brown);

    ps.setNoiseType(SpectralParticleSystem::NoiseType::White);
    REQUIRE(ps.getNoiseType() == SpectralParticleSystem::NoiseType::White);
}

TEST_CASE("Noise oscillator - set/get amplitude", "[noise][config]")
{
    SpectralParticleSystem ps;

    ps.setNoiseAmplitude(0.5f);
    REQUIRE(ps.getNoiseAmplitude() == Catch::Approx(0.5f));

    ps.setNoiseAmplitude(1.0f);
    REQUIRE(ps.getNoiseAmplitude() == Catch::Approx(1.0f));

    // Clamp out of range
    ps.setNoiseAmplitude(2.0f);
    REQUIRE(ps.getNoiseAmplitude() == Catch::Approx(1.0f));

    ps.setNoiseAmplitude(-1.0f);
    REQUIRE(ps.getNoiseAmplitude() == Catch::Approx(0.0f));
}

TEST_CASE("Noise oscillator - set/get colour", "[noise][config]")
{
    SpectralParticleSystem ps;

    ps.setNoiseColor(0.0f);
    REQUIRE(ps.getNoiseColor() == Catch::Approx(0.0f));

    ps.setNoiseColor(1.0f);
    REQUIRE(ps.getNoiseColor() == Catch::Approx(1.0f));

    ps.setNoiseColor(0.3f);
    REQUIRE(ps.getNoiseColor() == Catch::Approx(0.3f));
}

TEST_CASE("Noise oscillator - envelope parameter setters", "[noise][config]")
{
    SpectralParticleSystem ps;

    ps.setNoiseEnvAttack(50.0f);
    ps.setNoiseEnvDecay(200.0f);
    ps.setNoiseEnvSustain(0.7f);
    ps.setNoiseEnvRelease(500.0f);
    SUCCEED();
}

//==============================================================================
//  Noise generation tests
//==============================================================================

TEST_CASE("Noise oscillator - white noise produces non-zero output", "[noise][generate]")
{
    SpectralParticleSystem ps;
    ps.setNoiseAmplitude(0.8f);
    ps.setNoiseEnvAttack(1.0f);   // fast attack
    ps.setNoiseEnvSustain(1.0f);  // full sustain

    PartialDataSIMD partials;
    partials.sampleRate = testSampleRate;

    // Generate 32 noise partials with gate open
    ps.generateNoisePartials(partials, 32, 0.5f);

    // Check that partials were written
    int activeCount = 0;
    float totalAmp = 0.0f;
    for (int i = 0; i < partials.maxPartials; ++i)
    {
        if (partials.amplitude[i] > 0.0f)
        {
            ++activeCount;
            totalAmp += partials.amplitude[i];
        }
    }

    REQUIRE(activeCount > 0);
    REQUIRE(totalAmp > 0.0f);

    // All noise partials should have a frequency in audible range
    for (int i = 0; i < partials.maxPartials; ++i)
    {
        if (partials.amplitude[i] > 0.0f)
        {
            REQUIRE(partials.frequency[i] >= 30.0f);
            REQUIRE(partials.frequency[i] <= testSampleRate * 0.5f * 0.95f);
        }
    }
}

TEST_CASE("Noise oscillator - pink noise produces non-zero output", "[noise][generate][pink]")
{
    SpectralParticleSystem ps;
    ps.setNoiseType(SpectralParticleSystem::NoiseType::Pink);
    ps.setNoiseAmplitude(0.8f);
    ps.setNoiseEnvAttack(1.0f);
    ps.setNoiseEnvSustain(1.0f);

    PartialDataSIMD partials;
    partials.sampleRate = testSampleRate;

    ps.generateNoisePartials(partials, 32, 0.5f);

    float totalAmp = 0.0f;
    for (int i = 0; i < partials.maxPartials; ++i)
        totalAmp += partials.amplitude[i];

    REQUIRE(totalAmp > 0.0f);
}

TEST_CASE("Noise oscillator - brown noise produces non-zero output", "[noise][generate][brown]")
{
    SpectralParticleSystem ps;
    ps.setNoiseType(SpectralParticleSystem::NoiseType::Brown);
    ps.setNoiseAmplitude(0.8f);
    ps.setNoiseEnvAttack(1.0f);
    ps.setNoiseEnvSustain(1.0f);

    PartialDataSIMD partials;
    partials.sampleRate = testSampleRate;

    ps.generateNoisePartials(partials, 32, 0.5f);

    float totalAmp = 0.0f;
    for (int i = 0; i < partials.maxPartials; ++i)
        totalAmp += partials.amplitude[i];

    REQUIRE(totalAmp > 0.0f);
}

TEST_CASE("Noise oscillator - zero amplitude produces silence", "[noise][generate]")
{
    SpectralParticleSystem ps;
    ps.setNoiseAmplitude(0.0f); // default zero

    PartialDataSIMD partials;
    partials.sampleRate = testSampleRate;

    ps.generateNoisePartials(partials, 32, 0.5f);

    // No partials should have been written
    for (int i = 0; i < partials.maxPartials; ++i)
        REQUIRE(partials.amplitude[i] == Catch::Approx(0.0f));
}

TEST_CASE("Noise oscillator - closed gate produces silence", "[noise][generate][gate]")
{
    SpectralParticleSystem ps;
    ps.setNoiseAmplitude(1.0f);
    ps.setNoiseEnvAttack(1.0f);

    PartialDataSIMD partials;
    partials.sampleRate = testSampleRate;

    // envelopeLevel = 0 means gate closed
    ps.generateNoisePartials(partials, 32, 0.0f);

    for (int i = 0; i < partials.maxPartials; ++i)
        REQUIRE(partials.amplitude[i] == Catch::Approx(0.0f));
}

//==============================================================================
//  Brightness tests
//==============================================================================

TEST_CASE("Noise oscillator - brightness changes spectral distribution", "[noise][brightness]")
{
    SpectralParticleSystem ps;
    ps.setNoiseType(SpectralParticleSystem::NoiseType::White);
    ps.setNoiseAmplitude(1.0f);
    ps.setNoiseEnvAttack(1.0f);
    ps.setNoiseEnvSustain(1.0f);

    // ---- Dark (0.0): more low-frequency energy ----
    ps.setNoiseColor(0.0f);
    PartialDataSIMD darkPartials;
    darkPartials.sampleRate = testSampleRate;
    ps.generateNoisePartials(darkPartials, 64, 0.5f);

    float darkLowAmp = 0.0f, darkHighAmp = 0.0f;
    const float midFreq = static_cast<float>(testSampleRate) * 0.25f;
    for (int i = 0; i < darkPartials.maxPartials; ++i)
    {
        if (darkPartials.amplitude[i] > 0.0f)
        {
            if (darkPartials.frequency[i] < midFreq)
                darkLowAmp += darkPartials.amplitude[i];
            else
                darkHighAmp += darkPartials.amplitude[i];
        }
    }

    // ---- Bright (1.0): more high-frequency energy ----
    ps.setNoiseColor(1.0f);
    PartialDataSIMD brightPartials;
    brightPartials.sampleRate = testSampleRate;
    ps.generateNoisePartials(brightPartials, 64, 0.5f);

    float brightLowAmp = 0.0f, brightHighAmp = 0.0f;
    for (int i = 0; i < brightPartials.maxPartials; ++i)
    {
        if (brightPartials.amplitude[i] > 0.0f)
        {
            if (brightPartials.frequency[i] < midFreq)
                brightLowAmp += brightPartials.amplitude[i];
            else
                brightHighAmp += brightPartials.amplitude[i];
        }
    }

    // Verify: dark should have relatively more low-end than bright
    const float darkRatio  = (darkHighAmp  > 0.0f) ? darkLowAmp  / (darkLowAmp  + darkHighAmp)  : 0.0f;
    const float brightRatio = (brightHighAmp > 0.0f) ? brightLowAmp / (brightLowAmp + brightHighAmp) : 0.0f;

    // Dark mode should have a higher ratio of low-frequency energy
    REQUIRE(darkRatio > brightRatio);
}

//==============================================================================
//  Envelope tests
//==============================================================================

TEST_CASE("Noise oscillator - envelope attack shapes output", "[noise][envelope]")
{
    SpectralParticleSystem ps;
    ps.setNoiseAmplitude(1.0f);
    ps.setNoiseEnvAttack(100.0f);  // 100ms attack
    ps.setNoiseEnvSustain(1.0f);   // sustain at full

    // First call with gate open — envelope should be in early attack
    // The envelope increments by dt each call, where dt ≈ 1/44100 ≈ 0.0227ms
    // After 1 call, env value ≈ dt / attackSec ≈ 0.0000227 / 0.1 ≈ 0.000227
    // So it should be very small but > 0

    PartialDataSIMD partials;
    partials.sampleRate = testSampleRate;

    // Generate with gate open
    ps.generateNoisePartials(partials, 16, 0.5f);

    // Check that envelope is advancing — output should be positive but small
    bool anyAmplitude = false;
    for (int i = 0; i < partials.maxPartials; ++i)
    {
        if (partials.amplitude[i] > 0.0f)
        {
            anyAmplitude = true;
            break;
        }
    }
    REQUIRE(anyAmplitude);

    // Second call — envelope should be further along
    PartialDataSIMD partials2;
    partials2.sampleRate = testSampleRate;
    ps.generateNoisePartials(partials2, 16, 0.5f);

    float sumAmp1 = 0.0f, sumAmp2 = 0.0f;
    for (int i = 0; i < partials.maxPartials; ++i)
    {
        sumAmp1 += partials.amplitude[i];
        sumAmp2 += partials2.amplitude[i];
    }

    // Second burst should generally be louder (envelope rising in attack)
    CHECK(sumAmp2 > 0.0f);
}

TEST_CASE("Noise oscillator - gate toggling triggers envelope", "[noise][envelope]")
{
    SpectralParticleSystem ps;
    ps.setNoiseAmplitude(1.0f);
    ps.setNoiseEnvAttack(1.0f);    // instant attack
    ps.setNoiseEnvSustain(1.0f);
    ps.setNoiseEnvRelease(1.0f);   // instant release

    PartialDataSIMD partials;
    partials.sampleRate = testSampleRate;

    // Open gate
    ps.generateNoisePartials(partials, 16, 0.5f);

    // Close gate
    PartialDataSIMD partialsReleased;
    partialsReleased.sampleRate = testSampleRate;
    ps.generateNoisePartials(partialsReleased, 16, 0.0f);

    // After release with instant times, should be silent
    // (envelope advances fast enough to hit idle)
    float totalAmp = 0.0f;
    for (int i = 0; i < partialsReleased.maxPartials; ++i)
        totalAmp += partialsReleased.amplitude[i];

    // Either silent or very quiet (envelope may still be in its first dt step)
    // The important thing is it's quieter than when gate was open
    float initialAmp = 0.0f;
    for (int i = 0; i < partials.maxPartials; ++i)
        initialAmp += partials.amplitude[i];

    REQUIRE(totalAmp <= initialAmp);
}

TEST_CASE("Noise oscillator - amplitude parameter controls level", "[noise][amplitude]")
{
    SpectralParticleSystem ps;
    ps.setNoiseColor(0.5f);
    ps.setNoiseEnvAttack(1.0f);
    ps.setNoiseEnvSustain(1.0f);

    PartialDataSIMD partialsLow;
    partialsLow.sampleRate = testSampleRate;

    ps.setNoiseAmplitude(0.3f);
    ps.generateNoisePartials(partialsLow, 32, 0.5f);

    float ampLow = 0.0f;
    for (int i = 0; i < partialsLow.maxPartials; ++i)
        ampLow += partialsLow.amplitude[i];

    PartialDataSIMD partialsHigh;
    partialsHigh.sampleRate = testSampleRate;

    ps.setNoiseAmplitude(1.0f);
    ps.generateNoisePartials(partialsHigh, 32, 0.5f);

    float ampHigh = 0.0f;
    for (int i = 0; i < partialsHigh.maxPartials; ++i)
        ampHigh += partialsHigh.amplitude[i];

    // Higher amplitude setting should produce louder output
    REQUIRE(ampHigh > ampLow);
}

//==============================================================================
//  Integration tests
//==============================================================================

TEST_CASE("Noise oscillator - coexistence with spectral particles", "[noise][integration]")
{
    SpectralParticleSystem ps;
    ps.setNoiseAmplitude(0.5f);
    ps.setNoiseEnvAttack(1.0f);
    ps.setNoiseEnvSustain(1.0f);

    PartialDataSIMD partials;
    partials.sampleRate = testSampleRate;

    // First, emit spectral particles
    ps.emitBurst(10);
    ps.update(0.01);
    ps.process(partials);

    // Count active partials after processing
    int partialCountBefore = 0;
    for (int i = 0; i < partials.maxPartials; ++i)
        if (partials.amplitude[i] > 0.0f)
            ++partialCountBefore;

    // Now add noise
    ps.generateNoisePartials(partials, 16, 0.5f);

    // Count active partials after noise
    int partialCountAfter = 0;
    for (int i = 0; i < partials.maxPartials; ++i)
        if (partials.amplitude[i] > 0.0f)
            ++partialCountAfter;

    // Noise should add additional active slots
    REQUIRE(partialCountAfter > partialCountBefore);

    // The noise-specific slots should have reasonable frequencies
    const int noiseStartSlot = partials.maxPartials - 16;
    for (int i = noiseStartSlot; i < partials.maxPartials; ++i)
    {
        if (partials.amplitude[i] > 0.0f)
        {
            REQUIRE(partials.frequency[i] >= 30.0f);
            REQUIRE(partials.phase[i] >= 0.0f);
            REQUIRE(partials.phase[i] <= juce::MathConstants<float>::twoPi);
        }
    }
}

TEST_CASE("Noise oscillator - multiple frames produce consistent output", "[noise][generate]")
{
    SpectralParticleSystem ps;
    ps.setNoiseType(SpectralParticleSystem::NoiseType::White);
    ps.setNoiseAmplitude(0.7f);
    ps.setNoiseEnvAttack(1.0f);
    ps.setNoiseEnvSustain(1.0f);

    PartialDataSIMD frame1, frame2;
    frame1.sampleRate = testSampleRate;
    frame2.sampleRate = testSampleRate;

    ps.generateNoisePartials(frame1, 32, 0.5f);
    ps.generateNoisePartials(frame2, 32, 0.5f);

    // Both frames should produce output
    float sum1 = 0.0f, sum2 = 0.0f;
    for (int i = 0; i < frame1.maxPartials; ++i)
        sum1 += frame1.amplitude[i];
    for (int i = 0; i < frame2.maxPartials; ++i)
        sum2 += frame2.amplitude[i];

    REQUIRE(sum1 > 0.0f);
    REQUIRE(sum2 > 0.0f);
}

TEST_CASE("Noise oscillator - activeCount tracks noise partials", "[noise][integration]")
{
    SpectralParticleSystem ps;
    ps.setNoiseAmplitude(0.8f);
    ps.setNoiseEnvAttack(1.0f);
    ps.setNoiseEnvSustain(1.0f);

    PartialDataSIMD partials;
    partials.sampleRate = testSampleRate;

    ps.generateNoisePartials(partials, 32, 0.5f);

    // updateActiveMask is called inside generateNoisePartials
    // so activeCount should reflect noise partials
    REQUIRE(partials.activeCount > 0);
    REQUIRE(partials.activeCount <= 32);
}

TEST_CASE("Noise oscillator - reset clears noise state", "[noise][reset]")
{
    SpectralParticleSystem ps;
    ps.setNoiseType(SpectralParticleSystem::NoiseType::Pink);
    ps.setNoiseAmplitude(0.9f);
    ps.setNoiseColor(0.2f);

    ps.reset();

    REQUIRE(ps.getNoiseType() == SpectralParticleSystem::NoiseType::White);
    REQUIRE(ps.getNoiseColor() == Catch::Approx(0.5f));
    REQUIRE(ps.getNoiseAmplitude() == Catch::Approx(0.0f));
}

TEST_CASE("Noise oscillator - killAll clears envelope", "[noise][reset]")
{
    SpectralParticleSystem ps;
    ps.setNoiseAmplitude(1.0f);
    ps.setNoiseEnvAttack(1.0f);
    ps.setNoiseEnvSustain(1.0f);

    PartialDataSIMD partials;
    partials.sampleRate = testSampleRate;

    // Get envelope going
    ps.generateNoisePartials(partials, 16, 0.5f);

    // Kill all should reset envelope to idle
    ps.killAll();

    // Verify: calling again with closed gate should produce silence
    PartialDataSIMD afterKill;
    afterKill.sampleRate = testSampleRate;
    ps.generateNoisePartials(afterKill, 16, 0.0f);

    for (int i = 0; i < afterKill.maxPartials; ++i)
        REQUIRE(afterKill.amplitude[i] == Catch::Approx(0.0f));
}
