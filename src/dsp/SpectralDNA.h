#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstring>
#include <vector>
#include "PartialDataSIMD.h"
#include "STFTConfig.h"

namespace ana {

// ============================================================================
/** Coefficients controlling the weighting of each spectral metric
    and target values for fitness evaluation.
*/
struct SpectralDNACoefficients
{
    // Fitness weights
    float centroidWeight = 0.2f;       // spectral centroid
    float harmonicWeight = 0.3f;       // harmonic richness
    float flatnessWeight = 0.1f;       // spectral flatness
    float energyWeight   = 0.1f;       // energy distribution
    float countWeight    = 0.1f;       // active partial count

    // Target values
    float targetCentroid    = 0.4f;    // target centroid frequency (normalized)
    float targetActiveRatio = 0.2f;    // target active ratio (~100 of 512)

};

struct SpectralDNA {
    static constexpr int kMaxPartials = 512;

    alignas(32) float frequency[kMaxPartials]{};
    alignas(32) float amplitude[kMaxPartials]{};
    alignas(32) float phase[kMaxPartials]{};
    uint32_t activeMask[16]{};

    int parentA_id = -1;
    int parentB_id = -1;
    int generation = 0;
    float fitness = 0.0f;
    float mutationRate = 0.1f;

    float geneExpression[kMaxPartials];  // 表观遗传

    // 初始化所有数组为 0
    SpectralDNA();

    // 随机初始化（保证生成有意义的音色：少量随机 partial 活跃）
    void initialize();

    // 从现有的 SIMD partial 数据初始化
    void initializeFromPartials(const PartialDataSIMD& partials);

    // 从音频文件分析初始化
    void initializeFromAudio(const std::vector<float>& audio,
                              double sampleRate,
                              const STFTConfig& config = STFTConfig{});

    // 转换为 PartialDataSIMD
    PartialDataSIMD toPartials() const;

    // 钳位所有值到有效范围
    void clamp();

    // 检查 NaN/Inf
    bool isValid() const;

    // 重建 activeMask
    void updateActiveMask();

    // 判断 partial 是否活跃
    bool isActive(int index) const;

    // ========================================================================
    // Fitness evaluation
    // ========================================================================
    /** Heuristic timbre fitness from 5 spectral metrics.
        @param coeffs  per-metric weights (defaults provide balanced scoring)
        @return  clamped fitness in [0, 1]
    */
    float evaluateFitness(
        const SpectralDNACoefficients& coeffs = SpectralDNACoefficients{}) const;

    // ----- Static spectral analysis helpers --------------------------------
    /** Normalised spectral centroid [0, 1].
        centroid = sum(freq * amp) / sum(amp), normalised by 10 kHz. */
    static float computeSpectralCentroid(const SpectralDNA& dna);

    /** Ratio of mid-frequency active partials to total active partials [0, 1].
        Mid = 200-4000 Hz, Low = 20-200 Hz. Higher = more harmonic richness. */
    static float computeHarmonicRichness(const SpectralDNA& dna);

    /** Spectral flatness (geometric mean / arithmetic mean) [0, 1].
        Near 1 = noise-like, near 0 = tonal. */
    static float computeSpectralFlatness(const SpectralDNA& dna);

    /** Low-frequency energy distribution score [0, 1].
        Measures how close low-band energy ratio is to the 60 % target. */
    static float computeEnergyDistribution(const SpectralDNA& dna);

    /** Count of active (non-silent) partials. */
    static int computeActiveCount(const SpectralDNA& dna);

    // ========================================================================
    // Crossover (GA)
    // ========================================================================
    /** Uniform crossover: each partial (freq/amp/phase) inherits as a group
        from one parent with 50 % probability.
    */
    static SpectralDNA uniformCrossover(const SpectralDNA& a,
                                         const SpectralDNA& b,
                                         juce::Random& rng);

    /** Spectral crossover: split spectrum at a random frequency point.
        Low partials (0..splitPoint) are inherited from Parent A,
        high partials (splitPoint..kMaxPartials) from Parent B.
    */
    static SpectralDNA spectralCrossover(const SpectralDNA& a,
                                          const SpectralDNA& b,
                                          juce::Random& rng);

    // ========================================================================
    // Mutation (GA)
    // ========================================================================
    /** 变异入口：按 mutationRate 概率决定是否变异，再按概率选择一种策略 */
    static SpectralDNA mutate(const SpectralDNA& dna, juce::Random& rng);

private:
    static void gaussianMutation(SpectralDNA& dna, juce::Random& rng);         // 40%
    static void spectralDriftMutation(SpectralDNA& dna, juce::Random& rng);   // 20%
    static void geneLossMutation(SpectralDNA& dna, juce::Random& rng);        // 10%
    static void geneGainMutation(SpectralDNA& dna, juce::Random& rng);        // 10%
};



// ============================================================================
/** Manages a population of SpectralDNA individuals and runs the evolution loop.
    Uses tournament selection, elitism, uniform crossover, and multi-strategy mutation.
*/
class SpectralDNAEvolver {
public:
    SpectralDNAEvolver() = default;

    // 初始化
    void init(int populationSize = 16);
    void seedRandom();

    // 进化
    void evolveGeneration();
    void evolveN(int n);

    // 查询
    const SpectralDNA& getDNA(int index) const;
    const std::vector<SpectralDNA>& getPopulation() const { return population_; }
    int getGeneration() const { return generation_; }
    int getPopulationSize() const { return static_cast<int>(population_.size()); }
    const SpectralDNA& getFittest() const;

    // 种群规模变化
    void setPopulationSize(int newSize);

    /** Replace the lowest-fitness individual with a new specimen.
        If the population is empty, it is first initialised to size 1. */
    void replaceWorst(const SpectralDNA& dna);

    // 族谱
    struct GenealogyEntry {
        int individualId;
        int parentAId;
        int parentBId;
        int generation;
        float fitness;
    };
    const std::vector<GenealogyEntry>& getGenealogy() const { return genealogy_; }

    // ========================================================================
    // 序列化
    juce::ValueTree saveState() const;
    void loadState(const juce::ValueTree& state);

    // 文件 I/O
    void saveToFile(const juce::File& file);
    void loadFromFile(const juce::File& file);

private:
    std::vector<SpectralDNA> population_;
    std::vector<SpectralDNA> hallOfFame_;
    std::vector<SpectralDNA> nextGeneration_;
    std::vector<GenealogyEntry> genealogy_;
    int generation_ = 0;
    int nextId_ = 0;
    juce::Random rng_;

    void selection();    // 锦标赛选择
    void elitism();      // 精英保留
    void crossover();    // 种群繁殖
    void mutation();     // 种群变异

    int tournamentSelect();  // 锦标赛选择个体
};

} // namespace ana
