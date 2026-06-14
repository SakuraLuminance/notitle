#include "PresetFactory.h"

namespace ana {
juce::ValueTree PresetFactory::createFactoryBass()
{
    juce::ValueTree params("Parameters");

    // STFT
    juce::ValueTree stft("STFTConfig");
    stft.setProperty("FFTSize",      2048, nullptr);
    stft.setProperty("HopSize",      512, nullptr);
    stft.setProperty("WindowType",   "Hann", nullptr);
    stft.setProperty("Threshold",    -60.0f, nullptr);
    stft.setProperty("MaxPartials",  512, nullptr);
    params.addChild(stft, 0, nullptr);

    // Filters - LowPass at 200Hz with resonance
    juce::ValueTree filters("Filters");
    filters.setProperty("RoutingMode", "Serial", nullptr);
    filters.setProperty("MasterGain",  1.0f, nullptr);

    juce::ValueTree slot("Slot");
    slot.setProperty("Type",          "LowPass", nullptr);
    slot.setProperty("Cutoff",        200.0, nullptr);
    slot.setProperty("Resonance",     0.3f, nullptr);
    slot.setProperty("Drive",         0.2f, nullptr);
    slot.setProperty("Mix",           1.0f, nullptr);
    slot.setProperty("Bypassed",      false, nullptr);
    slot.setProperty("CrossoverLow",  200.0, nullptr);
    slot.setProperty("CrossoverHigh", 2000.0, nullptr);
    slot.setProperty("MorphSource",   "LowPass", nullptr);
    slot.setProperty("MorphTarget",   "HighPass", nullptr);
    slot.setProperty("MorphAmount",   0.0f, nullptr);
    filters.addChild(slot, 0, nullptr);
    params.addChild(filters, -1, nullptr);

    // Envelope - fast attack, medium decay, high sustain
    juce::ValueTree envelope("Envelope");
    envelope.setProperty("LoopMode",     "None", nullptr);
    envelope.setProperty("LoopStart",    0, nullptr);
    envelope.setProperty("LoopEnd",      -1, nullptr);
    envelope.setProperty("Tempo",        120.0, nullptr);
    envelope.setProperty("BeatDivision", 1.0, nullptr);
    envelope.setProperty("SyncEnabled",  false, nullptr);
    {
        auto bp1 = juce::ValueTree("Breakpoint");
        bp1.setProperty("Time", 0.0f, nullptr);
        bp1.setProperty("Value", 0.0f, nullptr);
        bp1.setProperty("Curve", "Exponential", nullptr);
        envelope.addChild(bp1, -1, nullptr);

        auto bp2 = juce::ValueTree("Breakpoint");
        bp2.setProperty("Time", 0.01f, nullptr);
        bp2.setProperty("Value", 1.0f, nullptr);
        bp2.setProperty("Curve", "Linear", nullptr);
        envelope.addChild(bp2, -1, nullptr);

        auto bp3 = juce::ValueTree("Breakpoint");
        bp3.setProperty("Time", 0.3f, nullptr);
        bp3.setProperty("Value", 0.8f, nullptr);
        bp3.setProperty("Curve", "Exponential", nullptr);
        envelope.addChild(bp3, -1, nullptr);

        auto bp4 = juce::ValueTree("Breakpoint");
        bp4.setProperty("Time", 1.5f, nullptr);
        bp4.setProperty("Value", 0.0f, nullptr);
        bp4.setProperty("Curve", "Exponential", nullptr);
        envelope.addChild(bp4, -1, nullptr);
    }
    params.addChild(envelope, -1, nullptr);

    // LFO
    juce::ValueTree lfo("LFO");
    lfo.setProperty("Waveform",    "Sine", nullptr);
    lfo.setProperty("RateHz",      4.0f, nullptr);
    lfo.setProperty("RateBeats",   1.0f, nullptr);
    lfo.setProperty("Depth",       30.0f, nullptr);
    lfo.setProperty("Phase",       0.0f, nullptr);
    lfo.setProperty("Bipolar",     false, nullptr);
    lfo.setProperty("SyncEnabled", false, nullptr);
    lfo.setProperty("Tempo",       120.0, nullptr);
    params.addChild(lfo, -1, nullptr);

    // Granular (minimal)
    juce::ValueTree granular("Granular");
    granular.setProperty("GrainSize",   50.0f, nullptr);
    granular.setProperty("Density",     5.0f, nullptr);
    granular.setProperty("Position",    0.5f, nullptr);
    granular.setProperty("Pitch",       0.0f, nullptr);
    granular.setProperty("Amplitude",   0.3f, nullptr);
    granular.setProperty("Pan",         0.0f, nullptr);
    granular.setProperty("WindowType",  "Hann", nullptr);
    granular.setProperty("PosModType",  "Off", nullptr);
    granular.setProperty("PosModDepth", 0.1f, nullptr);
    granular.setProperty("PosModRate",  1.0f, nullptr);
    params.addChild(granular, -1, nullptr);

    // Unison - 2 voices, slight detune
    juce::ValueTree unison("Unison");
    unison.setProperty("VoiceCount",   2, nullptr);
    unison.setProperty("Detune",       10.0f, nullptr);
    unison.setProperty("StereoSpread", 30.0f, nullptr);
    unison.setProperty("PhaseOffset",  0.3f, nullptr);
    params.addChild(unison, -1, nullptr);

    // Voice manager
    juce::ValueTree voice("VoiceManager");
    voice.setProperty("Attack",         0.01f, nullptr);
    voice.setProperty("Decay",          0.3f, nullptr);
    voice.setProperty("Sustain",        0.8f, nullptr);
    voice.setProperty("Release",        0.5f, nullptr);
    voice.setProperty("AllocationMode", "RoundRobin", nullptr);
    params.addChild(voice, -1, nullptr);

    // Modulation (empty)
    juce::ValueTree mod("Modulation");
    mod.setProperty("NumFilters", 1, nullptr);
    params.addChild(mod, -1, nullptr);

    return params;
}

juce::ValueTree PresetFactory::createFactoryLead()
{
    juce::ValueTree params("Parameters");

    // STFT (higher resolution for cleaner tones)
    juce::ValueTree stft("STFTConfig");
    stft.setProperty("FFTSize",      4096, nullptr);
    stft.setProperty("HopSize",      1024, nullptr);
    stft.setProperty("WindowType",   "BlackmanHarris", nullptr);
    stft.setProperty("Threshold",    -65.0f, nullptr);
    stft.setProperty("MaxPartials",  256, nullptr);
    params.addChild(stft, 0, nullptr);

    // Filters - BandPass at 2kHz with high resonance
    juce::ValueTree filters("Filters");
    filters.setProperty("RoutingMode", "Serial", nullptr);
    filters.setProperty("MasterGain",  1.0f, nullptr);

    juce::ValueTree slot("Slot");
    slot.setProperty("Type",          "BandPass", nullptr);
    slot.setProperty("Cutoff",        2000.0, nullptr);
    slot.setProperty("Resonance",     0.7f, nullptr);
    slot.setProperty("Drive",         0.3f, nullptr);
    slot.setProperty("Mix",           1.0f, nullptr);
    slot.setProperty("Bypassed",      false, nullptr);
    slot.setProperty("CrossoverLow",  200.0, nullptr);
    slot.setProperty("CrossoverHigh", 2000.0, nullptr);
    slot.setProperty("MorphSource",   "LowPass", nullptr);
    slot.setProperty("MorphTarget",   "HighPass", nullptr);
    slot.setProperty("MorphAmount",   0.0f, nullptr);
    filters.addChild(slot, 0, nullptr);
    params.addChild(filters, -1, nullptr);

    // Envelope - fast all around
    juce::ValueTree envelope("Envelope");
    envelope.setProperty("LoopMode",     "None", nullptr);
    envelope.setProperty("LoopStart",    0, nullptr);
    envelope.setProperty("LoopEnd",      -1, nullptr);
    envelope.setProperty("Tempo",        120.0, nullptr);
    envelope.setProperty("BeatDivision", 1.0, nullptr);
    envelope.setProperty("SyncEnabled",  false, nullptr);
    {
        auto bp1 = juce::ValueTree("Breakpoint");
        bp1.setProperty("Time", 0.0f, nullptr);
        bp1.setProperty("Value", 0.0f, nullptr);
        bp1.setProperty("Curve", "Exponential", nullptr);
        envelope.addChild(bp1, -1, nullptr);

        auto bp2 = juce::ValueTree("Breakpoint");
        bp2.setProperty("Time", 0.005f, nullptr);
        bp2.setProperty("Value", 1.0f, nullptr);
        bp2.setProperty("Curve", "Linear", nullptr);
        envelope.addChild(bp2, -1, nullptr);

        auto bp3 = juce::ValueTree("Breakpoint");
        bp3.setProperty("Time", 0.2f, nullptr);
        bp3.setProperty("Value", 0.6f, nullptr);
        bp3.setProperty("Curve", "Exponential", nullptr);
        envelope.addChild(bp3, -1, nullptr);

        auto bp4 = juce::ValueTree("Breakpoint");
        bp4.setProperty("Time", 0.4f, nullptr);
        bp4.setProperty("Value", 0.0f, nullptr);
        bp4.setProperty("Curve", "Exponential", nullptr);
        envelope.addChild(bp4, -1, nullptr);
    }
    params.addChild(envelope, -1, nullptr);

    // LFO - triangle for subtle modulation
    juce::ValueTree lfo("LFO");
    lfo.setProperty("Waveform",    "Triangle", nullptr);
    lfo.setProperty("RateHz",      6.0f, nullptr);
    lfo.setProperty("RateBeats",   0.25f, nullptr);
    lfo.setProperty("Depth",       20.0f, nullptr);
    lfo.setProperty("Phase",       90.0f, nullptr);
    lfo.setProperty("Bipolar",     true, nullptr);
    lfo.setProperty("SyncEnabled", false, nullptr);
    lfo.setProperty("Tempo",       120.0, nullptr);
    params.addChild(lfo, -1, nullptr);

    // Granular
    juce::ValueTree granular("Granular");
    granular.setProperty("GrainSize",   30.0f, nullptr);
    granular.setProperty("Density",     8.0f, nullptr);
    granular.setProperty("Position",    0.5f, nullptr);
    granular.setProperty("Pitch",       0.0f, nullptr);
    granular.setProperty("Amplitude",   0.4f, nullptr);
    granular.setProperty("Pan",         0.0f, nullptr);
    granular.setProperty("WindowType",  "Triangle", nullptr);
    granular.setProperty("PosModType",  "Off", nullptr);
    granular.setProperty("PosModDepth", 0.1f, nullptr);
    granular.setProperty("PosModRate",  1.0f, nullptr);
    params.addChild(granular, -1, nullptr);

    // Unison - 3 voices, moderate spread
    juce::ValueTree unison("Unison");
    unison.setProperty("VoiceCount",   3, nullptr);
    unison.setProperty("Detune",       5.0f, nullptr);
    unison.setProperty("StereoSpread", 60.0f, nullptr);
    unison.setProperty("PhaseOffset",  0.5f, nullptr);
    params.addChild(unison, -1, nullptr);

    // Voice manager
    juce::ValueTree voice("VoiceManager");
    voice.setProperty("Attack",         0.005f, nullptr);
    voice.setProperty("Decay",          0.2f, nullptr);
    voice.setProperty("Sustain",        0.6f, nullptr);
    voice.setProperty("Release",        0.2f, nullptr);
    voice.setProperty("AllocationMode", "OldestFirst", nullptr);
    params.addChild(voice, -1, nullptr);

    // Modulation
    juce::ValueTree mod("Modulation");
    mod.setProperty("NumFilters", 1, nullptr);
    auto conn = juce::ValueTree("Connection");
    conn.setProperty("Source",      "LFO1", nullptr);
    conn.setProperty("Target",      "Cutoff", nullptr);
    conn.setProperty("FilterIndex", 0, nullptr);
    conn.setProperty("Depth",       0.3f, nullptr);
    conn.setProperty("Bipolar",     true, nullptr);
    conn.setProperty("ID",          1, nullptr);
    mod.addChild(conn, 0, nullptr);
    params.addChild(mod, -1, nullptr);

    return params;
}

juce::ValueTree PresetFactory::createFactoryPad()
{
    juce::ValueTree params("Parameters");

    // STFT (large FFT for smoothness)
    juce::ValueTree stft("STFTConfig");
    stft.setProperty("FFTSize",      8192, nullptr);
    stft.setProperty("HopSize",      2048, nullptr);
    stft.setProperty("WindowType",   "Hamming", nullptr);
    stft.setProperty("Threshold",    -70.0f, nullptr);
    stft.setProperty("MaxPartials",  1024, nullptr);
    params.addChild(stft, 0, nullptr);

    // Filters - LowPass at 800Hz + gentle resonance
    juce::ValueTree filters("Filters");
    filters.setProperty("RoutingMode", "Serial", nullptr);
    filters.setProperty("MasterGain",  0.85f, nullptr);

    juce::ValueTree slot("Slot");
    slot.setProperty("Type",          "LowPass", nullptr);
    slot.setProperty("Cutoff",        800.0, nullptr);
    slot.setProperty("Resonance",     0.2f, nullptr);
    slot.setProperty("Drive",         0.1f, nullptr);
    slot.setProperty("Mix",           0.9f, nullptr);
    slot.setProperty("Bypassed",      false, nullptr);
    slot.setProperty("CrossoverLow",  200.0, nullptr);
    slot.setProperty("CrossoverHigh", 2000.0, nullptr);
    slot.setProperty("MorphSource",   "LowPass", nullptr);
    slot.setProperty("MorphTarget",   "BandPass", nullptr);
    slot.setProperty("MorphAmount",   0.2f, nullptr);
    filters.addChild(slot, 0, nullptr);
    params.addChild(filters, -1, nullptr);

    // Envelope - slow attack, high sustain, long release
    juce::ValueTree envelope("Envelope");
    envelope.setProperty("LoopMode",     "None", nullptr);
    envelope.setProperty("LoopStart",    0, nullptr);
    envelope.setProperty("LoopEnd",      -1, nullptr);
    envelope.setProperty("Tempo",        120.0, nullptr);
    envelope.setProperty("BeatDivision", 1.0, nullptr);
    envelope.setProperty("SyncEnabled",  false, nullptr);
    {
        auto bp1 = juce::ValueTree("Breakpoint");
        bp1.setProperty("Time", 0.0f, nullptr);
        bp1.setProperty("Value", 0.0f, nullptr);
        bp1.setProperty("Curve", "Exponential", nullptr);
        envelope.addChild(bp1, -1, nullptr);

        auto bp2 = juce::ValueTree("Breakpoint");
        bp2.setProperty("Time", 0.5f, nullptr);
        bp2.setProperty("Value", 1.0f, nullptr);
        bp2.setProperty("Curve", "SCurve", nullptr);
        envelope.addChild(bp2, -1, nullptr);

        auto bp3 = juce::ValueTree("Breakpoint");
        bp3.setProperty("Time", 1.0f, nullptr);
        bp3.setProperty("Value", 0.9f, nullptr);
        bp3.setProperty("Curve", "Exponential", nullptr);
        envelope.addChild(bp3, -1, nullptr);

        auto bp4 = juce::ValueTree("Breakpoint");
        bp4.setProperty("Time", 3.0f, nullptr);
        bp4.setProperty("Value", 0.0f, nullptr);
        bp4.setProperty("Curve", "Exponential", nullptr);
        envelope.addChild(bp4, -1, nullptr);
    }
    params.addChild(envelope, -1, nullptr);

    // LFO - slow sine, bipolar for movement
    juce::ValueTree lfo("LFO");
    lfo.setProperty("Waveform",    "Sine", nullptr);
    lfo.setProperty("RateHz",      0.8f, nullptr);
    lfo.setProperty("RateBeats",   2.0f, nullptr);
    lfo.setProperty("Depth",       40.0f, nullptr);
    lfo.setProperty("Phase",       0.0f, nullptr);
    lfo.setProperty("Bipolar",     true, nullptr);
    lfo.setProperty("SyncEnabled", false, nullptr);
    lfo.setProperty("Tempo",       120.0, nullptr);
    params.addChild(lfo, -1, nullptr);

    // Granular - medium density for texture
    juce::ValueTree granular("Granular");
    granular.setProperty("GrainSize",   60.0f, nullptr);
    granular.setProperty("Density",     20.0f, nullptr);
    granular.setProperty("Position",    0.3f, nullptr);
    granular.setProperty("Pitch",       -5.0f, nullptr);
    granular.setProperty("Amplitude",   0.35f, nullptr);
    granular.setProperty("Pan",         0.0f, nullptr);
    granular.setProperty("WindowType",  "Gaussian", nullptr);
    granular.setProperty("PosModType",  "LFO", nullptr);
    granular.setProperty("PosModDepth", 0.2f, nullptr);
    granular.setProperty("PosModRate",  0.3f, nullptr);
    params.addChild(granular, -1, nullptr);

    // Unison - 5 voices, wide spread
    juce::ValueTree unison("Unison");
    unison.setProperty("VoiceCount",   5, nullptr);
    unison.setProperty("Detune",       15.0f, nullptr);
    unison.setProperty("StereoSpread", 80.0f, nullptr);
    unison.setProperty("PhaseOffset",  0.7f, nullptr);
    params.addChild(unison, -1, nullptr);

    // Voice manager
    juce::ValueTree voice("VoiceManager");
    voice.setProperty("Attack",         0.5f, nullptr);
    voice.setProperty("Decay",          0.5f, nullptr);
    voice.setProperty("Sustain",        0.9f, nullptr);
    voice.setProperty("Release",        2.0f, nullptr);
    voice.setProperty("AllocationMode", "RoundRobin", nullptr);
    params.addChild(voice, -1, nullptr);

    // Modulation - LFO modulating cutoff
    juce::ValueTree mod("Modulation");
    mod.setProperty("NumFilters", 1, nullptr);
    auto conn = juce::ValueTree("Connection");
    conn.setProperty("Source",      "LFO1", nullptr);
    conn.setProperty("Target",      "Cutoff", nullptr);
    conn.setProperty("FilterIndex", 0, nullptr);
    conn.setProperty("Depth",       0.5f, nullptr);
    conn.setProperty("Bipolar",     true, nullptr);
    conn.setProperty("ID",          1, nullptr);
    mod.addChild(conn, 0, nullptr);
    params.addChild(mod, -1, nullptr);

    return params;
}

juce::ValueTree PresetFactory::createFactoryPluck()
{
    juce::ValueTree params("Parameters");

    // STFT
    juce::ValueTree stft("STFTConfig");
    stft.setProperty("FFTSize",      2048, nullptr);
    stft.setProperty("HopSize",      512, nullptr);
    stft.setProperty("WindowType",   "Hann", nullptr);
    stft.setProperty("Threshold",    -55.0f, nullptr);
    stft.setProperty("MaxPartials",  128, nullptr);
    params.addChild(stft, 0, nullptr);

    // Filters - HighPass at 400Hz
    juce::ValueTree filters("Filters");
    filters.setProperty("RoutingMode", "Serial", nullptr);
    filters.setProperty("MasterGain",  1.0f, nullptr);

    juce::ValueTree slot("Slot");
    slot.setProperty("Type",          "HighPass", nullptr);
    slot.setProperty("Cutoff",        400.0, nullptr);
    slot.setProperty("Resonance",     0.4f, nullptr);
    slot.setProperty("Drive",         0.4f, nullptr);
    slot.setProperty("Mix",           1.0f, nullptr);
    slot.setProperty("Bypassed",      false, nullptr);
    slot.setProperty("CrossoverLow",  200.0, nullptr);
    slot.setProperty("CrossoverHigh", 2000.0, nullptr);
    slot.setProperty("MorphSource",   "LowPass", nullptr);
    slot.setProperty("MorphTarget",   "HighPass", nullptr);
    slot.setProperty("MorphAmount",   0.0f, nullptr);
    filters.addChild(slot, 0, nullptr);
    params.addChild(filters, -1, nullptr);

    // Envelope - instant attack, fast decay, no sustain
    juce::ValueTree envelope("Envelope");
    envelope.setProperty("LoopMode",     "None", nullptr);
    envelope.setProperty("LoopStart",    0, nullptr);
    envelope.setProperty("LoopEnd",      -1, nullptr);
    envelope.setProperty("Tempo",        120.0, nullptr);
    envelope.setProperty("BeatDivision", 1.0, nullptr);
    envelope.setProperty("SyncEnabled",  false, nullptr);
    {
        auto bp1 = juce::ValueTree("Breakpoint");
        bp1.setProperty("Time", 0.0f, nullptr);
        bp1.setProperty("Value", 1.0f, nullptr);
        bp1.setProperty("Curve", "Linear", nullptr);
        envelope.addChild(bp1, -1, nullptr);

        auto bp2 = juce::ValueTree("Breakpoint");
        bp2.setProperty("Time", 0.001f, nullptr);
        bp2.setProperty("Value", 0.0f, nullptr);
        bp2.setProperty("Curve", "Exponential", nullptr);
        envelope.addChild(bp2, -1, nullptr);
    }
    params.addChild(envelope, -1, nullptr);

    // LFO (minimal)
    juce::ValueTree lfo("LFO");
    lfo.setProperty("Waveform",    "Sine", nullptr);
    lfo.setProperty("RateHz",      1.0f, nullptr);
    lfo.setProperty("RateBeats",   1.0f, nullptr);
    lfo.setProperty("Depth",       5.0f, nullptr);
    lfo.setProperty("Phase",       0.0f, nullptr);
    lfo.setProperty("Bipolar",     false, nullptr);
    lfo.setProperty("SyncEnabled", false, nullptr);
    lfo.setProperty("Tempo",       120.0, nullptr);
    params.addChild(lfo, -1, nullptr);

    // Granular (off)
    juce::ValueTree granular("Granular");
    granular.setProperty("GrainSize",   20.0f, nullptr);
    granular.setProperty("Density",     3.0f, nullptr);
    granular.setProperty("Position",    0.5f, nullptr);
    granular.setProperty("Pitch",       12.0f, nullptr);
    granular.setProperty("Amplitude",   0.5f, nullptr);
    granular.setProperty("Pan",         0.0f, nullptr);
    granular.setProperty("WindowType",  "Hann", nullptr);
    granular.setProperty("PosModType",  "Off", nullptr);
    granular.setProperty("PosModDepth", 0.1f, nullptr);
    granular.setProperty("PosModRate",  1.0f, nullptr);
    params.addChild(granular, -1, nullptr);

    // Unison - single voice, clean
    juce::ValueTree unison("Unison");
    unison.setProperty("VoiceCount",   1, nullptr);
    unison.setProperty("Detune",       0.0f, nullptr);
    unison.setProperty("StereoSpread", 0.0f, nullptr);
    unison.setProperty("PhaseOffset",  0.0f, nullptr);
    params.addChild(unison, -1, nullptr);

    // Voice manager
    juce::ValueTree voice("VoiceManager");
    voice.setProperty("Attack",         0.001f, nullptr);
    voice.setProperty("Decay",          0.1f, nullptr);
    voice.setProperty("Sustain",        0.0f, nullptr);
    voice.setProperty("Release",        0.05f, nullptr);
    voice.setProperty("AllocationMode", "RoundRobin", nullptr);
    params.addChild(voice, -1, nullptr);

    // Modulation (empty)
    juce::ValueTree mod("Modulation");
    mod.setProperty("NumFilters", 1, nullptr);
    params.addChild(mod, -1, nullptr);

    return params;
}

juce::ValueTree PresetFactory::createFactoryFX()
{
    juce::ValueTree params("Parameters");

    // STFT (high res)
    juce::ValueTree stft("STFTConfig");
    stft.setProperty("FFTSize",      4096, nullptr);
    stft.setProperty("HopSize",      1024, nullptr);
    stft.setProperty("WindowType",   "Hann", nullptr);
    stft.setProperty("Threshold",    -50.0f, nullptr);
    stft.setProperty("MaxPartials",  768, nullptr);
    params.addChild(stft, 0, nullptr);

    // Filters - Comb + Formant, parallel routing
    juce::ValueTree filters("Filters");
    filters.setProperty("RoutingMode", "Parallel", nullptr);
    filters.setProperty("MasterGain",  0.8f, nullptr);

    // Slot 1: Comb filter
    juce::ValueTree slot1("Slot");
    slot1.setProperty("Type",          "Comb", nullptr);
    slot1.setProperty("Cutoff",        1000.0, nullptr);
    slot1.setProperty("Resonance",     0.8f, nullptr);
    slot1.setProperty("Drive",         0.6f, nullptr);
    slot1.setProperty("Mix",           0.7f, nullptr);
    slot1.setProperty("Bypassed",      false, nullptr);
    slot1.setProperty("CrossoverLow",  200.0, nullptr);
    slot1.setProperty("CrossoverHigh", 2000.0, nullptr);
    slot1.setProperty("MorphSource",   "LowPass", nullptr);
    slot1.setProperty("MorphTarget",   "HighPass", nullptr);
    slot1.setProperty("MorphAmount",   0.0f, nullptr);
    filters.addChild(slot1, 0, nullptr);

    // Slot 2: Formant filter
    juce::ValueTree slot2("Slot");
    slot2.setProperty("Type",          "Formant", nullptr);
    slot2.setProperty("Cutoff",        500.0, nullptr);
    slot2.setProperty("Resonance",     0.5f, nullptr);
    slot2.setProperty("Drive",         0.3f, nullptr);
    slot2.setProperty("Mix",           0.5f, nullptr);
    slot2.setProperty("Bypassed",      false, nullptr);
    slot2.setProperty("CrossoverLow",  200.0, nullptr);
    slot2.setProperty("CrossoverHigh", 2000.0, nullptr);
    slot2.setProperty("MorphSource",   "LowPass", nullptr);
    slot2.setProperty("MorphTarget",   "HighPass", nullptr);
    slot2.setProperty("MorphAmount",   0.0f, nullptr);
    filters.addChild(slot2, -1, nullptr);
    params.addChild(filters, -1, nullptr);

    // Envelope - rhythmic ping-pong loop
    juce::ValueTree envelope("Envelope");
    envelope.setProperty("LoopMode",     "PingPong", nullptr);
    envelope.setProperty("LoopStart",    0, nullptr);
    envelope.setProperty("LoopEnd",      3, nullptr);
    envelope.setProperty("Tempo",        140.0, nullptr);
    envelope.setProperty("BeatDivision", 0.25f, nullptr);
    envelope.setProperty("SyncEnabled",  true, nullptr);
    {
        auto bp1 = juce::ValueTree("Breakpoint");
        bp1.setProperty("Time", 0.0f, nullptr);
        bp1.setProperty("Value", 0.0f, nullptr);
        bp1.setProperty("Curve", "Linear", nullptr);
        envelope.addChild(bp1, -1, nullptr);

        auto bp2 = juce::ValueTree("Breakpoint");
        bp2.setProperty("Time", 0.25f, nullptr);
        bp2.setProperty("Value", 1.0f, nullptr);
        bp2.setProperty("Curve", "SCurve", nullptr);
        envelope.addChild(bp2, -1, nullptr);

        auto bp3 = juce::ValueTree("Breakpoint");
        bp3.setProperty("Time", 0.5f, nullptr);
        bp3.setProperty("Value", 0.3f, nullptr);
        bp3.setProperty("Curve", "SCurve", nullptr);
        envelope.addChild(bp3, -1, nullptr);

        auto bp4 = juce::ValueTree("Breakpoint");
        bp4.setProperty("Time", 0.75f, nullptr);
        bp4.setProperty("Value", 0.8f, nullptr);
        bp4.setProperty("Curve", "Exponential", nullptr);
        envelope.addChild(bp4, -1, nullptr);

        auto bp5 = juce::ValueTree("Breakpoint");
        bp5.setProperty("Time", 1.0f, nullptr);
        bp5.setProperty("Value", 0.0f, nullptr);
        bp5.setProperty("Curve", "Exponential", nullptr);
        envelope.addChild(bp5, -1, nullptr);
    }
    params.addChild(envelope, -1, nullptr);

    // LFO - random sample-and-hold
    juce::ValueTree lfo("LFO");
    lfo.setProperty("Waveform",    "Random", nullptr);
    lfo.setProperty("RateHz",      12.0f, nullptr);
    lfo.setProperty("RateBeats",   0.125f, nullptr);
    lfo.setProperty("Depth",       80.0f, nullptr);
    lfo.setProperty("Phase",       0.0f, nullptr);
    lfo.setProperty("Bipolar",     true, nullptr);
    lfo.setProperty("SyncEnabled", false, nullptr);
    lfo.setProperty("Tempo",       140.0, nullptr);
    params.addChild(lfo, -1, nullptr);

    // Granular - high density with position modulation
    juce::ValueTree granular("Granular");
    granular.setProperty("GrainSize",   25.0f, nullptr);
    granular.setProperty("Density",     50.0f, nullptr);
    granular.setProperty("Position",    0.5f, nullptr);
    granular.setProperty("Pitch",       7.0f, nullptr);
    granular.setProperty("Amplitude",   0.6f, nullptr);
    granular.setProperty("Pan",         0.3f, nullptr);
    granular.setProperty("WindowType",  "Sinc", nullptr);
    granular.setProperty("PosModType",  "Random", nullptr);
    granular.setProperty("PosModDepth", 0.5f, nullptr);
    granular.setProperty("PosModRate",  8.0f, nullptr);
    params.addChild(granular, -1, nullptr);

    // Unison - 7 voices, extreme spread
    juce::ValueTree unison("Unison");
    unison.setProperty("VoiceCount",   7, nullptr);
    unison.setProperty("Detune",       25.0f, nullptr);
    unison.setProperty("StereoSpread", 100.0f, nullptr);
    unison.setProperty("PhaseOffset",  1.0f, nullptr);
    params.addChild(unison, -1, nullptr);

    // Voice manager
    juce::ValueTree voice("VoiceManager");
    voice.setProperty("Attack",         0.02f, nullptr);
    voice.setProperty("Decay",          0.4f, nullptr);
    voice.setProperty("Sustain",        0.5f, nullptr);
    voice.setProperty("Release",        0.8f, nullptr);
    voice.setProperty("AllocationMode", "Random", nullptr);
    params.addChild(voice, -1, nullptr);

    // Modulation - LFO modulating resonance, modwheel modulating mix
    juce::ValueTree mod("Modulation");
    mod.setProperty("NumFilters", 2, nullptr);

    auto conn1 = juce::ValueTree("Connection");
    conn1.setProperty("Source",      "LFO1", nullptr);
    conn1.setProperty("Target",      "Resonance", nullptr);
    conn1.setProperty("FilterIndex", 0, nullptr);
    conn1.setProperty("Depth",       0.7f, nullptr);
    conn1.setProperty("Bipolar",     true, nullptr);
    conn1.setProperty("ID",          1, nullptr);
    mod.addChild(conn1, 0, nullptr);

    auto conn2 = juce::ValueTree("Connection");
    conn2.setProperty("Source",      "Modwheel", nullptr);
    conn2.setProperty("Target",      "Mix", nullptr);
    conn2.setProperty("FilterIndex", 1, nullptr);
    conn2.setProperty("Depth",       0.5f, nullptr);
    conn2.setProperty("Bipolar",     false, nullptr);
    conn2.setProperty("ID",          2, nullptr);
    mod.addChild(conn2, -1, nullptr);

    auto conn3 = juce::ValueTree("Connection");
    conn3.setProperty("Source",      "Envelope1", nullptr);
    conn3.setProperty("Target",      "Cutoff", nullptr);
    conn3.setProperty("FilterIndex", 0, nullptr);
    conn3.setProperty("Depth",       0.6f, nullptr);
    conn3.setProperty("Bipolar",     true, nullptr);
    conn3.setProperty("ID",          3, nullptr);
    mod.addChild(conn3, -1, nullptr);

    params.addChild(mod, -1, nullptr);

    return params;
}
} // namespace ana

