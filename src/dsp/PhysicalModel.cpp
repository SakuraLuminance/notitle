#include "PhysicalModel.h"

#include <cmath>
#include <random>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ana {

//==============================================================================
// PhysicalModel
//==============================================================================

PhysicalModel::PhysicalModel()
{
    reset();
}

//==============================================================================
// Parameter setters
//==============================================================================

void PhysicalModel::setModelType(ModelType type)  { modelType_ = type; }
void PhysicalModel::setExcitation(Excitation excitation) { excitation_ = excitation; }

void PhysicalModel::setMaterial(float stiffness)
{
    stiffness_ = juce::jlimit(0.0f, 1.0f, stiffness);
}

void PhysicalModel::setDamping(float damping)
{
    damping_ = juce::jlimit(0.0f, 1.0f, damping);
}

void PhysicalModel::setTension(float tension)
{
    tension_ = juce::jlimit(0.0f, 1.0f, tension);
}

void PhysicalModel::setInharmonicity(float amount)
{
    inharmonicity_ = juce::jlimit(0.0f, 1.0f, amount);
}

void PhysicalModel::setDecay(float decay)
{
    decay_ = juce::jlimit(0.0f, 1.0f, decay);
}

void PhysicalModel::setPosition(float position)
{
    position_ = juce::jlimit(0.0f, 1.0f, position);
}

void PhysicalModel::setFrequency(float freqHz)
{
    fundamentalFreq_ = juce::jlimit(20.0f, 20000.0f, freqHz);
}

void PhysicalModel::setSampleRate(double sr)
{
    sampleRate_ = juce::jlimit(8000.0, 192000.0, sr);
}

//==============================================================================
// Waveguide implementation
//==============================================================================

void PhysicalModel::Waveguide::init(float freq, double sampleRate)
{
    const int length = std::max(2, static_cast<int>(std::round(sampleRate / freq)));
    delayLine.resize(length, 0.0f);
    writePos = 0;
    lowpassState = 0.0f;
}

float PhysicalModel::Waveguide::process(float input, float damping, float stiffness, float decay)
{
    if (delayLine.empty())
        return 0.0f;

    const int len = static_cast<int>(delayLine.size());

    // Read current sample from delay line
    const float out = delayLine[writePos];

    // One-pole lowpass coefficient: stiffness makes it brighter (lower coefficient),
    // damping makes it duller (higher coefficient)
    const float lpCoeff = juce::jlimit(0.05f, 0.95f,
                                       0.5f + damping * 0.35f - stiffness * 0.25f);

    // Apply one-pole lowpass to the output
    lowpassState = out * (1.0f - lpCoeff) + lowpassState * lpCoeff;

    // Feedback gain from decay parameter: 0.85 (short) to 0.999 (long sustain)
    const float feedback = 0.85f + (1.0f - decay) * 0.149f;

    // Write feedback signal back into delay line (Karplus-Strong recursion)
    delayLine[writePos] = input + lowpassState * feedback;

    // Advance write position
    writePos = (writePos + 1) % len;

    return out;
}

//==============================================================================
// Mode resonator implementation
//==============================================================================

float PhysicalModel::Mode::process(float input, double sampleRate)
{
    if (frequency <= 0.0f || frequency >= static_cast<float>(sampleRate) * 0.49f)
        return 0.0f;

    // Compute 2nd-order IIR resonator coefficients
    // R controls resonance bandwidth: lower decay = narrower bandwidth (more ring)
    const float bw = 5.0f + (1.0f - decay) * 500.0f;  // 5 Hz to 505 Hz bandwidth
    const float R = std::exp(static_cast<float>(-M_PI * bw / sampleRate));
    const float twoPiFreq = 2.0f * static_cast<float>(M_PI) * frequency / static_cast<float>(sampleRate);
    const float a1 = -2.0f * R * std::cos(twoPiFreq);
    const float a2 = R * R;

    // Gain normalisation so resonance peaks at unity at the resonant frequency
    const float b0 = (1.0f - R) * std::sin(twoPiFreq);

    // Process 2-pole resonator: y[n] = b0 * x[n] - a1 * y[n-1] - a2 * y[n-2]
    const float y = input * b0 * amplitude - a1 * state[0] - a2 * state[1];

    // Update state
    state[1] = state[0];
    state[0] = y;

    return y;
}

