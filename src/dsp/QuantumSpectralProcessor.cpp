#include "QuantumSpectralProcessor.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <complex>
#include <juce_dsp/juce_dsp.h>

namespace ana {

//==============================================================================
//  Forward declarations of file-local helpers
//==============================================================================
namespace {

/** Pauli-X (NOT) matrix: [[0,1],[1,0]] */
constexpr float kPauliX[4] = { 0.0f, 1.0f, 1.0f, 0.0f };

/** Minimum amplitude threshold for partial processing. */
constexpr float kAmpThreshold = 1e-8f;

/** Minimum frequency in Hz for synthesized partials. */
constexpr float kMinFreqHz = 20.0f;

} // namespace

//==============================================================================
//  Gate matrix definitions
//==============================================================================

std::vector<std::complex<float>> QuantumSpectralProcessor::hadamardMatrix()
{
    constexpr float invSqrt2 = 0.7071067811865475f; // 1/√2
    return {
        { invSqrt2,  0.0f}, { invSqrt2,  0.0f},
        { invSqrt2,  0.0f}, {-invSqrt2,  0.0f}
    };
}

std::vector<std::complex<float>> QuantumSpectralProcessor::phaseMatrix(float phase)
{
    const std::complex<float> eiTheta = std::exp(std::complex<float>(0.0f, phase));
    return {
        { 1.0f, 0.0f}, { 0.0f, 0.0f},
        { 0.0f, 0.0f}, eiTheta
    };
}

std::vector<std::complex<float>> QuantumSpectralProcessor::rotationXMatrix(float angle)
{
    const float cosHalf = std::cos(angle * 0.5f);
    const float sinHalf = std::sin(angle * 0.5f);
    return {
        { cosHalf,   0.0f}, { 0.0f, -sinHalf},
        { 0.0f, -sinHalf},  { cosHalf,  0.0f}
    };
}

std::vector<std::complex<float>> QuantumSpectralProcessor::rotationYMatrix(float angle)
{
    const float cosHalf = std::cos(angle * 0.5f);
    const float sinHalf = std::sin(angle * 0.5f);
    return {
        { cosHalf, 0.0f}, {-sinHalf, 0.0f},
        { sinHalf, 0.0f}, { cosHalf, 0.0f}
    };
}

std::vector<std::complex<float>> QuantumSpectralProcessor::cnotMatrix()
{
    // CNOT: |00⟩→|00⟩, |01⟩→|01⟩, |10⟩→|11⟩, |11⟩→|10⟩
    return {
        { 1.0f, 0.0f}, { 0.0f, 0.0f}, { 0.0f, 0.0f}, { 0.0f, 0.0f},
        { 0.0f, 0.0f}, { 1.0f, 0.0f}, { 0.0f, 0.0f}, { 0.0f, 0.0f},
        { 0.0f, 0.0f}, { 0.0f, 0.0f}, { 0.0f, 0.0f}, { 1.0f, 0.0f},
        { 0.0f, 0.0f}, { 0.0f, 0.0f}, { 1.0f, 0.0f}, { 0.0f, 0.0f}
    };
}

std::vector<std::complex<float>> QuantumSpectralProcessor::toffoliMatrix()
{
    // Toffoli (CCNOT): 8×8 matrix, flips target when both controls are |1⟩.
    // Identity except the |110⟩↔|111⟩ block (indices 6,7) gets an X gate.
    std::vector<std::complex<float>> m(64, { 0.0f, 0.0f });
    for (int i = 0; i < 8; ++i)
        m[static_cast<size_t>(i) * 8 + static_cast<size_t>(i)] = { 1.0f, 0.0f };

    // Overwrite the |110⟩/|111⟩ block
    m[6 * 8 + 6] = { 0.0f, 0.0f };
    m[6 * 8 + 7] = { 1.0f, 0.0f };
    m[7 * 8 + 6] = { 1.0f, 0.0f };
    m[7 * 8 + 7] = { 0.0f, 0.0f };
    return m;
}

//==============================================================================
//  Construction / reset
//==============================================================================

QuantumSpectralProcessor::QuantumSpectralProcessor()
{
    reset();
}

void QuantumSpectralProcessor::reset()
{
    numQubits_              = 8;
    interferenceMode_       = InterferenceMode::Constructive;
    entanglement_           = 0.5f;
    decoherence_            = 0.01f;
    interferenceStrength_   = 0.5f;
    measurementBias_        = 0.5f;
    numSuperpositionStates_ = 4;
    sampleRate_             = 44100.0;

    initState(numQubits_);
    densityMatrix_.clear();
}

void QuantumSpectralProcessor::initState(int numQubits)
{
    numQubits_ = juce::jlimit(1, 12, numQubits);
    qubits_.clear();
    qubits_.resize(static_cast<size_t>(numQubits_));

    for (auto& q : qubits_)
    {
        q.alpha = 1.0f;
        q.beta  = 0.0f;
    }
}

//==============================================================================
//  Setters
//==============================================================================

void QuantumSpectralProcessor::setState(int qubit, float probability)
{
    const int q = juce::jlimit(0, numQubits_ - 1, qubit);
    const float p = juce::jlimit(0.0f, 1.0f, probability);

    qubits_[static_cast<size_t>(q)].alpha = std::sqrt(1.0f - p);
    qubits_[static_cast<size_t>(q)].beta  = std::sqrt(p);
}

void QuantumSpectralProcessor::setInterferenceMode(InterferenceMode mode)
{
    interferenceMode_ = mode;
}

void QuantumSpectralProcessor::setMeasurementBias(float bias)
{
    measurementBias_ = juce::jlimit(0.0f, 1.0f, bias);
}

void QuantumSpectralProcessor::setEntanglement(float amount)
{
    entanglement_ = juce::jlimit(0.0f, 1.0f, amount);
}

void QuantumSpectralProcessor::setDecoherence(float rate)
{
    decoherence_ = juce::jlimit(0.0f, 1.0f, rate);
}

void QuantumSpectralProcessor::setInterferenceStrength(float str)
{
    interferenceStrength_ = juce::jlimit(0.0f, 1.0f, str);
}

void QuantumSpectralProcessor::setNumSuperpositionStates(int states)
{
    numSuperpositionStates_ = juce::jlimit(1, 32, states);
}

void QuantumSpectralProcessor::setSampleRate(double sr)
{
    sampleRate_ = sr > 0.0 ? sr : 44100.0;
}

//==============================================================================
//  Single-qubit gate application
//==============================================================================

void QuantumSpectralProcessor::applyGate(GateType gate, int targetQubit)
{
    const int q = juce::jlimit(0, numQubits_ - 1, targetQubit);

    switch (gate)
    {
        case GateType::Hadamard:
        {
            // |0⟩ → (|0⟩+|1⟩)/√2,  |1⟩ → (|0⟩-|1⟩)/√2
            const float newAlpha = (qubits_[q].alpha + qubits_[q].beta)  * 0.707106781f;
            const float newBeta  = (qubits_[q].alpha - qubits_[q].beta)  * 0.707106781f;
            qubits_[q].alpha = newAlpha;
            qubits_[q].beta  = newBeta;
            qubits_[q].normalize();
            break;
        }

        case GateType::Phase:
            applyPhaseShift(entanglement_ * juce::float_Pi, q);
            break;

        case GateType::RotationX:
            applyRotation(entanglement_ * juce::float_Pi, q);
            break;

        case GateType::RotationY:
        {
            // Ry(θ) = [[cos(θ/2), -sin(θ/2)], [sin(θ/2), cos(θ/2)]]
            const float angle  = entanglement_ * juce::float_Pi;
            const float cosHalf = std::cos(angle * 0.5f);
            const float sinHalf = std::sin(angle * 0.5f);
            const float newAlpha =  cosHalf * qubits_[q].alpha - sinHalf * qubits_[q].beta;
            const float newBeta  =  sinHalf * qubits_[q].alpha + cosHalf * qubits_[q].beta;
            qubits_[q].alpha = newAlpha;
            qubits_[q].beta  = newBeta;
            qubits_[q].normalize();
            break;
        }

        case GateType::CNOT:
        case GateType::Toffoli:
            // Single-qubit CNOT/Toffoli doesn't make sense; no-op
            break;

        case GateType::Measurement:
        {
            // Probabilistic collapse of a single qubit
            const float biasedProb = qubits_[q].prob1()
                * (1.0f - measurementBias_) + measurementBias_ * 0.5f;
            if (rng_.nextFloat() < biasedProb)
            {
                qubits_[q].alpha = 0.0f;
                qubits_[q].beta  = 1.0f;
            }
            else
            {
                qubits_[q].alpha = 1.0f;
                qubits_[q].beta  = 0.0f;
            }
            break;
        }
    }
}

void QuantumSpectralProcessor::applyControlledGate(GateType gate,
                                                    int controlQubit,
                                                    int targetQubit)
{
    const int c = juce::jlimit(0, numQubits_ - 1, controlQubit);
    const int t = juce::jlimit(0, numQubits_ - 1, targetQubit);

    if (c == t)
        return;

    switch (gate)
    {
        case GateType::CNOT:
            applyCNOT(c, t);
            break;

        case GateType::Toffoli:
            // For applyControlledGate with Toffoli, use c as first control
            // and t as the target; the second control is the qubit halfway between.
            applyToffoli(c, (c + t) / 2, t);
            break;

        case GateType::Hadamard:
        case GateType::Phase:
        case GateType::RotationX:
        case GateType::RotationY:
        case GateType::Measurement:
        {
            // Controlled version: apply gate on target only if control is |1⟩
            const float cProb1 = qubits_[c].prob1();
            const float effectiveProb = cProb1 * (1.0f - entanglement_)
                                        + entanglement_ * 0.5f;

            if (rng_.nextFloat() < effectiveProb)
                applyGate(gate, t);
            break;
        }
    }
}

//==============================================================================
//  Qubit-level gate implementations
//==============================================================================

void QuantumSpectralProcessor::applyRotation(float angle, int qubit)
{
    // Rx(θ) = [[cos(θ/2), -i·sin(θ/2)], [-i·sin(θ/2), cos(θ/2)]]
    const float cosHalf = std::cos(angle * 0.5f);
    const float sinHalf = std::sin(angle * 0.5f);

    // For real-valued amplitudes, the -i factor becomes a sign swap
    // between α and β described by Rx(θ):
    //   α' =  cos(θ/2) α - i·sin(θ/2) β
    //   β' = -i·sin(θ/2) α + cos(θ/2) β
    // For real amplitudes this simplifies to mixing with sign flips.
    const float newAlpha =  cosHalf * qubits_[qubit].alpha
                          - sinHalf * qubits_[qubit].beta;
    const float newBeta  = -sinHalf * qubits_[qubit].alpha
                          + cosHalf * qubits_[qubit].beta;
    qubits_[qubit].alpha = newAlpha;
    qubits_[qubit].beta  = newBeta;
    qubits_[qubit].normalize();
}

void QuantumSpectralProcessor::applyPhaseShift(float phase, int qubit)
{
    // P(θ) = [[1, 0], [0, e^(iθ)]]
    // Only affects |1⟩ amplitude: β → β·e^(iθ)
    // For real amplitudes, this becomes β → β·cos(θ)
    qubits_[qubit].beta *= std::cos(phase);
    qubits_[qubit].normalize();
}

void QuantumSpectralProcessor::applyCNOT(int control, int target)
{
    auto& c = qubits_[static_cast<size_t>(control)];
    auto& t = qubits_[static_cast<size_t>(target)];

    const float ac2 = c.alpha * c.alpha;
    const float bc2 = c.beta  * c.beta;
    const float at2 = t.alpha * t.alpha;
    const float bt2 = t.beta  * t.beta;

    // CNOT on product state, then trace out the control qubit.
    // After CNOT:
    //   |ψ'⟩ = αcαt|00⟩ + αcβt|01⟩ + βcβt|10⟩ + βcαt|11⟩
    // Reduced density matrix of target:
    //   ρ_t = diag(αc²αt² + βc²βt²,  αc²βt² + βc²αt²)
    const float newAlpha2 = ac2 * at2 + bc2 * bt2;
    const float newBeta2  = ac2 * bt2 + bc2 * at2;

    t.alpha = std::sqrt(std::max(0.0f, newAlpha2));
    t.beta  = std::sqrt(std::max(0.0f, newBeta2));
    t.normalize();

    // Partial entanglement: mix some original state back
    if (entanglement_ < 1.0f)
    {
        const float origAlpha = std::sqrt(at2);
        const float origBeta  = std::sqrt(bt2);
        t.alpha = t.alpha * entanglement_ + origAlpha * (1.0f - entanglement_);
        t.beta  = t.beta  * entanglement_ + origBeta  * (1.0f - entanglement_);
        t.normalize();
    }
}

void QuantumSpectralProcessor::applyToffoli(int control1, int control2, int target)
{
    auto& c1 = qubits_[static_cast<size_t>(control1)];
    auto& c2 = qubits_[static_cast<size_t>(control2)];
    auto& t  = qubits_[static_cast<size_t>(target)];

    const float ac12 = c1.alpha * c1.alpha;
    const float bc12 = c1.beta  * c1.beta;
    const float ac22 = c2.alpha * c2.alpha;
    const float bc22 = c2.beta  * c2.beta;
    const float at2  = t.alpha   * t.alpha;
    const float bt2  = t.beta    * t.beta;

    // Probability both controls are |1⟩ = bc12 * bc22
    const float pBothOn = bc12 * bc22;
    const float pNotBothOn = 1.0f - pBothOn;

    // Target flips (α↔β) only when both controls are |1⟩
    const float newAlpha2 = pNotBothOn * at2 + pBothOn * bt2;
    const float newBeta2  = pNotBothOn * bt2 + pBothOn * at2;

    t.alpha = std::sqrt(std::max(0.0f, newAlpha2));
    t.beta  = std::sqrt(std::max(0.0f, newBeta2));
    t.normalize();
}

//==============================================================================
//  Density-matrix operations (for arbitrary multi-qubit gates)
//==============================================================================

void QuantumSpectralProcessor::rebuildDensityMatrix()
{
    const size_t dim = static_cast<size_t>(1) << numQubits_;

    densityMatrix_.resize(dim);
    for (auto& row : densityMatrix_)
    {
        row.resize(dim);
        std::fill(row.begin(), row.end(), std::complex<float>(0.0f, 0.0f));
    }

    // Build |ψ⟩⟨ψ| from product state of individual qubits
    // |ψ⟩ = ⊗_i |q_i⟩  so  |ψ⟩_k = ∏_i q_i[bit(k,i)]
    // where bit(k,i) is the i-th bit of index k
    for (size_t r = 0; r < dim; ++r)
    {
        std::complex<float> psiR(1.0f, 0.0f);
        for (int q = 0; q < numQubits_; ++q)
        {
            const bool bit = (r >> q) & 1;
            psiR *= bit
                ? std::complex<float>(qubits_[q].beta, 0.0f)
                : std::complex<float>(qubits_[q].alpha, 0.0f);
        }

        for (size_t c = 0; c < dim; ++c)
        {
            std::complex<float> psiC(1.0f, 0.0f);
            for (int q = 0; q < numQubits_; ++q)
            {
                const bool bit = (c >> q) & 1;
                psiC *= bit
                    ? std::complex<float>(qubits_[q].beta, 0.0f)
                    : std::complex<float>(qubits_[q].alpha, 0.0f);
            }

            densityMatrix_[r][c] = psiR * std::conj(psiC);
        }
    }
}

void QuantumSpectralProcessor::applyMatrixToDensity(
    const std::vector<std::complex<float>>& gateMatrix,
    const std::vector<int>& /*qubitIndices*/)
{
    // Rebuild density matrix from current qubit states
    rebuildDensityMatrix();

    const size_t dim = densityMatrix_.size();
    const size_t gateDim = static_cast<size_t>(std::sqrt(gateMatrix.size()));

    if (gateDim != dim)
    {
        // Multi-qubit gate dimension doesn't match register – skip
        return;
    }

    // ρ' = G·ρ·G†
    std::vector<std::vector<std::complex<float>>> newRho(
        dim, std::vector<std::complex<float>>(dim, { 0.0f, 0.0f }));

    for (size_t r = 0; r < dim; ++r)
    {
        for (size_t c = 0; c < dim; ++c)
        {
            for (size_t k = 0; k < dim; ++k)
            {
                for (size_t l = 0; l < dim; ++l)
                {
                    newRho[r][c] += gateMatrix[r * dim + k]
                                  * densityMatrix_[k][l]
                                  * std::conj(gateMatrix[c * dim + l]);
                }
            }
        }
    }

    densityMatrix_ = std::move(newRho);

    // Collapse density matrix back to product-state qubit approximation
    // by computing reduced density matrices per qubit
    for (int q = 0; q < numQubits_; ++q)
    {
        const size_t step = static_cast<size_t>(1) << q;
        float prob1 = 0.0f;
        size_t count = 0;

        for (size_t i = 0; i < dim; i += step * 2)
        {
            for (size_t j = i + step; j < i + step * 2; ++j)
            {
                prob1 += densityMatrix_[j][j].real();
                ++count;
            }
        }

        if (count > 0)
            prob1 /= static_cast<float>(count);

        qubits_[q].alpha = std::sqrt(std::max(0.0f, 1.0f - prob1));
        qubits_[q].beta  = std::sqrt(std::max(0.0f, prob1));
        qubits_[q].normalize();
    }
}

void QuantumSpectralProcessor::applyGateMatrix(
    const std::vector<std::complex<float>>& matrix,
    const std::vector<int>& targetQubits)
{
    applyMatrixToDensity(matrix, targetQubits);
}

//==============================================================================
//  Tensor product helper
//==============================================================================

void QuantumSpectralProcessor::tensorProduct(const std::vector<float>& a,
                                              const std::vector<float>& b,
                                              std::vector<float>& result)
{
    const size_t na = a.size();
    const size_t nb = b.size();
    result.resize(na * nb);

    for (size_t i = 0; i < na; ++i)
        for (size_t j = 0; j < nb; ++j)
            result[i * nb + j] = a[i] * b[j];
}

//==============================================================================
//  process — quantum-inspired spectral modification
//==============================================================================
//
//  Maps each active partial to a qubit by frequency band, then applies:
//    1. Amplitude modulation from qubit superposition
//    2. Decoherence (noise injection)
//    3. Active mask update
//
void QuantumSpectralProcessor::process(PartialDataSIMD& partials)
{
    if (partials.activeCount == 0 || numQubits_ <= 0)
        return;

    const int N = partials.maxPartials;
    const int bandSize = std::max(1, N / numQubits_);
    const float noiseScale = decoherence_ * 0.5f;

    for (int i = 0; i < N; ++i)
    {
        if (! partials.isActive(i))
            continue;

        // Map partial to qubit by frequency band
        const int qIdx = std::min(i / bandSize, numQubits_ - 1);
        const auto& q = qubits_[static_cast<size_t>(qIdx)];

        // Quantum amplitude factor:
        //   |0⟩ (off)   → factor approaches 0.5 (attenuated)
        //   |1⟩ (on)    → factor approaches 1.0 (full)
        //   Entanglement controls how strongly the qubit state modulates.
        const float probOn = q.prob1();
        const float quantumFactor = probOn * entanglement_
                                  + (1.0f - entanglement_) * 0.5f;

        partials.amplitude[i] *= std::max(0.0f, quantumFactor);

        // Decoherence: inject noise proportional to decoherence rate
        if (decoherence_ > 0.0f)
        {
            const float noise = (rng_.nextFloat() * 2.0f - 1.0f) * noiseScale;
            partials.amplitude[i] = std::max(0.0f, partials.amplitude[i] + noise);
        }

        // Phase modulation from qubit phase relationship
        // (α ≈ cos(θ/2), β ≈ sin(θ/2) for some effective rotation angle)
        const float phaseMod = std::atan2(q.beta, q.alpha) * 0.1f;
        partials.phase[i] += phaseMod;
    }

    partials.updateActiveMask();
}

//==============================================================================
//  processAudio — FFT-based audio buffer processing
//==============================================================================
//
//  Performs an STFT on the buffer, applies qubit-state modulation per
//  frequency bin, then inverse-transforms.
//
void QuantumSpectralProcessor::processAudio(juce::AudioBuffer<float>& buffer,
                                             double sampleRate)
{
    sampleRate_ = sampleRate;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples < 8 || numChannels == 0)
        return;

