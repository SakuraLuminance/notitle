#pragma once
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

namespace ana {

enum class ArpMode
{
    Up,          // Lowest to highest
    Down,        // Highest to lowest
    UpDown,      // Up then Down
    Random,      // Random note from held
    AsPlayed,    // Order notes were pressed
    Pattern      // 16-step pattern
};

struct ArpStep
{
    bool active = false;
    float velocity = 1.0f;
    float gate = 0.5f;  // 0.0-1.0 as fraction of step
};

class Arpeggiator
{
public:
    Arpeggiator();
    ~Arpeggiator() = default;

    void prepare(double sampleRate);
    void reset();

    // Input
    void noteOn(int note, float velocity);
    void noteOff(int note);

    // Configuration
    void setMode(ArpMode mode);
    void setRate(float beats);        // Note division: 1.0=quarter, 0.5=eighth
    void setGate(float percent);      // 10-200%
    void setOctaveRange(int octaves); // 1-4
    void setSwing(float percent);     // 0-100%
    void setTempo(double bpm);

    // Pattern editing
    void setStep(int index, bool active, float velocity = 1.0f, float gate = 0.5f);
    const ArpStep& getStep(int index) const;

    // Processing - call every sample
    void process(int numSamples);
    
    // Output
    int getCurrentNote() const;      // -1 = rest
    float getCurrentVelocity() const;
    bool isNoteActive() const;

    // State
    int getNumHeldNotes() const;
    std::vector<int> getHeldNotes() const;
    bool isPlaying() const;

private:
    void advanceStep();
    int getNoteForStep(int stepIndex) const;
    void rebuildArpSequence();
    double getCurrentStepDuration() const;

    double sampleRate = 44100.0;
    double bpm = 120.0;

    ArpMode mode = ArpMode::Up;
    float rateBeats = 0.25f;  // 16th notes default
    float gatePercent = 50.0f;
    int octaveRange = 1;
    float swingPercent = 0.0f;

    // Held notes (chord memory)
    std::vector<int> heldNotes;
    std::vector<int> noteOrder;  // For AsPlayed mode
    std::vector<int> arpSequence;

    // 16-step pattern
    ArpStep steps[16];

    // Playback state
    int currentStep = 0;
    double sampleCounter = 0.0;
    double stepDurationSamples = 0.0;
    int noteInStep = 0;  // For octave range cycling
    int direction = 1;   // For UpDown mode

    bool playing = false;
    bool noteActive = false;
    int currentNote = -1;
    float currentVelocity = 0.0f;
    double gateCounter = 0.0;
    double gateDurationSamples = 0.0;
};

} // namespace ana
