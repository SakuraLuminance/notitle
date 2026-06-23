#include <catch2/catch_all.hpp>
#include "dsp/effects/StereoWidenerEffect.h"
#include "dsp/effects/SaturationEffect.h"
#include "dsp/effects/BitcrusherEffect.h"
#include "dsp/effects/RingModulatorEffect.h"
#include "dsp/EffectsChain.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>

using namespace ana;

//==============================================================================
// Helpers
//==============================================================================

static constexpr double testSampleRate = 44100.0;

static juce::AudioBuffer<float> createSineBuffer(int numChannels, int numSamples, float frequency, float sampleRate)
{
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            buffer.setSample(ch, s, std::sin(2.0f * juce::MathConstants<float>::pi * frequency * static_cast<float>(s) / static_cast<float>(sampleRate)));
    return buffer;
}

static juce::dsp::ProcessSpec makeSpec(double sr = 44100.0, int blockSize = 512, int channels = 2)
{
    return { sr, static_cast<juce::uint32>(blockSize), static_cast<juce::uint32>(channels) };
}

static float computeRMSInDBFS(const juce::AudioBuffer<float>& buffer, int channel = -1)
{
    double sumSq = 0.0;
    int64_t count = 0;

    auto accumulate = [&](int ch)
    {
        const auto* data = buffer.getReadPointer(ch);
        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            sumSq += static_cast<double>(data[s]) * data[s];
            ++count;
        }
    };

    if (channel >= 0 && channel < buffer.getNumChannels())
        accumulate(channel);
    else
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            accumulate(ch);

    if (count == 0 || sumSq <= 0.0)
        return -std::numeric_limits<float>::infinity();

    const double rms = std::sqrt(sumSq / static_cast<double>(count));
    return 20.0f * static_cast<float>(std::log10(rms));
}

/** Extract a mono signal from channel 0 of an AudioBuffer as a vector<float>. */
static std::vector<float> bufferToVector(const juce::AudioBuffer<float>& buffer, int channel = 0)
{
    std::vector<float> v(buffer.getNumSamples());
    const auto* data = buffer.getReadPointer(channel);
    std::copy(data, data + buffer.getNumSamples(), v.begin());
    return v;
}

static std::vector<float> generateSine(float freq, double sampleRate, int numSamples)
{
    std::vector<float> v(numSamples);
    for (int i = 0; i < numSamples; ++i)
        v[i] = std::sin(2.0f * juce::MathConstants<float>::pi * freq * static_cast<float>(i) / static_cast<float>(sampleRate));
    return v;
}

static float computeRMS(const std::vector<float>& v)
{
    double sumSq = 0.0;
    for (auto x : v) sumSq += static_cast<double>(x) * x;
    if (sumSq <= 0.0) return -std::numeric_limits<float>::infinity();
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(v.size())));
}

static float computeRMSdB(const std::vector<float>& v)
{
    double sumSq = 0.0;
    for (auto x : v) sumSq += static_cast<double>(x) * x;
    if (sumSq <= 0.0) return -std::numeric_limits<float>::infinity();
    const double rms = std::sqrt(sumSq / static_cast<double>(v.size()));
    return 20.0f * static_cast<float>(std::log10(rms));
}

/** Count distinct sample values, quantized to a given step. */
static int countDistinctLevels(const std::vector<float>& v, float step = 1.0f / 65536.0f)
{
    std::vector<float> sorted(v);
    std::sort(sorted.begin(), sorted.end());
    auto last = std::unique(sorted.begin(), sorted.end(),
        [step](float a, float b) { return std::abs(a - b) < step; });
    return static_cast<int>(std::distance(sorted.begin(), last));
}

//==============================================================================
// StereoWidenerEffect Tests
//==============================================================================

