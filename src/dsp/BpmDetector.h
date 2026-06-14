#pragma once

#include <vector>

namespace ana {

class BpmDetector
{
public:
    BpmDetector() = default;
    ~BpmDetector() = default;

    void setSampleRate(double sr);

    // Analyze a full audio buffer and return estimated BPM
    // Range typically 60 - 200 BPM
    float detectBpm(const std::vector<float>& audioData);

    // Analyze transients to find the first beat offset in samples (auto-alignment)
    int detectFirstBeatOffset(const std::vector<float>& audioData);

private:
    double sampleRate_ = 44100.0;
};

} // namespace ana
