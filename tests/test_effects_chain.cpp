#include <catch2/catch_all.hpp>
#include "dsp/EffectsChain.h"
#include <juce_dsp/juce_dsp.h>

using namespace ana;

class DummyEffect : public EffectBase
{
public:
    void prepare(const juce::dsp::ProcessSpec&) override {}
    void process(juce::AudioBuffer<float>&) override {}
    void reset() override {}
    juce::ValueTree getState() const override { return {}; }
    void setState(const juce::ValueTree&) override {}
};

TEST_CASE("EffectsChain - initial state", "[effects][init]")
{
    EffectsChain chain;
    REQUIRE(chain.getNumEffects() == 0);
}

TEST_CASE("EffectsChain - add and remove", "[effects][add]")
{
    EffectsChain chain;
    int id1 = chain.addEffect(std::make_unique<DummyEffect>(), "Dummy 1");
    int id2 = chain.addEffect(std::make_unique<DummyEffect>(), "Dummy 2");
    
    REQUIRE(chain.getNumEffects() == 2);
    REQUIRE(chain.getEffect(id1).name == "Dummy 1");
    
    chain.removeEffect(0);
    REQUIRE(chain.getNumEffects() == 1);
    REQUIRE(chain.getEffect(0).name == "Dummy 2");
}

TEST_CASE("EffectsChain - bypass and mix", "[effects][bypass]")
{
    EffectsChain chain;
    int id = chain.addEffect(std::make_unique<DummyEffect>(), "Dummy");
    
    chain.bypassEffect(id, true);
    REQUIRE(chain.getEffect(id).bypassed == true);
    
    chain.setMix(id, 0.5f);
    REQUIRE(chain.getEffect(id).mix == Catch::Approx(0.5f));
}

TEST_CASE("EffectsChain - process", "[effects][process]")
{
    EffectsChain chain;
    chain.addEffect(std::make_unique<DummyEffect>(), "Dummy");
    
    juce::dsp::ProcessSpec spec { 44100.0, 512, 2 };
    chain.prepare(spec);
    
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    chain.process(buffer);
    SUCCEED();
}