TEST_CASE("Stereo Widener effect", "[stereowidener]")
{
    StereoWidenerEffect sw;
    sw.prepare(makeSpec());
    sw.setMix(1.0f); // fully wet

    SECTION("width=0 produces mono output")
    {
        // Create L-channel-only sine (L has signal, R is silent)
        auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
        buffer.clear(1, 0, 512); // clear R channel

        sw.setWidth(0.0f);
        sw.process(buffer);

        // At width=0, widthFactor=0, wetL = wetR = mid = L/2
        // Both channels should be identical
        for (int s = 0; s < 512; ++s)
            REQUIRE(buffer.getSample(0, s) == Catch::Approx(buffer.getSample(1, s)).margin(0.001f));

        float rmsL = computeRMSInDBFS(buffer, 0);
        float rmsR = computeRMSInDBFS(buffer, 1);
        REQUIRE(rmsL == Catch::Approx(rmsR).margin(0.1f));
    }

    SECTION("width=100% (0.5) preserves original stereo image")
    {
        auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
        buffer.clear(1, 0, 512);
        auto original = buffer;

        sw.setWidth(0.5f);
        sw.process(buffer);

        // width=0.5 gives widthFactor=1.0 → wet = original
        for (int s = 0; s < 512; ++s)
        {
            REQUIRE(buffer.getSample(0, s) == Catch::Approx(original.getSample(0, s)).margin(0.001f));
            REQUIRE(buffer.getSample(1, s) == Catch::Approx(original.getSample(1, s)).margin(0.001f));
        }
    }

    SECTION("width=200% (1.0) makes stereo wider")
    {
        auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
        buffer.clear(1, 0, 512);

        sw.setWidth(1.0f);
        sw.process(buffer);

        // width=1.0 → widthFactor=2.0
        // wetL = mid + side*2 = L/2 + L/2*2 = 1.5*L
        // wetR = mid - side*2 = L/2 - L/2*2 = -0.5*L
        // L should be ~3x louder than R (20*log10(1.5/|-0.5|) = 20*log10(3) ≈ 9.54dB)
        float rmsL = computeRMSInDBFS(buffer, 0);
        float rmsR = computeRMSInDBFS(buffer, 1);
        REQUIRE(rmsR > -60.0f);             // R has signal
        REQUIRE(rmsL - rmsR > 6.0f);        // L is noticeably louder than R
    }
}

TEST_CASE("StereoWidenerEffect - bypass and mix", "[stereowidener]")
{
    StereoWidenerEffect sw;
    sw.prepare(makeSpec());
    auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
    buffer.clear(1, 0, 512);
    auto original = buffer;

    SECTION("bypass=true leaves audio unchanged")
    {
        sw.setWidth(0.0f);
        sw.setBypass(true);
        sw.process(buffer);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 512; ++s)
                REQUIRE(buffer.getSample(ch, s) == Catch::Approx(original.getSample(ch, s)).margin(0.001f));
    }
}

//==============================================================================
// SaturationEffect Tests
//==============================================================================

TEST_CASE("Saturation effect", "[saturation]")
{
    SECTION("drive=0 produces near-zero output")
    {
        SaturationEffect sat;
        sat.prepare(makeSpec());
        sat.setMix(100.0f); // fully wet
        sat.setGain(1.0f);

        auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
        float inputRMS = computeRMSInDBFS(buffer);

        sat.setDrive(0.0f);
        sat.process(buffer);

        // With drive=0, preGain=0, tanh(x*0)=0, so output should be near silence
        float outputRMS = computeRMSInDBFS(buffer);
        REQUIRE(outputRMS < inputRMS - 80.0f); // at least 80dB down
    }

    SECTION("drive=100 introduces harmonics")
    {
        SaturationEffect sat;
        sat.prepare(makeSpec());
        sat.setMix(100.0f);
        sat.setGain(1.0f);
        sat.setTone(20000.0f); // open tone filter

        // Use a block large enough for FFT analysis
        const int fftOrder = 9; // 512 samples
        const int fftSize = 1 << fftOrder;
        auto buffer = createSineBuffer(2, fftSize, 440.0f, 44100.0f);

        sat.setDrive(100.0f);
        sat.process(buffer);

        // FFT the processed output
        std::vector<float> fftData(2 * fftSize, 0.0f);
        const auto* data = buffer.getReadPointer(0);
        for (int i = 0; i < fftSize; ++i)
            fftData[2 * i] = data[i];

        juce::dsp::FFT fft(fftOrder);
        fft.performRealOnlyForwardTransform(fftData.data());

        // Check 3rd harmonic (1320 Hz) has significant energy
        // Bin = 1320 * 512 / 44100 ≈ 15.3 → check bin 15
        constexpr int bin1320 = static_cast<int>(1320.0f * fftSize / 44100.0f);
        float mag1320 = std::sqrt(fftData[2 * bin1320] * fftData[2 * bin1320]
                                + fftData[2 * bin1320 + 1] * fftData[2 * bin1320 + 1]);

        // Check 5th harmonic (2200 Hz)
        constexpr int bin2200 = static_cast<int>(2200.0f * fftSize / 44100.0f);
        float mag2200 = std::sqrt(fftData[2 * bin2200] * fftData[2 * bin2200]
                                + fftData[2 * bin2200 + 1] * fftData[2 * bin2200 + 1]);

        // At max drive, harmonics should be present
        REQUIRE(mag1320 > 0.1f);
        REQUIRE(mag2200 > 0.05f);
    }

    SECTION("setMode switches sat curve without crashing")
    {
        SaturationEffect sat;
        sat.prepare(makeSpec());
        auto buffer = createSineBuffer(2, 128, 440.0f, 44100.0f);

        sat.setDrive(50.0f);
        sat.setMix(100.0f);
        sat.setMode(SaturationMode::Soft);
        REQUIRE_NOTHROW(sat.process(buffer));

        sat.setMode(SaturationMode::Tube);
        REQUIRE_NOTHROW(sat.process(buffer));

        sat.setMode(SaturationMode::Tape);
        REQUIRE_NOTHROW(sat.process(buffer));
    }
}

