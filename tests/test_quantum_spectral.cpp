#include <catch2/catch_all.hpp>
#include "dsp/QuantumSpectralProcessor.h"
#include <cmath>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("QuantumSpectralProcessor - initial state", "[quantum][init]")
{
    QuantumSpectralProcessor qsp;
    qsp.setSampleRate(testSampleRate);
    SUCCEED();
}

TEST_CASE("QuantumSpectralProcessor - config", "[quantum][config]")
{
    QuantumSpectralProcessor qsp;
    qsp.initState(4);
    qsp.setInterferenceMode(QuantumSpectralProcessor::InterferenceMode::Constructive);
    qsp.setEntanglement(0.8f);
    qsp.setDecoherence(0.1f);
    SUCCEED();
}

TEST_CASE("QuantumSpectralProcessor - gates", "[quantum][gates]")
{
    QuantumSpectralProcessor qsp;
    qsp.initState(4);
    qsp.applyGate(QuantumSpectralProcessor::GateType::Hadamard, 0);
    qsp.applyControlledGate(QuantumSpectralProcessor::GateType::CNOT, 0, 1);
    SUCCEED();
}

TEST_CASE("QuantumSpectralProcessor - process", "[quantum][process]")
{
    QuantumSpectralProcessor qsp;
    qsp.setSampleRate(testSampleRate);
    qsp.initState(4);
    
    PartialDataSIMD data;
    data.frequency[0] = 440.0f;
    data.amplitude[0] = 1.0f;
    data.activeCount = 1;
    
    qsp.process(data);
    SUCCEED();
}
