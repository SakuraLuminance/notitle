#include <catch2/catch_all.hpp>
#include "../src/dsp/FilterModulation.h"

using namespace ana;

TEST_CASE("FilterModulationSystem - initial state", "[mod][init]")
{
    FilterModulationSystem fms;
    REQUIRE(fms.getNumConnections() == 0);
    REQUIRE(fms.getNumFilters() == 1);
}

TEST_CASE("FilterModulationSystem - connection", "[mod][connect]")
{
    FilterModulationSystem fms;
    int id = fms.connect(ModulationSource::LFO1, ModulationTarget::Cutoff, 0, 0.5f, true);
    REQUIRE(id > 0);
    REQUIRE(fms.getNumConnections() == 1);
    
    fms.setSourceValue(ModulationSource::LFO1, 0.5f);
    float mod = fms.getModulationValue(ModulationTarget::Cutoff, 0);
    REQUIRE(mod == Catch::Approx(0.25f)); // 0.5 depth * 0.5 source
}

TEST_CASE("FilterModulationSystem - apply modulation", "[mod][apply]")
{
    FilterModulationSystem fms;
    fms.connect(ModulationSource::Envelope1, ModulationTarget::Resonance, 0, 0.2f, false);
    
    fms.setSourceValue(ModulationSource::Envelope1, 1.0f);
    float res = fms.getModulatedResonance(0.5f, 0);
    REQUIRE(res == Catch::Approx(0.7f));
}

TEST_CASE("FilterModulationSystem - disconnect", "[mod][disconnect]")
{
    FilterModulationSystem fms;
    int id = fms.connect(ModulationSource::LFO1, ModulationTarget::Cutoff, 0, 0.5f, true);
    REQUIRE(fms.getNumConnections() == 1);
    
    bool removed = fms.disconnect(id);
    REQUIRE(removed == true);
    REQUIRE(fms.getNumConnections() == 0);
}
