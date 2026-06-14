#include "GenerativeTimbreDesigner.h"

#include <cmath>
#include <algorithm>
#include <cstring>

namespace ana {

// ============================================================================
// Anonymous-namespace helpers
// ============================================================================
namespace {

// ----------------------------------------------------------------------------
// Core latent-to-partials conversion (stateless, pure algorithm).
// Used by both GenerativeTimbreDesigner::latentToPartials and Individual::generate.
// ----------------------------------------------------------------------------
static void latentToPartialsCore(
    const GenerativeTimbreDesigner::LatentVector& latent,
    PartialDataSIMD& output,
    double sampleRate)
{
    output = PartialDataSIMD{};
    output.sampleRate = sampleRate;

    // ---- helper lambdas ----------------------------------------------------
    auto map01 = [](float v) { return v * 0.5f + 0.5f; };
    auto clamp01 = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };

    // ---- global controls ---------------------------------------------------
    const float volume     = map01(latent.values[60]);
    const float brightness = map01(latent.values[62]);
    const float warmth     = map01(latent.values[63]);

    // ---- spectral tilt -----------------------------------------------------
    // values[30] = noise spectral tilt, values[39] = spare tilt envelope
    const float tiltAvg = latent.values[30] * 0.7f + latent.values[39] * 0.3f;
    // tiltAvg ∈ [-1, 1] → exponent ∈ [2.8 … 1.2]  (lower = shallower = brighter)
    const float tiltExponent = 2.0f - tiltAvg * 0.8f;

    // ---- inharmonicity -----------------------------------------------------
    const float inharmAmount = latent.values[40] * 0.06f; // scale to ±6%

    // ---- formants ----------------------------------------------------------
    struct Formant { float freqHz, amp, bwHz; };
    Formant formants[3];
    for (int f = 0; f < 3; ++f) {
        const int base = 20 + f * 3;
        formants[f].freqHz = juce::jmap(latent.values[base + 0], -1.0f, 1.0f, 100.0f, 8000.0f);
        formants[f].amp    = juce::jmap(latent.values[base + 1], -1.0f, 1.0f, 0.0f, 10.0f);
        formants[f].bwHz   = juce::jmap(latent.values[base + 2], -1.0f, 1.0f, 60.0f, 1200.0f);
    }

    // ---- generate each partial ---------------------------------------------
    constexpr float kFundamental = 100.0f;

    for (int h = 1; h <= PartialDataSIMD::kMaxPartials; ++h) {
        float amp;

        // ----- 1. Harmonic envelope (values[0-19] → harmonics 1-20) --------
        if (h <= 20) {
            float norm = map01(latent.values[h - 1]);
            amp = norm * norm;   // square-law for natural curve
        }
        // ----- 2. Extension beyond 20th harmonic ----------------------------
        else {
            const float ratio20 = 20.0f / static_cast<float>(h);
            // Roll-off ∝ (20/h)^tiltExponent
            float rolloff = std::pow(ratio20, tiltExponent);

            // Brightness boost for upper harmonics
            float brightBoost = 0.0f;
            if (brightness > 0.5f) {
                const float pos = static_cast<float>(h - 20)
                    / static_cast<float>(PartialDataSIMD::kMaxPartials - 20);
                brightBoost = (brightness - 0.5f) * 2.0f * 0.3f
                    * std::sin(pos * juce::MathConstants<float>::pi * 0.5f);
            }

            // Tail amplitude from last envelope point
            float baseNorm = map01(latent.values[19]);
            float baseAmp  = baseNorm * baseNorm;

            amp = baseAmp * rolloff * (1.0f + brightBoost);
        }

        // ----- 3. Warmth boost (low harmonics) ------------------------------
        if (warmth > 0.5f && h <= 10) {
            const float factor = 1.0f - static_cast<float>(h - 1) / 10.0f;
            amp *= 1.0f + (warmth - 0.5f) * 2.0f * factor * 0.6f;
        }

        // ----- 4. Formant filtering (Gaussian peaks) ------------------------
        float formantGain = 1.0f;
        const float hz = static_cast<float>(h) * kFundamental;
        for (int f = 0; f < 3; ++f) {
            const float diff = std::abs(hz - formants[f].freqHz);
            if (diff < formants[f].bwHz * 4.0f) {
                const float gauss = std::exp(
                    -(diff * diff) / (2.0f * formants[f].bwHz * formants[f].bwHz));
                formantGain += formants[f].amp * gauss * 0.15f;
            }
        }
        amp *= formantGain;

        // ----- 5. Inharmonic frequency stretching ---------------------------
        float freq = static_cast<float>(h) * kFundamental;
        if (inharmAmount != 0.0f)
            freq *= std::pow(static_cast<float>(h), inharmAmount);

        // Clamp and store
        amp = clamp01(amp) * volume;

        output.amplitude[h - 1] = amp;
        output.frequency[h - 1] = freq;
        output.phase[h - 1]     = 0.0f;
    }

