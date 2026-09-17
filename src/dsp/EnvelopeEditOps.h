#pragma once

#include "MultiPointEnvelope.h"
#include <juce_graphics/juce_graphics.h>

namespace ana
{

//==============================================================================
/**
    Pure helpers shared by the envelope editing UI: clamping, derived ADSR
    readout and pixel <-> envelope coordinate mapping.

    No Component dependencies, so every rule here can be unit-tested headlessly.
    Axis units follow the envelope itself: seconds when sync is off, beats when
    tempo sync is enabled.
*/
namespace EnvelopeEditOps
{

/** Values derived from a breakpoint list, used to keep the ADSR sliders in sync
    with a freely drawn shape.
*/
struct DerivedADSR
{
    float attack  = 0.0f;
    float decay   = 0.0f;
    float sustain = 0.0f;
    float release = 0.0f;
};

//==============================================================================
/** Adds a breakpoint. Returns false when the envelope is already full. */
bool addPoint(MultiPointEnvelope& env, float time, float value, CurveType curve);

/** Removes a breakpoint, but never drops below two points. */
bool removePoint(MultiPointEnvelope& env, int index);

/** Moves a breakpoint, clamping time between its neighbours (and 0..1 value). */
bool movePoint(MultiPointEnvelope& env, int index, float time, float value);

/** Maps the current shape onto A/D/S/R (seconds), using the HOLD (loop end)
    point as the sustain position. */
DerivedADSR deriveADSR(const MultiPointEnvelope& env);

//==============================================================================
/** Largest time shown on the time axis (at least 1 unit, 10% headroom). */
float maxAxisTime(const MultiPointEnvelope& env);

float timeToX(const MultiPointEnvelope& env, float time, juce::Rectangle<float> area);
float xToTime(const MultiPointEnvelope& env, float x,    juce::Rectangle<float> area);
float valueToY(float value, juce::Rectangle<float> area);
float yToValue(float y,     juce::Rectangle<float> area);

/** Index of the breakpoint under the given point, or -1. */
int hitTestPoint(const MultiPointEnvelope& env, juce::Point<float> p,
                 juce::Rectangle<float> area, float radiusPx);

/** 0 = LOOP START marker, 1 = LOOP END / HOLD marker, -1 = none. */
int hitTestMarker(const MultiPointEnvelope& env, juce::Point<float> p,
                  juce::Rectangle<float> area, float radiusPx);

} // namespace EnvelopeEditOps
} // namespace ana