    // Choose FFT size as the largest power of two ≤ numSamples
    int fftOrder = 0;
    int fftSize  = 1;
    while (fftSize * 2 <= numSamples && fftOrder < 12)
    {
        ++fftOrder;
        fftSize *= 2;
    }

    if (fftSize < 8)
        return;

    juce::dsp::FFT fft(fftOrder);
    const int halfBins = fftSize / 2;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* input = buffer.getReadPointer(ch);
        auto*       output = buffer.getWritePointer(ch);

        // Interleaved FFT buffer: [r0, i0, r1, i1, ...]
        // JUCE's real-only FFT requires 2x fftSize floats
        std::vector<float> fftBuf(static_cast<size_t>(fftSize) * 2, 0.0f);

        // Copy and window with Hann
        for (int i = 0; i < fftSize; ++i)
        {
            const float hann = 0.5f * (1.0f - std::cos(
                juce::float_Pi * 2.0f * static_cast<float>(i)
                / static_cast<float>(fftSize - 1)));
            fftBuf[static_cast<size_t>(i) * 2] = input[i] * hann;
        }

        // Forward transform
        fft.performRealOnlyForwardTransform(fftBuf.data());

        // Apply qubit modulation per frequency bin
        for (int bin = 0; bin < halfBins; ++bin)
        {
            const size_t idx = static_cast<size_t>(bin) * 2;
            const int qIdx = (bin * numQubits_) / halfBins;
            const auto& q = qubits_[static_cast<size_t>(qIdx)];

            // Quantum gain from qubit probability
            const float gain = q.prob1() * entanglement_
                             + (1.0f - entanglement_) * 0.5f;

            // Scale with interference strength for additional character
            const float effectiveGain = 0.5f + gain * interferenceStrength_;

            fftBuf[idx]     *= effectiveGain;
            fftBuf[idx + 1] *= effectiveGain;

            // Decoherence noise in spectral domain
            if (decoherence_ > 0.0f)
            {
                const float noise = (rng_.nextFloat() * 2.0f - 1.0f)
                                    * decoherence_ * 0.2f;
                fftBuf[idx]     += noise;
                fftBuf[idx + 1] += noise;
            }
        }