    output.updateActiveMask();
}

// ----------------------------------------------------------------------------
// Build a vector of base-harmonic frequencies for use by Individual::generate.
// ----------------------------------------------------------------------------
static std::vector<float> defaultBaseHarmonics(int count, float fundamental)
{
    std::vector<float> h;
    h.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        h.push_back(static_cast<float>(i + 1) * fundamental);
    return h;
}

// ----------------------------------------------------------------------------
// Heuristic fitness for "interesting" timbre (used when no target is set).
// ----------------------------------------------------------------------------
static float heuristicFitness(const PartialDataSIMD& timbre)
{
    float energy  = 0.0f;
    float spread  = 0.0f;
    float oddSum  = 0.0f;
    int   nonZero = 0;

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i) {
        const float a = timbre.amplitude[i];
        energy += a * a;
        if (a > 1e-4f) {
            ++nonZero;
            spread += static_cast<float>(i);
            if ((i % 2) == 0)          // odd harmonic (0-indexed: index 0 = H1)
                oddSum += a;
        }
    }

    if (nonZero == 0)
        return 0.0f;

    spread /= static_cast<float>(nonZero * PartialDataSIMD::kMaxPartials);
    const float richness = static_cast<float>(nonZero) / static_cast<float>(PartialDataSIMD::kMaxPartials);
    const float evenness = 1.0f - std::abs(spread - 0.5f) * 2.0f;

    return energy * 0.30f + richness * 0.35f + evenness * 0.20f + oddSum * 0.15f;
}

} // anonymous namespace

// ============================================================================
// LatentVector
// ============================================================================

void GenerativeTimbreDesigner::LatentVector::randomize(juce::Random& rng)
{
    for (auto& v : values)
        v = rng.nextFloat() * 2.0f - 1.0f;
}

void GenerativeTimbreDesigner::LatentVector::interpolate(
    const LatentVector& a, const LatentVector& b, float t)
{
    const float ct = std::max(0.0f, std::min(1.0f, t));
    for (int i = 0; i < 64; ++i)
        values[i] = a.values[i] * (1.0f - ct) + b.values[i] * ct;
}

void GenerativeTimbreDesigner::LatentVector::mutate(float amount, juce::Random& rng)
{
    const float a = std::max(0.0f, amount);
    for (auto& v : values) {
        v += rng.nextFloat() * 2.0f * a - a;
        v = std::max(-1.0f, std::min(1.0f, v));
    }
}

float GenerativeTimbreDesigner::LatentVector::distance(const LatentVector& other) const
{
    float sumSq = 0.0f;
    for (int i = 0; i < 64; ++i) {
        const float d = values[i] - other.values[i];
        sumSq += d * d;
    }
    return std::sqrt(sumSq);
}

// ============================================================================
// Individual
// ============================================================================

