#include "SampleSlicer.h"
#include <cmath>
#include <algorithm>

namespace ana {

void SampleSlicer::setSampleRate(double sr)
{
    sampleRate_ = sr > 0.0 ? sr : 44100.0;
}

std::vector<SampleSlice> SampleSlicer::slice(const std::vector<float>& audioData, float sensitivity)
{
    std::vector<SampleSlice> slices;
    if (audioData.empty()) return slices;

    int n = static_cast<int>(audioData.size());
    
    // 1. Envelope follower to trace amplitude contours
    std::vector<float> envelope(n, 0.0f);
    float attackTime = 0.001f;  // 1ms
    float releaseTime = 0.05f;  // 50ms
    float attackCoeff = std::exp(-1.0f / (attackTime * static_cast<float>(sampleRate_)));
    float releaseCoeff = std::exp(-1.0f / (releaseTime * static_cast<float>(sampleRate_)));
    
    float env = 0.0f;
    for (int i = 0; i < n; ++i) {
        float in = std::abs(audioData[i]);
        if (in > env) env = attackCoeff * env + (1.0f - attackCoeff) * in;
        else env = releaseCoeff * env + (1.0f - releaseCoeff) * in;
        envelope[i] = env;
    }
    
    // 2. First derivative (difference) of envelope to find sudden energy bursts
    std::vector<float> diff(n, 0.0f);
    for (int i = 1; i < n; ++i) {
        diff[i] = envelope[i] - envelope[i - 1];
    }
    
    // 3. Find peaks in the derivative
    float clampedSens = std::clamp(sensitivity, 0.0f, 1.0f);
    float threshold = 0.01f + (1.0f - clampedSens) * 0.15f; 
    int minDistance = static_cast<int>(sampleRate_ * 0.05); // 50ms min between slices
    
    std::vector<int> transientIndices;
    int lastTransient = -minDistance;
    
    for (int i = 1; i < n - 1; ++i) {
        // Local maximum in derivative above threshold
        if (diff[i] > threshold && diff[i] > diff[i - 1] && diff[i] > diff[i + 1]) {
            if (i - lastTransient > minDistance) {
                transientIndices.push_back(i);
                lastTransient = i;
            }
        }
    }
    
    // 4. Create slices from transients
    if (transientIndices.empty()) {
        SampleSlice single;
        single.startSample = 0;
        single.endSample = n - 1;
        
        float peak = 0.0f;
        float energy = 0.0f;
        for (int j = 0; j < n; ++j) {
            float val = std::abs(audioData[j]);
            if (val > peak) peak = val;
            energy += val * val;
        }
        single.peakAmplitude = peak;
        single.energy = energy;
        
        slices.push_back(single);
        return slices;
    }
    
    // Always start the first slice at 0 if the first transient isn't near the start
    if (transientIndices[0] > minDistance) {
        transientIndices.insert(transientIndices.begin(), 0);
    } else {
        transientIndices[0] = 0;
    }
    
    for (size_t i = 0; i < transientIndices.size(); ++i) {
        SampleSlice s;
        s.startSample = transientIndices[i];
        
        // Zero-crossing snap for the end sample to prevent clicks
        int endS = (i + 1 < transientIndices.size()) ? transientIndices[i + 1] - 1 : n - 1;
        s.endSample = endS;
        
        // Calculate peak and energy for the slice
        float peak = 0.0f;
        float energy = 0.0f;
        for (int j = s.startSample; j <= s.endSample && j < n; ++j) {
            float val = std::abs(audioData[j]);
            if (val > peak) peak = val;
            energy += val * val;
        }
        s.peakAmplitude = peak;
        s.energy = energy;
        
        slices.push_back(s);
    }
    
    return slices;
}

} // namespace ana