        // Inverse transform
        fft.performRealOnlyInverseTransform(fftBuf.data());

        // Overlap-add / copy back (normalised by FFT size)
        const float norm = 1.0f / static_cast<float>(fftSize);
        for (int i = 0; i < std::min(numSamples, fftSize); ++i)
            output[i] = fftBuf[static_cast<size_t>(i) * 2] * norm;
    }
}

//==============================================================================
//  applyInterference — phase-based partial interaction
//==============================================================================
//
//  Interference between neighbouring partials based on their phase
//  relationships.  Sorts partials by frequency and applies the selected
//  interference mode.
//
void QuantumSpectralProcessor::applyInterference(PartialDataSIMD& partials)
{
    if (partials.activeCount < 2 || interferenceStrength_ <= 0.0f)
        return;

    const int N = partials.maxPartials;

    // Collect active partial indices sorted by frequency
    std::vector<int> order;
    order.reserve(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i)
        if (partials.isActive(i))
            order.push_back(i);

    if (order.size() < 2)
        return;

    std::sort(order.begin(), order.end(),
              [&](int a, int b) noexcept
              {
                  return partials.frequency[a] < partials.frequency[b];
              });

    const float strength = interferenceStrength_;
    const int numActive = static_cast<int>(order.size());

    switch (interferenceMode_)
    {
        case InterferenceMode::Constructive:
        {
            // Boost pairs whose phases are aligned (difference < π/2)
            for (int j = 0; j < numActive - 1; ++j)
            {
                const int a = order[j];
                const int b = order[j + 1];
                const float phaseDiff = std::abs(
                    partials.phase[a] - partials.phase[b]);

                if (phaseDiff < juce::float_Pi * 0.5f)
                {
                    const float factor = 1.0f + strength * 0.5f
                        * (1.0f - phaseDiff / (juce::float_Pi * 0.5f));
                    partials.amplitude[a] *= factor;
                    partials.amplitude[b] *= factor;
                }
            }
            break;
        }

        case InterferenceMode::Destructive:
        {
            // Attenuate pairs whose phases are opposite (difference > π/2)
            for (int j = 0; j < numActive - 1; ++j)
            {
                const int a = order[j];
                const int b = order[j + 1];
                const float phaseDiff = std::abs(
                    partials.phase[a] - partials.phase[b]);

                if (phaseDiff > juce::float_Pi * 0.5f)
                {
                    const float factor = 1.0f - strength * 0.5f
                        * std::min(1.0f, (phaseDiff - juce::float_Pi * 0.5f)
                                            / (juce::float_Pi * 0.5f));
                    partials.amplitude[a] *= std::max(0.0f, factor);
                    partials.amplitude[b] *= std::max(0.0f, factor);
                }
            }
            break;
        }

        case InterferenceMode::PhaseShift:
        {
            // Shift each partial's phase toward its nearest neighbour,
            // weighted by strength
            for (int j = 1; j < numActive - 1; ++j)
            {
                const int idx = order[j];
                const int leftIdx  = order[j - 1];
                const int rightIdx = order[j + 1];

                const float leftPhase  = partials.phase[leftIdx];
                const float rightPhase = partials.phase[rightIdx];

                // Average neighbour phase (accounting for wraparound)
                float neighbourAvg = std::atan2(
                    std::sin(leftPhase) + std::sin(rightPhase),
                    std::cos(leftPhase) + std::cos(rightPhase));

                // Shift current phase toward neighbour average
                float curPhase = partials.phase[idx];
                float phaseDelta = neighbourAvg - curPhase;

                // Keep delta in [-π, π]
                while (phaseDelta > juce::float_Pi)  phaseDelta -= juce::float_Pi * 2.0f;
                while (phaseDelta < -juce::float_Pi) phaseDelta += juce::float_Pi * 2.0f;

                partials.phase[idx] += phaseDelta * strength * 0.3f;
            }
            break;
        }

        case InterferenceMode::Probabilistic:
        {
            // Randomly boost or reduce based on probability amplitude
            for (int j = 0; j < numActive - 1; ++j)
            {
                const int a = order[j];
                const int b = order[j + 1];

                // Use qubit probability as the selection bias
                const int qIdx = std::min(a / std::max(1, N / numQubits_),
                                          numQubits_ - 1);
                const float bias = qubits_[static_cast<size_t>(qIdx)].prob1();

                if (rng_.nextFloat() < bias)
                {
                    // Boost
                    partials.amplitude[a] *= 1.0f + strength * 0.3f;
                    partials.amplitude[b] *= 1.0f + strength * 0.3f;
                }
                else
                {
                    // Reduce
                    partials.amplitude[a] *= 1.0f - strength * 0.3f;
                    partials.amplitude[b] *= 1.0f - strength * 0.3f;
                }
            }
            break;
        }
    }

    // Clamp amplitudes to non-negative
    for (int i = 0; i < N; ++i)
        if (partials.amplitude[i] < kAmpThreshold)
            partials.amplitude[i] = 0.0f;

    partials.updateActiveMask();
}

