#include <catch2/catch_all.hpp>
#include "dsp/ArpeggiatorDriver.h"

#include <vector>

// ---------------------------------------------------------------------------
// The driver is what turns the Arpeggiator's stepped output into MIDI, and it is
// the piece the plugin actually runs - the engine tests next door never exercise
// the buffer rewrite, the pass-through or the release-on-disable path.
// ---------------------------------------------------------------------------

static constexpr double TEST_SR = 44100.0;
static constexpr int    BLOCK   = 128;

namespace
{

/** Runs one block through the driver and hands back the resulting events. */
juce::MidiBuffer runBlock(ana::ArpeggiatorDriver& driver, juce::MidiBuffer input, int numSamples = BLOCK)
{
    driver.process(input, numSamples);
    return input;
}

struct NoteEvent
{
    bool isOn = false;
    int  note = 0;
    int  position = 0;
};

std::vector<NoteEvent> collectNotes(const juce::MidiBuffer& buffer)
{
    std::vector<NoteEvent> events;

    for (const auto metadata : buffer)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
            events.push_back({ true, message.getNoteNumber(), metadata.samplePosition });
        else if (message.isNoteOff())
            events.push_back({ false, message.getNoteNumber(), metadata.samplePosition });
    }

    return events;
}

/** A driver prepared at the test sample rate with the arp switched on. */
ana::ArpeggiatorDriver makeDriver()
{
    ana::ArpeggiatorDriver driver;
    driver.prepare(TEST_SR);
    driver.setTempo(120.0);
    driver.setRate(0.25f);      // 1/16 notes = 5512.5 samples per step
    driver.setGate(50.0f);      // half a step, so consecutive equal notes retrigger
    driver.setEnabled(true);
    return driver;
}

} // namespace

// ---------------------------------------------------------------------------

TEST_CASE("ArpeggiatorDriver - disabled driver passes MIDI through untouched", "[arp][driver]")
{
    ana::ArpeggiatorDriver driver;
    driver.prepare(TEST_SR);
    driver.setEnabled(false);

    juce::MidiBuffer input;
    input.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    input.addEvent(juce::MidiMessage::controllerEvent(1, 1, 64), 10);
    input.addEvent(juce::MidiMessage::noteOff(1, 60), 40);

    const auto output = runBlock(driver, input);

    // MidiBuffer has had no size() since JUCE 7; getNumEvents() is the count.
    REQUIRE(output.getNumEvents() == 3);

    const auto notes = collectNotes(output);
    REQUIRE(notes.size() == 2);
    REQUIRE(notes[0].isOn);
    REQUIRE(notes[0].note == 60);
    REQUIRE_FALSE(notes[1].isOn);
    REQUIRE(notes[1].note == 60);

    // The controller message must survive, position included.
    bool sawController = false;
    for (const auto metadata : output)
        if (metadata.getMessage().isController())
        {
            sawController = true;
            REQUIRE(metadata.samplePosition == 10);
        }

    REQUIRE(sawController);
}

TEST_CASE("ArpeggiatorDriver - a held chord becomes one stepped note at a time", "[arp][driver]")
{
    auto driver = makeDriver();

    juce::MidiBuffer input;
    input.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    input.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 0);
    input.addEvent(juce::MidiMessage::noteOn(1, 67, 1.0f), 0);

    // First block: the arp triggers its first step immediately, and the raw chord
    // must NOT reach the synth.
    const auto first = runBlock(driver, input);
    const auto firstNotes = collectNotes(first);

    REQUIRE(firstNotes.size() == 1);
    REQUIRE(firstNotes[0].isOn);

    const int firstNote = firstNotes[0].note;
    REQUIRE((firstNote == 60 || firstNote == 64 || firstNote == 67));
    REQUIRE(driver.getSoundingNote() == firstNote);

    // Step through ~2 steps worth of audio in small blocks and check the sequence
    // walks the chord in order (Up mode) with a note-off before every change.
    std::vector<int> sounded { firstNote };
    int lastSounding = firstNote;

    // 300 blocks of 128 samples is ~7 steps at 120 bpm and 1/16 notes.
    for (int block = 0; block < 300; ++block)
    {
        const auto out = runBlock(driver, {});
        const auto notes = collectNotes(out);

        for (const auto& e : notes)
        {
            if (!e.isOn)
            {
                REQUIRE(e.note == lastSounding);
            }
            else
            {
                sounded.push_back(e.note);
                lastSounding = e.note;
            }
        }
    }

    REQUIRE(sounded.size() >= 4);

    // Up mode walks the chord from the lowest note and wraps back to it.
    REQUIRE(sounded[0] == 60);
    REQUIRE(sounded[1] == 64);
    REQUIRE(sounded[2] == 67);

    for (size_t i = 0; i < sounded.size(); ++i)
        REQUIRE(sounded[i] == sounded[i % 3]);
}

