#include "AudioToMidiConverter.h"

namespace ana {

AudioToMidiConverter::AudioToMidiConverter()
{
}

void AudioToMidiConverter::setSampleRate(double sr)
{
    sampleRate_ = sr > 0.0 ? sr : 44100.0;
    processor_.setSampleRate(sampleRate_);
    slicer_.setSampleRate(sampleRate_);
}

std::vector<MidiNoteEvent> AudioToMidiConverter::convert(const std::vector<float>& audioData, float sensitivity)
{
    std::vector<MidiNoteEvent> result;
    if (audioData.empty()) return result;

    // 1. Slice audio into transients
    auto slices = slicer_.slice(audioData, sensitivity);

    // 2. Detect pitch for each slice
    for (const auto& slice : slices)
    {
        int length = slice.endSample - slice.startSample + 1;
        if (length < 256) continue; // Too short to detect pitch reliably

        // Extract slice data
        std::vector<float> sliceData(audioData.begin() + slice.startSample, 
                                     audioData.begin() + slice.endSample + 1);

        // Detect pitch
        auto pitchResult = processor_.detectPitch(sliceData, sampleRate_);

        // If confidence is decent, add a note event
        if (pitchResult.confidence > 0.3f && pitchResult.detectedMidiNote > 0)
        {
            MidiNoteEvent ev;
            ev.note = pitchResult.detectedMidiNote;
            
            // Map energy/peak to velocity [0.0, 1.0] (very basic mapping)
            float vel = slice.peakAmplitude;
            ev.velocity = std::min(1.0f, vel * 1.5f); 
            
            ev.startTimeSeconds = static_cast<double>(slice.startSample) / sampleRate_;
            ev.durationSeconds = static_cast<double>(length) / sampleRate_;
            ev.centsOffset = pitchResult.detectedCents;

            result.push_back(ev);
        }
    }

    return result;
}

} // namespace ana
