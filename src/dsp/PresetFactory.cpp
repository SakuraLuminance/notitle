#include "PresetFactory.h"
#include "effects/SpaceModule.h"

namespace ana {

//==============================================================================
// Internal helper - PresetParams struct + buildPreset
//==============================================================================
namespace {

struct FilterSlot {
    const char* type = "LowPass";
    double cutoff = 1000.0;
    float resonance = 0.0f, drive = 0.0f, mix = 1.0f;
    bool bypassed = false;
    double xoverLow = 200.0, xoverHigh = 2000.0;
    const char* morphSrc = "LowPass";
    const char* morphDst = "HighPass";
    float morphAmt = 0.0f;
};
struct EnvBp { float time, value; const char* curve; };

struct PresetParams {
    int fftSize = 2048, hopSize = 512, maxPartials = 512;
    const char* windowType = "Hann";
    float threshold = -60.0f;
    
    const char* routingMode = "Serial";
    float masterGain = 1.0f;
    int numSlots = 1;
    FilterSlot slot1, slot2;
    
    const char* loopMode = "None";
    int loopStart = 0, loopEnd = -1;
    double tempo = 120.0;
    float beatDivision = 1.0f;
    bool syncEnabled = false;
    int numBp = 4;
    EnvBp bp[6] = {{0,0,"Exponential"},{0.01f,1,"Linear"},{0.3f,0.8f,"Exponential"},{1.5f,0,"Exponential"},{0,0,"Linear"},{0,0,"Linear"}};
    
    const char* lfoWaveform = "Sine";
    float lfoRateHz = 4, lfoRateBeats = 1, lfoDepth = 30, lfoPhase = 0;
    bool lfoBipolar = false, lfoSync = false;
    
    float grainSize = 50, density = 5, position = 0.5f, pitch = 0;
    float amplitude = 0.3f, pan = 0, posModDepth = 0.1f, posModRate = 1;
    const char* grainWindow = "Hann", *posModType = "Off";
    
    int voiceCount = 1;
    float detune = 0, stereoSpread = 0, phaseOffset = 0;
    
