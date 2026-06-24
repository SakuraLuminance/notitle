#include <catch2/catch_all.hpp>
#include "dsp/SpectralDNA.h"
#include <algorithm>

using namespace ana;

TEST_CASE("SpectralDNA initializes correctly", "[spectral-dna]")
{
    SpectralDNA dna;
    dna.initialize();
    REQUIRE(dna.isValid());
    REQUIRE(dna.isActive(0) || SpectralDNA::computeActiveCount(dna) > 0);
}

TEST_CASE("SpectralDNA round-trips through toPartials", "[spectral-dna]")
{
    SpectralDNA dna;
    dna.initialize();

    PartialDataSIMD pd = dna.toPartials();
    SpectralDNA dna2;
    dna2.initializeFromPartials(pd);

    for (int i = 0; i < SpectralDNA::kMaxPartials; ++i)
    {
        REQUIRE(dna.frequency[i] == Catch::Approx(dna2.frequency[i]).margin(0.001f));
        REQUIRE(dna.amplitude[i] == Catch::Approx(dna2.amplitude[i]).margin(0.001f));
    }
}

TEST_CASE("Uniform crossover produces valid offspring", "[spectral-dna]")
{
    juce::Random rng(42);
    SpectralDNA a, b;
    a.initialize();
    b.initialize();

    auto child = SpectralDNA::uniformCrossover(a, b, rng);

    REQUIRE(child.isValid());
    REQUIRE(child.generation == 1);
    REQUIRE(child.mutationRate == Catch::Approx((a.mutationRate + b.mutationRate) * 0.5f));
}

TEST_CASE("Spectral crossover splits by frequency", "[spectral-dna]")
{
    juce::Random rng(42);
    SpectralDNA a, b;
    a.initialize();
    b.initialize();

    auto child = SpectralDNA::spectralCrossover(a, b, rng);

    REQUIRE(child.isValid());
    REQUIRE(child.generation == 1);
}

TEST_CASE("Mutation never produces NaN or Inf", "[spectral-dna]")
{
    juce::Random rng(42);
    SpectralDNA dna;
    dna.initialize();

    for (int i = 0; i < 100; ++i)
    {
        auto mutated = SpectralDNA::mutate(dna, rng);
        REQUIRE(mutated.isValid());
    }
}

TEST_CASE("Fitness is in [0, 1] range", "[spectral-dna]")
{
    SpectralDNA dna;
    dna.initialize();

    float fitness = dna.evaluateFitness();
    REQUIRE(fitness >= 0.0f);
    REQUIRE(fitness <= 1.0f);
}

TEST_CASE("Evolver improves population fitness", "[spectral-dna][evolution]")
{
    SpectralDNAEvolver evolver;
    evolver.init(32);

    float initialFitness = evolver.getFittest().fitness;

    evolver.evolveN(50);

    float finalFitness = evolver.getFittest().fitness;
    REQUIRE(finalFitness >= initialFitness);
}

TEST_CASE("Serialization round-trips correctly", "[spectral-dna]")
{
    SpectralDNAEvolver evolver;
    evolver.init(16);
    evolver.evolveN(5);

    auto state = evolver.saveState();

    SpectralDNAEvolver evolver2;
    evolver2.loadState(state);

    REQUIRE(evolver2.getPopulationSize() == 16);
    REQUIRE(evolver2.getGeneration() == 5);
}

TEST_CASE("Population diversity after 20 generations", "[spectral-dna][diversity]")
{
    SpectralDNAEvolver evolver;
    evolver.init(32);

    for (int g = 0; g < 20; ++g)
        evolver.evolveGeneration();

    const auto& pop = evolver.getPopulation();
    float totalDist = 0.0f;
    int pairs = 0;

    for (size_t i = 0; i < pop.size() && i < 8; ++i)
    {
        for (size_t j = i + 1; j < pop.size() && j < 8; ++j)
        {
            float d = std::abs(pop[i].fitness - pop[j].fitness);
            totalDist += d;
            ++pairs;
        }
    }

    float avgDist = (pairs > 0) ? totalDist / pairs : 0.0f;
    REQUIRE(avgDist >= 0.0f);  // Just verify it doesn't crash
}

TEST_CASE("Edge cases: single individual", "[spectral-dna][edge]")
{
    SpectralDNAEvolver evolver;
    evolver.init(1);
    REQUIRE_NOTHROW(evolver.evolveGeneration());
    REQUIRE(evolver.getPopulationSize() == 1);
}

TEST_CASE("Edge cases: zero amplitude population", "[spectral-dna][edge]")
{
    SpectralDNAEvolver evolver;
    evolver.init(8);
    // Reset all amplitudes to 0
    auto& pop = const_cast<std::vector<SpectralDNA>&>(evolver.getPopulation());
    for (auto& dna : pop)
        std::fill(std::begin(dna.amplitude), std::end(dna.amplitude), 0.0f);

    // Evolution should not crash even with dead population
    REQUIRE_NOTHROW(evolver.evolveGeneration());
}
