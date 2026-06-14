#include "BpmDetector.h"
#include <cmath>
#include <algorithm>

namespace ana {

void BpmDetector::setSampleRate(double sr)
{
    sampleRate_ = std::max(8000.0, sr);
}

float BpmDetector::detectBpm(const std::vector<float>& audioData)
{
    if (audioData.empty()) return 120.0f;

    // 1. Extract envelope (full wave rectification + low pass filter)
    std::vector<float> env(audioData.size(), 0.0f);
    const float rc = 0.05f; // 50ms time constant
    const float alpha = static_cast<float>(std::exp(-1.0 / (sampleRate_ * rc)));
    float prev = 0.0f;
    for (size_t i = 0; i < audioData.size(); ++i)
    {
        float val = std::abs(audioData[i]);
        prev = val * (1.0f - alpha) + prev * alpha;
        env[i] = prev;
    }

    // Downsample for faster autocorrelation (to ~1000 Hz)
    const int downFactor = std::max(1, static_cast<int>(sampleRate_ / 1000.0));
    std::vector<float> downEnv;
    downEnv.reserve(env.size() / downFactor + 1);
    for (size_t i = 0; i < env.size(); i += downFactor)
    {
        downEnv.push_back(env[i]);
    }
    const double downSR = sampleRate_ / static_cast<double>(downFactor);

    // 2. Autocorrelation to find periodicities
    // Search BPM range: 60 to 200
    const int minLag = static_cast<int>(downSR * 60.0 / 200.0);
    int maxLag = static_cast<int>(downSR * 60.0 / 60.0);
    
    if (downEnv.empty()) return 120.0f;
    if (maxLag >= static_cast<int>(downEnv.size())) 
        maxLag = static_cast<int>(downEnv.size()) - 1;

    if (minLag >= maxLag) return 120.0f;

    float bestCorr = -1.0f;
    int bestLag = minLag;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        float corr = 0.0f;
        for (size_t i = 0; i < downEnv.size() - lag; ++i)
        {
            corr += downEnv[i] * downEnv[i + lag];
        }
        if (corr > bestCorr)
        {
            bestCorr = corr;
            bestLag = lag;
        }
    }

    return static_cast<float>(60.0 * downSR / static_cast<double>(bestLag));
}

int BpmDetector::detectFirstBeatOffset(const std::vector<float>& audioData)
{
    if (audioData.empty()) return 0;
    
    // Find the first major transient
    const float rc = 0.01f; // 10ms
    const float alpha = static_cast<float>(std::exp(-1.0 / (sampleRate_ * rc)));
    float prev = 0.0f;
    
    float maxDiff = 0.0f;
    int bestIndex = 0;

    for (size_t i = 0; i < audioData.size(); ++i)
    {
        float val = std::abs(audioData[i]);
        float currEnv = val * (1.0f - alpha) + prev * alpha;
        
        // Simple first-order difference to find rapid attacks
        float diff = currEnv - prev;
        
        // We want the *first* major attack, not necessarily the absolute maximum.
        // If we find an attack that is significantly large, we can stop or keep going.
        // For robustness, find the max attack in the file, then back up to find the first one
        // that is at least 50% of the max.
        
        if (diff > maxDiff)
        {
            maxDiff = diff;
        }
        prev = currEnv;
    }

    // Second pass: find the first attack that exceeds 50% of the max attack
    prev = 0.0f;
    const float threshold = maxDiff * 0.5f;
    
    for (size_t i = 0; i < audioData.size(); ++i)
    {
        float val = std::abs(audioData[i]);
        float currEnv = val * (1.0f - alpha) + prev * alpha;
        float diff = currEnv - prev;
        
        if (diff >= threshold)
        {
            bestIndex = static_cast<int>(i);
            break;
        }
        prev = currEnv;
    }

    return bestIndex;
}

} // namespace ana