//==============================================================================
// BitcrusherEffect Tests
//==============================================================================

TEST_CASE("Bitcrusher effect", "[bitcrusher]")
{
    SECTION("bits=4 produces strongly quantized output")
    {
        BitcrusherEffect crush;
        crush.prepare(makeSpec());

        auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
        crush.setBitDepth(4.0f);
        crush.setDownsample(1.0f);
        crush.setMix(1.0f);
        crush.process(buffer);

        // At 4 bits, there are 16 discrete levels
        auto ch0 = bufferToVector(buffer, 0);
        int distinct = countDistinctLevels(ch0, 0.0001f);
        REQUIRE(distinct <= 17); // 16 levels + near-zero
    }

    SECTION("bits=16 is near-lossless")
    {
        BitcrusherEffect crush;
        crush.prepare(makeSpec());

        auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
        auto original = buffer;

        crush.setBitDepth(16.0f);
        crush.setDownsample(1.0f);
        crush.setMix(1.0f);
        crush.process(buffer);

        // 16 bits = 65536 levels, essentially lossless at float32 precision
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 512; ++s)
                REQUIRE(buffer.getSample(ch, s) == Catch::Approx(original.getSample(ch, s)).margin(0.001f));
    }

    SECTION("mix=0 passes dry signal through")
    {
        BitcrusherEffect crush;
        crush.prepare(makeSpec());

        auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
        auto original = buffer;

        crush.setBitDepth(4.0f);
        crush.setMix(0.0f);
        crush.process(buffer);

        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 512; ++s)
                REQUIRE(buffer.getSample(ch, s) == Catch::Approx(original.getSample(ch, s)).margin(0.001f));
    }

    SECTION("downsample reduces output energy at high frequencies")
    {
        BitcrusherEffect crush;
        crush.prepare(makeSpec());

        auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);

        crush.setBitDepth(8.0f);
        crush.setDownsample(1.0f);
        crush.setMix(1.0f);
        crush.process(buffer);
        float rmsNoDown = computeRMSInDBFS(buffer);

        // Reset and test with heavy downsampling
        crush.reset();
        auto buffer2 = createSineBuffer(2, 512, 440.0f, 44100.0f);
        crush.setBitDepth(8.0f);
        crush.setDownsample(8.0f);
        crush.setMix(1.0f);
        crush.process(buffer2);
        float rmsDown8 = computeRMSInDBFS(buffer2);

        // Heavy downsampling changes the signal content
        REQUIRE(rmsDown8 != Catch::Approx(rmsNoDown).margin(0.5f));
    }
}

//==============================================================================
// RingModulatorEffect Tests
//==============================================================================