//==============================================================================
//  measure — probabilistic collapse of all qubits
//==============================================================================
//
//  Each qubit collapses to |0⟩ or |1⟩ with probability determined by
//  its current |β|², modulated by measurementBias_.
//
void QuantumSpectralProcessor::measure(PartialDataSIMD& partials)
{
    for (auto& q : qubits_)
    {
        // Apply measurement bias: bias values > 0.5 skew toward |1⟩
        const float biasedProb = q.prob1() * (1.0f - measurementBias_)
                                + measurementBias_ * 0.5f;

        if (rng_.nextFloat() < biasedProb)
        {
            q.alpha = 0.0f;
            q.beta  = 1.0f;
        }
        else
        {
            q.alpha = 1.0f;
            q.beta  = 0.0f;
        }

        // Add uncertainty from decoherence after collapse
        if (decoherence_ > 0.0f)
        {
            q.alpha += (rng_.nextFloat() * 2.0f - 1.0f) * decoherence_;
            q.beta  += (rng_.nextFloat() * 2.0f - 1.0f) * decoherence_;
            q.normalize();
        }
    }

    // Re-process partials with the collapsed state
    process(partials);
}

//==============================================================================
//  collapse — deterministic collapse to most probable state
//==============================================================================

void QuantumSpectralProcessor::collapse()
{
    for (auto& q : qubits_)
    {
        if (q.prob1() > q.prob0())
        {
            q.alpha = 0.0f;
            q.beta  = 1.0f;
        }
        else
        {
            q.alpha = 1.0f;
            q.beta  = 0.0f;
        }
    }
}

