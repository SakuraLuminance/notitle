#include "EffectParamRegistry.h"
#include "DelayEffect.h"
#include "ReverbEffect.h"
#include "EQEffect.h"
#include "ChorusEffect.h"
#include "DistortionEffect.h"
#include "SaturationEffect.h"
#include "BitcrusherEffect.h"
#include "CompressorEffect.h"
#include "AutoTuneEffect.h"
#include "FlangerEffect.h"
#include "PhaserEffect.h"
#include "RingModulatorEffect.h"
#include "StereoWidenerEffect.h"
#include "LimiterEffect.h"
#include <vector>

namespace ana
{

namespace
{

//==============================================================================
// Adapter-wrapped effects: each owns a concrete effect and (for the first
// batch) a parameter table mapping the generic EffectBase param surface onto
// the concrete setters/getters. UI units are the table's min/max range; unit
// conversions live in the index mappings below.
//==============================================================================

class DelayAdapter : public EffectBase
{
public:
    DelayEffect fx;

    void prepare(const juce::dsp::ProcessSpec& spec) override { fx.prepare(spec); }
    void process(juce::AudioBuffer<float>& b) override { fx.process(b); }
    void reset() override { fx.reset(); }
    juce::ValueTree getState() const override { return fx.getState(); }
    void setState(const juce::ValueTree& s) override { fx.setState(s); }

    int getNumParams() const override { return 4; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "delay_ms", "TIME",      1.0f, 2000.0f, 300.0f, 0.25f, false },
            { "feedback", "FEEDBACK",  0.0f, 100.0f,   45.0f, 1.0f,  false },
            { "mix",      "MIX",       0.0f, 100.0f,   30.0f, 1.0f,  false },
            { "ping",     "PING-PONG", 0.0f, 1.0f,      0.0f, 1.0f,  true  },
        };
        return table[(size_t) i];
    }
    float getParamValue(int i) const override
    {
        switch (i)
        {
            case 0:  return fx.getDelayTimeMs();
            case 1:  return fx.getFeedback() * 100.0f;
            case 2:  return fx.getMix() * 100.0f;
            default: return fx.isPingPong() ? 1.0f : 0.0f;
        }
    }
    void setParamValue(int i, float v) override
    {
        switch (i)
        {
            case 0:  fx.setDelayTime(v); break;
            case 1:  fx.setFeedback(v); break;
            case 2:  fx.setMix(v); break;
            default: fx.setPingPong(v > 0.5f); break;
        }
    }
};

class ReverbAdapter : public EffectBase
{
public:
    ReverbEffect fx;

    void prepare(const juce::dsp::ProcessSpec& spec) override { fx.prepare(spec); }
    void process(juce::AudioBuffer<float>& b) override { fx.process(b); }
    void reset() override { fx.reset(); }
    juce::ValueTree getState() const override { return fx.getState(); }
    void setState(const juce::ValueTree& s) override { fx.setState(s); }

    int getNumParams() const override { return 5; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "room", "ROOM SIZE", 0.0f, 1.0f, 0.5f,  1.0f, false },
            { "damp", "DAMPING",   0.0f, 1.0f, 0.5f,  1.0f, false },
            { "wet",  "WET",       0.0f, 1.0f, 0.33f, 1.0f, false },
            { "dry",  "DRY",       0.0f, 1.0f, 0.4f,  1.0f, false },
            { "width", "WIDTH",    0.0f, 1.0f, 1.0f,  1.0f, false },
        };
        return table[(size_t) i];
    }
    float getParamValue(int i) const override
    {
        switch (i)
        {
            case 0:  return fx.getRoomSize();
            case 1:  return fx.getDamping();
            case 2:  return fx.getWetLevel();
            case 3:  return fx.getDryLevel();
            default: return fx.getWidth();
        }
    }
    void setParamValue(int i, float v) override
    {
        switch (i)
        {
            case 0:  fx.setRoomSize(v); break;
            case 1:  fx.setDamping(v); break;
            case 2:  fx.setWetLevel(v); break;
            case 3:  fx.setDryLevel(v); break;
            default: fx.setWidth(v); break;
        }
    }
};

class EQAdapter : public EffectBase
{
public:
    EQEffect fx;

    void prepare(const juce::dsp::ProcessSpec& spec) override { fx.prepare(spec); }
    void process(juce::AudioBuffer<float>& b) override { fx.process(b); }
    void reset() override { fx.reset(); }
    juce::ValueTree getState() const override { return fx.getState(); }
    void setState(const juce::ValueTree& s) override { fx.setState(s); }
};