void GenerativeTimbreDesigner::Individual::generate(
    PartialDataSIMD& output,
    const std::vector<float>& baseHarmonics)
{
    // Run core algorithm into our timbre member
    latentToPartialsCore(genome, timbre, 44100.0);

    // Overwrite frequencies from the provided harmonic series
    const int count = std::min(static_cast<int>(baseHarmonics.size()),
                               PartialDataSIMD::kMaxPartials);
    for (int i = 0; i < count; ++i)
        timbre.frequency[i] = baseHarmonics[i];

    timbre.sampleRate = 44100.0;
    timbre.updateActiveMask();

    // Copy to the external output reference
    std::memcpy(output.amplitude,  timbre.amplitude,  sizeof(timbre.amplitude));
    std::memcpy(output.frequency,  timbre.frequency,  sizeof(timbre.frequency));
    std::memcpy(output.phase,      timbre.phase,      sizeof(timbre.phase));
    std::memcpy(output.activeMask, timbre.activeMask,  sizeof(timbre.activeMask));
    output.activeCount = timbre.activeCount;
    output.sampleRate  = timbre.sampleRate;
    output.maxPartials = timbre.maxPartials;
    output.hopSize     = timbre.hopSize;
}

// ============================================================================
// Constructor
// ============================================================================

GenerativeTimbreDesigner::GenerativeTimbreDesigner()
{
    initPresets();
    juce::Random rng;
    currentLatent_.randomize(rng);
    targetLatent_ = currentLatent_;
}

// ============================================================================
// Latent vector control
// ============================================================================

void GenerativeTimbreDesigner::setLatentVector(const LatentVector& v) { currentLatent_ = v; }
GenerativeTimbreDesigner::LatentVector GenerativeTimbreDesigner::getLatentVector() const { return currentLatent_; }

void GenerativeTimbreDesigner::randomizeLatent()
{
    juce::Random rng;
    currentLatent_.randomize(rng);
}

void GenerativeTimbreDesigner::mutateLatent(float amount)
{
    juce::Random rng;
    currentLatent_.mutate(amount, rng);
}

// ============================================================================
// Interpolation / morphing
// ============================================================================

void GenerativeTimbreDesigner::setTargetTimbre(const LatentVector& target)
{
    targetLatent_ = target;
    morphProgress_ = 0.0f;
}

void GenerativeTimbreDesigner::morphToTarget(float t)
{
    morphProgress_ += t * morphSpeed_;
    morphProgress_  = std::max(0.0f, std::min(1.0f, morphProgress_));

    LatentVector result;
    result.interpolate(currentLatent_, targetLatent_, morphProgress_);
    currentLatent_ = result;
}

void GenerativeTimbreDesigner::setMorphSpeed(float speed)
{
    morphSpeed_ = std::max(0.0f, speed);
}

// ============================================================================
// Genetic algorithm — configuration
// ============================================================================

void GenerativeTimbreDesigner::setPopulationSize(int size)
{
    populationSize_ = std::max(8, std::min(128, size));
}

void GenerativeTimbreDesigner::setMutationRate(float rate)
{
    mutationRate_ = std::max(0.0f, std::min(1.0f, rate));
}

void GenerativeTimbreDesigner::setCrossoverRate(float rate)
{
    crossoverRate_ = std::max(0.0f, std::min(1.0f, rate));
}

// ============================================================================
// Genetic algorithm — core
// ============================================================================

void GenerativeTimbreDesigner::evaluateFitness(Individual& individual)
{
    // Proximity to target latent vector
    const float dist = individual.genome.distance(targetLatent_);
    const float targetScore = 1.0f / (1.0f + dist * dist * 0.1f);

    // Heuristic for harmonic quality (active partials, even spread)
    const float heuristic = heuristicFitness(individual.timbre);

    individual.fitness = targetScore * 0.6f + heuristic * 0.4f;
}

const GenerativeTimbreDesigner::Individual&
GenerativeTimbreDesigner::tournamentSelect(
    const std::vector<Individual>& pop,
    juce::Random& rng,
    int tournamentSize) const
{
    int bestIdx = rng.nextInt(static_cast<int>(pop.size()));
    for (int i = 1; i < tournamentSize; ++i) {
        const int idx = rng.nextInt(static_cast<int>(pop.size()));
        if (pop[idx].fitness > pop[bestIdx].fitness)
            bestIdx = idx;
    }
    return pop[bestIdx];
}