//==============================================================================
//  synthFromSuperposition — generate partials from superposition states
//==============================================================================
//
//  Creates a full set of partials by interpreting the superposition of
//  all qubit states as a frequency-domain amplitude profile.
//
void QuantumSpectralProcessor::synthFromSuperposition(PartialDataSIMD& output)
{
    const int N = output.maxPartials;

    // Clear output
    std::memset(output.amplitude, 0, sizeof(float) * static_cast<size_t>(N));
    std::memset(output.frequency, 0, sizeof(float) * static_cast<size_t>(N));
    std::memset(output.phase,     0, sizeof(float) * static_cast<size_t>(N));

    const int numStates = numSuperpositionStates_;
    const float nyquist = static_cast<float>(sampleRate_) * 0.5f;
    const float binWidth = nyquist / static_cast<float>(N);

    for (int s = 0; s < numStates; ++s)
    {
        const float stateOffset = static_cast<float>(s) / static_cast<float>(numStates);

        for (int q = 0; q < numQubits_; ++q)
        {
            const float prob = qubits_[static_cast<size_t>(q)].prob1();
            if (prob < 0.05f)
                continue;

            const int bandSize = N / numQubits_;
            const int baseIdx  = q * bandSize;

            for (int j = 0; j < bandSize; ++j)
            {
                const int idx = baseIdx + j;
                if (idx >= N) break;

                // Each superposition state contributes partial energy
                // proportional to the qubit's |1⟩ probability
                const float contribution = prob / static_cast<float>(numStates);

                // Accumulate amplitude (multiple states can contribute)
                output.amplitude[idx] += contribution;

                // Set frequency based on bin
                output.frequency[idx] = kMinFreqHz
                    + static_cast<float>(idx) * binWidth;

                // Phase determined by state index for interesting interference
                output.phase[idx] = juce::float_Pi * 2.0f
                    * (stateOffset + static_cast<float>(j) / static_cast<float>(bandSize))
                    * prob;
            }
        }
    }

    // Normalise amplitude so the maximum is 1.0
    float maxAmp = 1e-10f;
    for (int i = 0; i < N; ++i)
        maxAmp = std::max(maxAmp, output.amplitude[i]);

    if (maxAmp > 0.0f)
    {
        const float invMax = 1.0f / maxAmp;
        for (int i = 0; i < N; ++i)
            output.amplitude[i] *= invMax;
    }

    output.updateActiveMask();
}

} // namespace ana
