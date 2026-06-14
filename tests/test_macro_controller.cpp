#include <catch2/catch_all.hpp>
#include "../src/dsp/MacroController.h"

using namespace ana;

TEST_CASE("MacroController - initial state", "[macro][init]")
{
    MacroController mc;
    REQUIRE(mc.getNumTargets() == 0);
    SUCCEED();
}

TEST_CASE("MacroController - mappings", "[macro][mapping]")
{
    MacroController mc;
    mc.setNumMacros(4);
    
    MacroMapping m;
    m.targetParamIndex = 3;
    m.min = 20.0f;
    m.max = 20000.0f;
    m.curve = MacroMapping::Curve::Linear;
    
    mc.addMapping(0, m);
    REQUIRE(mc.getNumTargets() == 1);
    
    mc.setMacroValue(0, 0.5f);
    float val = mc.getTargetValue(3);
    REQUIRE(val == Catch::Approx(10010.0f));
}

TEST_CASE("MacroController - curves", "[macro][curve]")
{
    MacroController mc;
    mc.setNumMacros(1);
    
    MacroMapping m;
    m.targetParamIndex = 0;
    m.min = 0.0f;
    m.max = 1.0f;
    m.curve = MacroMapping::Curve::Exponential;
    
    mc.addMapping(0, m);
    mc.setMacroValue(0, 0.5f);
    
    float val = mc.getTargetValue(0);
    REQUIRE(val == Catch::Approx(0.25f)); // 0.5^2
}
