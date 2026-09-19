#include "ArpeggiatorDriver.h"

#include <cmath>

namespace ana
{

ArpeggiatorDriver::ArpeggiatorDriver() = default;

void ArpeggiatorDriver::prepare(double sampleRate)
{
    arp_.prepare(sampleRate);
    applyConfiguration();
    configDirty_ = false;
    reset();
}

void ArpeggiatorDriver::reset()
{
    arp_.reset();
    soundingNote_ = -1;
    wasEnabled_ = false;
}

void ArpeggiatorDriver::applyConfiguration()
{
    arp_.setMode(mode_);
    arp_.setRate(rateBeats_);
    arp_.setGate(gatePercent_);
    arp_.setTempo(tempo_);
    arp_.setOctaveRange(octaveRange_);
    arp_.setSwing(swing_);
}

void ArpeggiatorDriver::process(juce::MidiBuffer& buffer, int numSamples, int midiChannel)
{
    // Nothing to do while the arp is off and was off: the host's MIDI goes
    // straight through and nothing is allocated.
    if (!enabled_ && !wasEnabled_)
        return;

    if (configDirty_)
    {
        applyConfiguration();
        configDirty_ = false;
    }

    scratch_.clear();

    // ---- 1. held notes in, everything else straight through ----------------
    for (const auto metadata : buffer)
    {
        const auto message = metadata.getMessage();

        if (enabled_)
        {
            if (message.isNoteOn())
            {
                arp_.noteOn(message.getNoteNumber(), message.getFloatVelocity());
                continue;
            }

            if (message.isNoteOff())
            {
                arp_.noteOff(message.getNoteNumber());
                continue;
            }

            if (message.isAllNotesOff() || message.isAllSoundOff())
            {
                // The arp must forget the chord as well, or the next block would
                // keep stepping through notes the host has already cancelled.
                if (soundingNote_ >= 0)
                {
                    scratch_.addEvent(juce::MidiMessage::noteOff(midiChannel, soundingNote_),
                                      metadata.samplePosition);
                    soundingNote_ = -1;
                }

                arp_.reset();
            }
        }

        scratch_.addEvent(message, metadata.samplePosition);
    }

    // ---- 2. leaving arp mode releases whatever it was still holding --------
    if (!enabled_)
    {
        if (soundingNote_ >= 0)
        {
            scratch_.addEvent(juce::MidiMessage::noteOff(midiChannel, soundingNote_), 0);
            soundingNote_ = -1;
        }

        arp_.reset();
        wasEnabled_ = false;
        buffer.swapWith(scratch_);
        return;
    }

    // ---- 3. advance the step clock and emit the notes it produces ----------
    for (int offset = 0; offset < numSamples; offset += chunkSize_)
    {
        const int chunk = juce::jmin(chunkSize_, numSamples - offset);

        arp_.process(chunk);

        const bool active = arp_.isNoteActive();
        const int note = arp_.getCurrentNote();

        if (active && note >= 0 && note != soundingNote_)
        {
            if (soundingNote_ >= 0)
                scratch_.addEvent(juce::MidiMessage::noteOff(midiChannel, soundingNote_), offset);

            const int velocity = juce::jlimit(1, 127,
                                              static_cast<int>(std::lround(arp_.getCurrentVelocity() * 127.0f)));

            scratch_.addEvent(juce::MidiMessage::noteOn(midiChannel, note,
                                                        static_cast<juce::uint8>(velocity)),
                              offset);
            soundingNote_ = note;
        }
        else if (!active && soundingNote_ >= 0)
        {
            // The gate closed before the next step: release the note so the next
            // step (even at the same pitch) retriggers cleanly.
            scratch_.addEvent(juce::MidiMessage::noteOff(midiChannel, soundingNote_), offset);
            soundingNote_ = -1;
        }
    }

    buffer.swapWith(scratch_);
    wasEnabled_ = true;
}

} // namespace ana
