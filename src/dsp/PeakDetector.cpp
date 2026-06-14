#include "PeakDetector.h"
#include <cmath>
#include <algorithm>

namespace ana {

PeakDetector::PeakDetector()
{
}

PeakDetector::~PeakDetector()
{
}

std::vector<Partial> PeakDetector::detectPeaks(
    const std::vector<std::complex<float>>& spectrum,
    const STFTConfig& config,
    double sampleRate)
{
    const int numBins = static_cast<int>(spectrum.size());
    std::vector<Partial> peaks;

    // Compute magnitude spectrum in dB
    std::vector<float> magnitudes(numBins);
    for (int i = 0; i < numBins; ++i)
    {
        float mag = std::abs(spectrum[i]);
        magnitudes[i] = 20.0f * std::log10(mag + 1e-10f);
    }

    // Find local maxima
    for (int i = 1; i < numBins - 1; ++i)
    {
        if (magnitudes[i] > magnitudes[i - 1] &&
            magnitudes[i] > magnitudes[i + 1] &&
            magnitudes[i] > config.peakThresholdDB)
        {
            // Parabolic interpolation for refined frequency
            float alpha = magnitudes[i - 1];
            float beta  = magnitudes[i];
            float gamma = magnitudes[i + 1];
            float p = 0.5f * (alpha - gamma) / (alpha - 2.0f * beta + gamma);

            float refinedBin = i + p;
            float freq = refinedBin * static_cast<float>(sampleRate) / config.fftSize;

            // Extract phase
            float phase = std::arg(spectrum[i]);

            Partial peak;
            peak.frequency = freq;
            peak.amplitude = std::abs(spectrum[i]);
            peak.phase     = phase;

            peaks.push_back(peak);
        }
    }

    // Sort by magnitude descending
    std::sort(peaks.begin(), peaks.end(),
        [](const Partial& a, const Partial& b)
        {
            return a.amplitude > b.amplitude;
        });

    // Keep top maxPartials
    if (static_cast<int>(peaks.size()) > config.maxPartials)
    {
        peaks.resize(config.maxPartials);
    }

    return peaks;
}

} // namespace ana