//==============================================================================
// Excitation filling
//==============================================================================

void PhysicalModel::fillExcitation(std::vector<float>& delayLine, std::minstd_rand& rng)
{
    const int len = static_cast<int>(delayLine.size());

    switch (excitation_)
    {
        case Excitation::Noise:
        {
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            for (int i = 0; i < len; ++i)
                delayLine[i] = dist(rng);
            break;
        }

        case Excitation::Impulse:
        {
            for (int i = 0; i < len; ++i)
                delayLine[i] = (i == 0) ? 1.0f : 0.0f;
            break;
        }

        case Excitation::Sweep:
        {
            // Short frequency sweep from 2*f0 down to f0
            for (int i = 0; i < len; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(len);
                const float sweepFreq = fundamentalFreq_ * (2.0f - t);
                delayLine[i] = std::sin(2.0f * static_cast<float>(M_PI) * sweepFreq * t
                                        / static_cast<float>(sampleRate_));
            }
            break;
        }

        case Excitation::Sample:
        {
            std::fill(delayLine.begin(), delayLine.end(), 0.0f);
            break;
        }
    }
}

//==============================================================================
// Amplitude computation
//==============================================================================

float PhysicalModel::computeAmplitude(int partialIndex) const
{
    const float n = static_cast<float>(partialIndex + 1);
    float amp = 0.0f;

    switch (modelType_)
    {
        case ModelType::String:
        {
            // Bowed string: 1/f² rolloff, stiffness adds brightness
            amp = 1.0f / (n * n);
            amp *= (1.0f + stiffness_ * 0.5f * (1.0f - 1.0f / n));
            break;
        }

        case ModelType::Pluck:
        {
            // Plucked string: 1/f² rolloff with position-dependent suppression.
            // Harmonics with a node at the pluck position are attenuated.
            amp = 1.0f / (n * n);
            const float posFactor = std::max(0.005f,
                std::abs(std::sin(n * static_cast<float>(M_PI) * position_)));
            amp *= posFactor;
            // Tension adds brightness
            amp *= (0.7f + tension_ * 0.3f);
            break;
        }

        case ModelType::Blow:
        {
            // Reed-like: odd harmonics dominate, even harmonics are suppressed
            if (static_cast<int>(n) % 2 == 1)
                amp = 1.0f / (n * 0.5f + 1.0f);
            else
                amp = 0.25f / (n * n);

            // Stiffness adds even harmonics
            const float evenBoost = stiffness_ * 0.3f;
            if (static_cast<int>(n) % 2 == 0)
                amp *= (1.0f + evenBoost);
            break;
        }

        case ModelType::Membrane:
        {
            // 2D circular membrane: amplitudes follow mode-density distribution.
            // Simplified: mode energy spreads across more partials than 1D cases.
            static constexpr float membraneRatios[] = {
                1.000f, 1.593f, 2.135f, 2.295f,
                2.653f, 2.917f, 3.155f, 3.411f
            };
            const int modeSet = partialIndex / 8;
            const int modeInSet = partialIndex % 8;
            const float ratio = membraneRatios[modeInSet % 8];
            const float radialTerm = static_cast<float>(modeSet) * 2.0f;
            amp = 1.0f / ((ratio * ratio) + radialTerm);
            amp *= (0.5f + damping_ * 0.5f);
            break;
        }

        case ModelType::Plate:
        {
            // 2D plate: modes follow (m² + n²) distribution with 1/f^1.8 rolloff.
            // Plates have denser mode distributions than membranes.
            const float modeDensity = 1.0f + stiffness_ * 0.5f;
            amp = 1.0f / (std::pow(n, 1.6f) * modeDensity);
            amp *= (0.4f + tension_ * 0.6f);
            break;
        }
    }

    // Apply high-frequency damping: higher partials roll off faster
    const float dampingRolloff = 1.0f - damping_ * (n - 1.0f) * 0.02f;
    amp *= std::max(0.0f, dampingRolloff);

    // Clamp
    return std::max(0.0f, std::min(1.0f, amp));
}