GenerativeTimbreDesigner::Individual
GenerativeTimbreDesigner::crossover(const Individual& a, const Individual& b) const
{
    Individual child;
    juce::Random rng;

    // Uniform crossover: each gene randomly from A or B
    for (int i = 0; i < 64; ++i)
        child.genome.values[i] = rng.nextBool() ? a.genome.values[i] : b.genome.values[i];

    return child;
}

void GenerativeTimbreDesigner::mutate(Individual& individual)
{
    juce::Random rng;
    for (int i = 0; i < 64; ++i) {
        if (rng.nextFloat() < mutationRate_) {
            individual.genome.values[i] += rng.nextFloat() * 0.4f - 0.2f;
            individual.genome.values[i] = std::max(-1.0f, std::min(1.0f,
                                                   individual.genome.values[i]));
        }
    }
}

void GenerativeTimbreDesigner::evolve()
{
    juce::Random rng;
    const auto harmonics = defaultBaseHarmonics(PartialDataSIMD::kMaxPartials, 100.0f);

    // ---- Lazy initialisation on first call ---------------------------------
    if (!evolutionReady_) {
        population_.resize(static_cast<size_t>(populationSize_));
        for (auto& ind : population_) {
            ind.genome.randomize(rng);
            ind.generate(ind.timbre, harmonics);
        }
        evolutionReady_ = true;
    }

    // ---- Evaluate fitness --------------------------------------------------
    for (auto& ind : population_)
        evaluateFitness(ind);

    // ---- Create next generation --------------------------------------------
    std::vector<Individual> nextGeneration;
    nextGeneration.reserve(static_cast<size_t>(populationSize_));

    // Elitism: carry over the single fittest individual unchanged
    {
        const Individual* best = &population_[0];
        for (const auto& ind : population_) {
            if (ind.fitness > best->fitness)
                best = &ind;
        }
        nextGeneration.push_back(*best);
    }

    while (static_cast<int>(nextGeneration.size()) < populationSize_) {
        const Individual& parentA = tournamentSelect(population_, rng);
        const Individual& parentB = tournamentSelect(population_, rng);

        Individual child;
        if (rng.nextFloat() < crossoverRate_)
            child = crossover(parentA, parentB);
        else
            child = (rng.nextBool() ? parentA : parentB);

        mutate(child);
        nextGeneration.push_back(std::move(child));
    }

    population_ = std::move(nextGeneration);

    // ---- Regenerate timbres for the new population -------------------------
    for (auto& ind : population_)
        ind.generate(ind.timbre, harmonics);

    // ---- Track the fittest individual --------------------------------------
    fittest_ = population_[0];
    for (const auto& ind : population_) {
        if (ind.fitness > fittest_.fitness)
            fittest_ = ind;
    }

    ++generation_;
}

void GenerativeTimbreDesigner::resetEvolution()
{
    population_.clear();
    fittest_ = Individual{};
    generation_ = 0;
    evolutionReady_ = false;
}

const GenerativeTimbreDesigner::Individual& GenerativeTimbreDesigner::getFittest() const
{
    return fittest_;
}

int GenerativeTimbreDesigner::getGeneration() const
{
    return generation_;
}

// ============================================================================
// Presets
// ============================================================================

void GenerativeTimbreDesigner::loadPreset(Preset preset)
{
    loadLatentPreset(preset, currentLatent_);
}

void GenerativeTimbreDesigner::loadLatentPreset(Preset preset, LatentVector& out)
{
    if (!presetsInitialized_)
        const_cast<GenerativeTimbreDesigner*>(this)->initPresets();

    switch (preset) {
        case Preset::Warm:      out = presetWarm_;      break;
        case Preset::Bright:    out = presetBright_;    break;
        case Preset::Dark:      out = presetDark_;      break;
        case Preset::Metallic:  out = presetMetallic_;  break;
        case Preset::Glassy:    out = presetGlassy_;    break;
        case Preset::Hollow:    out = presetHollow_;    break;
        case Preset::Rich:      out = presetRich_;      break;
        case Preset::Thin:      out = presetThin_;      break;
    }
}

