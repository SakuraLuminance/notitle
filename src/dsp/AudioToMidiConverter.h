#pragma once
#include <vector>
#include <string>
#include "SampleProcessor.h"
#include "SampleSlicer.h"

namespace ana {

struct MidiNoteEvent {
    int note = 60;
    float velocity = 1.0f;
    double startTimeSeconds = 0.0;
    double durationSeconds = 0.0;
    float centsOffset = 0.0f;
};

class AudioToMidiConverter {
public:
    AudioToMidiConverter();
    ~AudioToMidiConverter() = default;

    void setSampleRate(double sr);

    /**
        Convert mono audio buffer to a sequence of MIDI notes.
        Uses SampleSlicer to find note boundaries, then SampleProcessor to 
        detect the pitch of each slice.
        
        @param audioData Mono audio
        @param sensitivity Transient sensitivity (0.0 to 1.0)
        @return A vector of detected MIDI note events
    */
    std::vector<MidiNoteEvent> convert(const std::vector<float>& audioData, float sensitivity = 0.5f);

private:
    double sampleRate_ = 44100.0;
    SampleProcessor processor_;
    SampleSlicer slicer_;
};

} // namespace ana
