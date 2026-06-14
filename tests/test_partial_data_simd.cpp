#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../src/dsp/PartialDataSIMD.h"

using namespace ana;

TEST_CASE("PartialDataSIMD - Basic Operations", "[PartialDataSIMD]")
{
    PartialDataSIMD data;
    
    SECTION("Initial state is empty")
    {
        REQUIRE(data.activeCount == 0);
        for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
        {
            REQUIRE_FALSE(data.isActive(i));
        }
    }
    
    SECTION("Setting partials active")
    {
        // Setup some data
        data.frequency[0] = 440.0f;
        data.amplitude[0] = 1.0f;
        
        data.frequency[5] = 880.0f;
        data.amplitude[5] = 0.5f;
        
        // Manually set bitmask for tests
        data.activeMask[0] = (1u << 0) | (1u << 5);
        data.activeCount = 2;
        
        REQUIRE(data.isActive(0));
        REQUIRE(data.isActive(5));
        REQUIRE_FALSE(data.isActive(1));
        
        REQUIRE(data.getNextActive(-1) == 0);
        REQUIRE(data.getNextActive(0) == 5);
        REQUIRE(data.getNextActive(5) == -1);
    }
    
    SECTION("Update Active Mask")
    {
        for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
        {
            data.amplitude[i] = 0.0f;
            data.activeMask[i / 32] = 0;
        }
        
        data.amplitude[10] = 0.1f;
        data.amplitude[100] = 0.5f;
        data.amplitude[511] = 0.8f;
        
        data.updateActiveMask();
        
        REQUIRE(data.activeCount == 3);
        REQUIRE(data.isActive(10));
        REQUIRE(data.isActive(100));
        REQUIRE(data.isActive(511));
        REQUIRE_FALSE(data.isActive(0));
        
        REQUIRE(data.getNextActive(-1) == 10);
        REQUIRE(data.getNextActive(10) == 100);
        REQUIRE(data.getNextActive(100) == 511);
        REQUIRE(data.getNextActive(511) == -1);
    }
}
