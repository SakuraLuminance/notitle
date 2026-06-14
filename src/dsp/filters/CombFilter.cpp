#include "CombFilter.h"
#include <cmath>
#include <algorithm>

namespace ana {

CombFilter::CombFilter() {}
CombFilter::~CombFilter() {}

void CombFilter::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    delayLine.prepare(spec);
    delayLine.reset();
    lastOutput[0] = lastOutput[1] = 0.0f;
}

void CombFilter::reset()
{
    delayLine.reset();
    lastOutput[0] = lastOutput[1] = 0.0f;
}

void CombFilter::setDelayTime(float ms)
{
    delayTimeMs = std::max(0.1f, std::min(50.0f, ms));
}

void CombFilter::setFeedback(float fb)
{
    feedback = std::max(-1.0f, std::min(1.0f, fb));
}

void CombFilter::setDamping(float damp)
{
    damping = std::max(0.0f, std::min(1.0f, damp));
}

void CombFilter::setMode(CombMode m) { mode = m; }

void CombFilter::process(juce::dsp::AudioBlock<float>& block)
{
    const float delaySamples = static_cast<float>(delayTimeMs * sampleRate / 1000.0);

    for (int sample = 0; sample < static_cast<int>(block.getNumSamples()); ++sample)
    {
        for (int ch = 0; ch < static_cast<int>(block.getNumChannels()); ++ch)
        {
            float input = block.getSample(ch, sample);
            float delayed = delayLine.popSample(ch, delaySamples);

            float output = 0.0f;

            switch (mode)
            {
                case CombMode::Feedforward:
                    output = input + delayed * feedback;
                    break;

                case CombMode::Feedback:
                {
                    // Apply damping on feedback path
                    float damped = lastOutput[ch] * damping + delayed * (1.0f - damping);
                    output = input + damped * feedback;
                    lastOutput[ch] = output;
                    break;
                }

                case CombMode::Dual:
                {
                    float damped = lastOutput[ch] * damping + delayed * (1.0f - damping);
                    output = input + (delayed + damped) * feedback * 0.5f;
                    lastOutput[ch] = output;
                    break;
                }
            }

            delayLine.pushSample(ch, output);
            block.setSample(ch, sample, output);
        }
    }
}

} // namespace ana
