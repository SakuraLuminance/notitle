#include "MultiPointEnvelope.h"
#include <algorithm>
#include <cmath>

namespace ana {

//==============================================================================
MultiPointEnvelope::MultiPointEnvelope()
{
    breakpoints.reserve(maxBreakpoints);
}

//==============================================================================
void MultiPointEnvelope::prepare(double sr)
{
    sampleRate = sr;
    reset();
}

void MultiPointEnvelope::reset()
{
    timePosSeconds = 0.0;
    currentValue = 0.0f;
    active = false;
    released = false;
    direction = 1;
}

//==============================================================================
bool MultiPointEnvelope::addBreakpoint(float time, float value, CurveType curve)
{
    if (breakpoints.size() >= static_cast<size_t>(maxBreakpoints))
        return false;

    time  = juce::jlimit(0.0f, 10.0f, time);
    value = juce::jlimit(0.0f, 1.0f, value);

    Breakpoint bp;
    bp.time  = time;
    bp.value = value;
    bp.curve = curve;

    // Insert sorted by time
    auto it = breakpoints.begin();
    while (it != breakpoints.end() && it->time <= time)
        ++it;

    breakpoints.insert(it, bp);
    return true;
}

bool MultiPointEnvelope::removeBreakpoint(int index)
{
    if (index < 0 || index >= static_cast<int>(breakpoints.size()))
        return false;

    breakpoints.erase(breakpoints.begin() + index);

    // Adjust loop indices
    if (loopStartIndex >= static_cast<int>(breakpoints.size()))
        loopStartIndex = juce::jmax(0, static_cast<int>(breakpoints.size()) - 1);
    if (loopEndIndex >= static_cast<int>(breakpoints.size()))
        loopEndIndex = static_cast<int>(breakpoints.size()) - 1;

    return true;
}

bool MultiPointEnvelope::moveBreakpoint(int index, float time, float value)
{
    if (index < 0 || index >= static_cast<int>(breakpoints.size()))
        return false;

    CurveType curve = breakpoints[index].curve;
    removeBreakpoint(index);
    return addBreakpoint(time, value, curve);
}

void MultiPointEnvelope::clearBreakpoints()
{
    breakpoints.clear();
    loopStartIndex = 0;
    loopEndIndex = -1;
}

int MultiPointEnvelope::getNumBreakpoints() const noexcept
{
    return static_cast<int>(breakpoints.size());
}

const Breakpoint& MultiPointEnvelope::getBreakpoint(int index) const
{
    return breakpoints[static_cast<size_t>(index)];
}

//==============================================================================
void MultiPointEnvelope::setLoopMode(LoopMode mode) noexcept   { loopMode = mode; }
LoopMode MultiPointEnvelope::getLoopMode() const noexcept      { return loopMode; }

void MultiPointEnvelope::setLoopStart(int index) noexcept
{
    loopStartIndex = juce::jlimit(0, juce::jmax(0, static_cast<int>(breakpoints.size()) - 1), index);
}

int MultiPointEnvelope::getLoopStart() const noexcept { return loopStartIndex; }

void MultiPointEnvelope::setLoopEnd(int index) noexcept
{
    loopEndIndex = (index >= 0 && index < static_cast<int>(breakpoints.size())) ? index : -1;
}

int MultiPointEnvelope::getLoopEnd() const noexcept { return loopEndIndex; }

//==============================================================================
void MultiPointEnvelope::setTempo(double bpm)
{
    tempo = juce::jmax(1.0, bpm);
}

double MultiPointEnvelope::getTempo() const noexcept { return tempo; }

void MultiPointEnvelope::setBeatDivision(double beats)
{
    beatDiv = juce::jmax(0.03125, beats); // minimum 1/32 note
}

double MultiPointEnvelope::getBeatDivision() const noexcept { return beatDiv; }

void MultiPointEnvelope::setSyncMode(bool sync) noexcept { syncEnabled = sync; }
bool MultiPointEnvelope::getSyncMode() const noexcept    { return syncEnabled; }

//==============================================================================
void MultiPointEnvelope::trigger()
{
    if (breakpoints.size() < 2)
        return;

    timePosSeconds = 0.0;
    currentValue = breakpoints[0].value;
    active = true;
    released = false;
    direction = 1;
}

void MultiPointEnvelope::release()
{
    released = true;
}

//==============================================================================
// ADSR convenience API
//==============================================================================

void MultiPointEnvelope::setAttack(float time)
{
    attack_ = juce::jlimit(0.0f, 10.0f, time);
    rebuildADSR();
}

void MultiPointEnvelope::setDecay(float time)
{
    decay_ = juce::jlimit(0.0f, 10.0f, time);
    rebuildADSR();
}

void MultiPointEnvelope::setSustain(float level)
{
    sustain_ = juce::jlimit(0.0f, 1.0f, level);
    rebuildADSR();
}

void MultiPointEnvelope::setRelease(float time)
{
    release_ = juce::jlimit(0.0f, 10.0f, time);
    rebuildADSR();
}

void MultiPointEnvelope::rebuildADSR()
{
    clearBreakpoints();
    addBreakpoint(0.0f, 0.0f, CurveType::Linear);
    addBreakpoint(attack_, 1.0f, CurveType::Linear);
    addBreakpoint(attack_ + decay_, sustain_, CurveType::Linear);
    addBreakpoint(attack_ + decay_ + release_, 0.0f, CurveType::Exponential);
}

//==============================================================================
bool MultiPointEnvelope::isActive() const noexcept  { return active; }
bool MultiPointEnvelope::isReleased() const noexcept { return released; }

//==============================================================================
double MultiPointEnvelope::timeToSeconds(float breakpointTime) const
{
    if (syncEnabled)
    {
        // Convert beats to seconds: time * division * 60 / bpm
        return static_cast<double>(breakpointTime) * beatDiv * 60.0 / tempo;
    }
    return static_cast<double>(breakpointTime);
}

float MultiPointEnvelope::process(int numSamples)
{
    if (!active || breakpoints.size() < 2)
        return currentValue;

    const double deltaTime = static_cast<double>(numSamples) / sampleRate;
    advanceEnvelope(deltaTime);

    return currentValue;
}

float MultiPointEnvelope::getValue() const noexcept
{
    return currentValue;
}

//==============================================================================
float MultiPointEnvelope::interpolateValue(float v0, float v1, float t, CurveType curve)
{
    t = juce::jlimit(0.0f, 1.0f, t);

    switch (curve)
    {
        case CurveType::Linear:
            return v0 + (v1 - v0) * t;

        case CurveType::Exponential:
        {
            // Exponential with curvature factor k=4.
            // Produces fast initial change that decelerates toward target.
            // shape: (1 - e^(-k*t)) / (1 - e^(-k))
            constexpr float k = 4.0f;
            const float curved = (1.0f - std::exp(-t * k)) / (1.0f - std::exp(-k));
            return v0 + (v1 - v0) * curved;
        }

        case CurveType::SCurve:
        {
            // Hermite smoothstep: 3t^2 - 2t^3
            const float curved = t * t * (3.0f - 2.0f * t);
            return v0 + (v1 - v0) * curved;
        }
    }

    return v0;
}

//==============================================================================
void MultiPointEnvelope::advanceEnvelope(double deltaSeconds)
{
    const int numSegments = static_cast<int>(breakpoints.size()) - 1;
    const double totalEndSec = timeToSeconds(breakpoints[numSegments].time);
    const double loopStartSec = timeToSeconds(breakpoints[loopStartIndex].time);
    const double loopEndSec = (loopEndIndex > loopStartIndex)
        ? timeToSeconds(breakpoints[loopEndIndex].time)
        : totalEndSec;

    timePosSeconds += deltaSeconds;

    // --- Boundary handling ---
    // Sustain: hold at loop end point until released
    if (loopMode == LoopMode::Sustain && !released && timePosSeconds >= loopEndSec)
    {
        timePosSeconds = loopEndSec;
    }
    // General: reached the absolute end of the envelope
    else if (timePosSeconds >= totalEndSec)
    {
        switch (loopMode)
        {
            case LoopMode::Forward:
            {
                // Wrap back to loop start
                const double wrapRange = totalEndSec - loopStartSec;
                if (wrapRange > 0.0)
                {
                    timePosSeconds = loopStartSec
                        + std::fmod(timePosSeconds - loopStartSec, wrapRange);
                }
                else
                {
                    timePosSeconds = loopStartSec;
                }
                break;
            }
            case LoopMode::PingPong:
            {
                // Reverse direction, reflect overshoot
                direction = -1;
                const double overshoot = timePosSeconds - totalEndSec;
                timePosSeconds = totalEndSec - std::min(overshoot, totalEndSec - loopStartSec);
                if (timePosSeconds < loopStartSec)
                    timePosSeconds = loopStartSec;
                break;
            }
            case LoopMode::Sustain:
                // Released sustain reached the end
                handleEnvelopeEnd();
                return;
            default:
                handleEnvelopeEnd();
                return;
        }
    }

    // Ping-pong: reverse at loop start when going backward
    if (loopMode == LoopMode::PingPong && direction < 0 && timePosSeconds <= loopStartSec)
    {
        direction = 1;
        const double undershoot = loopStartSec - timePosSeconds;
        timePosSeconds = loopStartSec + std::min(undershoot, loopEndSec - loopStartSec);
    }

    // --- Find current segment ---
    int segment = -1;
    for (int i = 0; i < numSegments; ++i)
    {
        const double s = timeToSeconds(breakpoints[i].time);
        const double e = timeToSeconds(breakpoints[i + 1].time);
        if (timePosSeconds >= s && timePosSeconds < e)
        {
            segment = i;
            break;
        }
    }

    // Fallback: if time landed exactly on a segment boundary
    if (segment == -1 && timePosSeconds >= totalEndSec)
    {
        segment = numSegments - 1;
    }

    if (segment < 0 || segment >= numSegments)
    {
        handleEnvelopeEnd();
        return;
    }

    // --- Compute progress and value ---
    const double segStart = timeToSeconds(breakpoints[segment].time);
    const double segEnd   = timeToSeconds(breakpoints[segment + 1].time);
    const double segDur   = segEnd - segStart;
    const float p = (segDur > 0.0)
        ? juce::jlimit(0.0f, 1.0f,
                       static_cast<float>((timePosSeconds - segStart) / segDur))
        : 0.0f;

    if (direction > 0)
    {
        currentValue = interpolateValue(
            breakpoints[segment].value,
            breakpoints[segment + 1].value, p,
            breakpoints[segment].curve);
    }
    else
    {
        currentValue = interpolateValue(
            breakpoints[segment + 1].value,
            breakpoints[segment].value, p,
            breakpoints[segment].curve);
    }
}

//==============================================================================
void MultiPointEnvelope::handleEnvelopeEnd()
{
    active = false;
    if (!breakpoints.empty())
        currentValue = breakpoints.back().value;
    else
        currentValue = 0.0f;
}

} // namespace ana