class ChorusAdapter : public EffectBase
{
public:
    ChorusEffect fx;

    void prepare(const juce::dsp::ProcessSpec& spec) override { fx.prepare(spec); }
    void process(juce::AudioBuffer<float>& b) override { fx.process(b); }
    void reset() override { fx.reset(); }
    juce::ValueTree getState() const override { return fx.getState(); }
    void setState(const juce::ValueTree& s) override { fx.setState(s); }

    int getNumParams() const override { return 5; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "rate",   "RATE",     0.05f, 10.0f, 1.0f,  0.3f, false },
            { "depth",  "DEPTH",    0.0f,  100.0f, 50.0f, 1.0f, false },
            { "cdelay", "DELAY",    0.0f,  100.0f, 20.0f, 1.0f, false },
            { "fb",     "FEEDBACK", 0.0f,  99.0f,  25.0f, 1.0f, false },
            { "mix",    "MIX",      0.0f,  100.0f, 50.0f, 1.0f, false },
        };
        return table[(size_t) i];
    }
    float getParamValue(int i) const override
    {
        switch (i)
        {
            case 0:  return fx.getRate();
            case 1:  return fx.getDepth() * 100.0f;
            case 2:  return fx.getCentreDelay();
            case 3:  return fx.getFeedback() * 100.0f;
            default: return fx.getMix() * 100.0f;
        }
    }
    void setParamValue(int i, float v) override
    {
        switch (i)
        {
            case 0:  fx.setRate(v); break;
            case 1:  fx.setDepth(v); break;
            case 2:  fx.setCentreDelay(v); break;
            case 3:  fx.setFeedback(v); break;
            default: fx.setMix(v); break;
        }
    }
};

class DistortionAdapter : public EffectBase
{
public:
    DistortionEffect fx;

    void prepare(const juce::dsp::ProcessSpec& spec) override { fx.prepare(spec); }
    void process(juce::AudioBuffer<float>& b) override { fx.process(b); }
    void reset() override { fx.reset(); }
    juce::ValueTree getState() const override { return fx.getState(); }
    void setState(const juce::ValueTree& s) override { fx.setState(s); }

    int getNumParams() const override { return 4; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "drive",  "DRIVE",  0.0f, 100.0f, 40.0f,  1.0f, false },
            { "range",  "RANGE",  0.0f, 100.0f, 50.0f,  1.0f, false },
            { "blend",  "BLEND",  0.0f, 100.0f, 50.0f,  1.0f, false },
            { "volume", "LEVEL",  0.0f, 200.0f, 100.0f, 1.0f, false },
        };
        return table[(size_t) i];
    }
    float getParamValue(int i) const override
    {
        switch (i)
        {
            case 0:  return fx.getDrive();
            case 1:  return fx.getRange();
            case 2:  return fx.getBlend();
            default: return fx.getVolume();
        }
    }
    void setParamValue(int i, float v) override
    {
        switch (i)
        {
            case 0:  fx.setDrive(v); break;
            case 1:  fx.setRange(v); break;
            case 2:  fx.setBlend(v); break;
            default: fx.setVolume(v); break;
        }
    }
};

class AutoTuneAdapter : public EffectBase
{
public:
    AutoTuneEffect effect;

    void prepare(const juce::dsp::ProcessSpec& spec) override { effect.setSampleRate(spec.sampleRate); }
    void process(juce::AudioBuffer<float>& b) override { effect.processBlock(b); }
    void reset() override { effect.reset(); }
    juce::ValueTree getState() const override { return effect.getState(); }
    void setState(const juce::ValueTree& s) override { effect.setState(s); }
};

class CompressorAdapter : public EffectBase
{
public:
    CompressorEffect fx;

    void prepare(const juce::dsp::ProcessSpec& spec) override { fx.prepare(spec); }
    void process(juce::AudioBuffer<float>& b) override { fx.process(b); }
    void reset() override { fx.reset(); }
    juce::ValueTree getState() const override { return fx.getState(); }
    void setState(const juce::ValueTree& s) override { fx.setState(s); }