//==============================================================================
// generate - fill PartialDataSIMD from physical model
//==============================================================================

void PhysicalModel::generate(PartialDataSIMD& output)
{
    const int numPartials = std::min(PartialDataSIMD::kMaxPartials, 128);
    output.activeCount = 0;
    std::memset(output.activeMask, 0, sizeof(output.activeMask));
    std::memset(output.phase, 0, sizeof(output.phase[0]) * numPartials);

    for (int i = 0; i < numPartials; ++i)
    {
        const float n = static_cast<float>(i + 1);

        // --- Frequency with inharmonicity ---
        // Stretch: f_n = n * f0 * (1 + inharmonicity * (n² - n))
        float freq = fundamentalFreq_ * n;
        if (inharmonicity_ > 0.0f)
        {
            const float stretch = 1.0f + inharmonicity_ * (n * n - n);
            freq *= stretch;
        }
        // Apply material stiffness to frequency as well (stiff strings are sharper)
        if (modelType_ == ModelType::String || modelType_ == ModelType::Pluck)
            freq *= (1.0f + stiffness_ * 0.005f * (n - 1.0f));

        output.frequency[i] = freq;

        // --- Amplitude ---
        float amp = computeAmplitude(i);
        output.amplitude[i] = amp;

        // --- Phase ---
        output.phase[i] = 0.0f;

        // --- Active mask ---
        if (amp > 1e-6f)
        {
            const int word = i >> 5;
            const int bit  = i & 31;
            output.activeMask[word] |= (1u << bit);
            ++output.activeCount;
        }
    }

    output.sampleRate = sampleRate_;
    output.maxPartials = PartialDataSIMD::kMaxPartials;
    output.hopSize = 512.0;
}

//==============================================================================
// processAudio - generate waveguide audio
//==============================================================================

void PhysicalModel::processAudio(juce::AudioBuffer<float>& buffer)
{
    buffer.clear();

    if (!active_ || waveguides_.empty())
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Pre-compute mode coefficients (they don't change per sample within a block)
    struct ModeCoeffs {
        float a1, a2, b0;
    };
    std::vector<ModeCoeffs> modeCoeffs(modes_.size());
    for (size_t m = 0; m < modes_.size(); ++m)
    {
        const auto& mode = modes_[m];
        if (mode.frequency <= 0.0f || mode.frequency >= static_cast<float>(sampleRate_) * 0.49f
            || mode.amplitude < 1e-6f)
        {
            modeCoeffs[m] = {0.0f, 0.0f, 0.0f};
            continue;
        }

        const float bw = 5.0f + (1.0f - mode.decay) * 500.0f;
        const float R = std::exp(static_cast<float>(-M_PI * bw / sampleRate_));
        const float twoPiFreq = 2.0f * static_cast<float>(M_PI) * mode.frequency
                                / static_cast<float>(sampleRate_);
        modeCoeffs[m] = {
            -2.0f * R * std::cos(twoPiFreq),
            R * R,
            (1.0f - R) * std::sin(twoPiFreq)
        };
    }

    // Process each channel
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* channelData = buffer.getWritePointer(ch);

        for (int s = 0; s < numSamples; ++s)
        {
            ++sampleCount_;

            // --- Waveguide processing ---
            float wgOutput = 0.0f;
            for (auto& wg : waveguides_)
            {
                // No external input during normal oscillation (input = 0)
                wgOutput += wg.process(0.0f, damping_, stiffness_, decay_);
            }
            wgOutput /= static_cast<float>(waveguides_.size());

            // --- Modal resonators driven by waveguide output ---
            float modalOutput = 0.0f;
            for (size_t m = 0; m < modes_.size(); ++m)
            {
                auto& mode = modes_[m];
                const auto& coeffs = modeCoeffs[m];

                // 2-pole: y = b0 * x - a1 * y[n-1] - a2 * y[n-2]
                const float y = wgOutput * coeffs.b0 * mode.amplitude
                              - coeffs.a1 * mode.state[0]
                              - coeffs.a2 * mode.state[1];

                mode.state[1] = mode.state[0];
                mode.state[0] = y;

                modalOutput += y;
            }

            // Mix waveguide and modal output
            float output = wgOutput + modalOutput * 0.3f;

            // --- Amplitude envelope ---
            // Linear fade-in over first ms to avoid click
            constexpr int fadeSamples = 64;
            if (sampleCount_ < fadeSamples)
            {
                const float env = static_cast<float>(sampleCount_) / static_cast<float>(fadeSamples);
                output *= env;
            }

            // --- Check for decay completion ---
            // If output is very quiet and we've been running, stop
            if (sampleCount_ > static_cast<int>(sampleRate_) * 2) // Safety: max 2 seconds
            {
                if (std::abs(output) < 1e-6f)
                {
                    active_ = false;
                    output = 0.0f;
                }
            }

            channelData[s] = output;
        }
    }
}

