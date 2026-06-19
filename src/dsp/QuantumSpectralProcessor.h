#pragma once

#include <vector>
#include <complex>
#include <cstdint>
#include <cmath>
#include <memory>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include "PartialDataSIMD.h"
#include "SIMDSupport.h"

namespace ana {

//==============================================================================
/**
    QuantumSpectralProcessor — quantum-inspired spectral processing for audio
    synthesis.

    Uses concepts from quantum computing (superposition, interference,
    entanglement) to transform spectral partial data in musically interesting
    ways.

    Each frequency band is represented as a quantum bit (qubit) with
    probability amplitudes for |0⟩ (off) and |1⟩ (on) states.  Quantum gates
    modify these amplitudes, interference creates phase-based interactions,
    and measurement collapses superposition into deterministic states.

    @see PartialDataSIMD, SIMDSupport
*/
class QuantumSpectralProcessor
{
public:
    //==============================================================================
    /// Quantum gate types
    enum class GateType
    {
        Hadamard,      ///< Create superposition of frequency bins
        Phase,         ///< Rotate phase of frequency bins
        CNOT,          ///< Controlled-NOT between frequency bands
        Toffoli,       ///< Controlled-controlled-NOT
        RotationX,     ///< Rotation around X-axis in frequency-space
        RotationY,     ///< Rotation around Y-axis
        Measurement    ///< Collapse superposition (probabilistic)
    };

    //==============================================================================
    /// Interference modes for phase-based partial interaction
    enum class InterferenceMode
    {
        Constructive,  ///< Amplify aligned frequencies
        Destructive,   ///< Cancel opposite phases
        PhaseShift,    ///< Shift based on phase relationships
        Probabilistic  ///< Random selection based on probability
    };

    //==============================================================================
    QuantumSpectralProcessor();
    ~QuantumSpectralProcessor() = default;

    //==============================================================================
    /// @name Quantum state management
    //@{
    void initState(int numQubits);
    void setState(int qubit, float probability);
    //@}

    //==============================================================================
    /// @name Quantum gates
    //@{
    void applyGate(GateType gate, int targetQubit);
    void applyControlledGate(GateType gate, int controlQubit, int targetQubit);
    //@}

    //==============================================================================
    /// @name Quantum operations on partials
    //@{
    void process(PartialDataSIMD& partials);
    void processAudio(juce::AudioBuffer<float>& buffer, double sampleRate);
    //@}

    //==============================================================================
    /// @name Interference
    //@{
    void setInterferenceMode(InterferenceMode mode);
    void applyInterference(PartialDataSIMD& partials);
    //@}

    //==============================================================================
    /// @name Quantum measurement
    //@{
    void measure(PartialDataSIMD& partials);
    void setMeasurementBias(float bias);
    //@}

    //==============================================================================
    /// @name Superposition synthesis
    //@{
    void synthFromSuperposition(PartialDataSIMD& output);
    void setNumSuperpositionStates(int states);
    //@}

    //==============================================================================
    /// @name Parameters
    //@{
    void setEntanglement(float amount);
    void setDecoherence(float rate);
    void setInterferenceStrength(float str);
    void setSampleRate(double sr);
    //@}

    //==============================================================================
    /// @name Reset
    //@{
    void reset();
    void collapse();
    //@}

private:
    //==============================================================================
    /// Qubit state (probability amplitudes for |0⟩ and |1⟩)
    struct QubitState
    {
        float alpha = 1.0f;   ///< Probability amplitude of |0⟩
        float beta  = 0.0f;   ///< Probability amplitude of |1⟩

        float prob0() const noexcept { return alpha * alpha; }
        float prob1() const noexcept { return beta  * beta;  }

        void normalize() noexcept
        {
            const float mag = std::sqrt(alpha * alpha + beta * beta);
            if (mag > 1e-12f)
            {
                const float inv = 1.0f / mag;
                alpha *= inv;
                beta  *= inv;
            }
            else
            {
                alpha = 1.0f;
                beta  = 0.0f;
            }
        }
    };

    //==============================================================================
    std::vector<QubitState> qubits_;
    std::vector<std::vector<std::complex<float>>> densityMatrix_;

    //==============================================================================
    /// Cached sin/cos for PhaseShift interference optimization
    struct SinCosPair { float sin; float cos; };
    std::vector<SinCosPair> sincosCache_;

    //==============================================================================
    // Gate matrix helpers
    void applyGateMatrix(const std::vector<std::complex<float>>& matrix,
                         const std::vector<int>& targetQubits);

    static std::vector<std::complex<float>> hadamardMatrix();
    static std::vector<std::complex<float>> phaseMatrix(float phase);
    static std::vector<std::complex<float>> rotationXMatrix(float angle);
    static std::vector<std::complex<float>> rotationYMatrix(float angle);
    static std::vector<std::complex<float>> cnotMatrix();
    static std::vector<std::complex<float>> toffoliMatrix();

    //==============================================================================
    // Internal operations
    void applyRotation(float angle, int qubit);
    void applyPhaseShift(float phase, int qubit);
    void applyCNOT(int control, int target);
    void applyToffoli(int control1, int control2, int target);

    //==============================================================================
    // Density-matrix operations
    void rebuildDensityMatrix();
    void applyMatrixToDensity(const std::vector<std::complex<float>>& gateMatrix,
                              const std::vector<int>& qubitIndices);

    //==============================================================================
    void tensorProduct(const std::vector<float>& a,
                       const std::vector<float>& b,
                       std::vector<float>& result);

    //==============================================================================
    /// Map a partial index to the qubit that controls it
    int qubitForPartial(int partialIndex) const noexcept
    {
        const int bandSize = std::max(1, PartialDataSIMD::kMaxPartials / numQubits_);
        return std::min(partialIndex / bandSize, numQubits_ - 1);
    }

    //==============================================================================
    int                     numQubits_              = 8;
    InterferenceMode        interferenceMode_       = InterferenceMode::Constructive;
    float                   entanglement_           = 0.5f;
    float                   decoherence_            = 0.01f;
    float                   interferenceStrength_   = 0.5f;
    float                   measurementBias_        = 0.5f;
    int                     numSuperpositionStates_ = 4;
    double                  sampleRate_             = 44100.0;

    //==============================================================================
    // Pre-allocated FFT buffers (zero heap allocs in processAudio hot path)
    std::unique_ptr<juce::dsp::FFT> fft_;
    std::vector<float>              fftBuf_;
    std::vector<float>              hannWindow_;
    int                             currentFftOrder_    = -1;

    juce::Random rng_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuantumSpectralProcessor)
};

} // namespace ana
