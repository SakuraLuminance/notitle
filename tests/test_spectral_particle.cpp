#include <catch2/catch_all.hpp>
#include "../src/dsp/SpectralParticleSystem.h"
#include <cmath>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("SpectralParticleSystem - initial state", "[particle][init]")
{
    SpectralParticleSystem ps;
    SUCCEED();
}

TEST_CASE("SpectralParticleSystem - config", "[particle][config]")
{
    SpectralParticleSystem ps;
    ps.setMaxParticles(1024);
    ps.setEmissionRate(50.0f);
    ps.setParticleLife(1.5f);
    ps.setGravity(9.8f);
    SUCCEED();
}

TEST_CASE("SpectralParticleSystem - forces", "[particle][forces]")
{
    SpectralParticleSystem ps;
    SpectralParticleSystem::ForceField field;
    field.type = SpectralParticleSystem::ForceField::Type::Attractor;
    field.strength = 1.0f;
    field.position = 0.5f;
    field.radius = 0.1f;
    
    ps.addForceField(field);
    REQUIRE(ps.getNumForceFields() == 1);
    
    ps.clearForceFields();
    REQUIRE(ps.getNumForceFields() == 0);
}

TEST_CASE("SpectralParticleSystem - emission and update", "[particle][process]")
{
    SpectralParticleSystem ps;
    ps.emitBurst(10);
    
    ps.update(0.1); // advance 100ms
    
    PartialDataSIMD data;
    ps.process(data);
    SUCCEED();
}