    int getNumParams() const override { return 6; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "thresh", "THRESHOLD", -60.0f, 0.0f,   -20.0f, 1.0f,  false },
            { "ratio",  "RATIO",     1.0f,   20.0f,  4.0f,   1.0f,  false },
            { "attack", "ATTACK",    0.1f,   100.0f, 10.0f,  0.3f,  false },
            { "release","RELEASE",   10.0f,  1000.0f, 200.0f, 0.3f, false },
            { "knee",   "KNEE",      0.0f,   10.0f,  3.0f,   1.0f,  false },
            { "makeup", "MAKEUP",    0.0f,   24.0f,  0.0f,   1.0f,  false },
        };
        return table[(size_t) i];
    }
    float getParamValue(int i) const override
    {
        switch (i)
        {
            case 0:  return fx.getThreshold();
            case 1:  return fx.getRatio();
            case 2:  return fx.getAttack();
            case 3:  return fx.getRelease();
            case 4:  return fx.getKnee();
            default: return fx.getMakeupGain();
        }
    }
    void setParamValue(int i, float v) override
    {
        switch (i)
        {
            case 0:  fx.setThreshold(v); break;
            case 1:  fx.setRatio(v); break;
            case 2:  fx.setAttack(v); break;
            case 3:  fx.setRelease(v); break;
            case 4:  fx.setKnee(v); break;
            default: fx.setMakeupGain(v); break;
        }
    }
};

class BitcrusherAdapter : public EffectBase
{
public:
    BitcrusherEffect fx;

    void prepare(const juce::dsp::ProcessSpec& spec) override { fx.prepare(spec); }
    void process(juce::AudioBuffer<float>& b) override { fx.process(b); }
    void reset() override { fx.reset(); }
    juce::ValueTree getState() const override { return fx.getState(); }
    void setState(const juce::ValueTree& s) override { fx.setState(s); }

    int getNumParams() const override { return 3; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "bits",       "BIT DEPTH",  1.0f, 16.0f, 8.0f, 1.0f, true  },
            { "downsample", "DOWNSAMPLE", 1.0f, 32.0f, 1.0f, 1.0f, true  },
            { "mix",        "MIX",        0.0f, 1.0f,  1.0f, 1.0f, false },
        };
        return table[(size_t) i];
    }
    float getParamValue(int i) const override
    {
        switch (i)
        {
            case 0:  return fx.getBitDepth();
            case 1:  return fx.getDownsample();
            default: return fx.getMix();
        }
    }
    void setParamValue(int i, float v) override
    {
        switch (i)
        {
            case 0:  fx.setBitDepth(v); break;
            case 1:  fx.setDownsample(v); break;
            default: fx.setMix(v); break;
        }
    }
};

class FlangerAdapter : public EffectBase
{
public:
    FlangerEffect fx;

    void prepare(const juce::dsp::ProcessSpec& spec) override { fx.prepare(spec); }
    void process(juce::AudioBuffer<float>& b) override { fx.process(b); }
    void reset() override { fx.reset(); }
    juce::ValueTree getState() const override { return fx.getState(); }
    void setState(const juce::ValueTree& s) override { fx.setState(s); }
};

class PhaserAdapter : public EffectBase
{
public:
    PhaserEffect fx;

    void prepare(const juce::dsp::ProcessSpec& spec) override { fx.prepare(spec); }
    void process(juce::AudioBuffer<float>& b) override { fx.process(b); }
    void reset() override { fx.reset(); }
    juce::ValueTree getState() const override { return fx.getState(); }
    void setState(const juce::ValueTree& s) override { fx.setState(s); }
};

class RingModulatorAdapter : public EffectBase
{
public:
    RingModulatorEffect fx;

    void prepare(const juce::dsp::ProcessSpec& spec) override { fx.prepare(spec); }
    void process(juce::AudioBuffer<float>& b) override { fx.process(b); }
    void reset() override { fx.reset(); }
    juce::ValueTree getState() const override { return fx.getState(); }
    void setState(const juce::ValueTree& s) override { fx.setState(s); }
};

class StereoWidenerAdapter : public EffectBase
{
public:
    StereoWidenerEffect fx;

    void prepare(const juce::dsp::ProcessSpec& spec) override { fx.prepare(spec); }
    void process(juce::AudioBuffer<float>& b) override { fx.process(b); }
    void reset() override { fx.reset(); }
    juce::ValueTree getState() const override { return fx.getState(); }
    void setState(const juce::ValueTree& s) override { fx.setState(s); }
};

class SaturationAdapter : public EffectBase
{
public:
    SaturationEffect fx;

    void prepare(const juce::dsp::ProcessSpec& spec) override { fx.prepare(spec); }
    void process(juce::AudioBuffer<float>& b) override { fx.process(b); }
    void reset() override { fx.reset(); }
    juce::ValueTree getState() const override { return fx.getState(); }
    void setState(const juce::ValueTree& s) override { fx.setState(s); }

