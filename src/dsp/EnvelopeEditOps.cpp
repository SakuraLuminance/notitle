#include "EnvelopeEditOps.h"

#include <cmath>

namespace ana
{
namespace EnvelopeEditOps
{

bool addPoint(MultiPointEnvelope& env, float time, float value, CurveType curve)
{
    return env.addBreakpoint(time, value, curve);
}

bool removePoint(MultiPointEnvelope& env, int index)
{
    if (env.getNumBreakpoints() <= 2)
        return false;

    return env.removeBreakpoint(index);
}

bool movePoint(MultiPointEnvelope& env, int index, float time, float value)
{
    const int n = env.getNumBreakpoints();
    if (index < 0 || index >= n)
        return false;

    const float epsilon = 1.0e-4f;
    const float minTime = (index > 0) ? env.getBreakpoint(index - 1).time + epsilon : 0.0f;
    const float maxTime = (index < n - 1) ? env.getBreakpoint(index + 1).time - epsilon : 10.0f;

    const float t = juce::jlimit(minTime, juce::jmax(minTime, maxTime), time);
    const float v = juce::jlimit(0.0f, 1.0f, value);

    return env.moveBreakpoint(index, t, v);
}

DerivedADSR deriveADSR(const MultiPointEnvelope& env)
{
    DerivedADSR out;

    const int n = env.getNumBreakpoints();
    if (n < 2)
        return out;

    const int holdIndex = (env.getLoopEnd() >= 0 && env.getLoopEnd() < n)
        ? env.getLoopEnd()
        : n - 1;

    int peakIndex = 0;
    float peakValue = env.getBreakpoint(0).value;
    for (int i = 1; i <= holdIndex; ++i)
    {
        if (env.getBreakpoint(i).value > peakValue)
        {
            peakValue = env.getBreakpoint(i).value;
            peakIndex = i;
        }
    }

    const double t0    = env.getTimeInSeconds(env.getBreakpoint(0).time);
    const double tPeak = env.getTimeInSeconds(env.getBreakpoint(peakIndex).time);
    const double tHold = env.getTimeInSeconds(env.getBreakpoint(holdIndex).time);
    const double tEnd  = env.getTimeInSeconds(env.getBreakpoint(n - 1).time);

    out.attack  = static_cast<float>(tPeak - t0);
    out.decay   = static_cast<float>(tHold - tPeak);
    out.sustain = env.getBreakpoint(holdIndex).value;
    out.release = static_cast<float>(tEnd - tHold);
    return out;
}

float maxAxisTime(const MultiPointEnvelope& env)
{
    const int n = env.getNumBreakpoints();
    const float last = (n > 0) ? env.getBreakpoint(n - 1).time : 1.0f;
    return juce::jmax(1.0f, last * 1.1f);
}

float timeToX(const MultiPointEnvelope& env, float time, juce::Rectangle<float> area)
{
    const float maxT = maxAxisTime(env);
    if (maxT <= 0.0f)
        return area.getX();

    return area.getX() + (time / maxT) * area.getWidth();
}

float xToTime(const MultiPointEnvelope& env, float x, juce::Rectangle<float> area)
{
    if (area.getWidth() <= 0.0f)
        return 0.0f;

    const float p = juce::jlimit(0.0f, 1.0f, (x - area.getX()) / area.getWidth());
    return p * maxAxisTime(env);
}

float valueToY(float value, juce::Rectangle<float> area)
{
    return area.getBottom() - juce::jlimit(0.0f, 1.0f, value) * area.getHeight();
}

float yToValue(float y, juce::Rectangle<float> area)
{
    if (area.getHeight() <= 0.0f)
        return 0.0f;

    return juce::jlimit(0.0f, 1.0f, (area.getBottom() - y) / area.getHeight());
}

int hitTestPoint(const MultiPointEnvelope& env, juce::Point<float> p,
                 juce::Rectangle<float> area, float radiusPx)
{
    const int n = env.getNumBreakpoints();
    int best = -1;
    float bestDist = radiusPx;

    for (int i = 0; i < n; ++i)
    {
        const auto bp = env.getBreakpoint(i);
        const float x = timeToX(env, bp.time, area);
        const float y = valueToY(bp.value, area);
        const float d = std::hypot(x - p.x, y - p.y);

        if (d <= bestDist)
        {
            bestDist = d;
            best = i;
        }
    }

    return best;
}

int hitTestMarker(const MultiPointEnvelope& env, juce::Point<float> p,
                  juce::Rectangle<float> area, float radiusPx)
{
    const int n = env.getNumBreakpoints();
    if (n < 2)
        return -1;

    int result = -1;
    float bestDist = radiusPx;

    const int ls = juce::jlimit(0, n - 1, env.getLoopStart());
    const float xs = timeToX(env, env.getBreakpoint(ls).time, area);
    const float ds = std::abs(p.x - xs);
    if (ds <= bestDist)
    {
        bestDist = ds;
        result = 0;
    }

    const int le = env.getLoopEnd();
    if (le >= 0 && le < n)
    {
        const float xe = timeToX(env, env.getBreakpoint(le).time, area);
        const float de = std::abs(p.x - xe);
        if (de <= bestDist)
            result = 1;
    }

    return result;
}

} // namespace EnvelopeEditOps
} // namespace ana