    float attack = 0.01f, decay = 0.3f, sustain = 0.7f, release = 0.5f;
    const char* allocMode = "RoundRobin";
    int numFilters = 1;
};
static juce::ValueTree buildPreset(const PresetParams& p) {
    auto params = juce::ValueTree("Parameters");
    
    auto stft = juce::ValueTree("STFTConfig");
    stft.setProperty("FFTSize",p.fftSize,nullptr);
    stft.setProperty("HopSize",p.hopSize,nullptr);
    stft.setProperty("WindowType",p.windowType,nullptr);
    stft.setProperty("Threshold",p.threshold,nullptr);
    stft.setProperty("MaxPartials",p.maxPartials,nullptr);
    params.addChild(stft,0,nullptr);
    
    auto filters = juce::ValueTree("Filters");
    filters.setProperty("RoutingMode",p.routingMode,nullptr);
    filters.setProperty("MasterGain",p.masterGain,nullptr);
    const FilterSlot* slots[] = {&p.slot1,&p.slot2};
    for(int i=0;i<(p.numSlots>2?2:p.numSlots);++i){
        auto s=slots[i]; auto slot=juce::ValueTree("Slot");
        slot.setProperty("Type",s->type,nullptr);
        slot.setProperty("Cutoff",s->cutoff,nullptr);
        slot.setProperty("Resonance",s->resonance,nullptr);
        slot.setProperty("Drive",s->drive,nullptr);
        slot.setProperty("Mix",s->mix,nullptr);
        slot.setProperty("Bypassed",s->bypassed,nullptr);
        slot.setProperty("CrossoverLow",s->xoverLow,nullptr);
        slot.setProperty("CrossoverHigh",s->xoverHigh,nullptr);
        slot.setProperty("MorphSource",s->morphSrc,nullptr);
        slot.setProperty("MorphTarget",s->morphDst,nullptr);
        slot.setProperty("MorphAmount",s->morphAmt,nullptr);
        filters.addChild(slot,-1,nullptr);
    }
    params.addChild(filters,-1,nullptr);
    
    auto env = juce::ValueTree("Envelope");
    env.setProperty("LoopMode",p.loopMode,nullptr);
    env.setProperty("LoopStart",p.loopStart,nullptr);
    env.setProperty("LoopEnd",p.loopEnd,nullptr);
    env.setProperty("Tempo",p.tempo,nullptr);
    env.setProperty("BeatDivision",p.beatDivision,nullptr);
    env.setProperty("SyncEnabled",p.syncEnabled,nullptr);
    for(int i=0;i<p.numBp&&i<6;++i){
        auto bp=juce::ValueTree("Breakpoint");
        bp.setProperty("Time",p.bp[i].time,nullptr);
        bp.setProperty("Value",p.bp[i].value,nullptr);
        bp.setProperty("Curve",p.bp[i].curve,nullptr);
        env.addChild(bp,-1,nullptr);
    }
    params.addChild(env,-1,nullptr);
    
    auto lfo = juce::ValueTree("LFO");
    lfo.setProperty("Waveform",p.lfoWaveform,nullptr);
    lfo.setProperty("RateHz",p.lfoRateHz,nullptr);
    lfo.setProperty("RateBeats",p.lfoRateBeats,nullptr);
    lfo.setProperty("Depth",p.lfoDepth,nullptr);
    lfo.setProperty("Phase",p.lfoPhase,nullptr);
    lfo.setProperty("Bipolar",p.lfoBipolar,nullptr);
    lfo.setProperty("SyncEnabled",p.lfoSync,nullptr);
    lfo.setProperty("Tempo",p.tempo,nullptr);
    params.addChild(lfo,-1,nullptr);
    
    auto granular = juce::ValueTree("Granular");
    granular.setProperty("GrainSize",p.grainSize,nullptr);
    granular.setProperty("Density",p.density,nullptr);
    granular.setProperty("Position",p.position,nullptr);
    granular.setProperty("Pitch",p.pitch,nullptr);
    granular.setProperty("Amplitude",p.amplitude,nullptr);
    granular.setProperty("Pan",p.pan,nullptr);
    granular.setProperty("WindowType",p.grainWindow,nullptr);
    granular.setProperty("PosModType",p.posModType,nullptr);
    granular.setProperty("PosModDepth",p.posModDepth,nullptr);
    granular.setProperty("PosModRate",p.posModRate,nullptr);
    params.addChild(granular,-1,nullptr);
    
    auto unison = juce::ValueTree("Unison");
    unison.setProperty("VoiceCount",p.voiceCount,nullptr);
    unison.setProperty("Detune",p.detune,nullptr);
    unison.setProperty("StereoSpread",p.stereoSpread,nullptr);
    unison.setProperty("PhaseOffset",p.phaseOffset,nullptr);
    params.addChild(unison,-1,nullptr);
    
    auto voice = juce::ValueTree("VoiceManager");
    voice.setProperty("Attack",p.attack,nullptr);
    voice.setProperty("Decay",p.decay,nullptr);
    voice.setProperty("Sustain",p.sustain,nullptr);
    voice.setProperty("Release",p.release,nullptr);
    voice.setProperty("AllocationMode",p.allocMode,nullptr);
    params.addChild(voice,-1,nullptr);
    
    auto mod = juce::ValueTree("Modulation");
    mod.setProperty("NumFilters",p.numFilters,nullptr);
    params.addChild(mod,-1,nullptr);
    
    return params;
}

} // anonymous namespace
// Legacy factory methods
juce::ValueTree PresetFactory::createFactoryBass(){
    PresetParams p;
    p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;
    p.slot1.type="LowPass";p.slot1.cutoff=80;p.slot1.resonance=0.2f;p.slot1.drive=0.1f;
    p.attack=0.02f;p.decay=0.5f;p.sustain=0.9f;p.release=0.8f;
    p.lfoWaveform="Sine";p.lfoRateHz=4;p.lfoDepth=25;p.lfoBipolar=true;
    p.voiceCount=2;p.detune=10;p.stereoSpread=30;
    return buildPreset(p);
}
juce::ValueTree PresetFactory::createFactoryLead(){
    PresetParams p;
    p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;p.maxPartials=256;
    p.slot1.type="BandPass";p.slot1.cutoff=2000;p.slot1.resonance=0.6f;p.slot1.drive=0.3f;
    p.attack=0.005f;p.decay=0.2f;p.sustain=0.6f;p.release=0.2f;
    p.lfoWaveform="Triangle";p.lfoRateHz=6;p.lfoDepth=20;p.lfoBipolar=true;
    p.voiceCount=3;p.detune=8;p.stereoSpread=60;p.allocMode="OldestFirst";
    return buildPreset(p);
}
juce::ValueTree PresetFactory::createFactoryPad(){
    PresetParams p;
    p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=1024;
    p.slot1.type="LowPass";p.slot1.cutoff=800;p.slot1.resonance=0.2f;p.slot1.drive=0.1f;p.slot1.mix=0.9f;
    p.masterGain=0.85f;
    p.attack=0.5f;p.decay=0.5f;p.sustain=0.9f;p.release=2.0f;
    p.bp[0]={0,0,"Exponential"};p.bp[1]={0.5f,1,"SCurve"};p.bp[2]={1,0.9f,"Exponential"};p.bp[3]={3,0,"Exponential"};
    p.lfoWaveform="Sine";p.lfoRateHz=0.8f;p.lfoDepth=40;p.lfoBipolar=true;
    p.voiceCount=5;p.detune=15;p.stereoSpread=80;p.phaseOffset=0.7f;
    return buildPreset(p);
}
juce::ValueTree PresetFactory::createFactoryPluck(){
    PresetParams p;
    p.fftSize=2048;p.hopSize=512;p.threshold=-55;p.maxPartials=128;
    p.slot1.type="HighPass";p.slot1.cutoff=400;p.slot1.resonance=0.4f;p.slot1.drive=0.4f;
    p.attack=0.001f;p.decay=0.1f;p.sustain=0;p.release=0.05f;
    p.numBp=2;p.bp[0]={0,1,"Linear"};p.bp[1]={0.001f,0,"Exponential"};
    p.lfoRateHz=1;p.lfoDepth=5;return buildPreset(p);
}
juce::ValueTree PresetFactory::createFactoryFX(){
    PresetParams p;
    p.fftSize=4096;p.hopSize=1024;p.threshold=-50;p.maxPartials=768;
    p.routingMode="Parallel";p.masterGain=0.8f;p.numSlots=2;
    p.slot1.type="Comb";p.slot1.cutoff=1000;p.slot1.resonance=0.8f;p.slot1.drive=0.6f;p.slot1.mix=0.7f;
    p.slot2.type="Formant";p.slot2.cutoff=500;p.slot2.resonance=0.5f;p.slot2.drive=0.3f;p.slot2.mix=0.5f;
    p.loopMode="PingPong";p.tempo=140;p.syncEnabled=true;
    p.numBp=5;p.bp[0]={0,0,"Linear"};p.bp[1]={0.25f,1,"SCurve"};p.bp[2]={0.5f,0.3f,"SCurve"};p.bp[3]={0.75f,0.8f,"Exponential"};p.bp[4]={1,0,"Exponential"};
    p.lfoWaveform="Random";p.lfoRateHz=12;p.lfoDepth=80;p.lfoBipolar=true;
    p.voiceCount=7;p.detune=25;p.stereoSpread=100;p.phaseOffset=1;
    p.attack=0.02f;p.decay=0.4f;p.sustain=0.5f;p.release=0.8f;p.allocMode="Random";p.numFilters=2;
    return buildPreset(p);
}
juce::ValueTree PresetFactory::createFactoryExperimental(){
    PresetParams p;
    p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=1024;
    p.slot1.type="Morph";p.slot1.cutoff=800;p.slot1.resonance=0.5f;p.slot1.drive=0.3f;
    p.attack=0.3f;p.decay=0.6f;p.sustain=0.7f;p.release=1.5f;
    p.lfoWaveform="Saw";p.lfoRateHz=3;p.lfoDepth=60;p.lfoBipolar=true;
    p.voiceCount=4;p.detune=20;p.stereoSpread=70;p.phaseOffset=0.5f;p.allocMode="Random";
    return buildPreset(p);
}
//==============================================================================
// Bass presets (20)
//==============================================================================
std::vector<std::pair<juce::String,juce::ValueTree>> PresetFactory::createBassPresets(){
std::vector<std::pair<juce::String,juce::ValueTree>> r; PresetParams p;
//1 Deep Sub Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;
p.slot1.type="LowPass";p.slot1.cutoff=80;p.slot1.resonance=0.2f;p.slot1.drive=0.1f;
p.attack=0.02f;p.decay=0.5f;p.sustain=0.9f;p.release=0.8f;
p.lfoWaveform="Sine";p.lfoRateHz=3;p.lfoDepth=20;p.lfoBipolar=true;
p.voiceCount=2;p.detune=5;p.stereoSpread=20;
r.emplace_back("Deep Sub Bass",buildPreset(p));
//2 Reese Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;
p.slot1.type="LowPass";p.slot1.cutoff=250;p.slot1.resonance=0.5f;p.slot1.drive=0.3f;
p.attack=0.01f;p.decay=0.4f;p.sustain=0.8f;p.release=0.3f;
p.lfoWaveform="Saw";p.lfoRateHz=6;p.lfoDepth=55;p.lfoBipolar=true;
p.voiceCount=4;p.detune=18;p.stereoSpread=65;
r.emplace_back("Reese Bass",buildPreset(p));
//3 Acid Bass
p=PresetParams();p.slot1.type="BandPass";p.slot1.cutoff=600;p.slot1.resonance=0.85f;p.slot1.drive=0.5f;
p.attack=0.003f;p.decay=0.3f;p.sustain=0.5f;p.release=0.1f;
p.bp[0]={0,0,"Exponential"};p.bp[1]={0.003f,1,"Linear"};p.bp[2]={0.3f,0.5f,"Exponential"};p.bp[3]={0.8f,0,"Exponential"};
p.lfoWaveform="Triangle";p.lfoRateHz=8;p.lfoDepth=70;
p.pitch=-12;r.emplace_back("Acid Bass",buildPreset(p));
//4 Wobble Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;
p.slot1.type="LowPass";p.slot1.cutoff=300;p.slot1.resonance=0.4f;p.slot1.drive=0.3f;
p.attack=0.01f;p.decay=0.3f;p.sustain=0.8f;p.release=0.2f;
p.lfoWaveform="Square";p.lfoRateHz=5;p.lfoDepth=80;p.lfoBipolar=true;p.lfoSync=true;p.lfoRateBeats=0.25f;
p.voiceCount=3;p.detune=12;p.stereoSpread=40;r.emplace_back("Wobble Bass",buildPreset(p));
//5 Growl Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;
p.slot1.type="BandPass";p.slot1.cutoff=800;p.slot1.resonance=0.7f;p.slot1.drive=0.6f;
p.attack=0.005f;p.decay=0.2f;p.sustain=0.7f;p.release=0.4f;
p.lfoWaveform="Saw";p.lfoRateHz=10;p.lfoDepth=65;p.lfoBipolar=true;
p.voiceCount=2;p.detune=15;p.stereoSpread=35;r.emplace_back("Growl Bass",buildPreset(p));
//6 Pluck Bass
p=PresetParams();p.threshold=-55;p.maxPartials=128;
p.slot1.type="HighPass";p.slot1.cutoff=200;p.slot1.resonance=0.3f;p.slot1.drive=0.2f;
p.attack=0.002f;p.decay=0.15f;p.sustain=0.1f;p.release=0.1f;
p.numBp=2;p.bp[0]={0,1,"Linear"};p.bp[1]={0.002f,0.1f,"Exponential"};
p.lfoRateHz=2;p.lfoDepth=10;r.emplace_back("Pluck Bass",buildPreset(p));
//7 FM Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-60;p.maxPartials=384;
p.slot1.type="LowPass";p.slot1.cutoff=350;p.slot1.resonance=0.3f;p.slot1.drive=0.4f;
p.attack=0.003f;p.decay=0.2f;p.sustain=0.6f;p.release=0.3f;
p.lfoWaveform="Sine";p.lfoRateHz=8;p.lfoDepth=40;p.lfoBipolar=true;
p.voiceCount=2;p.detune=8;p.stereoSpread=25;r.emplace_back("FM Bass",buildPreset(p));
//8 Sine Sub
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=512;
p.slot1.type="LowPass";p.slot1.cutoff=60;p.slot1.resonance=0.1f;p.slot1.drive=0;
p.attack=0.05f;p.decay=0.6f;p.sustain=0.95f;p.release=1.0f;
p.bp[0]={0,0,"Exponential"};p.bp[1]={0.05f,1,"SCurve"};p.bp[2]={0.6f,0.95f,"Exponential"};p.bp[3]={2.5f,0,"Exponential"};
p.lfoWaveform="Sine";p.lfoRateHz=2;p.lfoDepth=10;r.emplace_back("Sine Sub",buildPreset(p));
//9 Distorted Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-55;
p.slot1.type="LowPass";p.slot1.cutoff=400;p.slot1.resonance=0.4f;p.slot1.drive=0.8f;
p.attack=0.01f;p.decay=0.3f;p.sustain=0.8f;p.release=0.5f;
p.lfoWaveform="Square";p.lfoRateHz=4;p.lfoDepth=35;p.lfoBipolar=true;
p.voiceCount=3;p.detune=10;p.stereoSpread=40;r.emplace_back("Distorted Bass",buildPreset(p));
//10 Modul Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;
p.slot1.type="LowPass";p.slot1.cutoff=500;p.slot1.resonance=0.5f;p.slot1.drive=0.3f;
p.attack=0.01f;p.decay=0.3f;p.sustain=0.7f;p.release=0.4f;
p.lfoWaveform="Random";p.lfoRateHz=8;p.lfoDepth=75;p.lfoBipolar=true;
p.voiceCount=3;p.detune=15;p.stereoSpread=50;r.emplace_back("Modul Bass",buildPreset(p));
//11 Reese 2
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="Hamming";p.threshold=-65;
p.slot1.type="LowPass";p.slot1.cutoff=180;p.slot1.resonance=0.45f;p.slot1.drive=0.25f;
p.attack=0.015f;p.decay=0.5f;p.sustain=0.85f;p.release=0.4f;
p.lfoWaveform="Triangle";p.lfoRateHz=5;p.lfoDepth=50;p.lfoBipolar=true;
p.voiceCount=5;p.detune=22;p.stereoSpread=70;r.emplace_back("Reese 2",buildPreset(p));
//12 Psycho Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-60;
p.slot1.type="BandPass";p.slot1.cutoff=400;p.slot1.resonance=0.75f;p.slot1.drive=0.5f;
p.attack=0.005f;p.decay=0.25f;p.sustain=0.7f;p.release=0.35f;
p.lfoWaveform="Saw";p.lfoRateHz=7;p.lfoDepth=60;p.lfoBipolar=true;
p.voiceCount=3;p.detune=20;p.stereoSpread=55;r.emplace_back("Psycho Bass",buildPreset(p));
//13 Future Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;
p.slot1.type="LowPass";p.slot1.cutoff=350;p.slot1.resonance=0.25f;p.slot1.drive=0.2f;
p.attack=0.02f;p.decay=0.4f;p.sustain=0.85f;p.release=0.6f;
p.lfoWaveform="Sine";p.lfoRateHz=5;p.lfoDepth=30;p.lfoBipolar=true;
p.voiceCount=4;p.detune=12;p.stereoSpread=50;r.emplace_back("Future Bass",buildPreset(p));
//14 Neuro Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-55;
p.slot1.type="BandPass";p.slot1.cutoff=700;p.slot1.resonance=0.65f;p.slot1.drive=0.55f;
p.attack=0.008f;p.decay=0.3f;p.sustain=0.65f;p.release=0.3f;
p.lfoWaveform="Random";p.lfoRateHz=12;p.lfoDepth=70;p.lfoBipolar=true;
p.voiceCount=4;p.detune=18;p.stereoSpread=60;r.emplace_back("Neuro Bass",buildPreset(p));
//15 Tech Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="Hamming";p.threshold=-65;p.maxPartials=384;
p.slot1.type="LowPass";p.slot1.cutoff=250;p.slot1.resonance=0.35f;p.slot1.drive=0.25f;
p.attack=0.01f;p.decay=0.35f;p.sustain=0.75f;p.release=0.3f;
p.bp[0]={0,0,"Linear"};p.bp[1]={0.01f,1,"Linear"};p.bp[2]={0.35f,0.75f,"Linear"};p.bp[3]={1,0,"Exponential"};
p.lfoWaveform="Square";p.lfoRateHz=4;p.lfoDepth=25;p.lfoBipolar=true;
p.voiceCount=2;p.detune=8;p.stereoSpread=30;r.emplace_back("Tech Bass",buildPreset(p));
//16 House Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;
p.slot1.type="LowPass";p.slot1.cutoff=150;p.slot1.resonance=0.2f;p.slot1.drive=0.15f;
p.attack=0.03f;p.decay=0.5f;p.sustain=0.9f;p.release=0.6f;
p.lfoWaveform="Sine";p.lfoRateHz=2;p.lfoDepth=15;
p.voiceCount=2;p.detune=5;p.stereoSpread=20;r.emplace_back("House Bass",buildPreset(p));
//17 Trap Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="Hamming";p.threshold=-65;p.maxPartials=384;
p.slot1.type="LowPass";p.slot1.cutoff=55;p.slot1.resonance=0.15f;p.slot1.drive=0.05f;
p.attack=0.005f;p.decay=0.8f;p.sustain=0.95f;p.release=1.5f;
p.bp[0]={0,0,"Exponential"};p.bp[1]={0.005f,1,"Linear"};p.bp[2]={0.8f,0.95f,"Exponential"};p.bp[3]={3,0,"Exponential"};
p.lfoWaveform="Sine";p.lfoRateHz=1;p.lfoDepth=8;
p.voiceCount=1;p.detune=3;p.stereoSpread=10;r.emplace_back("Trap Bass",buildPreset(p));
//18 Dubstep Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-60;
p.slot1.type="LowPass";p.slot1.cutoff=350;p.slot1.resonance=0.5f;p.slot1.drive=0.6f;
p.attack=0.005f;p.decay=0.2f;p.sustain=0.7f;p.release=0.3f;
p.lfoWaveform="Square";p.lfoRateHz=4;p.lfoDepth=85;p.lfoBipolar=true;p.lfoSync=true;p.lfoRateBeats=0.25f;
p.voiceCount=4;p.detune=20;p.stereoSpread=50;r.emplace_back("Dubstep Bass",buildPreset(p));
//19 DnB Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;
p.slot1.type="HighPass";p.slot1.cutoff=120;p.slot1.resonance=0.3f;p.slot1.drive=0.2f;
p.attack=0.005f;p.decay=0.2f;p.sustain=0.8f;p.release=0.2f;
p.lfoWaveform="Triangle";p.lfoRateHz=8;p.lfoDepth=45;p.lfoBipolar=true;
p.voiceCount=3;p.detune=12;p.stereoSpread=45;r.emplace_back("DnB Bass",buildPreset(p));
//20 Ambient Sub
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-75;p.maxPartials=512;
p.slot1.type="LowPass";p.slot1.cutoff=70;p.slot1.resonance=0.1f;p.slot1.drive=0;
p.attack=0.5f;p.decay=1.0f;p.sustain=0.9f;p.release=3.0f;
p.bp[0]={0,0,"Exponential"};p.bp[1]={0.5f,1,"SCurve"};p.bp[2]={1.5f,0.9f,"Exponential"};p.bp[3]={4,0,"Exponential"};
p.lfoWaveform="Sine";p.lfoRateHz=0.5f;p.lfoDepth=15;
p.voiceCount=2;p.detune=3;p.stereoSpread=15;r.emplace_back("Ambient Sub",buildPreset(p));
return r;}
//==============================================================================
// Lead presets (20)
//==============================================================================
std::vector<std::pair<juce::String,juce::ValueTree>> PresetFactory::createLeadPresets(){
std::vector<std::pair<juce::String,juce::ValueTree>> r; PresetParams p;
//1 Saw Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;p.maxPartials=256;
p.slot1.type="LowPass";p.slot1.cutoff=2000;p.slot1.resonance=0.3f;p.slot1.drive=0.2f;
p.attack=0.005f;p.decay=0.2f;p.sustain=0.7f;p.release=0.2f;
p.lfoWaveform="Triangle";p.lfoRateHz=5;p.lfoDepth=15;p.lfoBipolar=true;
p.voiceCount=3;p.detune=8;p.stereoSpread=50;p.allocMode="OldestFirst";
r.emplace_back("Saw Lead",buildPreset(p));
//2 Square Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="Hamming";p.threshold=-60;p.maxPartials=256;
p.slot1.type="BandPass";p.slot1.cutoff=1500;p.slot1.resonance=0.5f;p.slot1.drive=0.3f;
p.attack=0.005f;p.decay=0.15f;p.sustain=0.6f;p.release=0.15f;
p.lfoWaveform="Square";p.lfoRateHz=4;p.lfoDepth=20;p.lfoBipolar=true;
p.voiceCount=2;p.detune=5;p.stereoSpread=30;p.allocMode="OldestFirst";
r.emplace_back("Square Lead",buildPreset(p));
//3 Supersaw Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;p.maxPartials=384;
p.slot1.type="LowPass";p.slot1.cutoff=2500;p.slot1.resonance=0.2f;p.slot1.drive=0.15f;
p.attack=0.005f;p.decay=0.3f;p.sustain=0.8f;p.release=0.3f;
p.lfoWaveform="Triangle";p.lfoRateHz=6;p.lfoDepth=25;p.lfoBipolar=true;
p.voiceCount=7;p.detune=20;p.stereoSpread=90;p.phaseOffset=0.8f;p.allocMode="OldestFirst";
r.emplace_back("Supersaw Lead",buildPreset(p));
//4 Vocal Lead
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=512;
p.slot1.type="BandPass";p.slot1.cutoff=1800;p.slot1.resonance=0.6f;p.slot1.drive=0.2f;
p.attack=0.02f;p.decay=0.25f;p.sustain=0.75f;p.release=0.4f;
p.lfoWaveform="Sine";p.lfoRateHz=3;p.lfoDepth=20;p.lfoBipolar=true;
p.voiceCount=2;p.detune=5;p.stereoSpread=35;p.allocMode="OldestFirst";
r.emplace_back("Vocal Lead",buildPreset(p));
//5 Brass Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-60;p.maxPartials=256;
p.slot1.type="BandPass";p.slot1.cutoff=1200;p.slot1.resonance=0.5f;p.slot1.drive=0.4f;
p.attack=0.02f;p.decay=0.3f;p.sustain=0.8f;p.release=0.3f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={0.02f,1,"SCurve"};
p.lfoWaveform="Triangle";p.lfoRateHz=4;p.lfoDepth=10;
p.voiceCount=2;p.detune=5;p.stereoSpread=25;p.allocMode="OldestFirst";
r.emplace_back("Brass Lead",buildPreset(p));
//6 Synth Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;p.maxPartials=256;
p.slot1.type="LowPass";p.slot1.cutoff=2200;p.slot1.resonance=0.3f;p.slot1.drive=0.2f;
p.attack=0.003f;p.decay=0.2f;p.sustain=0.65f;p.release=0.15f;
p.lfoWaveform="Sine";p.lfoRateHz=5;p.lfoDepth=18;p.lfoBipolar=true;
p.voiceCount=3;p.detune=10;p.stereoSpread=55;p.allocMode="OldestFirst";
r.emplace_back("Synth Lead",buildPreset(p));
//7 Acid Lead
p=PresetParams();p.slot1.type="BandPass";p.slot1.cutoff=1200;p.slot1.resonance=0.8f;p.slot1.drive=0.45f;
p.attack=0.003f;p.decay=0.2f;p.sustain=0.4f;p.release=0.1f;
p.bp[0]={0,0,"Exponential"};p.bp[1]={0.003f,1,"Linear"};p.bp[2]={0.2f,0.4f,"Exponential"};p.bp[3]={0.5f,0,"Exponential"};
p.lfoWaveform="Triangle";p.lfoRateHz=9;p.lfoDepth=60;
p.allocMode="OldestFirst";r.emplace_back("Acid Lead",buildPreset(p));
//8 Hard Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-55;p.maxPartials=256;
p.slot1.type="LowPass";p.slot1.cutoff=1800;p.slot1.resonance=0.5f;p.slot1.drive=0.6f;
p.attack=0.003f;p.decay=0.15f;p.sustain=0.7f;p.release=0.2f;
p.lfoWaveform="Square";p.lfoRateHz=4;p.lfoDepth=25;p.lfoBipolar=true;
p.voiceCount=4;p.detune=15;p.stereoSpread=60;p.allocMode="OldestFirst";
r.emplace_back("Hard Lead",buildPreset(p));
//9 Soft Lead
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=512;
p.slot1.type="LowPass";p.slot1.cutoff=1600;p.slot1.resonance=0.15f;p.slot1.drive=0.1f;
p.attack=0.03f;p.decay=0.35f;p.sustain=0.8f;p.release=0.5f;
p.lfoWaveform="Sine";p.lfoRateHz=3;p.lfoDepth=12;
p.voiceCount=2;p.detune=4;p.stereoSpread=30;p.allocMode="OldestFirst";
r.emplace_back("Soft Lead",buildPreset(p));
//10 Wide Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;p.maxPartials=384;
p.slot1.type="LowPass";p.slot1.cutoff=2000;p.slot1.resonance=0.25f;p.slot1.drive=0.2f;
p.attack=0.005f;p.decay=0.2f;p.sustain=0.7f;p.release=0.25f;
p.lfoWaveform="Sine";p.lfoRateHz=4;p.lfoDepth=20;p.lfoBipolar=true;
p.voiceCount=5;p.detune=15;p.stereoSpread=90;p.phaseOffset=0.6f;p.allocMode="OldestFirst";
r.emplace_back("Wide Lead",buildPreset(p));
//11 Mono Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;p.maxPartials=256;
p.slot1.type="BandPass";p.slot1.cutoff=1500;p.slot1.resonance=0.55f;p.slot1.drive=0.3f;
p.attack=0.003f;p.decay=0.15f;p.sustain=0.6f;p.release=0.1f;
p.lfoWaveform="Triangle";p.lfoRateHz=6;p.lfoDepth=20;p.lfoBipolar=true;
p.voiceCount=1;p.allocMode="OldestFirst";r.emplace_back("Mono Lead",buildPreset(p));
//12 Poly Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="Hamming";p.threshold=-65;p.maxPartials=256;
p.slot1.type="LowPass";p.slot1.cutoff=2400;p.slot1.resonance=0.2f;p.slot1.drive=0.1f;
p.attack=0.01f;p.decay=0.3f;p.sustain=0.75f;p.release=0.4f;
p.lfoWaveform="Sine";p.lfoRateHz=4;p.lfoDepth=15;
p.voiceCount=3;p.detune=8;p.stereoSpread=45;r.emplace_back("Poly Lead",buildPreset(p));
//13 Retro Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="Hamming";p.threshold=-60;p.maxPartials=256;
p.slot1.type="BandPass";p.slot1.cutoff=1000;p.slot1.resonance=0.4f;p.slot1.drive=0.25f;
p.attack=0.005f;p.decay=0.2f;p.sustain=0.65f;p.release=0.2f;
p.lfoWaveform="Triangle";p.lfoRateHz=5;p.lfoDepth=15;
p.voiceCount=2;p.detune=5;p.stereoSpread=25;p.allocMode="OldestFirst";
r.emplace_back("Retro Lead",buildPreset(p));
//14 Modern Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;p.maxPartials=384;
p.slot1.type="LowPass";p.slot1.cutoff=2800;p.slot1.resonance=0.3f;p.slot1.drive=0.25f;
p.attack=0.003f;p.decay=0.2f;p.sustain=0.7f;p.release=0.2f;
p.lfoWaveform="Triangle";p.lfoRateHz=5;p.lfoDepth=25;p.lfoBipolar=true;
p.voiceCount=4;p.detune=12;p.stereoSpread=65;p.allocMode="OldestFirst";
r.emplace_back("Modern Lead",buildPreset(p));
//15 Dark Lead
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=384;
p.slot1.type="LowPass";p.slot1.cutoff=600;p.slot1.resonance=0.4f;p.slot1.drive=0.15f;
p.attack=0.02f;p.decay=0.3f;p.sustain=0.7f;p.release=0.5f;
p.lfoWaveform="Sine";p.lfoRateHz=3;p.lfoDepth=25;p.lfoBipolar=true;
p.voiceCount=3;p.detune=10;p.stereoSpread=40;p.allocMode="OldestFirst";
r.emplace_back("Dark Lead",buildPreset(p));
//16 Bright Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;p.maxPartials=256;
p.slot1.type="HighPass";p.slot1.cutoff=800;p.slot1.resonance=0.25f;p.slot1.drive=0.15f;
p.attack=0.003f;p.decay=0.25f;p.sustain=0.75f;p.release=0.2f;
p.lfoWaveform="Sine";p.lfoRateHz=6;p.lfoDepth=20;p.lfoBipolar=true;
p.voiceCount=3;p.detune=8;p.stereoSpread=50;p.allocMode="OldestFirst";
r.emplace_back("Bright Lead",buildPreset(p));
//17 Pad Lead
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=512;
p.slot1.type="LowPass";p.slot1.cutoff=1200;p.slot1.resonance=0.2f;p.slot1.drive=0.1f;p.masterGain=0.85f;
p.attack=0.2f;p.decay=0.5f;p.sustain=0.85f;p.release=1.0f;
p.bp[0]={0,0,"Exponential"};p.bp[1]={0.2f,1,"SCurve"};p.bp[2]={0.7f,0.85f,"Exponential"};p.bp[3]={1.5f,0,"Exponential"};
p.lfoWaveform="Sine";p.lfoRateHz=2;p.lfoDepth=30;p.lfoBipolar=true;
p.voiceCount=4;p.detune=12;p.stereoSpread=55;r.emplace_back("Pad Lead",buildPreset(p));
//18 Pluck Lead
p=PresetParams();p.threshold=-55;p.maxPartials=128;
p.slot1.type="HighPass";p.slot1.cutoff=500;p.slot1.resonance=0.3f;p.slot1.drive=0.2f;
p.attack=0.001f;p.decay=0.12f;p.sustain=0;p.release=0.05f;
p.numBp=2;p.bp[0]={0,1,"Linear"};p.bp[1]={0.001f,0,"Exponential"};
p.lfoRateHz=2;p.lfoDepth=5;p.allocMode="OldestFirst";r.emplace_back("Pluck Lead",buildPreset(p));
//19 FM Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-60;p.maxPartials=384;
p.slot1.type="BandPass";p.slot1.cutoff=1800;p.slot1.resonance=0.35f;p.slot1.drive=0.3f;
p.attack=0.005f;p.decay=0.2f;p.sustain=0.6f;p.release=0.2f;
p.lfoWaveform="Sine";p.lfoRateHz=8;p.lfoDepth=35;p.lfoBipolar=true;
p.voiceCount=2;p.detune=5;p.stereoSpread=30;p.allocMode="OldestFirst";
r.emplace_back("FM Lead",buildPreset(p));
//20 Noise Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="Hamming";p.threshold=-50;p.maxPartials=256;
p.slot1.type="BandPass";p.slot1.cutoff=2000;p.slot1.resonance=0.6f;p.slot1.drive=0.5f;
p.attack=0.005f;p.decay=0.2f;p.sustain=0.5f;p.release=0.15f;
p.lfoWaveform="Random";p.lfoRateHz=10;p.lfoDepth=50;p.lfoBipolar=true;
p.voiceCount=3;p.detune=12;p.stereoSpread=50;p.allocMode="OldestFirst";
r.emplace_back("Noise Lead",buildPreset(p));
return r;}
//==============================================================================
// Pad presets (20)
//==============================================================================
std::vector<std::pair<juce::String,juce::ValueTree>> PresetFactory::createPadPresets(){
std::vector<std::pair<juce::String,juce::ValueTree>> r; PresetParams p;
//1 Ambient Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=1024;
p.slot1.type="LowPass";p.slot1.cutoff=800;p.slot1.resonance=0.2f;p.slot1.drive=0.1f;p.slot1.mix=0.9f;p.masterGain=0.85f;
p.attack=0.5f;p.decay=0.5f;p.sustain=0.9f;p.release=2.0f;
p.bp[0]={0,0,"Exponential"};p.bp[1]={0.5f,1,"SCurve"};p.bp[2]={1,0.9f,"Exponential"};p.bp[3]={3,0,"Exponential"};
p.lfoWaveform="Sine";p.lfoRateHz=0.8f;p.lfoDepth=40;p.lfoBipolar=true;
p.voiceCount=5;p.detune=15;p.stereoSpread=80;p.phaseOffset=0.7f;
r.emplace_back("Ambient Pad",buildPreset(p));
//2 Strings Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-72;p.maxPartials=1024;
p.slot1.type="LowPass";p.slot1.cutoff=1200;p.slot1.resonance=0.25f;p.slot1.drive=0.15f;p.masterGain=0.8f;
p.attack=0.3f;p.decay=0.6f;p.sustain=0.85f;p.release=2.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={0.3f,1,"SCurve"};p.bp[2]={0.9f,0.85f,"Exponential"};p.bp[3]={3.5f,0,"Exponential"};
p.lfoWaveform="Triangle";p.lfoRateHz=1.2f;p.lfoDepth=25;p.lfoBipolar=true;
p.voiceCount=4;p.detune=12;p.stereoSpread=60;p.phaseOffset=0.5f;
r.emplace_back("Strings Pad",buildPreset(p));
//3 Choir Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=1024;
p.slot1.type="BandPass";p.slot1.cutoff=1500;p.slot1.resonance=0.4f;p.slot1.drive=0.1f;p.masterGain=0.8f;
p.attack=0.4f;p.decay=0.5f;p.sustain=0.9f;p.release=2.0f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={0.4f,1,"SCurve"};p.bp[2]={0.9f,0.9f,"Exponential"};p.bp[3]={3,0,"Exponential"};
p.lfoWaveform="Sine";p.lfoRateHz=1.5f;p.lfoDepth=20;p.lfoBipolar=true;
p.voiceCount=3;p.detune=8;p.stereoSpread=50;r.emplace_back("Choir Pad",buildPreset(p));
//4 Sweep Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-68;p.maxPartials=1024;
p.slot1.type="LowPass";p.slot1.cutoff=2000;p.slot1.resonance=0.2f;p.slot1.drive=0.15f;p.masterGain=0.9f;
p.attack=1.0f;p.decay=0.5f;p.sustain=0.9f;p.release=2.0f;
p.bp[0]={0,0,"Exponential"};p.bp[1]={1,1,"SCurve"};p.bp[2]={1.5f,0.9f,"Exponential"};p.bp[3]={3,0,"Exponential"};
p.lfoWaveform="Triangle";p.lfoRateHz=2;p.lfoDepth=50;p.lfoBipolar=true;
p.voiceCount=5;p.detune=15;p.stereoSpread=75;r.emplace_back("Sweep Pad",buildPreset(p));
//5 Dark Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-72;p.maxPartials=768;
p.slot1.type="LowPass";p.slot1.cutoff=400;p.slot1.resonance=0.35f;p.slot1.drive=0.15f;p.masterGain=0.75f;
p.attack=0.2f;p.decay=0.6f;p.sustain=0.85f;p.release=3.0f;
p.lfoWaveform="Sine";p.lfoRateHz=0.6f;p.lfoDepth=35;p.lfoBipolar=true;
p.voiceCount=4;p.detune=12;p.stereoSpread=55;r.emplace_back("Dark Pad",buildPreset(p));
//6 Bright Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=1024;
p.slot1.type="HighPass";p.slot1.cutoff=500;p.slot1.resonance=0.2f;p.slot1.drive=0.1f;p.masterGain=0.9f;
p.attack=0.3f;p.decay=0.4f;p.sustain=0.85f;p.release=1.5f;
p.lfoWaveform="Triangle";p.lfoRateHz=2;p.lfoDepth=20;p.lfoBipolar=true;
p.voiceCount=5;p.detune=10;p.stereoSpread=70;r.emplace_back("Bright Pad",buildPreset(p));
//7 Warm Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=1024;
p.slot1.type="LowPass";p.slot1.cutoff=600;p.slot1.resonance=0.2f;p.slot1.drive=0.08f;p.masterGain=0.85f;
p.attack=0.4f;p.decay=0.5f;p.sustain=0.9f;p.release=2.0f;
p.lfoWaveform="Sine";p.lfoRateHz=1;p.lfoDepth=20;
p.voiceCount=3;p.detune=8;p.stereoSpread=40;r.emplace_back("Warm Pad",buildPreset(p));
//8 Cold Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-72;p.maxPartials=768;
p.slot1.type="HighPass";p.slot1.cutoff=400;p.slot1.resonance=0.3f;p.slot1.drive=0.12f;p.masterGain=0.8f;
p.attack=0.2f;p.decay=0.5f;p.sustain=0.8f;p.release=2.5f;
p.lfoWaveform="Saw";p.lfoRateHz=1.5f;p.lfoDepth=30;p.lfoBipolar=true;
p.voiceCount=4;p.detune=12;p.stereoSpread=60;r.emplace_back("Cold Pad",buildPreset(p));
//9 Evolving Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-72;p.maxPartials=1024;
p.slot1.type="BandPass";p.slot1.cutoff=1000;p.slot1.resonance=0.3f;p.slot1.drive=0.15f;p.masterGain=0.8f;
p.attack=0.5f;p.decay=0.5f;p.sustain=0.9f;p.release=3.5f;
p.bp[1]={0.5f,1,"SCurve"};
p.lfoWaveform="Random";p.lfoRateHz=0.4f;p.lfoDepth=55;p.lfoBipolar=true;
p.voiceCount=5;p.detune=18;p.stereoSpread=80;r.emplace_back("Evolving Pad",buildPreset(p));
//10 Motion Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=1024;
p.slot1.type="LowPass";p.slot1.cutoff=1000;p.slot1.resonance=0.25f;p.slot1.drive=0.12f;p.masterGain=0.85f;
p.attack=0.3f;p.decay=0.5f;p.sustain=0.85f;p.release=2.0f;
p.lfoWaveform="Triangle";p.lfoRateHz=3;p.lfoDepth=45;p.lfoBipolar=true;
p.voiceCount=5;p.detune=15;p.stereoSpread=70;r.emplace_back("Motion Pad",buildPreset(p));
//11 Synth Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=1024;
p.slot1.type="LowPass";p.slot1.cutoff=1500;p.slot1.resonance=0.2f;p.slot1.drive=0.1f;p.masterGain=0.9f;
p.attack=0.2f;p.decay=0.4f;p.sustain=0.85f;p.release=1.5f;
p.lfoWaveform="Sine";p.lfoRateHz=2;p.lfoDepth=25;p.lfoBipolar=true;
p.voiceCount=4;p.detune=10;p.stereoSpread=50;r.emplace_back("Synth Pad",buildPreset(p));
//12 Soft Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-72;p.maxPartials=1024;
p.slot1.type="LowPass";p.slot1.cutoff=500;p.slot1.resonance=0.15f;p.slot1.drive=0.05f;p.masterGain=0.75f;
p.attack=0.6f;p.decay=0.4f;p.sustain=0.9f;p.release=2.5f;
p.bp[1]={0.6f,1,"SCurve"};
p.lfoWaveform="Sine";p.lfoRateHz=0.8f;p.lfoDepth=15;
p.voiceCount=3;p.detune=6;p.stereoSpread=35;r.emplace_back("Soft Pad",buildPreset(p));
//13 Wide Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=1024;
p.slot1.type="LowPass";p.slot1.cutoff=1200;p.slot1.resonance=0.2f;p.slot1.drive=0.1f;p.masterGain=0.85f;
p.attack=0.3f;p.decay=0.5f;p.sustain=0.85f;p.release=2.0f;
p.lfoWaveform="Triangle";p.lfoRateHz=1.5f;p.lfoDepth=30;p.lfoBipolar=true;
p.voiceCount=7;p.detune=18;p.stereoSpread=100;p.phaseOffset=0.9f;
r.emplace_back("Wide Pad",buildPreset(p));
//14 Atmos Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-74;p.maxPartials=1024;
p.slot1.type="BandPass";p.slot1.cutoff=700;p.slot1.resonance=0.3f;p.slot1.drive=0.08f;p.masterGain=0.7f;
p.attack=0.8f;p.decay=0.5f;p.sustain=0.9f;p.release=4.0f;
p.bp[1]={0.8f,1,"SCurve"};p.bp[3]={4,0,"Exponential"};
p.lfoWaveform="Random";p.lfoRateHz=0.3f;p.lfoDepth=50;p.lfoBipolar=true;
p.voiceCount=5;p.detune=15;p.stereoSpread=80;r.emplace_back("Atmos Pad",buildPreset(p));
//15 Dream Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-72;p.maxPartials=1024;
p.slot1.type="LowPass";p.slot1.cutoff=700;p.slot1.resonance=0.15f;p.slot1.drive=0.08f;p.masterGain=0.8f;
p.attack=0.5f;p.decay=0.5f;p.sustain=0.9f;p.release=3.0f;
p.bp[1]={0.5f,1,"SCurve"};
p.lfoWaveform="Sine";p.lfoRateHz=0.6f;p.lfoDepth=35;p.lfoBipolar=true;
p.voiceCount=4;p.detune=12;p.stereoSpread=60;r.emplace_back("Dream Pad",buildPreset(p));
//16 Epic Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=1024;
p.slot1.type="LowPass";p.slot1.cutoff=1500;p.slot1.resonance=0.25f;p.slot1.drive=0.15f;p.masterGain=0.9f;
p.attack=0.4f;p.decay=0.6f;p.sustain=0.9f;p.release=3.0f;
p.bp[1]={0.4f,1,"SCurve"};
p.lfoWaveform="Triangle";p.lfoRateHz=1.8f;p.lfoDepth=40;p.lfoBipolar=true;
p.voiceCount=6;p.detune=16;p.stereoSpread=85;r.emplace_back("Epic Pad",buildPreset(p));
//17 Minimal Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-74;p.maxPartials=512;
p.slot1.type="LowPass";p.slot1.cutoff=400;p.slot1.resonance=0.15f;p.slot1.drive=0.05f;p.masterGain=0.7f;
p.attack=0.5f;p.decay=0.4f;p.sustain=0.85f;p.release=2.0f;
p.bp[1]={0.5f,1,"SCurve"};
p.lfoWaveform="Sine";p.lfoRateHz=1;p.lfoDepth=15;
p.voiceCount=2;p.detune=4;p.stereoSpread=25;r.emplace_back("Minimal Pad",buildPreset(p));
//18 Texture Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=1024;
p.slot1.type="BandPass";p.slot1.cutoff=900;p.slot1.resonance=0.35f;p.slot1.drive=0.15f;p.masterGain=0.75f;
p.attack=0.3f;p.decay=0.5f;p.sustain=0.8f;p.release=3.0f;
p.lfoWaveform="Random";p.lfoRateHz=5;p.lfoDepth=60;p.lfoBipolar=true;
p.voiceCount=4;p.detune=14;p.stereoSpread=65;r.emplace_back("Texture Pad",buildPreset(p));
//19 Bass Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=768;
p.slot1.type="LowPass";p.slot1.cutoff=300;p.slot1.resonance=0.2f;p.slot1.drive=0.1f;p.masterGain=0.8f;
p.attack=0.3f;p.decay=0.6f;p.sustain=0.9f;p.release=2.5f;
p.lfoWaveform="Sine";p.lfoRateHz=1.5f;p.lfoDepth=25;p.lfoBipolar=true;
p.voiceCount=3;p.detune=8;p.stereoSpread=40;r.emplace_back("Bass Pad",buildPreset(p));
//20 FX Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-68;p.maxPartials=1024;
p.routingMode="Parallel";p.masterGain=0.8f;p.numSlots=2;
p.slot1.type="BandPass";p.slot1.cutoff=1200;p.slot1.resonance=0.4f;p.slot1.drive=0.2f;
p.slot2.type="Formant";p.slot2.cutoff=400;p.slot2.resonance=0.3f;p.slot2.drive=0.15f;p.slot2.mix=0.5f;
p.attack=0.5f;p.decay=0.5f;p.sustain=0.85f;p.release=2.5f;
p.bp[1]={0.5f,1,"SCurve"};
p.lfoWaveform="Random";p.lfoRateHz=3;p.lfoDepth=55;p.lfoBipolar=true;
p.voiceCount=5;p.detune=16;p.stereoSpread=75;r.emplace_back("FX Pad",buildPreset(p));
return r;}
//==============================================================================
// FX presets (20)
//==============================================================================
std::vector<std::pair<juce::String,juce::ValueTree>> PresetFactory::createFXPresets(){
std::vector<std::pair<juce::String,juce::ValueTree>> r; PresetParams p;
//1 Riser
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-55;p.maxPartials=768;
p.slot1.type="BandPass";p.slot1.cutoff=500;p.slot1.resonance=0.5f;p.slot1.drive=0.4f;p.masterGain=0.85f;
p.attack=0.1f;p.decay=0.5f;p.sustain=0.9f;p.release=0.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={2,1,"SCurve"};p.bp[2]={2.5f,0.9f,"Exponential"};p.bp[3]={3,0,"Exponential"};
p.lfoWaveform="Saw";p.lfoRateHz=4;p.lfoDepth=70;p.lfoBipolar=true;
p.voiceCount=6;p.detune=20;p.stereoSpread=80;r.emplace_back("Riser",buildPreset(p));
//2 Impact
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-50;p.maxPartials=768;
p.slot1.type="LowPass";p.slot1.cutoff=2000;p.slot1.resonance=0.4f;p.slot1.drive=0.7f;p.masterGain=1;
p.attack=0.001f;p.decay=0.5f;p.sustain=0.2f;p.release=0.3f;
p.bp[0]={0,0,"Linear"};p.bp[1]={0.001f,1,"Linear"};p.bp[2]={0.5f,0.2f,"Exponential"};p.bp[3]={0.8f,0,"Exponential"};
p.lfoWaveform="Random";p.lfoRateHz=20;p.lfoDepth=90;p.lfoBipolar=true;
p.voiceCount=8;p.detune=30;p.stereoSpread=100;p.allocMode="Random";
r.emplace_back("Impact",buildPreset(p));
//3 Sweep
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-60;p.maxPartials=768;
p.slot1.type="BandPass";p.slot1.cutoff=3000;p.slot1.resonance=0.3f;p.slot1.drive=0.2f;p.masterGain=0.7f;
p.attack=0.05f;p.decay=0.3f;p.sustain=0.8f;p.release=0.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={1.5f,1,"SCurve"};p.bp[2]={1.8f,0.8f,"Exponential"};p.bp[3]={2,0,"Exponential"};
p.lfoWaveform="Triangle";p.lfoRateHz=6;p.lfoDepth=50;p.lfoBipolar=true;
p.voiceCount=3;p.detune=12;p.stereoSpread=60;r.emplace_back("Sweep",buildPreset(p));
//4 Transition
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-58;p.maxPartials=768;
p.slot1.type="Comb";p.slot1.cutoff=800;p.slot1.resonance=0.6f;p.slot1.drive=0.3f;p.masterGain=0.75f;
p.attack=0.05f;p.decay=0.4f;p.sustain=0.7f;p.release=0.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={1,1,"SCurve"};p.bp[2]={1.4f,0.7f,"Exponential"};p.bp[3]={1.8f,0,"Exponential"};
p.lfoWaveform="Random";p.lfoRateHz=8;p.lfoDepth=65;p.lfoBipolar=true;
p.voiceCount=4;p.detune=18;p.stereoSpread=70;r.emplace_back("Transition",buildPreset(p));
//5 Glitch
p=PresetParams();p.threshold=-50;p.maxPartials=512;
p.slot1.type="Comb";p.slot1.cutoff=1500;p.slot1.resonance=0.7f;p.slot1.drive=0.5f;p.masterGain=0.7f;
p.attack=0.01f;p.decay=0.2f;p.sustain=0.4f;p.release=0.2f;
p.bp[0]={0,0,"Linear"};p.bp[1]={0.01f,1,"Linear"};p.bp[2]={0.2f,0.4f,"Exponential"};p.bp[3]={0.4f,0,"Exponential"};
p.lfoWaveform="Random";p.lfoRateHz=16;p.lfoDepth=85;p.lfoBipolar=true;
p.voiceCount=6;p.detune=25;p.stereoSpread=90;r.emplace_back("Glitch",buildPreset(p));
//6 Stutter
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-55;p.maxPartials=512;
p.slot1.type="Notch";p.slot1.cutoff=1000;p.slot1.resonance=0.5f;p.slot1.drive=0.2f;p.masterGain=0.8f;
p.loopMode="Forward";p.loopStart=0;p.loopEnd=2;p.tempo=140;p.syncEnabled=true;
p.numBp=1;p.bp[0]={0,1,"Linear"};
p.lfoWaveform="Square";p.lfoRateHz=16;p.lfoDepth=80;p.lfoBipolar=true;p.lfoSync=true;p.lfoRateBeats=0.125f;
p.voiceCount=3;p.detune=15;p.stereoSpread=55;r.emplace_back("Stutter",buildPreset(p));
//7 Reverse
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-65;p.maxPartials=768;
p.slot1.type="BandPass";p.slot1.cutoff=1500;p.slot1.resonance=0.3f;p.slot1.drive=0.2f;p.masterGain=0.7f;
p.attack=0.05f;p.decay=0.3f;p.sustain=0.8f;p.release=0.5f;
p.numBp=2;p.bp[0]={0,0,"SCurve"};p.bp[1]={1.5f,1,"SCurve"};
p.lfoWaveform="Sine";p.lfoRateHz=3;p.lfoDepth=40;p.lfoBipolar=true;
p.voiceCount=3;p.detune=10;p.stereoSpread=45;r.emplace_back("Reverse",buildPreset(p));
//8 Noise Sweep
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-45;p.maxPartials=768;
p.slot1.type="HighPass";p.slot1.cutoff=200;p.slot1.resonance=0.2f;p.slot1.drive=0.1f;p.masterGain=0.65f;
p.attack=0.05f;p.decay=0.3f;p.sustain=0.8f;p.release=0.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={2,1,"SCurve"};p.bp[2]={2.3f,0.8f,"Exponential"};p.bp[3]={2.5f,0,"Exponential"};
p.lfoWaveform="Saw";p.lfoRateHz=5;p.lfoDepth=60;p.lfoBipolar=true;
p.voiceCount=5;p.detune=20;p.stereoSpread=80;r.emplace_back("Noise Sweep",buildPreset(p));
//9 Buildup
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-55;p.maxPartials=768;
p.routingMode="Parallel";p.numSlots=2;p.masterGain=0.8f;
p.slot1.type="Comb";p.slot1.cutoff=600;p.slot1.resonance=0.5f;p.slot1.drive=0.3f;
p.slot2.type="BandPass";p.slot2.cutoff=2000;p.slot2.resonance=0.4f;p.slot2.drive=0.2f;p.slot2.mix=0.6f;
p.attack=0.05f;p.decay=0.5f;p.sustain=0.9f;p.release=0.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={3,1,"SCurve"};p.bp[2]={3.5f,0.9f,"Exponential"};p.bp[3]={4,0,"Exponential"};
p.lfoWaveform="Saw";p.lfoRateHz=2;p.lfoDepth=55;p.lfoBipolar=true;
p.voiceCount=5;p.detune=18;p.stereoSpread=75;r.emplace_back("Buildup",buildPreset(p));
//10 Drop
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-50;p.maxPartials=768;
p.slot1.type="LowPass";p.slot1.cutoff=2000;p.slot1.resonance=0.5f;p.slot1.drive=0.6f;p.masterGain=1;
p.attack=0.001f;p.decay=0.8f;p.sustain=0;p.release=0.5f;
p.numBp=3;p.bp[0]={0,0,"Linear"};p.bp[1]={0.001f,1,"Linear"};p.bp[2]={0.8f,0,"Exponential"};
p.lfoWaveform="Square";p.lfoRateHz=10;p.lfoDepth=75;p.lfoBipolar=true;
p.voiceCount=7;p.detune=25;p.stereoSpread=95;p.allocMode="Random";
r.emplace_back("Drop",buildPreset(p));
//11 Crash
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-45;p.maxPartials=768;
p.slot1.type="HighPass";p.slot1.cutoff=400;p.slot1.resonance=0.3f;p.slot1.drive=0.5f;p.masterGain=0.9f;
p.attack=0.001f;p.decay=0.6f;p.sustain=0;p.release=0.8f;
p.numBp=3;p.bp[0]={0,0,"Linear"};p.bp[1]={0.001f,1,"Linear"};p.bp[2]={0.6f,0,"Exponential"};
p.lfoWaveform="Random";p.lfoRateHz=24;p.lfoDepth=95;p.lfoBipolar=true;
p.voiceCount=8;p.detune=35;p.stereoSpread=100;p.allocMode="Random";
r.emplace_back("Crash",buildPreset(p));
//12 Hit
p=PresetParams();p.threshold=-50;p.maxPartials=512;
p.slot1.type="Comb";p.slot1.cutoff=1200;p.slot1.resonance=0.6f;p.slot1.drive=0.5f;p.masterGain=0.9f;
p.attack=0.001f;p.decay=0.3f;p.sustain=0;p.release=0.1f;
p.numBp=3;p.bp[0]={0,0,"Linear"};p.bp[1]={0.001f,1,"Linear"};p.bp[2]={0.3f,0,"Exponential"};
p.lfoWaveform="Random";p.lfoRateHz=20;p.lfoDepth=70;p.lfoBipolar=true;
p.voiceCount=4;p.detune=20;p.stereoSpread=70;r.emplace_back("Hit",buildPreset(p));
//13 Whoosh
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-58;p.maxPartials=768;
p.slot1.type="BandPass";p.slot1.cutoff=1500;p.slot1.resonance=0.4f;p.slot1.drive=0.3f;p.masterGain=0.7f;
p.attack=0.05f;p.decay=0.4f;p.sustain=0.7f;p.release=0.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={1,1,"SCurve"};p.bp[2]={1.4f,0.7f,"Exponential"};p.bp[3]={1.8f,0,"Exponential"};
p.lfoWaveform="Triangle";p.lfoRateHz=5;p.lfoDepth=45;p.lfoBipolar=true;
p.voiceCount=3;p.detune=12;p.stereoSpread=55;r.emplace_back("Whoosh",buildPreset(p));
//14 Downlifter
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-55;p.maxPartials=768;
p.slot1.type="LowPass";p.slot1.cutoff=3000;p.slot1.resonance=0.3f;p.slot1.drive=0.25f;p.masterGain=0.8f;
p.attack=0.05f;p.decay=0.4f;p.sustain=0.8f;p.release=0.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={2,1,"SCurve"};p.bp[2]={2.4f,0.8f,"Exponential"};p.bp[3]={2.8f,0,"Exponential"};
p.lfoWaveform="Triangle";p.lfoRateHz=3;p.lfoDepth=60;p.lfoBipolar=true;
p.voiceCount=4;p.detune=15;p.stereoSpread=65;r.emplace_back("Downlifter",buildPreset(p));
//15 Uplifter
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-55;p.maxPartials=768;
p.slot1.type="HighPass";p.slot1.cutoff=200;p.slot1.resonance=0.3f;p.slot1.drive=0.2f;p.masterGain=0.8f;
p.attack=0.05f;p.decay=0.4f;p.sustain=0.8f;p.release=0.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={2,1,"SCurve"};p.bp[2]={2.4f,0.8f,"Exponential"};p.bp[3]={2.8f,0,"Exponential"};
p.lfoWaveform="Saw";p.lfoRateHz=3;p.lfoDepth=60;p.lfoBipolar=true;
p.voiceCount=4;p.detune=15;p.stereoSpread=65;r.emplace_back("Uplifter",buildPreset(p));
//16 Rise
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-55;p.maxPartials=768;
p.slot1.type="BandPass";p.slot1.cutoff=500;p.slot1.resonance=0.45f;p.slot1.drive=0.3f;p.masterGain=0.75f;
p.attack=0.05f;p.decay=0.3f;p.sustain=0.9f;p.release=0.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={3,1,"SCurve"};p.bp[2]={3.3f,0.9f,"Exponential"};p.bp[3]={3.5f,0,"Exponential"};
p.lfoWaveform="Sine";p.lfoRateHz=4;p.lfoDepth=50;p.lfoBipolar=true;
p.voiceCount=4;p.detune=15;p.stereoSpread=60;r.emplace_back("Rise",buildPreset(p));
//17 Fall
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-55;p.maxPartials=768;
p.slot1.type="LowPass";p.slot1.cutoff=3000;p.slot1.resonance=0.35f;p.slot1.drive=0.3f;p.masterGain=0.75f;
p.attack=0.05f;p.decay=0.3f;p.sustain=0.7f;p.release=0.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={2.5f,1,"SCurve"};p.bp[2]={2.8f,0.7f,"Exponential"};p.bp[3]={3,0,"Exponential"};
p.lfoWaveform="Triangle";p.lfoRateHz=4;p.lfoDepth=55;p.lfoBipolar=true;
p.voiceCount=4;p.detune=15;p.stereoSpread=60;r.emplace_back("Fall",buildPreset(p));
//18 Impact Hit
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-48;p.maxPartials=768;
p.slot1.type="Comb";p.slot1.cutoff=800;p.slot1.resonance=0.7f;p.slot1.drive=0.6f;p.masterGain=1;
p.attack=0.001f;p.decay=0.4f;p.sustain=0;p.release=0.2f;
p.numBp=3;p.bp[0]={0,0,"Linear"};p.bp[1]={0.001f,1,"Linear"};p.bp[2]={0.4f,0,"Exponential"};
p.lfoWaveform="Random";p.lfoRateHz=20;p.lfoDepth=80;p.lfoBipolar=true;
p.voiceCount=6;p.detune=25;p.stereoSpread=85;p.allocMode="Random";
r.emplace_back("Impact Hit",buildPreset(p));
//19 Noise Burst
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-42;p.maxPartials=768;
p.slot1.type="HighPass";p.slot1.cutoff=2000;p.slot1.resonance=0.3f;p.slot1.drive=0.5f;p.masterGain=0.8f;
p.attack=0.001f;p.decay=0.2f;p.sustain=0;p.release=0.1f;
p.numBp=3;p.bp[0]={0,0,"Linear"};p.bp[1]={0.001f,1,"Linear"};p.bp[2]={0.2f,0,"Exponential"};
p.lfoWaveform="Random";p.lfoRateHz=30;p.lfoDepth=90;p.lfoBipolar=true;
p.voiceCount=8;p.detune=30;p.stereoSpread=100;p.allocMode="Random";
r.emplace_back("Noise Burst",buildPreset(p));
//20 Filter Sweep
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-60;p.maxPartials=768;
p.slot1.type="BandPass";p.slot1.cutoff=300;p.slot1.resonance=0.5f;p.slot1.drive=0.2f;p.masterGain=0.7f;
p.attack=0.05f;p.decay=0.4f;p.sustain=0.8f;p.release=0.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={1.5f,1,"SCurve"};p.bp[2]={1.9f,0.8f,"Exponential"};p.bp[3]={2.2f,0,"Exponential"};
p.lfoWaveform="Triangle";p.lfoRateHz=4;p.lfoDepth=70;p.lfoBipolar=true;
p.voiceCount=3;p.detune=10;p.stereoSpread=50;r.emplace_back("Filter Sweep",buildPreset(p));
return r;}
//==============================================================================
// Experimental presets (20)
//==============================================================================
std::vector<std::pair<juce::String,juce::ValueTree>> PresetFactory::createExperimentalPresets(){
std::vector<std::pair<juce::String,juce::ValueTree>> r; PresetParams p;
//1 DNA Hybrid
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=1024;
p.routingMode="Parallel";p.numSlots=2;p.masterGain=0.8f;
p.slot1.type="Morph";p.slot1.cutoff=800;p.slot1.resonance=0.5f;p.slot1.drive=0.3f;
p.slot2.type="Formant";p.slot2.cutoff=600;p.slot2.resonance=0.4f;p.slot2.drive=0.2f;p.slot2.mix=0.5f;
p.attack=0.3f;p.decay=0.6f;p.sustain=0.7f;p.release=1.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={0.3f,1,"SCurve"};p.bp[2]={0.9f,0.7f,"Exponential"};p.bp[3]={2,0,"Exponential"};
p.lfoWaveform="Random";p.lfoRateHz=2;p.lfoDepth=55;p.lfoBipolar=true;
p.voiceCount=4;p.detune=18;p.stereoSpread=65;r.emplace_back("DNA Hybrid",buildPreset(p));
//2 Spectral Morph
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="BlackmanHarris";p.threshold=-72;p.maxPartials=1024;
p.slot1.type="Morph";p.slot1.cutoff=1000;p.slot1.resonance=0.4f;p.slot1.drive=0.25f;p.slot1.morphAmt=0.5f;
p.slot1.morphSrc="LowPass";p.slot1.morphDst="BandPass";p.masterGain=0.75f;
p.attack=0.4f;p.decay=0.5f;p.sustain=0.75f;p.release=2.0f;
p.lfoWaveform="Triangle";p.lfoRateHz=1.5f;p.lfoDepth=50;p.lfoBipolar=true;
p.voiceCount=4;p.detune=15;p.stereoSpread=60;r.emplace_back("Spectral Morph",buildPreset(p));
//3 Particle Cloud
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-72;p.maxPartials=1024;
p.slot1.type="BandPass";p.slot1.cutoff=1200;p.slot1.resonance=0.45f;p.slot1.drive=0.15f;p.masterGain=0.7f;
p.attack=0.5f;p.decay=0.5f;p.sustain=0.8f;p.release=3.0f;
p.lfoWaveform="Random";p.lfoRateHz=0.8f;p.lfoDepth=60;p.lfoBipolar=true;
p.voiceCount=6;p.detune=20;p.stereoSpread=80;r.emplace_back("Particle Cloud",buildPreset(p));
//4 Granular Swarm
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=1024;
p.slot1.type="Notch";p.slot1.cutoff=2000;p.slot1.resonance=0.4f;p.slot1.drive=0.2f;p.masterGain=0.7f;
p.attack=0.2f;p.decay=0.5f;p.sustain=0.7f;p.release=2.5f;
p.lfoWaveform="Random";p.lfoRateHz=4;p.lfoDepth=65;p.lfoBipolar=true;
p.voiceCount=5;p.detune=22;p.stereoSpread=75;r.emplace_back("Granular Swarm",buildPreset(p));
//5 Neural Blend
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="BlackmanHarris";p.threshold=-72;p.maxPartials=1024;
p.slot1.type="Morph";p.slot1.cutoff=700;p.slot1.resonance=0.4f;p.slot1.drive=0.2f;
p.slot1.morphSrc="LowPass";p.slot1.morphDst="HighPass";p.slot1.morphAmt=0.3f;p.masterGain=0.8f;
p.attack=0.3f;p.decay=0.5f;p.sustain=0.8f;p.release=2.0f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={0.3f,1,"SCurve"};p.bp[2]={0.8f,0.8f,"Exponential"};p.bp[3]={2.5f,0,"Exponential"};
p.lfoWaveform="Sine";p.lfoRateHz=2.5f;p.lfoDepth=40;p.lfoBipolar=true;
p.voiceCount=4;p.detune=15;p.stereoSpread=60;r.emplace_back("Neural Blend",buildPreset(p));
//6 Frequency Shift
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;p.maxPartials=768;
p.slot1.type="Comb";p.slot1.cutoff=500;p.slot1.resonance=0.7f;p.slot1.drive=0.4f;p.masterGain=0.7f;
p.attack=0.1f;p.decay=0.4f;p.sustain=0.6f;p.release=1.5f;
p.lfoWaveform="Saw";p.lfoRateHz=6;p.lfoDepth=70;p.lfoBipolar=true;
p.voiceCount=5;p.detune=25;p.stereoSpread=80;r.emplace_back("Frequency Shift",buildPreset(p));
//7 Harmonic Chaos
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-55;p.maxPartials=768;
p.routingMode="Parallel";p.numSlots=2;p.masterGain=0.65f;
p.slot1.type="Comb";p.slot1.cutoff=600;p.slot1.resonance=0.8f;p.slot1.drive=0.6f;
p.slot2.type="Comb";p.slot2.cutoff=1200;p.slot2.resonance=0.6f;p.slot2.drive=0.4f;p.slot2.mix=0.7f;
p.attack=0.01f;p.decay=0.3f;p.sustain=0.5f;p.release=0.8f;
p.lfoWaveform="Random";p.lfoRateHz=12;p.lfoDepth=85;p.lfoBipolar=true;
p.voiceCount=6;p.detune=30;p.stereoSpread=90;p.allocMode="Random";
r.emplace_back("Harmonic Chaos",buildPreset(p));
//8 Sub Space
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-74;p.maxPartials=512;
p.slot1.type="LowPass";p.slot1.cutoff=100;p.slot1.resonance=0.2f;p.slot1.drive=0.05f;p.masterGain=0.7f;
p.attack=0.5f;p.decay=0.8f;p.sustain=0.9f;p.release=3.5f;
p.bp[0]={0,0,"Exponential"};p.bp[1]={0.5f,1,"SCurve"};p.bp[2]={1.3f,0.9f,"Exponential"};p.bp[3]={4,0,"Exponential"};
p.lfoWaveform="Sine";p.lfoRateHz=0.3f;p.lfoDepth=25;
p.voiceCount=2;p.detune=5;p.stereoSpread=20;r.emplace_back("Sub Space",buildPreset(p));
//9 Phase Warp
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;p.maxPartials=512;
p.slot1.type="AllPass";p.slot1.cutoff=800;p.slot1.resonance=0.5f;p.slot1.drive=0.2f;p.masterGain=0.75f;
p.attack=0.1f;p.decay=0.3f;p.sustain=0.6f;p.release=1.0f;
p.lfoWaveform="Triangle";p.lfoRateHz=5;p.lfoDepth=55;p.lfoBipolar=true;p.lfoPhase=180;
p.voiceCount=4;p.detune=18;p.stereoSpread=70;r.emplace_back("Phase Warp",buildPreset(p));
//10 Quantum Haze
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-72;p.maxPartials=1024;
p.routingMode="Parallel";p.numSlots=2;p.masterGain=0.7f;
p.slot1.type="BandPass";p.slot1.cutoff=1500;p.slot1.resonance=0.4f;p.slot1.drive=0.15f;
p.slot2.type="Notch";p.slot2.cutoff=800;p.slot2.resonance=0.3f;p.slot2.drive=0.1f;p.slot2.mix=0.6f;
p.attack=0.5f;p.decay=0.5f;p.sustain=0.8f;p.release=3.0f;
p.lfoWaveform="Random";p.lfoRateHz=1;p.lfoDepth=60;p.lfoBipolar=true;
p.voiceCount=5;p.detune=18;p.stereoSpread=75;r.emplace_back("Quantum Haze",buildPreset(p));
//11 Morph Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-72;p.maxPartials=1024;
p.slot1.type="Morph";p.slot1.cutoff=900;p.slot1.resonance=0.35f;p.slot1.drive=0.15f;p.masterGain=0.8f;
p.slot1.morphSrc="LowPass";p.slot1.morphDst="BandPass";p.slot1.morphAmt=0.4f;
p.attack=0.4f;p.decay=0.6f;p.sustain=0.85f;p.release=2.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={0.4f,1,"SCurve"};
p.lfoWaveform="Sine";p.lfoRateHz=1.2f;p.lfoDepth=40;p.lfoBipolar=true;
p.voiceCount=4;p.detune=14;p.stereoSpread=60;r.emplace_back("Morph Pad",buildPreset(p));
//12 Spectral Lead
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="BlackmanHarris";p.threshold=-68;p.maxPartials=512;
p.slot1.type="BandPass";p.slot1.cutoff=1800;p.slot1.resonance=0.5f;p.slot1.drive=0.3f;p.masterGain=0.8f;
p.attack=0.01f;p.decay=0.3f;p.sustain=0.65f;p.release=0.5f;
p.lfoWaveform="Saw";p.lfoRateHz=4;p.lfoDepth=45;p.lfoBipolar=true;
p.voiceCount=4;p.detune=16;p.stereoSpread=65;p.allocMode="OldestFirst";
r.emplace_back("Spectral Lead",buildPreset(p));
//13 Bio Bass
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-62;p.maxPartials=512;
p.routingMode="Parallel";p.numSlots=2;p.masterGain=0.75f;
p.slot1.type="LowPass";p.slot1.cutoff=250;p.slot1.resonance=0.4f;p.slot1.drive=0.3f;
p.slot2.type="Formant";p.slot2.cutoff=300;p.slot2.resonance=0.5f;p.slot2.drive=0.2f;p.slot2.mix=0.4f;
p.attack=0.02f;p.decay=0.4f;p.sustain=0.8f;p.release=0.6f;
p.lfoWaveform="Sine";p.lfoRateHz=3;p.lfoDepth=35;p.lfoBipolar=true;
p.voiceCount=3;p.detune=12;p.stereoSpread=40;r.emplace_back("Bio Bass",buildPreset(p));
//14 Plasma Lead
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-60;p.maxPartials=512;
p.slot1.type="HighPass";p.slot1.cutoff=400;p.slot1.resonance=0.55f;p.slot1.drive=0.5f;p.masterGain=0.85f;
p.attack=0.003f;p.decay=0.2f;p.sustain=0.6f;p.release=0.3f;
p.lfoWaveform="Random";p.lfoRateHz=8;p.lfoDepth=65;p.lfoBipolar=true;
p.voiceCount=5;p.detune=22;p.stereoSpread=75;p.allocMode="Random";
r.emplace_back("Plasma Lead",buildPreset(p));
//15 Crystal Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-72;p.maxPartials=1024;
p.slot1.type="HighPass";p.slot1.cutoff=600;p.slot1.resonance=0.25f;p.slot1.drive=0.1f;p.masterGain=0.75f;
p.attack=0.3f;p.decay=0.5f;p.sustain=0.8f;p.release=2.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={0.3f,1,"SCurve"};p.bp[2]={0.8f,0.8f,"Exponential"};p.bp[3]={3,0,"Exponential"};
p.lfoWaveform="Triangle";p.lfoRateHz=2;p.lfoDepth=35;p.lfoBipolar=true;
p.voiceCount=5;p.detune=12;p.stereoSpread=70;r.emplace_back("Crystal Pad",buildPreset(p));
//16 Void Pad
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-74;p.maxPartials=768;
p.slot1.type="LowPass";p.slot1.cutoff=300;p.slot1.resonance=0.3f;p.slot1.drive=0.1f;p.masterGain=0.65f;
p.attack=0.8f;p.decay=0.6f;p.sustain=0.85f;p.release=4.0f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={0.8f,1,"SCurve"};p.bp[3]={4,0,"Exponential"};
p.lfoWaveform="Sine";p.lfoRateHz=0.4f;p.lfoDepth=45;p.lfoBipolar=true;
p.voiceCount=3;p.detune=10;p.stereoSpread=50;r.emplace_back("Void Pad",buildPreset(p));
//17 Echoes
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=1024;
p.slot1.type="Comb";p.slot1.cutoff=400;p.slot1.resonance=0.5f;p.slot1.drive=0.2f;p.masterGain=0.7f;
p.attack=0.5f;p.decay=0.5f;p.sustain=0.8f;p.release=3.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={0.5f,1,"SCurve"};
p.lfoWaveform="Random";p.lfoRateHz=1.5f;p.lfoDepth=50;p.lfoBipolar=true;
p.voiceCount=4;p.detune=15;p.stereoSpread=65;r.emplace_back("Echoes",buildPreset(p));
//18 Starlight
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-72;p.maxPartials=1024;
p.slot1.type="BandPass";p.slot1.cutoff=2500;p.slot1.resonance=0.35f;p.slot1.drive=0.12f;p.masterGain=0.75f;
p.attack=0.2f;p.decay=0.4f;p.sustain=0.75f;p.release=2.0f;
p.lfoWaveform="Triangle";p.lfoRateHz=3;p.lfoDepth=40;p.lfoBipolar=true;
p.voiceCount=5;p.detune=16;p.stereoSpread=70;r.emplace_back("Starlight",buildPreset(p));
//19 Deep Space
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-74;p.maxPartials=768;
p.routingMode="Serial";p.numSlots=2;p.masterGain=0.65f;
p.slot1.type="LowPass";p.slot1.cutoff=500;p.slot1.resonance=0.2f;p.slot1.drive=0.08f;
p.slot2.type="Notch";p.slot2.cutoff=1200;p.slot2.resonance=0.3f;p.slot2.drive=0.1f;p.slot2.mix=0.5f;
p.attack=0.8f;p.decay=0.5f;p.sustain=0.9f;p.release=4.0f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={0.8f,1,"SCurve"};p.bp[3]={4,0,"Exponential"};
p.lfoWaveform="Sine";p.lfoRateHz=0.2f;p.lfoDepth=50;p.lfoBipolar=true;
p.voiceCount=3;p.detune=8;p.stereoSpread=45;r.emplace_back("Deep Space",buildPreset(p));
//20 Neural Dream
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="BlackmanHarris";p.threshold=-72;p.maxPartials=1024;
p.slot1.type="Morph";p.slot1.cutoff=600;p.slot1.resonance=0.3f;p.slot1.drive=0.15f;p.masterGain=0.7f;
p.slot1.morphSrc="LowPass";p.slot1.morphDst="BandPass";p.slot1.morphAmt=0.6f;
p.attack=0.6f;p.decay=0.5f;p.sustain=0.85f;p.release=3.5f;
p.bp[0]={0,0,"SCurve"};p.bp[1]={0.6f,1,"SCurve"};p.bp[2]={1.1f,0.85f,"Exponential"};p.bp[3]={4,0,"Exponential"};
p.lfoWaveform="Sine";p.lfoRateHz=0.6f;p.lfoDepth=45;p.lfoBipolar=true;
p.voiceCount=5;p.detune=16;p.stereoSpread=70;r.emplace_back("Neural Dream",buildPreset(p));
return r;}
//==============================================================================
// Vocal presets (5)
//==============================================================================
std::vector<std::pair<juce::String,juce::ValueTree>> PresetFactory::createVocalPresets(){
std::vector<std::pair<juce::String,juce::ValueTree>> r; PresetParams p;
//1 Pop Lead: SoloistChain(presence+3dB,air+2dB,compRatio=4:1,satDrive=0.3,reverbWet=30%,width=60%)
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;p.maxPartials=384;
p.slot1.type="BandPass";p.slot1.cutoff=2000;p.slot1.resonance=0.4f;p.slot1.drive=0.2f;
p.attack=0.02f;p.decay=0.25f;p.sustain=0.8f;p.release=0.4f;
p.lfoWaveform="Sine";p.lfoRateHz=4;p.lfoDepth=20;p.lfoBipolar=true;
p.voiceCount=3;p.detune=8;p.stereoSpread=60;
{auto vc=juce::ValueTree("VocalConfig");
vc.setProperty("VocalCharacter",static_cast<int>(VocalCharacter::Breathy),nullptr);
vc.setProperty("PresenceDb",3.0,nullptr);vc.setProperty("AirDb",2.0,nullptr);
vc.setProperty("CompRatio",4.0,nullptr);vc.setProperty("SatDrive",0.3f,nullptr);
vc.setProperty("ReverbWet",0.3f,nullptr);vc.setProperty("Width",0.6f,nullptr);
auto pTree=buildPreset(p);pTree.addChild(vc,-1,nullptr);r.emplace_back("Pop Lead",pTree);}
//2 Breathy Ballad: VocalCharacter=Breathy, SpaceModule(Plate,decay=2.5s), SubHarmonicGenerator(level0.2)
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-70;p.maxPartials=512;
p.slot1.type="HighPass";p.slot1.cutoff=400;p.slot1.resonance=0.2f;p.slot1.drive=0.05f;
p.attack=0.05f;p.decay=0.3f;p.sustain=0.7f;p.release=1.0f;
p.lfoWaveform="Sine";p.lfoRateHz=2;p.lfoDepth=15;
p.voiceCount=2;p.detune=5;p.stereoSpread=40;
{auto vc=juce::ValueTree("VocalConfig");
vc.setProperty("VocalCharacter",static_cast<int>(VocalCharacter::Breathy),nullptr);
vc.setProperty("SpaceMode",static_cast<int>(SpaceMode::Plate),nullptr);
vc.setProperty("SpaceDecay",2.5,nullptr);vc.setProperty("SubLevel",0.2f,nullptr);
auto pTree=buildPreset(p);pTree.addChild(vc,-1,nullptr);r.emplace_back("Breathy Ballad",pTree);}
//3 Chest Warm: VocalCharacter=Chest, FormantTuner(shift=-2,spread=0.8), Compressor(ratio=2:1)
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.windowType="BlackmanHarris";p.threshold=-65;p.maxPartials=256;
p.slot1.type="LowPass";p.slot1.cutoff=800;p.slot1.resonance=0.3f;p.slot1.drive=0.1f;
p.attack=0.03f;p.decay=0.3f;p.sustain=0.85f;p.release=0.5f;
p.lfoWaveform="Sine";p.lfoRateHz=3;p.lfoDepth=10;
p.voiceCount=2;p.detune=4;p.stereoSpread=30;
{auto vc=juce::ValueTree("VocalConfig");
vc.setProperty("VocalCharacter",static_cast<int>(VocalCharacter::Chest),nullptr);
vc.setProperty("FormantShift",-2.0,nullptr);vc.setProperty("FormantSpread",0.8,nullptr);
vc.setProperty("CompRatio",2.0,nullptr);
auto pTree=buildPreset(p);pTree.addChild(vc,-1,nullptr);r.emplace_back("Chest Warm",pTree);}
//4 Telephone Effect: VocalCharacter=Telephone, SpaceModule(Room,decay=0.3s), distortion
p=PresetParams();p.fftSize=4096;p.hopSize=1024;p.threshold=-55;p.maxPartials=256;
p.slot1.type="BandPass";p.slot1.cutoff=300;p.slot1.resonance=0.7f;p.slot1.drive=0.3f;
p.slot2.type="LowPass";p.slot2.cutoff=3400;p.slot2.resonance=0.5f;p.slot2.drive=0.1f;p.slot2.mix=1.0f;
p.numSlots=2;p.routingMode="Serial";
p.attack=0.005f;p.decay=0.2f;p.sustain=0.5f;p.release=0.3f;
p.voiceCount=1;
{auto vc=juce::ValueTree("VocalConfig");
vc.setProperty("VocalCharacter",static_cast<int>(VocalCharacter::Telephone),nullptr);
vc.setProperty("SpaceMode",static_cast<int>(SpaceMode::Room),nullptr);
vc.setProperty("SpaceDecay",0.3,nullptr);vc.setProperty("DistortionDrive",0.4f,nullptr);
auto pTree=buildPreset(p);pTree.addChild(vc,-1,nullptr);r.emplace_back("Telephone Effect",pTree);}
//5 Choir Ensemble: VocalCharacter=Choir, Reverb(Hall,decay=3s), UnisonEngine(8voices)
p=PresetParams();p.fftSize=8192;p.hopSize=2048;p.windowType="Hamming";p.threshold=-72;p.maxPartials=768;
p.slot1.type="BandPass";p.slot1.cutoff=1500;p.slot1.resonance=0.4f;p.slot1.drive=0.1f;p.masterGain=0.85f;
p.attack=0.2f;p.decay=0.5f;p.sustain=0.9f;p.release=2.0f;
p.lfoWaveform="Sine";p.lfoRateHz=1.5f;p.lfoDepth=20;
p.voiceCount=8;p.detune=15;p.stereoSpread=80;
{auto vc=juce::ValueTree("VocalConfig");
vc.setProperty("VocalCharacter",static_cast<int>(VocalCharacter::Choir),nullptr);
vc.setProperty("ReverbMode",static_cast<int>(SpaceMode::Hall),nullptr);
vc.setProperty("ReverbDecay",3.0,nullptr);
auto pTree=buildPreset(p);pTree.addChild(vc,-1,nullptr);r.emplace_back("Choir Ensemble",pTree);}
return r;}

//==============================================================================
// Create all presets (100+)
//==============================================================================
std::vector<std::pair<juce::String,juce::ValueTree>> PresetFactory::createAllPresets(){
std::vector<std::pair<juce::String,juce::ValueTree>> all;
auto b=createBassPresets(),l=createLeadPresets(),p=createPadPresets(),f=createFXPresets(),e=createExperimentalPresets(),v=createVocalPresets();
all.insert(all.end(),b.begin(),b.end());
all.insert(all.end(),l.begin(),l.end());
all.insert(all.end(),p.begin(),p.end());
all.insert(all.end(),f.begin(),f.end());
all.insert(all.end(),e.begin(),e.end());
all.insert(all.end(),v.begin(),v.end());
return all;}

//==============================================================================
// Effect factory presets (Delay, Reverb, Chorus, Distortion, etc.)
//==============================================================================
namespace {

static juce::ValueTree buildSimpleTree(const char* typeName, std::initializer_list<std::pair<const char*, double>> params)
{
    juce::ValueTree tree(typeName);
    for (auto& p : params)
        tree.setProperty(p.first, p.second, nullptr);
    return tree;
}

static juce::ValueTree buildEQPreset(const juce::String& presetName)
{
    auto tree = juce::ValueTree("EQEffect");

    auto lowBand  = juce::ValueTree("Band");
    lowBand.setProperty("index", 0, nullptr);
    lowBand.setProperty("frequency", 200.0, nullptr);
    lowBand.setProperty("q", 0.707, nullptr);
    lowBand.setProperty("type", 0, nullptr); // LowShelf

    auto midBand  = juce::ValueTree("Band");
    midBand.setProperty("index", 1, nullptr);
    midBand.setProperty("frequency", 1000.0, nullptr);
    midBand.setProperty("q", 0.707, nullptr);
    midBand.setProperty("type", 1, nullptr); // Peaking

    auto highBand = juce::ValueTree("Band");
    highBand.setProperty("index", 2, nullptr);
    highBand.setProperty("frequency", 8000.0, nullptr);
    highBand.setProperty("q", 0.707, nullptr);
    highBand.setProperty("type", 2, nullptr); // HighShelf

    if (presetName == "Scoop")
    {
        lowBand.setProperty("gain", 3.0, nullptr);
        midBand.setProperty("gain", -4.0, nullptr);
        highBand.setProperty("gain", 2.0, nullptr);
    }
    else if (presetName == "Bright")
    {
        lowBand.setProperty("gain", 0.0, nullptr);
        midBand.setProperty("gain", 2.0, nullptr);
        highBand.setProperty("gain", 4.0, nullptr);
    }

    tree.addChild(lowBand, -1, nullptr);
    tree.addChild(midBand, -1, nullptr);
    tree.addChild(highBand, -1, nullptr);
    return tree;
}

static const char* effectPresetNames[][8] = {
    { "Delay",       "Slapback", "Dub", nullptr },
    { "Reverb",      "Small Room", "Large Hall", "Plate", nullptr },
    { "Chorus",      "Subtle", "Wide Ensemble", nullptr },
    { "Distortion",  "Warm Overdrive", "Heavy Fuzz", nullptr },
    { "Saturation",  "Tape Warmth", "Tube Drive", nullptr },
    { "RingMod",     "Tremolo", "Bell Tones", nullptr },
    { "StereoWidener", "Natural Width", "Extreme Wide", nullptr },
    { "Bitcrusher",  "8-bit Lo-Fi", "Voice", nullptr },
    { "Phaser",      "Sweep", "Jet", nullptr },
    { "Flanger",     "Slow Sweep", "Through Zero", nullptr },
    { "Compressor",  "Gentle", "Squash", nullptr },
    { "Limiter",     "Transparent", "Brick Wall", nullptr },
    { "AutoTune",    "Natural", "Hard Tune", nullptr },
    { "EQ",          "Scoop", "Bright", nullptr },
};

} // anonymous namespace

juce::StringArray PresetFactory::getFactoryPresets(const juce::String& effectType)
{
    for (auto& entry : effectPresetNames)
    {
        if (effectType != entry[0])
            continue;

        juce::StringArray names;
        for (int i = 1; entry[i] != nullptr; ++i)
            names.add(juce::String(entry[i]));
        return names;
    }
    return {};
}

juce::ValueTree PresetFactory::getFactoryPreset(const juce::String& effectType, const juce::String& presetName)
{
    // EQ uses a different tree structure (child bands), handle separately
    if (effectType == "EQ")
    {
        if (presetName == "Scoop" || presetName == "Bright")
            return buildEQPreset(presetName);
        return {};
    }

    // Simple parameter-based presets
    if (effectType == "Delay")
    {
        if (presetName == "Slapback") return buildSimpleTree("DelayEffect",         {{"delayMs", 80.0},    {"feedback", 0.2},   {"mix", 0.3}});
        if (presetName == "Dub")      return buildSimpleTree("DelayEffect",         {{"delayMs", 500.0},   {"feedback", 0.7},   {"mix", 0.5},  {"wetHighCut", 8000.0}});
    }

    if (effectType == "Reverb")
    {
        if (presetName == "Small Room") return buildSimpleTree("ReverbEffect",      {{"roomSize", 0.3},    {"damping", 0.5},    {"wetLevel", 0.3}});
        if (presetName == "Large Hall") return buildSimpleTree("ReverbEffect",      {{"roomSize", 0.9},    {"damping", 0.2},    {"wetLevel", 0.4}});
        if (presetName == "Plate")      return buildSimpleTree("ReverbEffect",      {{"roomSize", 0.6},    {"damping", 0.7},    {"wetLevel", 0.4}});
    }

    if (effectType == "Chorus")
    {
        if (presetName == "Subtle")        return buildSimpleTree("ChorusEffect",   {{"rate", 0.5},        {"depth", 0.2},      {"mix", 0.3}});
        if (presetName == "Wide Ensemble") return buildSimpleTree("ChorusEffect",   {{"rate", 1.5},        {"depth", 0.7},      {"mix", 0.6}});
    }

    if (effectType == "Distortion")
    {
        // type: 0=SoftClip, 1=HardClip, 2=Tube, 3=WaveFolder, 4=BitCrush
        // drive/range/blend stored as percent (0-100) matching setState defaults
        if (presetName == "Warm Overdrive") return buildSimpleTree("DistortionEffect", {{"type", 2.0}, {"drive", 40.0}, {"range", 60.0}, {"blend", 50.0}});
        if (presetName == "Heavy Fuzz")     return buildSimpleTree("DistortionEffect", {{"type", 1.0}, {"drive", 90.0}, {"range", 30.0}, {"blend", 70.0}});
    }

    if (effectType == "Saturation")
    {
        // mode: 0=Soft, 1=Tube, 2=Tape; drive/mix stored as percent (0-100)
        if (presetName == "Tape Warmth") return buildSimpleTree("SaturationEffect", {{"mode", 2.0}, {"drive", 30.0}, {"mix", 40.0}});
        if (presetName == "Tube Drive")  return buildSimpleTree("SaturationEffect", {{"mode", 1.0}, {"drive", 60.0}, {"mix", 60.0}});
    }

    if (effectType == "RingMod")
    {
        // waveform: 0=Sine, 1=Triangle, 2=Square
        if (presetName == "Tremolo")    return buildSimpleTree("RingModulatorEffect", {{"frequency", 2.0},    {"waveform", 0.0}, {"mix", 0.5}});
        if (presetName == "Bell Tones") return buildSimpleTree("RingModulatorEffect", {{"frequency", 1200.0}, {"waveform", 0.0}, {"mix", 0.3}});
    }

    if (effectType == "StereoWidener")
    {
        // width: 0.0=0%, 0.5=100%, 1.0=200%
        if (presetName == "Natural Width") return buildSimpleTree("StereoWidenerEffect", {{"width", 0.6}});
        if (presetName == "Extreme Wide")  return buildSimpleTree("StereoWidenerEffect", {{"width", 0.9}});
    }

    if (effectType == "Bitcrusher")
    {
        if (presetName == "8-bit Lo-Fi") return buildSimpleTree("BitcrusherEffect", {{"bitDepth", 8.0},  {"downsample", 4.0}, {"mix", 0.5}});
        if (presetName == "Voice")       return buildSimpleTree("BitcrusherEffect", {{"bitDepth", 4.0},  {"downsample", 8.0}, {"mix", 0.7}});
    }

    if (effectType == "Phaser")
    {
        if (presetName == "Sweep") return buildSimpleTree("PhaserEffect", {{"rate", 0.5}, {"depth", 0.7}, {"feedback", 0.4}});
        if (presetName == "Jet")   return buildSimpleTree("PhaserEffect", {{"rate", 4.0}, {"depth", 0.5}, {"feedback", 0.6}});
    }

    if (effectType == "Flanger")
    {
        if (presetName == "Slow Sweep")   return buildSimpleTree("FlangerEffect", {{"rate", 0.3}, {"depth", 0.6}, {"feedback", 0.4}});
        if (presetName == "Through Zero") return buildSimpleTree("FlangerEffect", {{"rate", 1.0}, {"depth", 0.8}, {"feedback", -0.3}});
    }

    if (effectType == "Compressor")
    {
        if (presetName == "Gentle") return buildSimpleTree("CompressorEffect", {{"threshold", -12.0}, {"ratio", 2.0}, {"mix", 0.4}});
        if (presetName == "Squash") return buildSimpleTree("CompressorEffect", {{"threshold", -24.0}, {"ratio", 8.0}, {"mix", 0.7}});
    }

    if (effectType == "Limiter")
    {
        if (presetName == "Transparent") return buildSimpleTree("LimiterEffect", {{"threshold", -3.0}, {"ceiling", -0.5}});
        if (presetName == "Brick Wall")  return buildSimpleTree("LimiterEffect", {{"threshold", -6.0}, {"ceiling", -1.0}});
    }

    if (effectType == "AutoTune")
    {
        if (presetName == "Natural")  return buildSimpleTree("AutoTuneEffect", {{"retuneSpeed", 0.3}, {"amount", 0.5}});
        if (presetName == "Hard Tune") return buildSimpleTree("AutoTuneEffect", {{"retuneSpeed", 0.9}, {"amount", 1.0}});
    }

    return {};
}

//==============================================================================
// Consolidated module factory presets
//==============================================================================
namespace {

static const char* modulePresetNames[][32] = {
    { "ConsolidatedDelay",
      "Simple Mono",       "Slapback Mono",     // Mono (0)
      "Stereo Spread",     "Dual Delay",         // Stereo (1)
      "Ping Pong",         "Bouncing Echo",      // PingPong (2)
      "Reverse Swell",     "Backward Riser",     // Reverse (3)
      "Tape Echo",         "Worn Tape",          // Tape (4)
      "Ducked Delay",      "Rhythm Echo",        // Ducking (5)
      nullptr },
    { "DriveModule",
      "Warm Drive",        "Edge Of Breakup",    // Soft (0)
      "Tube Scream",       "British Plexi",      // Tube (1)
      "Tape Saturate",     "Console Drive",      // Tape (2)
      "Hard Clipper",      "Fuzz Face",          // Hard (3)
      "Fold Synth",        "Octa Fold",          // Fold (4)
      "Lo-Fi Crush",       "8 Bit",              // Crush (5)
      "Tremolo Ring",      "Bell Tone",          // Ring (6)
      nullptr },
    { "DynamicsModule",
      "Gentle Comp",       "Vocal Leveler",      // Compressor (0)
      "Transparent Limit", "Brick Wall",         // Limiter (1)
      "Tight Gate",        "Noise Gate",         // Gate (2)
      nullptr },
    { "EQModule",
      "Scoop",             "Smile EQ",           // Band3 (0)
      "Full Mix",          "Mastering 5",        // Band5 (1)
      "Bright Tilt",       "Dark Tilt",          // Tilt (2)
      "Surgical Mid",      "Wide Q Cut",         // Para (3)
      nullptr },
    { "ModulationModule",
      "Subtle Chorus",     "Wide Ensemble",      // Chorus (0)
      "Slow Flange",       "Jet Flange",         // Flanger (1)
      "Phase Sweep",       "Phase Bubble",       // Phaser (2)
      nullptr },
    { "SpaceModule",
      "Small Room",        "Live Room",          // Room (0)
      "Concert Hall",      "Cathedral",          // Hall (1)
      "Plate Reverb",      "Vintage Plate",      // Plate (2)
      "Shimmer Pad",       "Celestial",          // Shimmer (3)
      "Natural Width",     "Extreme Wide",       // Widener (4)
      nullptr },
    { "PitchModule",
      "Soft Tune",         "Hard Tune",          // AutoTune (0)
      "Octave Up",         "Fifth Up",           // PitchShift (1)
      "Third Harmony",     "Fifth Harmony",      // Harmonize (2)
      "Vocal Formant",     "Robot Formant",      // Formant (3)
      nullptr },
};

} // anonymous namespace

juce::StringArray PresetFactory::getModuleFactoryPresets(const juce::String& moduleType)
{
    for (auto& entry : modulePresetNames)
    {
        if (moduleType != entry[0])
            continue;

        juce::StringArray names;
        for (int i = 1; entry[i] != nullptr; ++i)
            names.add(juce::String(entry[i]));
        return names;
    }
    return {};
}

juce::ValueTree PresetFactory::getModuleFactoryPreset(const juce::String& moduleType, const juce::String& presetName)
{
    //==========================================================================
    // ConsolidatedDelay (6 modes × 2 presets = 12)
    //==========================================================================
    if (moduleType == "ConsolidatedDelay")
    {
        auto tree = juce::ValueTree("ConsolidatedDelay");
        // Common defaults
        tree.setProperty("wetHPF", 20.0, nullptr);
        tree.setProperty("wetLPF", 20000.0, nullptr);
        tree.setProperty("bypass", false, nullptr);

        // Mono mode presets (mode=0)
        if (presetName == "Simple Mono") {
            tree.setProperty("mode", 0, nullptr);
            tree.setProperty("timeMs", 250.0, nullptr);
            tree.setProperty("feedback", 0.3, nullptr);
            tree.setProperty("mix", 0.35, nullptr);
        }
        else if (presetName == "Slapback Mono") {
            tree.setProperty("mode", 0, nullptr);
            tree.setProperty("timeMs", 80.0, nullptr);
            tree.setProperty("feedback", 0.15, nullptr);
            tree.setProperty("mix", 0.3, nullptr);
        }
        // Stereo mode presets (mode=1)
        else if (presetName == "Stereo Spread") {
            tree.setProperty("mode", 1, nullptr);
            tree.setProperty("timeMs", 300.0, nullptr);
            tree.setProperty("feedback", 0.4, nullptr);
            tree.setProperty("mix", 0.45, nullptr);
        }
        else if (presetName == "Dual Delay") {
            tree.setProperty("mode", 1, nullptr);
            tree.setProperty("timeMs", 500.0, nullptr);
            tree.setProperty("feedback", 0.25, nullptr);
            tree.setProperty("mix", 0.3, nullptr);
        }
        // PingPong mode presets (mode=2)
        else if (presetName == "Ping Pong") {
            tree.setProperty("mode", 2, nullptr);
            tree.setProperty("timeMs", 200.0, nullptr);
            tree.setProperty("feedback", 0.5, nullptr);
            tree.setProperty("mix", 0.5, nullptr);
        }
        else if (presetName == "Bouncing Echo") {
            tree.setProperty("mode", 2, nullptr);
            tree.setProperty("timeMs", 350.0, nullptr);
            tree.setProperty("feedback", 0.65, nullptr);
            tree.setProperty("mix", 0.6, nullptr);
        }
        // Reverse mode presets (mode=3)
        else if (presetName == "Reverse Swell") {
            tree.setProperty("mode", 3, nullptr);
            tree.setProperty("timeMs", 250.0, nullptr);
            tree.setProperty("feedback", 0.4, nullptr);
            tree.setProperty("mix", 0.5, nullptr);
            tree.setProperty("windowLen", 300.0, nullptr);
        }
        else if (presetName == "Backward Riser") {
            tree.setProperty("mode", 3, nullptr);
            tree.setProperty("timeMs", 200.0, nullptr);
            tree.setProperty("feedback", 0.6, nullptr);
            tree.setProperty("mix", 0.7, nullptr);
            tree.setProperty("windowLen", 500.0, nullptr);
        }
        // Tape mode presets (mode=4)
        else if (presetName == "Tape Echo") {
            tree.setProperty("mode", 4, nullptr);
            tree.setProperty("timeMs", 180.0, nullptr);
            tree.setProperty("feedback", 0.3, nullptr);
            tree.setProperty("mix", 0.4, nullptr);
            tree.setProperty("wowFlut", 0.3, nullptr);
            tree.setProperty("tone", 0.6, nullptr);
        }
        else if (presetName == "Worn Tape") {
            tree.setProperty("mode", 4, nullptr);
            tree.setProperty("timeMs", 120.0, nullptr);
            tree.setProperty("feedback", 0.2, nullptr);
            tree.setProperty("mix", 0.3, nullptr);
            tree.setProperty("wowFlut", 0.6, nullptr);
            tree.setProperty("tone", 0.3, nullptr);
        }
        // Ducking mode presets (mode=5)
        else if (presetName == "Ducked Delay") {
            tree.setProperty("mode", 5, nullptr);
            tree.setProperty("timeMs", 250.0, nullptr);
            tree.setProperty("feedback", 0.3, nullptr);
            tree.setProperty("mix", 0.5, nullptr);
            tree.setProperty("threshold", -30.0, nullptr);
            tree.setProperty("duckRel", 150.0, nullptr);
        }
        else if (presetName == "Rhythm Echo") {
            tree.setProperty("mode", 5, nullptr);
            tree.setProperty("timeMs", 150.0, nullptr);
            tree.setProperty("feedback", 0.2, nullptr);
            tree.setProperty("mix", 0.4, nullptr);
            tree.setProperty("threshold", -20.0, nullptr);
            tree.setProperty("duckRel", 50.0, nullptr);
        }
        else return {};
        return tree;
    }

    //==========================================================================
    // DriveModule (7 modes × 2 presets = 14)
    //==========================================================================
    if (moduleType == "DriveModule")
    {
        auto tree = juce::ValueTree("DriveModule");
        tree.setProperty("tone", 1.0, nullptr);
        tree.setProperty("mix", 1.0, nullptr);
        tree.setProperty("wetHPF", 20.0, nullptr);
        tree.setProperty("wetLPF", 20000.0, nullptr);
        tree.setProperty("bypass", false, nullptr);
        tree.setProperty("gain", 1.0, nullptr);

        // Soft mode (mode=0)
        if (presetName == "Warm Drive") {
            tree.setProperty("mode", 0, nullptr);
            tree.setProperty("drive", 0.35, nullptr);
            tree.setProperty("tone", 0.7, nullptr);
        }
        else if (presetName == "Edge Of Breakup") {
            tree.setProperty("mode", 0, nullptr);
            tree.setProperty("drive", 0.6, nullptr);
            tree.setProperty("tone", 0.8, nullptr);
        }
        // Tube mode (mode=1)
        else if (presetName == "Tube Scream") {
            tree.setProperty("mode", 1, nullptr);
            tree.setProperty("drive", 0.5, nullptr);
            tree.setProperty("tone", 0.5, nullptr);
        }
        else if (presetName == "British Plexi") {
            tree.setProperty("mode", 1, nullptr);
            tree.setProperty("drive", 0.75, nullptr);
            tree.setProperty("tone", 0.4, nullptr);
        }
        // Tape mode (mode=2)
        else if (presetName == "Tape Saturate") {
            tree.setProperty("mode", 2, nullptr);
            tree.setProperty("drive", 0.4, nullptr);
            tree.setProperty("tone", 0.6, nullptr);
        }
        else if (presetName == "Console Drive") {
            tree.setProperty("mode", 2, nullptr);
            tree.setProperty("drive", 0.55, nullptr);
            tree.setProperty("tone", 0.7, nullptr);
        }
        // Hard mode (mode=3)
        else if (presetName == "Hard Clipper") {
            tree.setProperty("mode", 3, nullptr);
            tree.setProperty("drive", 0.8, nullptr);
        }
        else if (presetName == "Fuzz Face") {
            tree.setProperty("mode", 3, nullptr);
            tree.setProperty("drive", 1.0, nullptr);
            tree.setProperty("tone", 0.3, nullptr);
        }
        // Fold mode (mode=4)
        else if (presetName == "Fold Synth") {
            tree.setProperty("mode", 4, nullptr);
            tree.setProperty("drive", 0.5, nullptr);
            tree.setProperty("mix", 0.7, nullptr);
        }
        else if (presetName == "Octa Fold") {
            tree.setProperty("mode", 4, nullptr);
            tree.setProperty("drive", 0.8, nullptr);
            tree.setProperty("mix", 0.6, nullptr);
        }
        // Crush mode (mode=5)
        else if (presetName == "Lo-Fi Crush") {
            tree.setProperty("mode", 5, nullptr);
            tree.setProperty("drive", 0.6, nullptr);
            tree.setProperty("mix", 0.5, nullptr);
        }
        else if (presetName == "8 Bit") {
            tree.setProperty("mode", 5, nullptr);
            tree.setProperty("drive", 0.3, nullptr);
            tree.setProperty("mix", 0.7, nullptr);
        }
        // Ring mode (mode=6)
        else if (presetName == "Tremolo Ring") {
            tree.setProperty("mode", 6, nullptr);
            tree.setProperty("drive", 0.15, nullptr);
            tree.setProperty("mix", 0.4, nullptr);
        }
        else if (presetName == "Bell Tone") {
            tree.setProperty("mode", 6, nullptr);
            tree.setProperty("drive", 0.5, nullptr);
            tree.setProperty("mix", 0.3, nullptr);
        }
        else return {};
        return tree;
    }

    //==========================================================================
    // DynamicsModule (3 modes × 2 presets = 6)
    //==========================================================================
    if (moduleType == "DynamicsModule")
    {
        auto tree = juce::ValueTree("DynamicsModule");
        tree.setProperty("mix", 1.0, nullptr);
        tree.setProperty("bypass", false, nullptr);

        // Compressor mode (mode=0)
        if (presetName == "Gentle Comp") {
            tree.setProperty("mode", 0, nullptr);
            tree.setProperty("compRatio", 2.0, nullptr);
            tree.setProperty("compThreshold", -12.0, nullptr);
            tree.setProperty("compAttack", 15.0, nullptr);
            tree.setProperty("compRelease", 80.0, nullptr);
        }
        else if (presetName == "Vocal Leveler") {
            tree.setProperty("mode", 0, nullptr);
            tree.setProperty("compRatio", 4.0, nullptr);
            tree.setProperty("compThreshold", -18.0, nullptr);
            tree.setProperty("compAttack", 5.0, nullptr);
            tree.setProperty("compRelease", 50.0, nullptr);
        }
        // Limiter mode (mode=1)
        else if (presetName == "Transparent Limit") {
            tree.setProperty("mode", 1, nullptr);
            tree.setProperty("limThreshold", -3.0, nullptr);
            tree.setProperty("limRelease", 20.0, nullptr);
            tree.setProperty("limCeiling", -0.5, nullptr);
        }
        else if (presetName == "Brick Wall") {
            tree.setProperty("mode", 1, nullptr);
            tree.setProperty("limThreshold", -6.0, nullptr);
            tree.setProperty("limRelease", 5.0, nullptr);
            tree.setProperty("limCeiling", -1.0, nullptr);
        }
        // Gate mode (mode=2)
        else if (presetName == "Tight Gate") {
            tree.setProperty("mode", 2, nullptr);
            tree.setProperty("gateThreshold", -40.0, nullptr);
            tree.setProperty("gateHold", 5.0, nullptr);
            tree.setProperty("gateRelease", 30.0, nullptr);
        }
        else if (presetName == "Noise Gate") {
            tree.setProperty("mode", 2, nullptr);
            tree.setProperty("gateThreshold", -60.0, nullptr);
            tree.setProperty("gateHold", 20.0, nullptr);
            tree.setProperty("gateRelease", 80.0, nullptr);
        }
        else return {};
        return tree;
    }

    //==========================================================================
    // EQModule (4 modes × 2 presets = 8)
    //==========================================================================
    if (moduleType == "EQModule")
    {
        auto tree = juce::ValueTree("EQModule");
        tree.setProperty("mix", 1.0, nullptr);
        tree.setProperty("wetLowCut", 20.0, nullptr);
        tree.setProperty("wetHighCut", 20000.0, nullptr);

        // Band3 mode (mode=0)
        if (presetName == "Scoop") {
            tree.setProperty("mode", 0, nullptr);
            tree.setProperty("lowGain", 3.0, nullptr);
            tree.setProperty("midGain", -4.0, nullptr);
            tree.setProperty("highGain", 2.0, nullptr);
            tree.setProperty("midFreq", 800.0, nullptr);
        }
        else if (presetName == "Smile EQ") {
            tree.setProperty("mode", 0, nullptr);
            tree.setProperty("lowGain", 4.0, nullptr);
            tree.setProperty("midGain", -6.0, nullptr);
            tree.setProperty("highGain", 4.0, nullptr);
            tree.setProperty("midFreq", 1000.0, nullptr);
        }
        // Band5 mode (mode=1)
        else if (presetName == "Full Mix") {
            tree.setProperty("mode", 1, nullptr);
            tree.setProperty("subGain", 1.0, nullptr);
            tree.setProperty("lowGain5", 0.5, nullptr);
            tree.setProperty("midGain5", -1.0, nullptr);
            tree.setProperty("highGain5", 1.5, nullptr);
            tree.setProperty("airGain", 2.0, nullptr);
        }
        else if (presetName == "Mastering 5") {
            tree.setProperty("mode", 1, nullptr);
            tree.setProperty("subGain", -0.5, nullptr);
            tree.setProperty("lowGain5", 0.0, nullptr);
            tree.setProperty("midGain5", 0.0, nullptr);
            tree.setProperty("highGain5", 0.5, nullptr);
            tree.setProperty("airGain", 1.0, nullptr);
            tree.setProperty("subFreq", 50.0, nullptr);
        }
        // Tilt mode (mode=2)
        else if (presetName == "Bright Tilt") {
            tree.setProperty("mode", 2, nullptr);
            tree.setProperty("tiltAmount", 6.0, nullptr);
            tree.setProperty("centerFreq", 1000.0, nullptr);
        }
        else if (presetName == "Dark Tilt") {
            tree.setProperty("mode", 2, nullptr);
            tree.setProperty("tiltAmount", -6.0, nullptr);
            tree.setProperty("centerFreq", 1000.0, nullptr);
        }
        // Para mode (mode=3)
        else if (presetName == "Surgical Mid") {
            tree.setProperty("mode", 3, nullptr);
            {
                auto band = juce::ValueTree("ParaBand");
                band.setProperty("index", 0, nullptr);
                band.setProperty("frequency", 400.0, nullptr);
                band.setProperty("gain", -6.0, nullptr);
                band.setProperty("q", 4.0, nullptr);
                band.setProperty("type", 1, nullptr); // Peaking
                tree.addChild(band, -1, nullptr);
            }
        }
        else if (presetName == "Wide Q Cut") {
            tree.setProperty("mode", 3, nullptr);
            {
                auto band = juce::ValueTree("ParaBand");
                band.setProperty("index", 0, nullptr);
                band.setProperty("frequency", 300.0, nullptr);
                band.setProperty("gain", -3.0, nullptr);
                band.setProperty("q", 0.5, nullptr);
                band.setProperty("type", 1, nullptr); // Peaking
                tree.addChild(band, -1, nullptr);
            }
        }
        else return {};
        return tree;
    }

    //==========================================================================
    // ModulationModule (3 modes × 2 presets = 6)
    //==========================================================================
    if (moduleType == "ModulationModule")
    {
        auto tree = juce::ValueTree("ModulationModule");
        tree.setProperty("mix", 1.0, nullptr);
        tree.setProperty("bypass", false, nullptr);

        // Chorus mode (mode=0)
        if (presetName == "Subtle Chorus") {
            tree.setProperty("mode", 0, nullptr);
            tree.setProperty("chorusRate", 0.5, nullptr);
            tree.setProperty("chorusDepth", 0.25, nullptr);
            tree.setProperty("chorusCentreDelay", 12.0, nullptr);
            tree.setProperty("mix", 0.35, nullptr);
        }
        else if (presetName == "Wide Ensemble") {
            tree.setProperty("mode", 0, nullptr);
            tree.setProperty("chorusRate", 1.5, nullptr);
            tree.setProperty("chorusDepth", 0.7, nullptr);
            tree.setProperty("chorusCentreDelay", 8.0, nullptr);
            tree.setProperty("mix", 0.55, nullptr);
        }
        // Flanger mode (mode=1)
        else if (presetName == "Slow Flange") {
            tree.setProperty("mode", 1, nullptr);
            tree.setProperty("flangerRate", 0.2, nullptr);
            tree.setProperty("flangerDepth", 0.5, nullptr);
            tree.setProperty("flangerFeedback", 0.3, nullptr);
            tree.setProperty("flangerDelay", 3.0, nullptr);
            tree.setProperty("mix", 0.5, nullptr);
        }
        else if (presetName == "Jet Flange") {
            tree.setProperty("mode", 1, nullptr);
            tree.setProperty("flangerRate", 1.5, nullptr);
            tree.setProperty("flangerDepth", 0.8, nullptr);
            tree.setProperty("flangerFeedback", 0.6, nullptr);
            tree.setProperty("flangerDelay", 1.5, nullptr);
            tree.setProperty("mix", 0.6, nullptr);
        }
        // Phaser mode (mode=2)
        else if (presetName == "Phase Sweep") {
            tree.setProperty("mode", 2, nullptr);
            tree.setProperty("phaserRate", 0.4, nullptr);
            tree.setProperty("phaserDepth", 0.6, nullptr);
            tree.setProperty("phaserFeedback", 0.3, nullptr);
            tree.setProperty("phaserStages", 6, nullptr);
            tree.setProperty("mix", 0.5, nullptr);
        }
        else if (presetName == "Phase Bubble") {
            tree.setProperty("mode", 2, nullptr);
            tree.setProperty("phaserRate", 2.0, nullptr);
            tree.setProperty("phaserDepth", 0.4, nullptr);
            tree.setProperty("phaserFeedback", 0.5, nullptr);
            tree.setProperty("phaserStages", 4, nullptr);
            tree.setProperty("mix", 0.6, nullptr);
        }
        else return {};
        return tree;
    }

    //==========================================================================
    // SpaceModule (5 modes × 2 presets = 10)
    //==========================================================================
    if (moduleType == "SpaceModule")
    {
        auto tree = juce::ValueTree("SpaceModule");
        tree.setProperty("mix", 1.0, nullptr);
        tree.setProperty("bypass", false, nullptr);

        // Room mode (mode=0)
        if (presetName == "Small Room") {
            tree.setProperty("mode", 0, nullptr);
            tree.setProperty("reverbSize", 0.2, nullptr);
            tree.setProperty("reverbDamping", 0.6, nullptr);
            tree.setProperty("reverbWidth", 0.4, nullptr);
            tree.setProperty("mix", 0.25, nullptr);
        }
        else if (presetName == "Live Room") {
            tree.setProperty("mode", 0, nullptr);
            tree.setProperty("reverbSize", 0.4, nullptr);
            tree.setProperty("reverbDamping", 0.3, nullptr);
            tree.setProperty("reverbWidth", 0.6, nullptr);
            tree.setProperty("mix", 0.35, nullptr);
        }
        // Hall mode (mode=1)
        else if (presetName == "Concert Hall") {
            tree.setProperty("mode", 1, nullptr);
            tree.setProperty("reverbSize", 0.8, nullptr);
            tree.setProperty("reverbDamping", 0.3, nullptr);
            tree.setProperty("reverbWidth", 0.8, nullptr);
            tree.setProperty("mix", 0.35, nullptr);
        }
        else if (presetName == "Cathedral") {
            tree.setProperty("mode", 1, nullptr);
            tree.setProperty("reverbSize", 1.0, nullptr);
            tree.setProperty("reverbDamping", 0.15, nullptr);
            tree.setProperty("reverbWidth", 1.0, nullptr);
            tree.setProperty("mix", 0.4, nullptr);
        }
        // Plate mode (mode=2)
        else if (presetName == "Plate Reverb") {
            tree.setProperty("mode", 2, nullptr);
            tree.setProperty("reverbSize", 0.65, nullptr);
            tree.setProperty("reverbDamping", 0.7, nullptr);
            tree.setProperty("reverbWidth", 0.7, nullptr);
            tree.setProperty("mix", 0.35, nullptr);
        }
        else if (presetName == "Vintage Plate") {
            tree.setProperty("mode", 2, nullptr);
            tree.setProperty("reverbSize", 0.55, nullptr);
            tree.setProperty("reverbDamping", 0.8, nullptr);
            tree.setProperty("reverbWidth", 0.65, nullptr);
            tree.setProperty("mix", 0.3, nullptr);
        }
        // Shimmer mode (mode=3)
        else if (presetName == "Shimmer Pad") {
            tree.setProperty("mode", 3, nullptr);
            tree.setProperty("reverbSize", 0.7, nullptr);
            tree.setProperty("reverbDamping", 0.3, nullptr);
            tree.setProperty("reverbWidth", 0.8, nullptr);
            tree.setProperty("shimmerShift", 12.0, nullptr);
            tree.setProperty("shimmerFeedback", 0.4, nullptr);
            tree.setProperty("mix", 0.5, nullptr);
        }
        else if (presetName == "Celestial") {
            tree.setProperty("mode", 3, nullptr);
            tree.setProperty("reverbSize", 0.9, nullptr);
            tree.setProperty("reverbDamping", 0.1, nullptr);
            tree.setProperty("reverbWidth", 1.0, nullptr);
            tree.setProperty("shimmerShift", 7.0, nullptr);
            tree.setProperty("shimmerFeedback", 0.6, nullptr);
            tree.setProperty("mix", 0.6, nullptr);
        }
        // Widener mode (mode=4)
        else if (presetName == "Natural Width") {
            tree.setProperty("mode", 4, nullptr);
            tree.setProperty("widenerWidth", 0.4, nullptr);
            tree.setProperty("mix", 0.5, nullptr);
        }
        else if (presetName == "Extreme Wide") {
            tree.setProperty("mode", 4, nullptr);
            tree.setProperty("widenerWidth", 0.9, nullptr);
            tree.setProperty("mix", 0.7, nullptr);
        }
        else return {};
        return tree;
    }

    //==========================================================================
    // PitchModule (4 modes × 2 presets = 8)
    //==========================================================================
    if (moduleType == "PitchModule")
    {
        auto tree = juce::ValueTree("PitchModule");
        tree.setProperty("mix", 1.0, nullptr);
        tree.setProperty("wetLowCut", 20.0, nullptr);
        tree.setProperty("wetHighCut", 20000.0, nullptr);
        tree.setProperty("scaleMask", 0x0FFF, nullptr); // chromatic

        // AutoTune mode (mode=0)
        if (presetName == "Soft Tune") {
            tree.setProperty("mode", 0, nullptr);
            tree.setProperty("retuneSpeed", 30.0, nullptr);
            tree.setProperty("amount", 0.4, nullptr);
        }
        else if (presetName == "Hard Tune") {
            tree.setProperty("mode", 0, nullptr);
            tree.setProperty("retuneSpeed", 5.0, nullptr);
            tree.setProperty("amount", 1.0, nullptr);
        }
        // PitchShift mode (mode=1)
        else if (presetName == "Octave Up") {
            tree.setProperty("mode", 1, nullptr);
            tree.setProperty("semitones", 12.0, nullptr);
            tree.setProperty("cents", 0.0, nullptr);
            tree.setProperty("window", 2048, nullptr);
            tree.setProperty("mix", 0.5, nullptr);
        }
        else if (presetName == "Fifth Up") {
            tree.setProperty("mode", 1, nullptr);
            tree.setProperty("semitones", 7.0, nullptr);
            tree.setProperty("cents", 0.0, nullptr);
            tree.setProperty("window", 2048, nullptr);
            tree.setProperty("mix", 0.5, nullptr);
        }
        // Harmonize mode (mode=2)
        else if (presetName == "Third Harmony") {
            tree.setProperty("mode", 2, nullptr);
            tree.setProperty("interval", 4.0, nullptr);
            tree.setProperty("harmonyMix", 0.5, nullptr);
            tree.setProperty("voices", 2, nullptr);
            tree.setProperty("mix", 0.6, nullptr);
        }
        else if (presetName == "Fifth Harmony") {
            tree.setProperty("mode", 2, nullptr);
            tree.setProperty("interval", 7.0, nullptr);
            tree.setProperty("harmonyMix", 0.6, nullptr);
            tree.setProperty("voices", 2, nullptr);
            tree.setProperty("mix", 0.7, nullptr);
        }
        // Formant mode (mode=3)
        else if (presetName == "Vocal Formant") {
            tree.setProperty("mode", 3, nullptr);
            tree.setProperty("formantShift", 0.0, nullptr);
            tree.setProperty("formantPreserve", 1.0, nullptr);
            tree.setProperty("mix", 0.5, nullptr);
        }
        else if (presetName == "Robot Formant") {
            tree.setProperty("mode", 3, nullptr);
            tree.setProperty("formantShift", 5.0, nullptr);
            tree.setProperty("formantPreserve", 0.2, nullptr);
            tree.setProperty("mix", 0.7, nullptr);
        }
        else return {};
        return tree;
    }

    return {};
}

//==============================================================================
// Factory rack presets
//==============================================================================

juce::ValueTree PresetFactory::createCleanRackPreset()
{
    juce::ValueTree root("AnaPlugPreset");
    root.setProperty("Name", "Clean Rack", nullptr);
    root.setProperty("Category", "Effects", nullptr);
    root.setProperty("Version", "2.0", nullptr);

    // Ordered effect chain: Delay → Reverb → EQ → Limiter
    // Uses the new format where child tag IS the type name
    {
        auto tree = juce::ValueTree("ConsolidatedDelay");
        tree.setProperty("mode", 0, nullptr);           // Mono
        tree.setProperty("timeMs", 120.0, nullptr);
        tree.setProperty("feedback", 0.2, nullptr);
        tree.setProperty("mix", 0.3, nullptr);
        tree.setProperty("wetHPF", 20.0, nullptr);
        tree.setProperty("wetLPF", 20000.0, nullptr);
        tree.setProperty("bypass", false, nullptr);
        tree.setProperty("windowLen", 200.0, nullptr);
        tree.setProperty("wowFlut", 0.0, nullptr);
        tree.setProperty("tone", 0.5, nullptr);
        tree.setProperty("threshold", -24.0, nullptr);
        tree.setProperty("duckRel", 200.0, nullptr);
        root.addChild(tree, -1, nullptr);
    }
    {
        auto tree = juce::ValueTree("SpaceModule");
        tree.setProperty("mode", 0, nullptr);           // Room
        tree.setProperty("reverbSize", 0.3, nullptr);
        tree.setProperty("reverbDamping", 0.5, nullptr);
        tree.setProperty("reverbWidth", 0.5, nullptr);
        tree.setProperty("shimmerShift", 12.0, nullptr);
        tree.setProperty("shimmerFeedback", 0.4, nullptr);
        tree.setProperty("widenerWidth", 0.5, nullptr);
        tree.setProperty("mix", 0.25, nullptr);
        tree.setProperty("bypass", false, nullptr);
        root.addChild(tree, -1, nullptr);
    }
    {
        auto tree = juce::ValueTree("EQModule");
        tree.setProperty("mode", 0, nullptr);           // Band3
        tree.setProperty("lowGain", 0.0, nullptr);
        tree.setProperty("midGain", 0.0, nullptr);
        tree.setProperty("highGain", 0.0, nullptr);
        tree.setProperty("midFreq", 1000.0, nullptr);
        tree.setProperty("subGain", 0.0, nullptr);
        tree.setProperty("subFreq", 60.0, nullptr);
        tree.setProperty("lowGain5", 0.0, nullptr);
        tree.setProperty("lowFreq5", 250.0, nullptr);
        tree.setProperty("midGain5", 0.0, nullptr);
        tree.setProperty("midFreq5", 1000.0, nullptr);
        tree.setProperty("highGain5", 0.0, nullptr);
        tree.setProperty("highFreq5", 5000.0, nullptr);
        tree.setProperty("airGain", 0.0, nullptr);
        tree.setProperty("airFreq", 12000.0, nullptr);
        tree.setProperty("tiltAmount", 0.0, nullptr);
        tree.setProperty("centerFreq", 1000.0, nullptr);
        tree.setProperty("mix", 1.0, nullptr);
        tree.setProperty("wetLowCut", 20.0, nullptr);
        tree.setProperty("wetHighCut", 20000.0, nullptr);
        root.addChild(tree, -1, nullptr);
    }
    {
        auto tree = juce::ValueTree("DynamicsModule");
        tree.setProperty("mode", 1, nullptr);           // Limiter
        tree.setProperty("compRatio", 4.0, nullptr);
        tree.setProperty("compThreshold", -24.0, nullptr);
        tree.setProperty("compAttack", 10.0, nullptr);
        tree.setProperty("compRelease", 100.0, nullptr);
        tree.setProperty("limThreshold", -3.0, nullptr);
        tree.setProperty("limRelease", 20.0, nullptr);
        tree.setProperty("limCeiling", -0.5, nullptr);
        tree.setProperty("gateThreshold", -60.0, nullptr);
        tree.setProperty("gateHold", 20.0, nullptr);
        tree.setProperty("gateRelease", 50.0, nullptr);
        tree.setProperty("mix", 1.0, nullptr);
        tree.setProperty("bypass", false, nullptr);
        root.addChild(tree, -1, nullptr);
    }

    return root;
}

juce::ValueTree PresetFactory::createCreativeRackPreset()
{
    juce::ValueTree root("AnaPlugPreset");
    root.setProperty("Name", "Creative Rack", nullptr);
    root.setProperty("Category", "Effects", nullptr);
    root.setProperty("Version", "2.0", nullptr);

    // Ordered effect chain: DriveModule → ModulationModule → SpaceModule → PitchModule
    {
        auto tree = juce::ValueTree("DriveModule");
        tree.setProperty("mode", 1, nullptr);           // Tube
        tree.setProperty("drive", 0.4, nullptr);
        tree.setProperty("tone", 0.6, nullptr);
        tree.setProperty("mix", 0.7, nullptr);
        tree.setProperty("wetHPF", 20.0, nullptr);
        tree.setProperty("wetLPF", 20000.0, nullptr);
        tree.setProperty("bypass", false, nullptr);
        tree.setProperty("gain", 1.0, nullptr);
        root.addChild(tree, -1, nullptr);
    }
    {
        auto tree = juce::ValueTree("ModulationModule");
        tree.setProperty("mode", 0, nullptr);           // Chorus
        tree.setProperty("chorusRate", 1.0, nullptr);
        tree.setProperty("chorusDepth", 0.5, nullptr);
        tree.setProperty("chorusCentreDelay", 10.0, nullptr);
        tree.setProperty("flangerRate", 0.5, nullptr);
        tree.setProperty("flangerDepth", 0.5, nullptr);
        tree.setProperty("flangerFeedback", 0.3, nullptr);
        tree.setProperty("flangerDelay", 3.0, nullptr);
        tree.setProperty("phaserRate", 1.0, nullptr);
        tree.setProperty("phaserDepth", 0.5, nullptr);
        tree.setProperty("phaserFeedback", 0.3, nullptr);
        tree.setProperty("phaserStages", 6, nullptr);
        tree.setProperty("mix", 0.4, nullptr);
        tree.setProperty("bypass", false, nullptr);
        root.addChild(tree, -1, nullptr);
    }
    {
        auto tree = juce::ValueTree("SpaceModule");
        tree.setProperty("mode", 2, nullptr);           // Plate
        tree.setProperty("reverbSize", 0.65, nullptr);
        tree.setProperty("reverbDamping", 0.6, nullptr);
        tree.setProperty("reverbWidth", 0.7, nullptr);
        tree.setProperty("shimmerShift", 12.0, nullptr);
        tree.setProperty("shimmerFeedback", 0.4, nullptr);
        tree.setProperty("widenerWidth", 0.5, nullptr);
        tree.setProperty("mix", 0.3, nullptr);
        tree.setProperty("bypass", false, nullptr);
        root.addChild(tree, -1, nullptr);
    }
    {
        auto tree = juce::ValueTree("PitchModule");
        tree.setProperty("mode", 1, nullptr);           // PitchShift
        tree.setProperty("retuneSpeed", 50.0, nullptr);
        tree.setProperty("amount", 1.0, nullptr);
        tree.setProperty("scaleMask", 0x0FFF, nullptr);
        tree.setProperty("semitones", 7.0, nullptr);
        tree.setProperty("cents", 0.0, nullptr);
        tree.setProperty("window", 2048, nullptr);
        tree.setProperty("interval", 7.0, nullptr);
        tree.setProperty("harmonyMix", 0.5, nullptr);
        tree.setProperty("voices", 2, nullptr);
        tree.setProperty("formantShift", 0.0, nullptr);
        tree.setProperty("formantPreserve", 1.0, nullptr);
        tree.setProperty("mix", 0.5, nullptr);
        tree.setProperty("wetLowCut", 20.0, nullptr);
        tree.setProperty("wetHighCut", 20000.0, nullptr);
        root.addChild(tree, -1, nullptr);
    }

    return root;
}

} // namespace ana
