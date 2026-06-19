#pragma once
#include "../EffectsChain.h"
#include "DeEsserModule.h"
#include "CompressorEffect.h"
#include "EQModule.h"
#include "DriveModule.h"
#include "../LFOSystem.h"
#include "PitchModule.h"
#include "StereoWidenerEffect.h"
#include "SpaceModule.h"

namespace ana {

//==============================================================================
/**
    Soloist Vocal Chain — composite preset effect for lead vocal processing.

    Routes audio through a fixed chain designed for solo/prominent vocals:
        DeEsser -> Compressor -> EQ -> Drive (Tube) -> PitchDrift
            -> StereoWidener -> SpaceModule (Reverb)

    Owns internal instances of each sub-module and exposes a curated set of
    high-level parameters that map to the underlying DSP controls.

    Call preset() to load the soloist vocal defaults, then tweak individual
    parameters.

    Parameters:
        Presence     0-6 dB boost at 3-5 kHz    (EQ mid-high band)
        Air          0-4 dB shelf at 10 kHz     (EQ air band)
        Compression  2:1 – 8:1 ratio, attack=3ms
        Saturation   0-0.5 drive, Tube mode     (DriveModule)
        PitchDrift   0-10 cents, rate via LFO
        DriftRate    0.5 – 2.0 Hz               (LFOSystem)
        ReverbWet    0-100 %                     (SpaceModule mix)
        Width        50-100 %                    (StereoWidener)
*/
class SoloistVocalChain : public EffectBase
{
public:
    SoloistVocalChain();
    ~SoloistVocalChain() override = default;

    //==============================================================================
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    //==============================================================================
    /** @name High-Level Parameter Setters */

    /** Presence boost, 0-6 dB.  Applies a peaking boost centred on 4 kHz. */
    void setPresence(float db);

    /** Air shelf boost, 0-4 dB.  Applies a high-shelf at 10 kHz. */
    void setAir(float db);

    /** Compression ratio, 2.0 – 8.0 (:1).  Attack is fixed at 3 ms. */
    void setCompression(float ratio);

    /** Saturation drive, 0.0 – 0.5.  DriveModule in Tube mode. */
    void setSaturation(float drive);

    /** Maximum pitch drift, 0-10 cents.  Modulated by internal LFO. */
    void setPitchDrift(float cents);

    /** Pitch drift LFO rate, 0.5 – 2.0 Hz. */
    void setDriftRate(float hz);

    /** Reverb wet mix, 0.0 – 1.0 (0-100 %). */
    void setReverbWet(float wet);

    /** Stereo width, 50-100 % (0.5 – 1.0 mapped to StereoWidener). */
    void setWidth(float percent);

    //==============================================================================
    /** Load soloist vocal defaults for every sub-module. */
    void preset();

    //==============================================================================
    juce::ValueTree getState() const override;
    void setState(const juce::ValueTree& state) override;

private:
    //==============================================================================
    // Owned sub-modules in chain processing order
    DeEsserModule       deEsser_;
    CompressorEffect    compressor_;
    EQModule            eq_;
    DriveModule         drive_;
    LFOSystem           driftLfo_;   // modulates pitch drift
    PitchModule         pitch_;      // pitch shift applied via LFO modulation
    StereoWidenerEffect widener_;
    SpaceModule         reverb_;

    //==============================================================================
    double sampleRate_ = 44100.0;
    int    blockSize_  = 512;

    //==============================================================================
    /** Current parameter cache (for getState / setState). */
    float presenceDb_    = 3.0f;
    float airDb_         = 2.0f;
    float compression_   = 4.0f;
    float saturation_    = 0.25f;
    float pitchDriftCents_ = 5.0f;
    float driftRateHz_   = 1.2f;
    float reverbWet_     = 0.25f;
    float widthPercent_  = 0.8f;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoloistVocalChain)
};

} // namespace ana
