#pragma once

#include "Arpeggiator.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace ana
{

/**
    Drives an Arpeggiator from a live MIDI buffer.

    The Arpeggiator itself is a note-level engine: it keeps held notes, advances a
    step clock and reports the note that should sound.  Turning that into a MIDI
    stream - and, just as importantly, not leaking a hanging note when the mode
    changes - is what this class does, so it can be tested without a plugin host.

    Enabled:   incoming note-ons and note-offs only feed the arp; everything else
               (CC, pitch bend, channel pressure, all-notes-off) passes through,
               and the arp's stepped notes are emitted in their place.
    Disabled:  the buffer is untouched, except for the note-off that releases the
               note the arp was still holding when it was switched off.

    The block is advanced in small chunks rather than one call per block, so step
    timing is not quantised to the host's buffer size.
*/
class ArpeggiatorDriver
{
public:
    ArpeggiatorDriver();

    void prepare (double sampleRate);
    void reset();

    void setEnabled (bool shouldBeEnabled) noexcept  { enabled_ = shouldBeEnabled; }
    bool isEnabled() const noexcept                  { return enabled_; }

    void setMode (ArpMode mode) noexcept             { mode_ = mode; configDirty_ = true; }
    void setRate (float beats) noexcept              { rateBeats_ = beats; configDirty_ = true; }
    void setGate (float percent) noexcept            { gatePercent_ = percent; configDirty_ = true; }
    void setTempo (double bpm) noexcept              { tempo_ = bpm; configDirty_ = true; }
    void setOctaveRange (int octaves) noexcept       { octaveRange_ = octaves; configDirty_ = true; }
    void setSwing (float percent) noexcept           { swing_ = percent; configDirty_ = true; }

    ArpMode getMode() const noexcept                 { return mode_; }

    /** Chunk size used to advance the step clock inside one block.  Smaller means
        tighter step timing but more work per block. */
    void setChunkSize (int samples) noexcept         { chunkSize_ = juce::jmax (1, samples); }

    /** Rewrites @a buffer in place (see the class comment). */
    void process (juce::MidiBuffer& buffer, int numSamples, int midiChannel = 1);

    /** Pitch the driver is currently sounding, or -1.  For tests and diagnostics. */
    int getSoundingNote() const noexcept             { return soundingNote_; }

    /** Notes the arp is holding. */
    int getNumHeldNotes() const noexcept             { return arp_.getNumHeldNotes(); }

private:
    void applyConfiguration();

    Arpeggiator arp_;
    juce::MidiBuffer scratch_;

    bool enabled_ = false;
    bool wasEnabled_ = false;
    bool configDirty_ = false;

    ArpMode mode_ = ArpMode::Up;
    float rateBeats_ = 0.25f;
    float gatePercent_ = 50.0f;
    double tempo_ = 120.0;
    int octaveRange_ = 1;
    float swing_ = 0.0f;

    int soundingNote_ = -1;
    int chunkSize_ = 32;
};

} // namespace ana