// ============================================================================
// Timbre generation
// ============================================================================

void GenerativeTimbreDesigner::generate(PartialDataSIMD& output)
{
    generateFromLatent(currentLatent_, output);
}

void GenerativeTimbreDesigner::generateFromLatent(const LatentVector& latent,
                                                   PartialDataSIMD& output)
{
    latentToPartials(latent, output);
    generatedTimbre_ = output;   // cache for applyToPartials
}

// ============================================================================
// Apply / mix
// ============================================================================

void GenerativeTimbreDesigner::applyToPartials(PartialDataSIMD& partials, float mix)
{
    const float m = std::max(0.0f, std::min(1.0f, mix));

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i) {
        partials.amplitude[i] = partials.amplitude[i] * (1.0f - m)
                              + generatedTimbre_.amplitude[i] * m;
    }
    partials.updateActiveMask();
}

// ============================================================================
// Capture from partials (inverse mapping)
// ============================================================================

void GenerativeTimbreDesigner::captureFromPartials(const PartialDataSIMD& partials)
{
    LatentVector v{};
    auto map11 = [](float val) {
        return std::max(-1.0f, std::min(1.0f, val * 2.0f - 1.0f));
    };

    // --- 1. Harmonic envelope (values[0-19]) --------------------------------
    for (int i = 0; i < 20; ++i)
        v.values[i] = map11(partials.amplitude[i]);

    // --- 2. Formant estimation from spectral peaks --------------------------
    struct Peak { int index; float amplitude; };
    std::vector<Peak> peaks;
    peaks.reserve(32);

    for (int i = 1; i < PartialDataSIMD::kMaxPartials; ++i) {
        if (!partials.isActive(i))
            continue;
        const float a  = partials.amplitude[i];
        const float l  = (i > 0)                         ? partials.amplitude[i - 1] : 0.0f;
        const float r  = (i < PartialDataSIMD::kMaxPartials - 1)
                       ? partials.amplitude[i + 1] : 0.0f;
        if (a >= l && a >= r && a > 0.01f)
            peaks.push_back({i, a});
    }

    std::sort(peaks.begin(), peaks.end(),
              [](const Peak& a, const Peak& b) { return a.amplitude > b.amplitude; });

    const int numFormants = std::min(3, static_cast<int>(peaks.size()));
    for (int f = 0; f < numFormants; ++f) {
        const int base = 20 + f * 3;
        const float hz = partials.frequency[peaks[f].index];
        v.values[base + 0] = std::max(-1.0f, std::min(1.0f, (hz / 8000.0f) * 2.0f - 1.0f));
        v.values[base + 1] = map11(peaks[f].amplitude);
        v.values[base + 2] = 0.0f; // bandwidth — hard to estimate from static snapshot
    }

    // --- 3. Spectral tilt (values[30]) from log-log regression --------------
    float sumX = 0.0f, sumY = 0.0f, sumX2 = 0.0f, sumXY = 0.0f;
    int n = 0;
    for (int i = 1; i < std::min(64, PartialDataSIMD::kMaxPartials); ++i) {
        if (partials.amplitude[i] > 1e-5f) {
            const float x = std::log(static_cast<float>(i + 1));
            const float y = std::log(partials.amplitude[i]);
            sumX  += x;   sumY  += y;
            sumX2 += x*x; sumXY += x*y;
            ++n;
        }
    }
    if (n > 2) {
        const float slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
        v.values[30] = std::max(-1.0f, std::min(1.0f, -slope * 0.5f));
    }

    // --- 4. Brightness from spectral centroid (values[62]) ------------------
    float weightedSum = 0.0f, totalAmp = 0.0f;
    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i) {
        if (partials.isActive(i)) {
            weightedSum += static_cast<float>(i) * partials.amplitude[i];
            totalAmp    += partials.amplitude[i];
        }
    }
    if (totalAmp > 1e-5f) {
        const float centroid = weightedSum / totalAmp;
        v.values[62] = map11(centroid / static_cast<float>(PartialDataSIMD::kMaxPartials));
    }

    // --- 5. Warmth from low/high energy ratio (values[63]) ------------------
    float lowEnergy = 0.0f, highEnergy = 0.0f;
    const int split = 10;
    for (int i = 0; i < split && i < PartialDataSIMD::kMaxPartials; ++i)
        lowEnergy  += partials.amplitude[i] * partials.amplitude[i];
    for (int i = split; i < PartialDataSIMD::kMaxPartials; ++i)
        highEnergy += partials.amplitude[i] * partials.amplitude[i];
    if (highEnergy > 1e-5f) {
        const float ratio = lowEnergy / highEnergy;
        v.values[63] = std::max(-1.0f, std::min(1.0f, ratio * 0.5f - 0.5f));
    }

    // --- 6. Inharmonicity from frequency deviation (values[40]) -------------
    float devSum = 0.0f;
    int   devN   = 0;
    for (int i = 1; i < std::min(32, PartialDataSIMD::kMaxPartials); ++i) {
        if (partials.isActive(i) && partials.frequency[0] > 0.0f) {
            const float expected = static_cast<float>(i + 1) * partials.frequency[0];
            const float actual   = partials.frequency[i];
            devSum += (actual - expected) / expected;
            ++devN;
        }
    }
    if (devN > 0)
        v.values[40] = std::max(-1.0f, std::min(1.0f, (devSum / static_cast<float>(devN)) * 5.0f));

    currentLatent_ = v;
}

