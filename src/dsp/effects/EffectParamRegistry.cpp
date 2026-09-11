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

    int getNumParams() const override { return 9; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "low_freq",  "LOW FREQ",  20.0f,  20000.0f, 200.0f, 0.3f,  false, nullptr },
            { "low_gain",  "LOW GAIN",  -24.0f, 24.0f,    0.0f,   1.0f,  false, nullptr },
            { "low_q",     "LOW Q",     0.1f,   10.0f,    0.707f, 1.0f,  false, nullptr },
            { "mid_freq",  "MID FREQ",  20.0f,  20000.0f, 1000.0f, 0.3f, false, nullptr },
            { "mid_gain",  "MID GAIN",  -24.0f, 24.0f,    0.0f,   1.0f,  false, nullptr },
            { "mid_q",     "MID Q",     0.1f,   10.0f,    0.707f, 1.0f,  false, nullptr },
            { "high_freq", "HIGH FREQ", 20.0f,  20000.0f, 5000.0f, 0.3f, false, nullptr },
            { "high_gain", "HIGH GAIN", -24.0f, 24.0f,    0.0f,   1.0f,  false, nullptr },
            { "high_q",    "HIGH Q",    0.1f,   10.0f,    0.707f, 1.0f,  false, nullptr },
        };
        return table[(size_t) i];
    }
    float getParamValue(int i) const override
    {
        const int band = i / 3;
        const int field = i % 3;
        const auto& b = fx.getBand(band);
        if (field == 0) return b.frequency;
        if (field == 1) return b.gain;
        return b.q;
    }
    void setParamValue(int i, float v) override
    {
        const int band = i / 3;
        const int field = i % 3;
        const auto& b = fx.getBand(band);
        if (band == 0)
        {
            if (field == 0)      fx.setLowBand(v, b.gain, b.q);
            else if (field == 1) fx.setLowBand(b.frequency, v, b.q);
            else                 fx.setLowBand(b.frequency, b.gain, v);
        }
        else if (band == 1)
        {
            if (field == 0)      fx.setMidBand(v, b.gain, b.q);
            else if (field == 1) fx.setMidBand(b.frequency, v, b.q);
            else                 fx.setMidBand(b.frequency, b.gain, v);
        }
        else
        {
            if (field == 0)      fx.setHighBand(v, b.gain, b.q);
            else if (field == 1) fx.setHighBand(b.frequency, v, b.q);
            else                 fx.setHighBand(b.frequency, b.gain, v);
        }
    }
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

    int getNumParams() const override { return 5; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "drive",  "DRIVE",  0.0f, 100.0f, 40.0f,  1.0f, false, nullptr },
            { "range",  "RANGE",  0.0f, 100.0f, 50.0f,  1.0f, false, nullptr },
            { "blend",  "BLEND",  0.0f, 100.0f, 50.0f,  1.0f, false, nullptr },
            { "volume", "LEVEL",  0.0f, 200.0f, 100.0f, 1.0f, false, nullptr },
            { "type",   "TYPE",   0.0f, 4.0f,   0.0f,   1.0f, true,  "0=SOFT 1=HARD 2=TUBE 3=FOLDER 4=CRUSH" },
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
            case 3:  return fx.getVolume();
            default: return static_cast<float>(static_cast<int>(fx.getType()));
        }
    }
    void setParamValue(int i, float v) override
    {
        switch (i)
        {
            case 0:  fx.setDrive(v); break;
            case 1:  fx.setRange(v); break;
            case 2:  fx.setBlend(v); break;
            case 3:  fx.setVolume(v); break;
            default: fx.setType(static_cast<DistortionType>(
                         juce::jlimit(0, 4, static_cast<int>(v)))); break;
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

    int getNumParams() const override { return 3; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "speed",   "SPEED",   0.01f, 20.0f, 10.0f, 0.3f, false, nullptr },
            { "amount",  "AMOUNT",  0.0f,  1.0f,  0.5f,  1.0f, false, nullptr },
            { "enabled", "ON",      0.0f,  1.0f,  1.0f,  1.0f, true,  nullptr },
        };
        return table[(size_t) i];
    }
    float getParamValue(int i) const override
    {
        switch (i)
        {
            case 0:  return effect.getRetuneSpeed();
            case 1:  return effect.getAmount();
            default: return effect.isEnabled() ? 1.0f : 0.0f;
        }
    }
    void setParamValue(int i, float v) override
    {
        switch (i)
        {
            case 0:  effect.setRetuneSpeed(v); break;
            case 1:  effect.setAmount(v); break;
            default: effect.setEnabled(v > 0.5f); break;
        }
    }
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

    int getNumParams() const override { return 5; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "rate",   "RATE",     0.1f, 10.0f, 0.5f, 0.3f, false, nullptr },
            { "depth",  "DEPTH",    0.0f, 1.0f,  0.5f, 1.0f, false, nullptr },
            { "delay",  "DELAY",    0.1f, 10.0f, 3.0f, 0.3f, false, nullptr },
            { "fb",     "FEEDBACK", 0.0f, 1.0f,  0.3f, 1.0f, false, nullptr },
            { "mix",    "MIX",      0.0f, 1.0f,  0.5f, 1.0f, false, nullptr },
        };
        return table[(size_t) i];
    }
    float getParamValue(int i) const override
    {
        switch (i)
        {
            case 0:  return fx.getRate();
            case 1:  return fx.getDepth();
            case 2:  return fx.getDelay();
            case 3:  return fx.getFeedback();
            default: return fx.getMix();
        }
    }
    void setParamValue(int i, float v) override
    {
        switch (i)
        {
            case 0:  fx.setRate(v); break;
            case 1:  fx.setDepth(v); break;
            case 2:  fx.setDelay(v); break;
            case 3:  fx.setFeedback(v); break;
            default: fx.setMix(v); break;
        }
    }
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

    int getNumParams() const override { return 6; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "rate",   "RATE",     0.1f,  20.0f,  1.0f, 0.3f, false, nullptr },
            { "depth",  "DEPTH",    0.0f,  1.0f,   0.5f, 1.0f, false, nullptr },
            { "fb",     "FEEDBACK", 0.0f,  1.0f,   0.3f, 1.0f, false, nullptr },
            { "stages", "STAGES",   2.0f,  12.0f,  8.0f, 1.0f, true,  nullptr },
            { "mix",    "MIX",      0.0f,  1.0f,   0.5f, 1.0f, false, nullptr },
            { "stereo", "WIDTH",    0.0f,  180.0f, 0.0f, 1.0f, false, nullptr },
        };
        return table[(size_t) i];
    }
    float getParamValue(int i) const override
    {
        switch (i)
        {
            case 0:  return fx.getRate();
            case 1:  return fx.getDepth();
            case 2:  return fx.getFeedback();
            case 3:  return static_cast<float>(fx.getStages());
            case 4:  return fx.getMix();
            default: return fx.getStereoPhaseOffset();
        }
    }
    void setParamValue(int i, float v) override
    {
        switch (i)
        {
            case 0:  fx.setRate(v); break;
            case 1:  fx.setDepth(v); break;
            case 2:  fx.setFeedback(v); break;
            case 3:  fx.setStages(static_cast<int>(v)); break;
            case 4:  fx.setMix(v); break;
            default: fx.setStereoPhaseOffset(v); break;
        }
    }
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

    int getNumParams() const override { return 3; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "freq",     "FREQUENCY", 0.1f, 5000.0f, 100.0f, 0.3f, false, nullptr },
            { "waveform", "WAVEFORM",  0.0f, 2.0f,    0.0f,   1.0f, true,  "0=SINE 1=TRI 2=SQUARE" },
            { "mix",      "MIX",       0.0f, 1.0f,    0.5f,   1.0f, false, nullptr },
        };
        return table[(size_t) i];
    }
    float getParamValue(int i) const override
    {
        switch (i)
        {
            case 0:  return fx.getFrequency();
            case 1:  return static_cast<float>(fx.getWaveform());
            default: return fx.getMix();
        }
    }
    void setParamValue(int i, float v) override
    {
        switch (i)
        {
            case 0:  fx.setFrequency(v); break;
            case 1:  fx.setWaveform(juce::jlimit(0, 2, static_cast<int>(v))); break;
            default: fx.setMix(v); break;
        }
    }
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

    int getNumParams() const override { return 2; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "width", "WIDTH", 0.0f, 1.0f, 0.5f, 1.0f, false, "0=MONO 0.5=100% 1=200%" },
            { "mix",   "MIX",   0.0f, 1.0f, 1.0f, 1.0f, false, nullptr },
        };
        return table[(size_t) i];
    }
    float getParamValue(int i) const override
    {
        return i == 0 ? fx.getWidth() : fx.getMix();
    }
    void setParamValue(int i, float v) override
    {
        if (i == 0) fx.setWidth(v);
        else        fx.setMix(v);
    }
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

    int getNumParams() const override { return 5; }
    const EffectParamSpec& getParamSpec(int i) const override
    {
        static const std::vector<EffectParamSpec> table = {
            { "drive", "DRIVE", 0.0f,    100.0f,   30.0f,  1.0f,  false, nullptr },
            { "tone",  "TONE",  20.0f,   20000.0f, 8000.0f, 0.3f, false, nullptr },
            { "mix",   "MIX",   0.0f,    100.0f,   100.0f, 1.0f,  false, nullptr },
            { "gain",  "GAIN",  0.0f,    4.0f,     1.0f,   1.0f,  false, nullptr },
            { "mode",  "MODE",  0.0f,    2.0f,     0.0f,   1.0f,  true,  "0=SOFT 1=TUBE 2=TAPE" },
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
            case 3:  return fx.getGain();
            default: return static_cast<float>(fx.getMode());
        }
    }
    void setParamValue(int i, float v) override
    {
        switch (i)
        {
            case 0:  fx.setDrive(v); break;
            case 1:  fx.setTone(v); break;
            case 2:  fx.setMix(v); break;
            case 3:  fx.setGain(v); break;
            default: fx.setMode(static_cast<SaturationMode>(
                         juce::jlimit(0, 2, static_cast<int>(v)))); break;
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
