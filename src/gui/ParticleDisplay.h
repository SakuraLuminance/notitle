#pragma once

#include "CyberpunkTheme.h"
#include "../dsp/SpectralParticleSystem.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ana
{

//==============================================================================
/**
    Visualisation for the spectral particle system.

    Draws each active particle as a dot: frequency on a log axis (x), amplitude
    on the y axis, hue from the particle's hue and size from its brightness.

    The particle physics is advanced by the host editor timer (message thread),
    so painting reads the particle list without any locking.
*/
class ParticleDisplay : public juce::Component
{
public:
    ParticleDisplay() = default;

    /** Binds the system to visualise.  Does not take ownership. */
    void setParticleSystem(SpectralParticleSystem* system) noexcept
    {
        system_ = system;
        repaint();
    }

    void paint(juce::Graphics&) override;

private:
    SpectralParticleSystem* system_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParticleDisplay)
};

} // namespace ana
