#include <catch2/catch_all.hpp>
#include "dsp/PhysicalModel.h"
#include <cmath>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("PhysicalModel - initial state", "[physical][init]")
{
    PhysicalModel model;
    model.setSampleRate(testSampleRate);
    SUCCEED();
}

TEST_CASE("PhysicalModel - config", "[physical][config]")
{
    PhysicalModel model;
    model.setSampleRate(testSampleRate);
    model.setModelType(PhysicalModel::ModelType::String);
    model.setStiffness(0.5f);
    model.setDamping(0.2f);
    model.setExcitation(0.8f);
    SUCCEED();
}

TEST_CASE("PhysicalModel - process", "[physical][process]")
{
    PhysicalModel model;
    model.setSampleRate(testSampleRate);
    model.setModelType(PhysicalModel::ModelType::Membrane);
    
    PartialDataSIMD data;
    data.frequency[0] = 440.0f;
    data.amplitude[0] = 1.0f;
    data.activeCount = 1;
    
    model.process(data);
    SUCCEED();
}
