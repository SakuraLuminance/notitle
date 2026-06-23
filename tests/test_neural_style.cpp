#include <catch2/catch_all.hpp>
#include "dsp/NeuralStyleTransfer.h"
#include <cmath>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("NeuralStyleTransfer - initial state", "[neural][init]")
{
    NeuralStyleTransfer nst;
    nst.setSampleRate(testSampleRate);
    SUCCEED();
}

TEST_CASE("NeuralStyleTransfer - config", "[neural][config]")
{
    NeuralStyleTransfer nst;
    nst.setStrength(0.8f);
    nst.setPreserveTransients(true);
    nst.setIterations(5);
    nst.setSpectralSmoothness(0.5f);
    SUCCEED();
}

TEST_CASE("NeuralStyleTransfer - audio process", "[neural][process]")
{
    NeuralStyleTransfer nst;
    nst.setSampleRate(testSampleRate);
    
    std::vector<float> content(1024, 0.5f);
    std::vector<float> style(1024, 0.1f);
    
    nst.setContent(content, testSampleRate);
    nst.setStyle(style, testSampleRate);
    
    auto output = nst.process();
    REQUIRE(output.size() > 0);
}

TEST_CASE("NeuralStyleTransfer - partial process", "[neural][process]")
{
    NeuralStyleTransfer nst;
    nst.setSampleRate(testSampleRate);
    
    PartialDataSIMD data;
    data.frequency[0] = 440.0f;
    data.amplitude[0] = 1.0f;
    data.activeCount = 1;
    
    nst.process(data);
    SUCCEED();
}
