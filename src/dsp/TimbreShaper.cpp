#include "TimbreShaper.h"

#include "BlurEffect.h"
#include "DualTimbre.h"

#include <algorithm>
#include <cmath>

namespace ana
{

//==============================================================================
void TimbreShaper::applyBright(PartialDataSIMD& p, float bright01)
{
    const float tilt = (std::clamp(bright01, 0.0f, 1.0f) - 0.5f) * 4.0f;   // -2..+2
    if (std::abs(tilt) < 1.0e-4f)
        return;

    constexpr float refHz = 1000.0f;

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        if (! p.isActive(i))
            continue;

        const float f    = std::max(20.0f, p.frequency[i]);
        const float gain = std::clamp(std::pow(f / refHz, tilt), 0.0625f, 16.0f);
        p.amplitude[i]   = std::clamp(p.amplitude[i] * gain, 0.0f, 1.0f);
    }

    p.updateActiveMask();
}

//==============================================================================
void TimbreShaper::applyHpf(PartialDataSIMD& p, float cutoffHz)
{
    const float cut = std::max(0.0f, cutoffHz);
    if (cut <= 0.0f)
        return;

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
        if (p.amplitude[i] > 0.0f && p.frequency[i] < cut)
            p.amplitude[i] = 0.0f;

    p.updateActiveMask();
}

//==============================================================================
void TimbreShaper::applyBlur(PartialDataSIMD& p, float amount01, double sampleRate)
{
    const float amount = std::clamp(amount01, 0.0f, 1.0f);
    if (amount <= 0.0f || p.activeCount <= 1)
        return;

    BlurEffect blur;
    blur.setSampleRate(sampleRate > 0.0 ? sampleRate : 44100.0);
    blur.setAttackBlur(0.0f);
    blur.setDecayBlur(0.0f);
    blur.setTopTension(0.5f);
    blur.setBottomTension(0.5f);
    blur.setHarmonicBlur(amount);
    blur.setMix(1.0f);
    blur.process(p);

    p.updateActiveMask();
}

//==============================================================================
void TimbreShaper::shape(PartialDataSIMD& p,
                         const TimbreShapeParams& params,
                         double sampleRate)
{
    applyBright(p, params.bright);
    applyHpf(p, params.hpfHz);
    applyBlur(p, params.blur, sampleRate);
}

//==============================================================================
PartialDataSIMD TimbreShaper::blend(const PartialDataSIMD& a,
                                    const PartialDataSIMD& b,
                                    float mix01)
{
    PartialDataSIMD out = a;

    DualTimbre::blend(a.amplitude, b.amplitude, out.amplitude,
                      PartialDataSIMD::kMaxPartials,
                      std::clamp(mix01, 0.0f, 1.0f),
                      TimbreBlendMode::Fade);

    out.updateActiveMask();
    return out;
}

} // namespace ana