TEST_CASE("ArpeggiatorDriver - switching the arp off releases the held note", "[arp][driver]")
{
    auto driver = makeDriver();

    juce::MidiBuffer input;
    input.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);

    runBlock(driver, input);
    REQUIRE(driver.getSoundingNote() == 72);

    driver.setEnabled(false);

    const auto output = runBlock(driver, {});
    const auto notes = collectNotes(output);

    REQUIRE(notes.size() == 1);
    REQUIRE_FALSE(notes[0].isOn);
    REQUIRE(notes[0].note == 72);
    REQUIRE(driver.getSoundingNote() == -1);

    // And it stays off: the next block is a pure pass-through.
    juce::MidiBuffer later;
    later.addEvent(juce::MidiMessage::noteOn(1, 50, 1.0f), 3);
    const auto laterOut = runBlock(driver, later);
    REQUIRE(laterOut.getNumEvents() == 1);
    REQUIRE(driver.getSoundingNote() == -1);
}

TEST_CASE("ArpeggiatorDriver - all notes off forgets the chord and stops the note", "[arp][driver]")
{
    auto driver = makeDriver();

    juce::MidiBuffer input;
    input.addEvent(juce::MidiMessage::noteOn(1, 62, 1.0f), 0);
    runBlock(driver, input);
    REQUIRE(driver.getSoundingNote() == 62);
    REQUIRE(driver.getNumHeldNotes() == 1);

    juce::MidiBuffer panic;
    panic.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    const auto out = runBlock(driver, panic);

    REQUIRE(driver.getSoundingNote() == -1);
    REQUIRE(driver.getNumHeldNotes() == 0);

    const auto notes = collectNotes(out);
    REQUIRE(notes.size() == 1);
    REQUIRE_FALSE(notes[0].isOn);

    // The panic message itself still reaches the synth.
    bool sawAllNotesOff = false;
    for (const auto metadata : out)
        if (metadata.getMessage().isAllNotesOff())
            sawAllNotesOff = true;

    REQUIRE(sawAllNotesOff);
}

TEST_CASE("ArpeggiatorDriver - gate shorter than a step retriggers the same pitch", "[arp][driver]")
{
    auto driver = makeDriver();

    // A single held note repeats every step; with a 50% gate there must be a
    // note-off between two note-ons, or the retrigger would be silent.
    juce::MidiBuffer input;
    input.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);

    std::vector<bool> onOff;

    for (int block = 0; block < 200; ++block)
        for (const auto& e : collectNotes(runBlock(driver, {})))
            onOff.push_back(e.isOn);

    REQUIRE(onOff.size() >= 3);

    for (size_t i = 1; i < onOff.size(); ++i)
        REQUIRE(onOff[i] != onOff[i - 1]);
}

TEST_CASE("ArpeggiatorDriver - reset stops everything", "[arp][driver]")
{
    auto driver = makeDriver();

    juce::MidiBuffer input;
    input.addEvent(juce::MidiMessage::noteOn(1, 55, 1.0f), 0);
    runBlock(driver, input);

    driver.reset();
    REQUIRE(driver.getSoundingNote() == -1);
    REQUIRE(driver.getNumHeldNotes() == 0);

    // After a reset the next block emits nothing (there is nothing held).
    REQUIRE(runBlock(driver, {}).isEmpty());
}