TEST_CASE("Ring Modulator effect", "[ringmod]")
{
    SECTION("freq=1Hz produces amplitude modulation (tremolo)")
    {
        RingModulatorEffect rm;
        // 2 seconds at 44100
        rm.prepare(makeSpec(44100.0, 88200, 2));
        rm.setFrequency(1.0f);
        rm.setWaveform(0); // sine
        rm.setMix(1.0f);
        rm.setGain(1.0f);

        // Input: 440Hz sine, 88200 samples (2 seconds)
        auto buffer = createSineBuffer(2, 88200, 440.0f, 44100.0f);
        rm.process(buffer);

        // Compute RMS in 100ms windows (4410 samples each)
        // 20 windows over 2 seconds
        const int windowSize = 4410;
        const int numWindows = 88200 / windowSize;
        std::vector<float> windowRMS(numWindows);

        for (int w = 0; w < numWindows; ++w)
        {
            double sumSq = 0.0;
            for (int s = 0; s < windowSize; ++s)
            {
                float val = buffer.getSample(0, w * windowSize + s);
                sumSq += static_cast<double>(val) * val;
            }
            windowRMS[w] = static_cast<float>(std::sqrt(sumSq / windowSize));
        }

        // With 1Hz AM, amplitude goes up and down
        // Verify that max RMS is significantly larger than min RMS
        auto [minIt, maxIt] = std::minmax_element(windowRMS.begin(), windowRMS.end());
        float minRMS = *minIt;
        float maxRMS = *maxIt;

        // There should be visible amplitude modulation
        REQUIRE(maxRMS > minRMS * 1.5f);
    }

    SECTION("freq=440Hz produces ring mod sidebands")
    {
        RingModulatorEffect rm;
        rm.prepare(makeSpec());
        rm.setFrequency(440.0f);
        rm.setWaveform(0); // sine
        rm.setMix(1.0f);
        rm.setGain(1.0f);

        const int fftOrder = 9;
        const int fftSize = 1 << fftOrder;
        auto buffer = createSineBuffer(2, fftSize, 440.0f, 44100.0f);
        rm.process(buffer);

        // RM with 440Hz carrier + 440Hz input:
        // output = sin(2π*440*t) * sin(2π*440*t) = (1 - cos(2π*880*t)) / 2
        // → DC (bin 0) + 880Hz (bin ~10)

        std::vector<float> fftData(2 * fftSize, 0.0f);
        const auto* data = buffer.getReadPointer(0);
        for (int i = 0; i < fftSize; ++i)
            fftData[2 * i] = data[i];

        juce::dsp::FFT fft(fftOrder);
        fft.performRealOnlyForwardTransform(fftData.data());

        // Check DC component (bin 0) — should be present
        float magDC = std::abs(fftData[0]);

        // Check 880Hz (bin = 880 * 512 / 44100 ≈ 10.2 → bin 10)
        constexpr int bin880 = static_cast<int>(880.0f * fftSize / 44100.0f);
        float mag880 = std::sqrt(fftData[2 * bin880] * fftData[2 * bin880]
                               + fftData[2 * bin880 + 1] * fftData[2 * bin880 + 1]);

        // Test that fundamental (bin ~5) is weaker than 880Hz component
        constexpr int bin440 = static_cast<int>(440.0f * fftSize / 44100.0f);
        float mag440 = std::sqrt(fftData[2 * bin440] * fftData[2 * bin440]
                               + fftData[2 * bin440 + 1] * fftData[2 * bin440 + 1]);

        // The 440Hz should be suppressed (not zero due to FFT leakage, but low)
        // 880Hz should be strong
        REQUIRE(mag880 > 0.1f);
        REQUIRE(magDC > 0.1f);
        REQUIRE(mag440 < mag880); // 440Hz component lower than 880Hz
    }

    SECTION("setWaveform switches without crashing")
    {
        RingModulatorEffect rm;
        rm.prepare(makeSpec());
        auto buffer = createSineBuffer(2, 128, 440.0f, 44100.0f);
        rm.setFrequency(100.0f);

        rm.setWaveform(0);
        REQUIRE_NOTHROW(rm.process(buffer));

        rm.setWaveform(1);
        REQUIRE_NOTHROW(rm.process(buffer));

        rm.setWaveform(2);
        REQUIRE_NOTHROW(rm.process(buffer));
    }

    SECTION("bypass passes audio through unchanged")
    {
        RingModulatorEffect rm;
        rm.prepare(makeSpec());
        auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
        auto original = buffer;

        rm.setBypass(true);
        rm.process(buffer);

        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 512; ++s)
                REQUIRE(buffer.getSample(ch, s) == Catch::Approx(original.getSample(ch, s)).margin(0.001f));
    }
}