// ============================================================================
// Reset
// ============================================================================

void GenerativeTimbreDesigner::reset()
{
    juce::Random rng;
    currentLatent_.randomize(rng);
    targetLatent_   = currentLatent_;
    morphProgress_  = 0.0f;
    morphSpeed_     = 0.01f;
    generatedTimbre_ = PartialDataSIMD{};
    resetEvolution();
}

// ============================================================================
// Private helpers
// ============================================================================

void GenerativeTimbreDesigner::latentToPartials(const LatentVector& latent,
                                                 PartialDataSIMD& output)
{
    latentToPartialsCore(latent, output, sampleRate_);
}

void GenerativeTimbreDesigner::applyPcaWhitening(LatentVector& v)
{
    // Clamp to [-1, 1] then normalise by peak absolute value
    float maxAbs = 0.0f;
    for (auto& val : v.values) {
        val = std::max(-1.0f, std::min(1.0f, val));
        maxAbs = std::max(maxAbs, std::abs(val));
    }
    if (maxAbs > 1e-6f) {
        const float scale = 1.0f / maxAbs;
        for (auto& val : v.values)
            val *= scale;
    }
}

// ============================================================================
// Static definitions
// ============================================================================

GenerativeTimbreDesigner::LatentVector GenerativeTimbreDesigner::presetWarm_;
GenerativeTimbreDesigner::LatentVector GenerativeTimbreDesigner::presetBright_;
GenerativeTimbreDesigner::LatentVector GenerativeTimbreDesigner::presetDark_;
GenerativeTimbreDesigner::LatentVector GenerativeTimbreDesigner::presetMetallic_;
GenerativeTimbreDesigner::LatentVector GenerativeTimbreDesigner::presetGlassy_;
GenerativeTimbreDesigner::LatentVector GenerativeTimbreDesigner::presetHollow_;
GenerativeTimbreDesigner::LatentVector GenerativeTimbreDesigner::presetRich_;
GenerativeTimbreDesigner::LatentVector GenerativeTimbreDesigner::presetThin_;
bool GenerativeTimbreDesigner::presetsInitialized_ = false;

// ============================================================================
// Preset initialisation
// ============================================================================

