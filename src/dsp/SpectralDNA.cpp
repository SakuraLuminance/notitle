#include "SpectralDNA.h"

#include <algorithm>
#include <cmath>

namespace ana {

SpectralDNA::SpectralDNA()
{
    std::memset(amplitude, 0, sizeof(amplitude));
    std::memset(frequency, 0, sizeof(frequency));
    std::memset(phase, 0, sizeof(phase));
    std::memset(activeMask, 0, sizeof(activeMask));
    std::memset(geneExpression, 0, sizeof(geneExpression));

    parentA_id = -1;
    parentB_id = -1;
    generation = 0;
    fitness = 0.0f;
    mutationRate = 0.1f;
}

void SpectralDNA::initialize()
{
    juce::Random rng;

    // Random active count between 10 and 50
    const int numActive = 10 + rng.nextInt(41);

    // Zero everything first
    std::memset(frequency, 0, sizeof(frequency));
    std::memset(amplitude, 0, sizeof(amplitude));
    std::memset(phase, 0, sizeof(phase));
    std::memset(activeMask, 0, sizeof(activeMask));

    // Activate a random set of partials with meaningful timbral values
    for (int i = 0; i < numActive; ++i)
    {
        const int idx = rng.nextInt(kMaxPartials);

        frequency[idx] = 20.0f + static_cast<float>(rng.nextDouble()) * 7980.0f;    // 20-8000 Hz
        amplitude[idx] = 0.1f + static_cast<float>(rng.nextDouble()) * 0.9f;        // 0.1-1.0
        phase[idx]     = static_cast<float>(rng.nextDouble())
                         * juce::MathConstants<float>::twoPi;                        // 0-2π
    }

    mutationRate = 0.1f;

    updateActiveMask();
}

void SpectralDNA::initializeFromPartials(const PartialDataSIMD& partials)
{
    std::memcpy(frequency, partials.frequency, sizeof(frequency));
    std::memcpy(amplitude, partials.amplitude, sizeof(amplitude));
    std::memcpy(phase, partials.phase, sizeof(phase));

    updateActiveMask();
}

void SpectralDNA::initializeFromAudio(const std::vector<float>& /*audio*/,
                                       double /*sampleRate*/,
                                       const STFTConfig& /*config*/)
{
    // NOTE: Full audio-to-partial analysis pipeline will be implemented in Task 7.
    // For now this is a no-op — callers should analyze audio externally and pass
    // the resulting PartialDataSIMD to initializeFromPartials().
}

PartialDataSIMD SpectralDNA::toPartials() const
{
    PartialDataSIMD result;

    std::memcpy(result.frequency, frequency, sizeof(frequency));
    std::memcpy(result.amplitude, amplitude, sizeof(amplitude));
    std::memcpy(result.phase, phase, sizeof(phase));
    std::memcpy(result.activeMask, activeMask, sizeof(activeMask));

    // Recompute activeCount from the mask
    result.activeCount = 0;
    for (int i = 0; i < kMaxPartials; ++i)
        if (isActive(i))
            ++result.activeCount;

    return result;
}

void SpectralDNA::clamp()
{
    for (int i = 0; i < kMaxPartials; ++i)
    {
        amplitude[i] = std::clamp(amplitude[i], 0.0f, 1.0f);
        frequency[i] = std::clamp(frequency[i], 0.0f, 20000.0f);
        phase[i]     = std::fmod(phase[i], juce::MathConstants<float>::twoPi);
        if (phase[i] < 0.0f)
            phase[i] += juce::MathConstants<float>::twoPi;
    }

    mutationRate = std::clamp(mutationRate, 0.001f, 0.5f);
}

bool SpectralDNA::isValid() const
{
    for (int i = 0; i < kMaxPartials; ++i)
    {
        if (std::isnan(frequency[i]) || std::isinf(frequency[i]))
            return false;
        if (std::isnan(amplitude[i]) || std::isinf(amplitude[i]))
            return false;
        if (amplitude[i] < 0.0f)
            return false;
        if (std::isnan(phase[i]) || std::isinf(phase[i]))
            return false;
    }

    if (std::isnan(mutationRate) || std::isinf(mutationRate))
        return false;
    if (mutationRate < 0.001f || mutationRate > 0.5f)
        return false;

    return true;
}

void SpectralDNA::updateActiveMask()
{
    static constexpr float kThreshold = 1e-6f;

    std::memset(activeMask, 0, sizeof(activeMask));

    for (int i = 0; i < kMaxPartials; ++i)
    {
        if (amplitude[i] > kThreshold)
        {
            const int word = i >> 5;
            const int bit  = i & 31;
            activeMask[word] |= (1u << bit);
        }
    }
}

bool SpectralDNA::isActive(int index) const
{
    if (index < 0 || index >= kMaxPartials)
        return false;

    const int word = index >> 5;
    const int bit  = index & 31;
    return (activeMask[word] & (1u << bit)) != 0;
}

SpectralDNA SpectralDNA::uniformCrossover(const SpectralDNA& a,
                                           const SpectralDNA& b,
                                           juce::Random& rng)
{
    SpectralDNA child;

    // 每个 partial 的 3 个参数（freq/amp/phase）一起从同个父本继承
    for (int i = 0; i < kMaxPartials; ++i)
    {
        if (rng.nextBool())
        {
            child.frequency[i] = a.frequency[i];
            child.amplitude[i] = a.amplitude[i];
            child.phase[i]     = a.phase[i];
        }
        else
        {
            child.frequency[i] = b.frequency[i];
            child.amplitude[i] = b.amplitude[i];
            child.phase[i]     = b.phase[i];
        }
    }

    // 族谱
    child.parentA_id = a.parentA_id;
    child.parentB_id = b.parentB_id;
    child.generation = std::max(a.generation, b.generation) + 1;
    child.mutationRate = (a.mutationRate + b.mutationRate) * 0.5f;
    child.fitness = 0.0f;  // 需要重新评估

    child.updateActiveMask();
    child.clamp();

    return child;
}

// ============================================================================
// Static spectral helpers
// ============================================================================

float SpectralDNA::computeSpectralCentroid(const SpectralDNA& dna)
{
    float weightedSum = 0.0f;
    float totalAmp    = 0.0f;

    for (int i = 0; i < kMaxPartials; ++i)
    {
        weightedSum += dna.frequency[i] * dna.amplitude[i];
        totalAmp    += dna.amplitude[i];
    }

    if (totalAmp < kAmplitudeThreshold)
        return 0.0f;

    const float centroid = weightedSum / totalAmp;          // Hz
    return std::min(1.0f, centroid / kNormalizationFreq);   // normalised by 10 kHz
}

float SpectralDNA::computeHarmonicRichness(const SpectralDNA& dna)
{
    int lowCount = 0;    // 20-200 Hz
    int midCount = 0;    // 200-4000 Hz

    for (int i = 0; i < kMaxPartials; ++i)
    {
        if (dna.amplitude[i] <= kAmplitudeThreshold)
            continue;

        const float freq = dna.frequency[i];
        if (freq >= 20.0f && freq < 200.0f)
            ++lowCount;
        else if (freq >= 200.0f && freq <= 4000.0f)
            ++midCount;
    }

    const int total = lowCount + midCount;
    if (total == 0)
        return 0.0f;

    return static_cast<float>(midCount) / static_cast<float>(total);
}

float SpectralDNA::computeSpectralFlatness(const SpectralDNA& dna)
{
    constexpr float kEpsilon = 1e-10f;
    float logSum    = 0.0f;
    float arithSum  = 0.0f;
    int   count     = 0;

    for (int i = 0; i < kMaxPartials; ++i)
    {
        const float a = dna.amplitude[i];
        if (a > kAmplitudeThreshold)
        {
            logSum   += std::log(a + kEpsilon);
            arithSum += a;
            ++count;
        }
    }

    if (count == 0 || arithSum < kEpsilon)
        return 1.0f;    // silence is noise-like

    const float geoMean   = std::exp(logSum / static_cast<float>(count));
    const float arithMean = arithSum / static_cast<float>(count);

    return geoMean / arithMean;
}

float SpectralDNA::computeEnergyDistribution(const SpectralDNA& dna)
{
    constexpr int kLowBand = 50;   // first 50 partials ≈ low-band
    float lowEnergy   = 0.0f;
    float totalEnergy = 0.0f;

    for (int i = 0; i < kMaxPartials; ++i)
    {
        const float energy = dna.amplitude[i] * dna.amplitude[i];
        totalEnergy += energy;
        if (i < kLowBand)
            lowEnergy += energy;
    }

    if (totalEnergy < 1e-10f)
        return 0.0f;

    const float lowRatio = lowEnergy / totalEnergy;
    float score = 1.0f - std::abs(lowRatio - 0.6f) * 2.0f;
    return std::max(0.0f, std::min(1.0f, score));
}

int SpectralDNA::computeActiveCount(const SpectralDNA& dna)
{
    int count = 0;
    for (int i = 0; i < kMaxPartials; ++i)
        if (dna.amplitude[i] > kAmplitudeThreshold)
            ++count;
    return count;
}

// ============================================================================
// Fitness evaluation
// ============================================================================

float SpectralDNA::evaluateFitness(
    const SpectralDNACoefficients& coeffs) const
{
    const float sc = computeSpectralCentroid(*this);
    const float hr = computeHarmonicRichness(*this);
    const float sf = computeSpectralFlatness(*this);
    const float ed = computeEnergyDistribution(*this);
    const int   ac = computeActiveCount(*this);

    // Count score: target ~100 active partials (≈20 % of 512)
    const float countScore = std::max(0.0f,
        1.0f - std::abs(static_cast<float>(ac) - 100.0f) / 100.0f);

    float fitness = 0.0f;
    fitness += sc * coeffs.centroidWeight;
    fitness += hr * coeffs.harmonicWeight;
    fitness += (1.0f - sf) * coeffs.flatnessWeight;  // low flatness = tonal = good
    fitness += ed * coeffs.energyWeight;
    fitness += countScore * coeffs.countWeight;

    const float totalWeight = coeffs.centroidWeight
                            + coeffs.harmonicWeight
                            + coeffs.flatnessWeight
                            + coeffs.energyWeight
                            + coeffs.countWeight;

    if (totalWeight > 0.0f)
        fitness /= totalWeight;

    return std::clamp(fitness, 0.0f, 1.0f);
}

// =============================================================================
// Mutation — unified entry point
// =============================================================================
SpectralDNA SpectralDNA::mutate(const SpectralDNA& dna, juce::Random& rng)
{
    SpectralDNA result = dna;

    // 先决定是否突变（按 mutationRate 概率）
    if (rng.nextFloat() > dna.mutationRate)
        return result;

    // 按概率选择突变类型
    const float r = rng.nextFloat();
    if (r < 0.40f)          gaussianMutation(result, rng);
    else if (r < 0.60f)     spectralDriftMutation(result, rng);
    else if (r < 0.70f)     geneLossMutation(result, rng);
    else                    geneGainMutation(result, rng);

    result.clamp();
    result.updateActiveMask();
    return result;
}

// =============================================================================
// Mutation strategy 1: GaussianMutation (40%)
// 给每个活跃 partial 的 freq/amp 加高斯噪声 (Box-Muller)
// =============================================================================
void SpectralDNA::gaussianMutation(SpectralDNA& dna, juce::Random& rng)
{
    for (int i = 0; i < SpectralDNA::kMaxPartials; ++i)
    {
        if (!dna.isActive(i)) continue;

        // Box-Muller 变换生成 N(0,1)
        const float u1 = rng.nextFloat();
        const float u2 = rng.nextFloat();
        const float gauss = std::sqrt(-2.0f * std::log(u1 + 1e-10f))
                          * std::cos(2.0f * juce::MathConstants<float>::pi * u2);

        dna.frequency[i] *= (1.0f + gauss * 0.05f);  // 5% 频率偏移
        dna.amplitude[i] += gauss * 0.1f;            // 振幅增量
    }
}

// =============================================================================
// Mutation strategy 2: SpectralDriftMutation (20%)
// 随机选中一个频段做整体频移
// =============================================================================
void SpectralDNA::spectralDriftMutation(SpectralDNA& dna, juce::Random& rng)
{
    const int bandStart = static_cast<int>(
        rng.nextFloat() * static_cast<float>(SpectralDNA::kMaxPartials - 20));
    const int bandLen   = 10 + static_cast<int>(rng.nextFloat() * 40.0f);
    const int bandEnd   = std::min(bandStart + bandLen, SpectralDNA::kMaxPartials);
    const float shift   = (rng.nextFloat() - 0.5f) * 0.2f;  // ±10%

    for (int i = bandStart; i < bandEnd; ++i)
    {
        if (dna.isActive(i))
            dna.frequency[i] *= (1.0f + shift);
    }
}

// =============================================================================
// Mutation strategy 3: GeneLossMutation (10%)
// 随机关闭 1-3 个活跃 partial
// =============================================================================
void SpectralDNA::geneLossMutation(SpectralDNA& dna, juce::Random& rng)
{
    int active[512];
    int count = 0;
    for (int i = 0; i < SpectralDNA::kMaxPartials; ++i)
        if (dna.isActive(i)) active[count++] = i;

    if (count <= 1) return;  // 至少保留一个活跃 partial

    const int toLose = std::min(1 + static_cast<int>(rng.nextFloat() * 3.0f),
                                count - 1);
    for (int j = 0; j < toLose; ++j)
    {
        const int idx = active[static_cast<int>(rng.nextFloat()
                                               * static_cast<float>(count))];
        dna.amplitude[idx] = 0.0f;
        dna.phase[idx]     = 0.0f;
    }
}

// =============================================================================
// Mutation strategy 4: GeneGainMutation (10%)
// 随机激活 1-3 个非活跃 partial
// =============================================================================
void SpectralDNA::geneGainMutation(SpectralDNA& dna, juce::Random& rng)
{
    int inactive[512];
    int count = 0;
    for (int i = 0; i < SpectralDNA::kMaxPartials; ++i)
        if (!dna.isActive(i)) inactive[count++] = i;

    if (count == 0) return;

    const int toGain = std::min(1 + static_cast<int>(rng.nextFloat() * 3.0f),
                                count);
    for (int j = 0; j < toGain; ++j)
    {
        const int idx = inactive[static_cast<int>(rng.nextFloat()
                                                 * static_cast<float>(count))];
        dna.frequency[idx] = 20.0f + rng.nextFloat() * 8000.0f;
        dna.amplitude[idx] = 0.1f + rng.nextFloat() * 0.5f;
        dna.phase[idx]     = rng.nextFloat() * juce::MathConstants<float>::twoPi;
    }
}

} // namespace ana