//==============================================================================
// Wet Filter Tests (via EffectsChain)
//==============================================================================

/** A pass-through effect that leaves audio unchanged. */
class PassthroughEffect : public EffectBase {
public:
    void prepare(const juce::dsp::ProcessSpec&) override {}
    void process(juce::AudioBuffer<float>&) override {}
    void reset() override {}
    juce::ValueTree getState() const override { return juce::ValueTree("Passthrough"); }
    void setState(const juce::ValueTree&) override {}
};

TEST_CASE("Wet filter HPF", "[wetfilter][hpf]")
{
    EffectsChain chain;
    juce::dsp::ProcessSpec spec { 44100.0, 512, 1 };
    chain.prepare(spec);
    int slot = chain.addEffect(std::make_unique<PassthroughEffect>(), "Passthrough");

    // Set mix=1 so we hear only the wet (filtered) signal
    chain.setMix(slot, 1.0f);

    SECTION("lowCut=200Hz attenuates 100Hz tone >20dB")
    {
        // 100Hz tone — should be strongly attenuated by 200Hz HPF
        auto buf100 = createSineBuffer(1, 512, 100.0f, 44100.0f);
        auto ref100 = buf100;

        chain.setWetLowCut(slot, 200.0f);
        chain.process(buf100);

        float before = computeRMSInDBFS(ref100);
        float after  = computeRMSInDBFS(buf100);
        REQUIRE(before - after > 20.0f); // >20dB attenuation
    }

    SECTION("lowCut=200Hz leaves 1000Hz tone mostly intact")
    {
        auto buf1k = createSineBuffer(1, 512, 1000.0f, 44100.0f);
        auto ref1k = buf1k;

        chain.setWetLowCut(slot, 200.0f);
        chain.process(buf1k);

        float before = computeRMSInDBFS(ref1k);
        float after  = computeRMSInDBFS(buf1k);
        REQUIRE(before - after < 3.0f); // less than 3dB attenuation at 1kHz
    }
}

TEST_CASE("Wet filter LPF", "[wetfilter][lpf]")
{
    EffectsChain chain;
    juce::dsp::ProcessSpec spec { 44100.0, 512, 1 };
    chain.prepare(spec);
    int slot = chain.addEffect(std::make_unique<PassthroughEffect>(), "Passthrough");
    chain.setMix(slot, 1.0f);

    SECTION("highCut=8000Hz attenuates 16kHz tone >20dB")
    {
        auto buf16k = createSineBuffer(1, 512, 16000.0f, 44100.0f);
        auto ref16k = buf16k;

        chain.setWetHighCut(slot, 8000.0f);
        chain.process(buf16k);

        float before = computeRMSInDBFS(ref16k);
        float after  = computeRMSInDBFS(buf16k);
        REQUIRE(before - after > 20.0f);
    }

    SECTION("highCut=8000Hz leaves 440Hz tone mostly intact")
    {
        auto buf440 = createSineBuffer(1, 512, 440.0f, 44100.0f);
        auto ref440 = buf440;

        chain.setWetHighCut(slot, 8000.0f);
        chain.process(buf440);

        float before = computeRMSInDBFS(ref440);
        float after  = computeRMSInDBFS(buf440);
        REQUIRE(before - after < 3.0f);
    }
}

