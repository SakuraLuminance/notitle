#include "Randomizer.h"

namespace ana {

//==============================================================================
Randomizer::Randomizer()
{
    reseed();
}

//==============================================================================
void Randomizer::setSeed(unsigned int seed)
{
    if (seed == 0)
    {
        reseed();
        return;
    }
    seed_ = seed;
    rng_.seed(seed_);
}

void Randomizer::reseed()
{
    seed_ = static_cast<unsigned int>(std::random_device{}());
    rng_.seed(seed_);
}

//==============================================================================
void Randomizer::setRangePercent(float percent)
{
    // Clamp to one of the allowed values: 5, 10, 25, 50
    if (percent <= 7.5f)
        rangePercent_ = 5.0f;
    else if (percent <= 17.5f)
        rangePercent_ = 10.0f;
    else if (percent <= 37.5f)
        rangePercent_ = 25.0f;
    else
        rangePercent_ = 50.0f;
}

//==============================================================================
float Randomizer::apply(float baseValue, float min, float max)
{
    const float factor = 1.0f + dist_(rng_) * (rangePercent_ / 100.0f);
    const float raw = baseValue * factor;
    return juce::jlimit(min, max, raw);
}

//==============================================================================
juce::ValueTree Randomizer::getState() const
{
    juce::ValueTree tree("Randomizer");
    tree.setProperty("seed", static_cast<int>(seed_), nullptr);
    tree.setProperty("rangePercent", rangePercent_, nullptr);
    return tree;
}

void Randomizer::setState(const juce::ValueTree& tree)
{
    if (!tree.isValid() || !tree.hasType("Randomizer"))
        return;

    const int s = tree.getProperty("seed", 0);
    setSeed(static_cast<unsigned int>(s));

    rangePercent_ = static_cast<float>(tree.getProperty("rangePercent", 25.0f));
}

} // namespace ana
