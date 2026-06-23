#include <catch2/catch_all.hpp>
#include "dsp/GenerativeTimbreDesigner.h"
#include <cmath>

using namespace ana;

TEST_CASE("GenerativeTimbreDesigner - initial state", "[timbre][init]")
{
    GenerativeTimbreDesigner gtd;
    SUCCEED();
}

TEST_CASE("GenerativeTimbreDesigner - preset", "[timbre][preset]")
{
    GenerativeTimbreDesigner gtd;
    gtd.loadPreset(GenerativeTimbreDesigner::Preset::Warm);
    
    PartialDataSIMD data;
    gtd.generate(data);
    SUCCEED();
}

TEST_CASE("GenerativeTimbreDesigner - morphing", "[timbre][morph]")
{
    GenerativeTimbreDesigner gtd;
    gtd.loadPreset(GenerativeTimbreDesigner::Preset::Bright);
    
    GenerativeTimbreDesigner::LatentVector target;
    gtd.loadLatentPreset(GenerativeTimbreDesigner::Preset::Dark, target);
    
    gtd.setTargetTimbre(target);
    gtd.morphToTarget(0.5f);
    SUCCEED();
}

TEST_CASE("GenerativeTimbreDesigner - evolution", "[timbre][evolve]")
{
    GenerativeTimbreDesigner gtd;
    gtd.setPopulationSize(10);
    gtd.evolve();
    
    REQUIRE(gtd.getGeneration() == 1);
    
    auto fittest = gtd.getFittest();
    SUCCEED();
}