void GenerativeTimbreDesigner::initPresets()
{
    if (presetsInitialized_)
        return;

    // ---- Warm: strong fundamental, quick HF rolloff, emphasised lows -------
    {
        auto& p = presetWarm_;
        p.values[0]  = 1.00f;  p.values[1]  = 0.80f;
        p.values[2]  = 0.50f;  p.values[3]  = 0.25f;
        p.values[4]  = 0.05f;  p.values[5]  = -0.10f;
        p.values[6]  = -0.25f; p.values[7]  = -0.35f;
        p.values[8]  = -0.45f; p.values[9]  = -0.50f;
        for (int i = 10; i < 20; ++i) p.values[i] = -0.55f;
        // Formant 1: ~300 Hz
        p.values[20] = -0.65f; p.values[21] = 0.40f; p.values[22] = -0.50f;
        // Formant 2: ~800 Hz
        p.values[23] = -0.40f; p.values[24] = 0.20f; p.values[25] = -0.30f;
        // Spectral tilt negative (steep rolloff)
        p.values[30] = -0.60f; p.values[39] = -0.40f;
        // Global
        p.values[60] = 0.60f;  // volume
        p.values[62] = -0.40f; // brightness
        p.values[63] = 0.80f;  // warmth
    }

    // ---- Bright: boosted high harmonics, formant at 3-5 kHz ----------------
    {
        auto& p = presetBright_;
        p.values[0]  = 0.50f;  p.values[1]  = 0.40f;
        p.values[2]  = 0.30f;  p.values[3]  = 0.40f;
        p.values[4]  = 0.50f;  p.values[5]  = 0.50f;
        p.values[6]  = 0.40f;  p.values[7]  = 0.40f;
        p.values[8]  = 0.50f;  p.values[9]  = 0.50f;
        p.values[10] = 0.40f;  p.values[11] = 0.30f;
        p.values[12] = 0.20f;  p.values[13] = 0.20f;
        p.values[14] = 0.10f;  p.values[15] = 0.00f;
        p.values[16] = -0.10f; p.values[17] = -0.10f;
        p.values[18] = -0.20f; p.values[19] = -0.20f;
        // Formant 1: ~4 kHz
        p.values[20] = 0.00f;  p.values[21] = 0.60f; p.values[22] = 0.00f;
        // Formant 2: ~6 kHz
        p.values[23] = 0.50f;  p.values[24] = 0.40f; p.values[25] = 0.30f;
        // Spectral tilt positive (slow rolloff)
        p.values[30] = 0.60f;  p.values[39] = 0.30f;
        // Global
        p.values[62] = 0.80f;
        p.values[63] = -0.30f;
    }

    // ---- Dark: steep HF rolloff, emphasised low-mids -----------------------
    {
        auto& p = presetDark_;
        p.values[0]  = 0.90f;  p.values[1]  = 0.80f;
        p.values[2]  = 0.60f;  p.values[3]  = 0.40f;
        p.values[4]  = 0.20f;  p.values[5]  = 0.00f;
        p.values[6]  = -0.20f; p.values[7]  = -0.40f;
        p.values[8]  = -0.50f; p.values[9]  = -0.60f;
        for (int i = 10; i < 20; ++i) p.values[i] = -0.70f;
        // Formant: ~200 Hz
        p.values[20] = -0.80f; p.values[21] = 0.50f; p.values[22] = -0.60f;
        // Spectral tilt very negative
        p.values[30] = -0.80f; p.values[39] = -0.70f;
        // Global
        p.values[62] = -0.70f;
        p.values[63] = 0.70f;
    }

    // ---- Metallic: strong odd harmonics, slight inharmonicity --------------
    {
        auto& p = presetMetallic_;
        for (int i = 0; i < 20; ++i)
            p.values[i] = ((i % 2) == 0) ? 0.70f : -0.10f;
        p.values[40] = 0.30f;   // inharmonic stretch
        p.values[20] = -0.20f;  p.values[21] = 0.40f; p.values[22] = -0.10f;
        p.values[30] = 0.10f;
        p.values[62] = 0.30f;
        p.values[63] = -0.10f;
    }

    // ---- Glassy: very bright, high formant frequencies ---------------------
    {
        auto& p = presetGlassy_;
        p.values[0]  = 0.10f;  p.values[1]  = 0.20f;
        p.values[2]  = 0.30f;  p.values[3]  = 0.40f;
        p.values[4]  = 0.50f;  p.values[5]  = 0.50f;
        p.values[6]  = 0.60f;  p.values[7]  = 0.60f;
        p.values[8]  = 0.50f;  p.values[9]  = 0.50f;
        for (int i = 10; i < 20; ++i) p.values[i] = 0.30f;
        // Formant 1: ~6.8 kHz
        p.values[20] = 0.70f;  p.values[21] = 0.70f; p.values[22] = 0.40f;
        // Formant 2: ~7.5 kHz
        p.values[23] = 0.80f;  p.values[24] = 0.50f; p.values[25] = 0.50f;
        // Spectral tilt very positive
        p.values[30] = 0.80f;  p.values[39] = 0.60f;
        // Global
        p.values[62] = 1.00f;
        p.values[63] = -0.50f;
    }

    // ---- Hollow: suppressed fundamental, odd-harmonic emphasis -------------
    {
        auto& p = presetHollow_;
        p.values[0]  = -0.50f; p.values[1]  = 0.10f;
        p.values[2]  = 0.80f;  p.values[3]  = -0.10f;
        p.values[4]  = 0.70f;  p.values[5]  = -0.20f;
        p.values[6]  = 0.60f;  p.values[7]  = -0.20f;
        p.values[8]  = 0.50f;  p.values[9]  = -0.30f;
        p.values[10] = 0.40f;  p.values[11] = -0.30f;
        p.values[12] = 0.30f;  p.values[13] = -0.30f;
        p.values[14] = 0.20f;  p.values[15] = -0.30f;
        p.values[16] = 0.10f;  p.values[17] = -0.30f;
        p.values[18] = 0.00f;  p.values[19] = -0.30f;
        p.values[20] = 0.00f;  p.values[21] = 0.50f; p.values[22] = 0.00f;
        p.values[62] = 0.10f;
        p.values[63] = -0.20f;
    }

    // ---- Rich: strong harmonics throughout the spectrum --------------------
    {
        auto& p = presetRich_;
        for (int i = 0; i < 20; ++i)
            p.values[i] = 0.70f - static_cast<float>(i) * 0.03f;
        // Distributed formants
        p.values[20] = -0.50f; p.values[21] = 0.30f; p.values[22] = -0.30f;
        p.values[23] = 0.00f;  p.values[24] = 0.30f; p.values[25] = 0.00f;
        p.values[26] = 0.50f;  p.values[27] = 0.20f; p.values[28] = 0.30f;
        p.values[30] = 0.00f;
        p.values[60] = 0.60f;
        p.values[62] = 0.40f;
        p.values[63] = 0.40f;
    }

    // ---- Thin: weak low end, emphasis on upper harmonics -------------------
    {
        auto& p = presetThin_;
        p.values[0]  = -0.70f; p.values[1]  = -0.50f;
        p.values[2]  = -0.30f; p.values[3]  = -0.10f;
        p.values[4]  = 0.10f;  p.values[5]  = 0.20f;
        p.values[6]  = 0.30f;  p.values[7]  = 0.20f;
        p.values[8]  = 0.10f;  p.values[9]  = 0.00f;
        for (int i = 10; i < 20; ++i)
            p.values[i] = -0.20f - static_cast<float>(i - 10) * 0.05f;
        // Formant: upper-mid
        p.values[20] = 0.30f;  p.values[21] = 0.40f; p.values[22] = 0.20f;
        p.values[30] = 0.20f;
        p.values[62] = 0.50f;
        p.values[63] = -0.60f;
    }

    presetsInitialized_ = true;
}

} // namespace ana
