#pragma once
#include <vector>
#include <mutex>
#include <juce_audio_basics/juce_audio_basics.h>

namespace ana {

//==============================================================================
/**
    Represents a note waiting to be triggered by the StrumManager.
*/
struct StrumEvent {
    int note = 0;
    float velocity = 0.0f;
    double samplesRemaining = 0.0;
};

//==============================================================================
/**
    StrumManager intercepts simultaneous or near-simultaneous MIDI notes
    (chords) and staggers their playback, simulating a guitar strum.
*/
class StrumManager {
public:
    StrumManager();
    ~StrumManager() = default;

    enum class StrumDirection {
        Up,      // Lowest note first
        Down,    // Highest note first
        Alt      // Alternates Up and Down per chord
    };

    void prepare(double sampleRate);
    void reset();

    //==============================================================================
    /** Configuration */
    void setStrumTime(float timeMs);
    void setDirection(StrumDirection dir);
    void setEnabled(bool shouldBeEnabled);

    //==============================================================================
    /** 
        Call this when a note-on arrives.
        If enabled, it may queue the note. If it queues it, returns true.
        If it returns false, the caller should trigger the note immediately.
    */
    bool noteOn(int note, float velocity);

    /** Call this to clear a note from the queue if a noteOff arrives early. */
    void noteOff(int note);

    /**
        Advance time by numSamples. 
        Any notes whose delay has expired are added to outReadyNotes.
    */
    void process(int numSamples, std::vector<StrumEvent>& outReadyNotes);

private:
    double sampleRate_ = 44100.0;
    float strumTimeMs_ = 30.0f;
    StrumDirection direction_ = StrumDirection::Up;
    bool enabled_ = false;

    // State
    bool lastWasUp_ = false;
    double currentChordTime_ = 0.0; // Time since the start of the current chord
    int chordNoteCount_ = 0;        // How many notes are in the current chord so far

    // Buffer of pending notes
    std::vector<StrumEvent> pendingNotes_;
    std::mutex mutex_;
};

} // namespace ana
