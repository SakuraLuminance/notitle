#include <catch2/catch_all.hpp>
#include "dsp/ParallelProcessor.h"
#include <cmath>

using namespace ana;

TEST_CASE("ParallelProcessor - initial state", "[parallel][init]")
{
    ParallelProcessor pp;
    pp.init(2); // test with 2 threads
    REQUIRE(pp.getNumThreads() == 2);
}

TEST_CASE("ParallelProcessor - parallelFor", "[parallel][for]")
{
    ParallelProcessor pp;
    pp.init(4);
    
    std::atomic<int> sum{0};
    pp.parallelFor(0, 100, [&](int start, int end) {
        for (int i = start; i < end; ++i) {
            sum += 1;
        }
    });
    
    REQUIRE(sum.load() == 100);
}

TEST_CASE("ParallelProcessor - processPartials", "[parallel][process]")
{
    ParallelProcessor pp;
    pp.init(2);
    
    PartialDataSIMD data;
    data.frequency[0] = 440.0f;
    data.amplitude[0] = 1.0f;
    data.activeCount = 1;
    
    pp.processPartials(data, [](float* freq, float* amp, float* phase, int start, int end, void*) {
        for (int i = start; i < end; ++i) {
            freq[i] *= 2.0f;
        }
    });
    
    REQUIRE(data.frequency[0] == Catch::Approx(880.0f));
}