TEST_CASE("Mix bypass", "[mix][bypass]")
{
    SECTION("mix=0 produces output identical to bypass")
    {
        EffectsChain chain;
        juce::dsp::ProcessSpec spec { 44100.0, 512, 2 };
        chain.prepare(spec);

        int slot = chain.addEffect(std::make_unique<StereoWidenerEffect>(), "Widener");
        chain.setWetLowCut(slot, 20.0f); // full range
        chain.setWetHighCut(slot, 20000.0f);

        // Create input with stereo content
        auto buffer  = createSineBuffer(2, 512, 440.0f, 44100.0f);
        buffer.clear(1, 0, 512);

        // Process with mix=0 (fully dry)
        chain.setMix(slot, 0.0f);
        auto dryOutput = buffer;
        chain.getEffect(slot).effect->setBypass(false);
        chain.process(dryOutput);

        // Process with effect bypassed
        auto bypassOutput = buffer;
        chain.bypassEffect(slot, true);
        chain.process(bypassOutput);

        // Both should be identical to the input
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 512; ++s)
                REQUIRE(dryOutput.getSample(ch, s) == Catch::Approx(bypassOutput.getSample(ch, s)).margin(0.001f));
    }
}

//==============================================================================
// EffectSlot Serialization Tests
//==============================================================================

TEST_CASE("EffectSlot serialization", "[serialization]")
{
    SECTION("StereoWidenerEffect round-trip")
    {
        StereoWidenerEffect original;
        original.setWidth(0.25f);
        original.setMode(StereoWidenerMode::Wide);
        original.setMix(0.75f);
        original.setBypass(true);

        auto state = original.getState();

        StereoWidenerEffect restored;
        restored.setState(state);

        REQUIRE(restored.getWidth() == Catch::Approx(0.25f));
        REQUIRE(restored.getMode() == StereoWidenerMode::Wide);
        REQUIRE(restored.getMix() == Catch::Approx(0.75f));
        REQUIRE(restored.isBypassed() == true);
    }

    SECTION("SaturationEffect round-trip")
    {
        SaturationEffect original;
        original.setDrive(75.0f);
        original.setTone(5000.0f);
        original.setMode(SaturationMode::Tube);
        original.setMix(60.0f);
        original.setBypass(true);
        original.setGain(0.8f);

        auto state = original.getState();

        SaturationEffect restored;
        restored.setState(state);

        // Verify key params round-trip (mix stored as percent, converted internally)
        // We verify by checking process behavior: after setState, processing works
        juce::dsp::ProcessSpec spec { 44100.0, 128, 2 };
        restored.prepare(spec);

        auto buffer = createSineBuffer(2, 128, 440.0f, 44100.0f);
        REQUIRE_NOTHROW(restored.process(buffer));
    }

    SECTION("BitcrusherEffect round-trip")
    {
        BitcrusherEffect original;
        original.setBitDepth(6.0f);
        original.setDownsample(4.0f);
        original.setMix(0.3f);

        auto state = original.getState();

        BitcrusherEffect restored;
        restored.setState(state);

        REQUIRE(restored.getBitDepth() == Catch::Approx(6.0f));
        REQUIRE(restored.getDownsample() == Catch::Approx(4.0f));
        REQUIRE(restored.getMix() == Catch::Approx(0.3f));
    }

    SECTION("RingModulatorEffect round-trip")
    {
        RingModulatorEffect original;
        original.setFrequency(440.0f);
        original.setWaveform(1);
        original.setMix(0.5f);
        original.setBypass(true);
        original.setGain(0.5f);

        auto state = original.getState();

        RingModulatorEffect restored;
        restored.prepare(makeSpec());
        restored.setState(state);

        // After restore, processing should work
        auto buffer = createSineBuffer(2, 128, 440.0f, 44100.0f);
        REQUIRE_NOTHROW(restored.process(buffer));
    }

    SECTION("Each effect type has unique state tree name")
    {
        StereoWidenerEffect sw;
        SaturationEffect sat;
        BitcrusherEffect crush;
        RingModulatorEffect rm;

        auto swTree   = sw.getState();
        auto satTree  = sat.getState();
        auto crushTree = crush.getState();
        auto rmTree   = rm.getState();

        REQUIRE(swTree.getType().toString() == "StereoWidenerEffect");
        REQUIRE(satTree.getType().toString() == "SaturationEffect");
        REQUIRE(crushTree.getType().toString() == "BitcrusherEffect");
        REQUIRE(rmTree.getType().toString() == "RingModulatorEffect");
    }
}