    int getNumParams() const override { return 4; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "drive", "DRIVE", 0.0f,    100.0f,   30.0f,  1.0f,  false },
            { "tone",  "TONE",  20.0f,   20000.0f, 8000.0f, 0.3f, false },
            { "mix",   "MIX",   0.0f,    100.0f,   100.0f, 1.0f,  false },
            { "gain",  "GAIN",  0.0f,    4.0f,     1.0f,   1.0f,  false },
        };
        return table[(size_t) i];
    }
    float getParamValue(int i) const override
    {
        switch (i)
        {
            case 0:  return fx.getDrive();
            case 1:  return fx.getTone();
            case 2:  return fx.getMix();
            default: return fx.getGain();
        }
    }
    void setParamValue(int i, float v) override
    {
        switch (i)
        {
            case 0:  fx.setDrive(v); break;
            case 1:  fx.setTone(v); break;
            case 2:  fx.setMix(v); break;
            default: fx.setGain(v); break;
        }
    }
};

class LimiterAdapter : public EffectBase
{
public:
    LimiterEffect fx;

    void prepare(const juce::dsp::ProcessSpec& spec) override { fx.prepare(spec); }
    void process(juce::AudioBuffer<float>& b) override { fx.process(b); }
    void reset() override { fx.reset(); }
    juce::ValueTree getState() const override { return fx.getState(); }
    void setState(const juce::ValueTree& s) override { fx.setState(s); }

    int getNumParams() const override { return 5; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "thresh",    "THRESHOLD",  -30.0f, 0.0f,  -3.0f, 1.0f,  false },
            { "attack",    "ATTACK",     0.01f,  10.0f, 1.0f,  0.3f,  false },
            { "release",   "RELEASE",    1.0f,   100.0f, 60.0f, 1.0f, false },
            { "lookahead", "LOOKAHEAD",  0.0f,   10.0f, 2.0f,  1.0f,  false },
            { "mix",       "MIX",        0.0f,   1.0f,  1.0f,  1.0f,  false },
        };
        return table[(size_t) i];
    }
    float getParamValue(int i) const override
    {
        switch (i)
        {
            case 0:  return fx.getThreshold();
            case 1:  return fx.getAttack();
            case 2:  return fx.getRelease();
            case 3:  return fx.getLookahead();
            default: return fx.getMix();
        }
    }
    void setParamValue(int i, float v) override
    {
        switch (i)
        {
            case 0:  fx.setThreshold(v); break;
            case 1:  fx.setAttack(v); break;
            case 2:  fx.setRelease(v); break;
            case 3:  fx.setLookahead(v); break;
            default: fx.setMix(v); break;
        }
    }
};

} // namespace

//==============================================================================
const juce::StringArray& EffectParamRegistry::getTypeNames()
{
    static const juce::StringArray names = {
        "Delay", "Reverb", "EQ", "Chorus", "Distortion", "Saturation",
        "Bitcrusher", "Compressor", "AutoTune", "Flanger", "Phaser",
        "RingModulator", "StereoWidener", "Limiter",
    };
    return names;
}

std::unique_ptr<EffectBase> EffectParamRegistry::create(const juce::String& typeName)
{
    if (typeName == "Delay")         return std::make_unique<DelayAdapter>();
    if (typeName == "Reverb")        return std::make_unique<ReverbAdapter>();
    if (typeName == "EQ")            return std::make_unique<EQAdapter>();
    if (typeName == "Chorus")        return std::make_unique<ChorusAdapter>();
    if (typeName == "Distortion")    return std::make_unique<DistortionAdapter>();
    if (typeName == "AutoTune")      return std::make_unique<AutoTuneAdapter>();
    if (typeName == "Bitcrusher")    return std::make_unique<BitcrusherAdapter>();
    if (typeName == "Compressor")    return std::make_unique<CompressorAdapter>();
    if (typeName == "Flanger")       return std::make_unique<FlangerAdapter>();
    if (typeName == "Phaser")        return std::make_unique<PhaserAdapter>();
    if (typeName == "RingModulator") return std::make_unique<RingModulatorAdapter>();
    if (typeName == "StereoWidener") return std::make_unique<StereoWidenerAdapter>();
    if (typeName == "Saturation")    return std::make_unique<SaturationAdapter>();
    if (typeName == "Limiter")       return std::make_unique<LimiterAdapter>();

    jassertfalse;
    return nullptr;
}

} // namespace ana