//==============================================================================
// Lifecycle
//==============================================================================

void PhysicalModel::trigger()
{
    active_ = true;
    sampleCount_ = 0;

    // --- Initialise waveguides ---
    waveguides_.clear();

    if (modelType_ == ModelType::Membrane)
    {
        // Membrane: multiple waveguides at slightly different frequencies
        // to simulate radial mode splitting
        const int numWG = 6;
        for (int i = 0; i < numWG; ++i)
        {
            Waveguide wg;
            const float freqOffset = fundamentalFreq_ * (1.0f + static_cast<float>(i) * 0.02f);
            wg.init(freqOffset, sampleRate_);
            waveguides_.push_back(std::move(wg));
        }
    }
    else if (modelType_ == ModelType::Plate)
    {
        // Plate: more waveguides for the denser mode distribution
        const int numWG = 8;
        for (int i = 0; i < numWG; ++i)
        {
            Waveguide wg;
            const float freqOffset = fundamentalFreq_
                * (1.0f + static_cast<float>(i) * 0.015f);
            wg.init(freqOffset, sampleRate_);
            waveguides_.push_back(std::move(wg));
        }
    }
    else
    {
        // String / Pluck / Blow: single waveguide
        Waveguide wg;
        wg.init(fundamentalFreq_, sampleRate_);
        waveguides_.push_back(std::move(wg));
    }

    // Fill delay lines with excitation
    std::minstd_rand rng(static_cast<std::minstd_rand::result_type>(
        static_cast<int>(fundamentalFreq_ * 1000.0f) ^ 0xDEADBEEF));
    for (auto& wg : waveguides_)
    {
        fillExcitation(wg.delayLine, rng);
        wg.writePos = 0;
        wg.lowpassState = 0.0f;
    }

    // --- Initialise modal resonators ---
    // 64 modes covering the harmonic series
    constexpr int kNumModes = 64;
    modes_.resize(kNumModes);

    for (int i = 0; i < kNumModes; ++i)
    {
        const float n = static_cast<float>(i + 1);
        auto& mode = modes_[i];

        // Frequency with inharmonicity
        float freq = fundamentalFreq_ * n;
        if (inharmonicity_ > 0.0f)
            freq *= (1.0f + inharmonicity_ * (n * n - n));

        mode.frequency = freq;
        mode.amplitude = computeAmplitude(i);
        mode.decay = decay_;
        mode.phase = 0.0f;
        mode.state[0] = 0.0f;
        mode.state[1] = 0.0f;
    }
}

void PhysicalModel::release()
{
    // Stop the excitation: setting active to false will let the waveguide
    // ring out naturally during processAudio via feedback, but we set a flag
    // so the envelope can fade. For simplicity, we just keep active_ true
    // and let the decay parameter handle the release.
    // The oscillator will naturally fade out via the feedback loop.
}

void PhysicalModel::reset()
{
    waveguides_.clear();
    modes_.clear();
    active_ = false;
    sampleCount_ = 0;
}

} // namespace ana
