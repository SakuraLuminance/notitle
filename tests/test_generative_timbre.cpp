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

// Regression test for P1: latent-modifying setters must mark the LUT cache
// dirty so the next generate() rebuilds the transcendental LUTs from the new
// latent. Before the fix, setLatentVector/loadPreset/etc. left lutsDirty_
// false, so generate() reused STALE tilt/formant/inharm LUTs from the
// previous latent — producing incorrect high-harmonic amplitudes.
TEST_CASE("GenerativeTimbreDesigner - LUTs rebuild after setLatentVector (P1)", "[timbre][lut]")
{
    GenerativeTimbreDesigner gtd;

    // Two latents identical except spectral tilt (values[30],[39]).
    // Steep tilt -> high harmonics roll off quickly (quiet).
    // Shallow tilt -> high harmonics stay loud.
    GenerativeTimbreDesigner::LatentVector steep, shallow;
    for (int i = 0; i < 64; ++i)
    {
        steep.values[i]   = 0.5f;
        shallow.values[i] = 0.5f;
    }
    steep.values[30]   = -0.8f;  steep.values[39]   = -0.8f;
    shallow.values[30] =  0.8f;  shallow.values[39] =  0.8f;

    PartialDataSIMD steepOut, shallowOut;

    gtd.setLatentVector(steep);
    gtd.generate(steepOut);

    gtd.setLatentVector(shallow);
    gtd.generate(shallowOut);

    // Harmonic 100 (index 99) amplitude is dominated by the tiltLUT rolloff.
    // With stale LUTs (the bug) shallow would reuse steep's rolloff and come
    // out equal; with the fix shallow's high harmonic must be louder.
    INFO("steep amp[99]=" << steepOut.amplitude[99]
         << " shallow amp[99]=" << shallowOut.amplitude[99]);
    REQUIRE(shallowOut.amplitude[99] > steepOut.amplitude[99]);
}

// Regression test for the deeper LUT-consistency bug: generateFromLatent()
// accepts an arbitrary latent, but latentToPartials() only rebuilt LUTs on
// the lutsDirty_ flag. A second call with a *different* latent therefore
// reused the first latent's cached LUTs. The fix compares the incoming
// latent against a stored snapshot and rebuilds when they differ.
TEST_CASE("GenerativeTimbreDesigner - generateFromLatent uses matching LUTs", "[timbre][lut]")
{
    GenerativeTimbreDesigner gtd;

    GenerativeTimbreDesigner::LatentVector steep, shallow;
    for (int i = 0; i < 64; ++i)
    {
        steep.values[i]   = 0.5f;
        shallow.values[i] = 0.5f;
    }
    steep.values[30]   = -0.8f;  steep.values[39]   = -0.8f;
    shallow.values[30] =  0.8f;  shallow.values[39] =  0.8f;

    PartialDataSIMD steepOut, shallowOut;

    gtd.generateFromLatent(steep, steepOut);    // builds LUTs from steep
    gtd.generateFromLatent(shallow, shallowOut); // must rebuild from shallow

    INFO("steep amp[99]=" << steepOut.amplitude[99]
         << " shallow amp[99]=" << shallowOut.amplitude[99]);
    REQUIRE(shallowOut.amplitude[99] > steepOut.amplitude[99]);
}
