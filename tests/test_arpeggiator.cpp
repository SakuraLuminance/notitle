#include <catch2/catch_all.hpp>
#include "dsp/Arpeggiator.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr double TEST_SR = 44100.0;

/**
 * Process enough samples for the arpeggiator to advance through
 * the given number of steps at the configured rate.
 */
static void advanceSteps(ana::Arpeggiator& arp, int steps)
{
    if (steps <= 0)
        return;

    // Process a generous number of samples to ensure we hit all steps.
    // Each step at 120 bpm / 16th note is ~5512 samples at 44.1 kHz.
    const int samplesPerStep = static_cast<int>(5512.5);
    for (int i = 0; i < steps * 3; ++i)
        arp.process(samplesPerStep);
}

// ---------------------------------------------------------------------------
// Note on/off
// ---------------------------------------------------------------------------

TEST_CASE("Arpeggiator - note on/off adds/removes notes", "[arp][notes]")
{
    ana::Arpeggiator arp;
    arp.prepare(TEST_SR);

    SECTION("starts with no held notes")
    {
        REQUIRE(arp.getNumHeldNotes() == 0);
        REQUIRE_FALSE(arp.isPlaying());
    }

    SECTION("noteOn adds a held note")
    {
        arp.noteOn(60, 1.0f);
        REQUIRE(arp.getNumHeldNotes() == 1);
        REQUIRE(arp.isPlaying());
    }

    SECTION("multiple noteOn adds accumulate")
    {
        arp.noteOn(60, 1.0f);
        arp.noteOn(64, 1.0f);
        arp.noteOn(67, 1.0f);
        REQUIRE(arp.getNumHeldNotes() == 3);
    }

    SECTION("duplicate noteOn is idempotent")
    {
        arp.noteOn(60, 1.0f);
        arp.noteOn(60, 0.5f);
        REQUIRE(arp.getNumHeldNotes() == 1);
    }

    SECTION("noteOff removes a held note")
    {
        arp.noteOn(60, 1.0f);
        arp.noteOn(64, 1.0f);
        REQUIRE(arp.getNumHeldNotes() == 2);

        arp.noteOff(60);
        REQUIRE(arp.getNumHeldNotes() == 1);
    }

    SECTION("noteOff of last note stops playing")
    {
        arp.noteOn(60, 1.0f);
        REQUIRE(arp.isPlaying());

        arp.noteOff(60);
        REQUIRE_FALSE(arp.isPlaying());
        REQUIRE(arp.getNumHeldNotes() == 0);
    }

    SECTION("noteOff of unknown note does nothing")
    {
        arp.noteOn(60, 1.0f);
        REQUIRE(arp.getNumHeldNotes() == 1);

        arp.noteOff(99);
        REQUIRE(arp.getNumHeldNotes() == 1);
    }
}

// ---------------------------------------------------------------------------
// Up mode
// ---------------------------------------------------------------------------

TEST_CASE("Arpeggiator - Up mode plays notes ascending", "[arp][mode]")
{
    ana::Arpeggiator arp;
    arp.prepare(TEST_SR);
    arp.setMode(ana::ArpMode::Up);
    arp.setRate(0.25f);   // 16th notes
    arp.setTempo(120.0);

    arp.noteOn(60, 1.0f);
    arp.noteOn(64, 1.0f);
    arp.noteOn(67, 1.0f);

    // Held notes are 60, 64, 67 -> sequence should be 60, 64, 67, wrap
    advanceSteps(arp, 1);

    SECTION("first step is the lowest note")
    {
        REQUIRE(arp.getCurrentNote() == 60);
    }

    SECTION("second step is the middle note")
    {
        advanceSteps(arp, 1);
        REQUIRE(arp.getCurrentNote() == 64);
    }

    SECTION("third step is the highest note")
    {
        advanceSteps(arp, 2);
        REQUIRE(arp.getCurrentNote() == 67);
    }

    SECTION("fourth step wraps back to lowest")
    {
        advanceSteps(arp, 3);
        REQUIRE(arp.getCurrentNote() == 60);
    }
}

// ---------------------------------------------------------------------------
// Down mode
// ---------------------------------------------------------------------------

TEST_CASE("Arpeggiator - Down mode plays notes descending", "[arp][mode]")
{
    ana::Arpeggiator arp;
    arp.prepare(TEST_SR);
    arp.setMode(ana::ArpMode::Down);
    arp.setRate(0.25f);
    arp.setTempo(120.0);

    arp.noteOn(60, 1.0f);
    arp.noteOn(64, 1.0f);
    arp.noteOn(67, 1.0f);

    SECTION("first step is the highest note")
    {
        advanceSteps(arp, 1);
        REQUIRE(arp.getCurrentNote() == 67);
    }

    SECTION("second step is the middle note")
    {
        advanceSteps(arp, 2);
        REQUIRE(arp.getCurrentNote() == 64);
    }

    SECTION("third step is the lowest note")
    {
        advanceSteps(arp, 3);
        REQUIRE(arp.getCurrentNote() == 60);
    }
}

// ---------------------------------------------------------------------------
// Octave range
// ---------------------------------------------------------------------------

TEST_CASE("Arpeggiator - octave range extends notes across octaves", "[arp][octave]")
{
    ana::Arpeggiator arp;
    arp.prepare(TEST_SR);
    arp.setMode(ana::ArpMode::Up);
    arp.setRate(0.25f);
    arp.setTempo(120.0);
    arp.setOctaveRange(2); // Two octaves

    arp.noteOn(60, 1.0f);
    arp.noteOn(64, 1.0f);

    // Sequence with octaveRange=2: 60, 64, 72, 76, wrap
    SECTION("first octave notes come first")
    {
        advanceSteps(arp, 1);
        REQUIRE(arp.getCurrentNote() == 60);

        advanceSteps(arp, 1);
        REQUIRE(arp.getCurrentNote() == 64);
    }

    SECTION("second octave notes follow")
    {
        advanceSteps(arp, 3); // advance past 60, 64, back to 60 -> then 72
        REQUIRE(arp.getCurrentNote() == 72);
    }

    SECTION("octave range of 1 gives no transposition")
    {
        arp.setOctaveRange(1);
        advanceSteps(arp, 1);
        REQUIRE(arp.getCurrentNote() == 60);
        advanceSteps(arp, 1);
        REQUIRE(arp.getCurrentNote() == 64);
        // No third note at octave +12
        advanceSteps(arp, 1);
        REQUIRE(arp.getCurrentNote() == 60);
    }

    SECTION("octave range of 3 extends further")
    {
        arp.setOctaveRange(3);
        advanceSteps(arp, 5); // 60, 64, 72, 76, 84
        REQUIRE(arp.getCurrentNote() == 84);
    }
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST_CASE("Arpeggiator - reset clears state", "[arp][state]")
{
    ana::Arpeggiator arp;
    arp.prepare(TEST_SR);

    SECTION("reset after adding notes clears everything")
    {
        arp.noteOn(60, 1.0f);
        arp.noteOn(64, 1.0f);
        arp.process(1024);
        REQUIRE(arp.isPlaying());
        REQUIRE(arp.getNumHeldNotes() == 2);

        arp.reset();

        REQUIRE(arp.getNumHeldNotes() == 0);
        REQUIRE_FALSE(arp.isPlaying());
        REQUIRE(arp.getCurrentNote() == -1);
        REQUIRE_FALSE(arp.isNoteActive());
    }

    SECTION("reset on fresh arpeggiator is safe")
    {
        REQUIRE_NOTHROW(arp.reset());
        REQUIRE(arp.getNumHeldNotes() == 0);
        REQUIRE_FALSE(arp.isPlaying());
    }

    SECTION("reset twice is safe")
    {
        arp.noteOn(60, 1.0f);
        arp.reset();
        arp.reset();
        REQUIRE(arp.getNumHeldNotes() == 0);
    }
}

// ---------------------------------------------------------------------------
// Velocity and gate output
// ---------------------------------------------------------------------------

TEST_CASE("Arpeggiator - gate and velocity behavior", "[arp][gate]")
{
    ana::Arpeggiator arp;
    arp.prepare(TEST_SR);
    arp.setMode(ana::ArpMode::Up);
    arp.setRate(0.25f);
    arp.setTempo(120.0);
    arp.setGate(50.0f); // 50% gate

    arp.noteOn(60, 1.0f);

    // Step duration at 120 bpm / 16th: ~5512.5 samples
    // Gate at 50%: ~2756 samples

    SECTION("note is active at start of step")
    {
        arp.process(1);
        REQUIRE(arp.isNoteActive());
        REQUIRE(arp.getCurrentNote() == 60);
    }

    SECTION("note turns off after gate duration")
    {
        // Process samples past the gate
        int gateSamps = static_cast<int>(5512.5 * 0.5) + 10;
        arp.process(gateSamps);
        REQUIRE_FALSE(arp.isNoteActive());
    }

    SECTION("velocity reflects setting")
    {
        arp.setStep(0, true, 0.75f, 0.5f);
        arp.setMode(ana::ArpMode::Pattern);
        arp.process(1);
        REQUIRE(arp.getCurrentVelocity() == Approx(0.75f).margin(0.01f));
    }
}

// ---------------------------------------------------------------------------
// Pattern mode
// ---------------------------------------------------------------------------

TEST_CASE("Arpeggiator - Pattern mode step control", "[arp][pattern]")
{
    ana::Arpeggiator arp;
    arp.prepare(TEST_SR);
    arp.setMode(ana::ArpMode::Pattern);
    arp.setRate(0.25f);
    arp.setTempo(120.0);

    arp.noteOn(60, 1.0f);
    arp.noteOn(64, 1.0f);

    // Disable every other step
    for (int i = 1; i < 16; i += 2)
        arp.setStep(i, false, 1.0f, 0.5f);
    arp.setStep(0, true, 0.8f, 0.5f);

    SECTION("step is active and plays the correct note")
    {
        arp.process(1);
        REQUIRE(arp.isNoteActive());
        REQUIRE(arp.getCurrentVelocity() == Approx(0.8f).margin(0.01f));
    }

    SECTION("inactive step produces rest")
    {
        // Advance past step 0 into step 1 (inactive)
        int stepSamps = static_cast<int>(5512.5);
        arp.process(stepSamps + 1);
        REQUIRE_FALSE(arp.isNoteActive());
        REQUIRE(arp.getCurrentNote() == -1);
    }
}

// ---------------------------------------------------------------------------
// Swing
// ---------------------------------------------------------------------------

TEST_CASE("Arpeggiator - swing affects step timing", "[arp][swing]")
{
    ana::Arpeggiator arp;
    arp.prepare(TEST_SR);
    arp.setMode(ana::ArpMode::Up);
    arp.setRate(0.25f);
    arp.setTempo(120.0);

    arp.noteOn(60, 1.0f);
    arp.noteOn(64, 1.0f);

    SECTION("zero swing makes both steps equal duration")
    {
        arp.setSwing(0.0f);
        // With swing=0, even and odd steps have the same duration (~5512.5)
        // Advance first step
        arp.process(static_cast<int>(5512.5) + 1);
        REQUIRE(arp.getCurrentNote() == 64);
    }

    SECTION("non-zero swing alters step cadence")
    {
        arp.setSwing(50.0f);
        // Even step (0): 5512.5 * 1.5 = ~8268.75
        // Odd step (1):  5512.5 * 0.5 = ~2756.25
        //
        // At 6000 samples we should still be on step 0 (8269 > 6000)
        arp.process(6000);
        REQUIRE(arp.getCurrentNote() == 60);

        // Process another 6000 samples -> should have advanced through
        // step 0 (ended at ~8269) and step 1 (ended at ~8269+2756=11025)
        arp.process(6000);
        REQUIRE(arp.getCurrentNote() == 64);
    }
}

// ---------------------------------------------------------------------------
// AsPlayed mode
// ---------------------------------------------------------------------------

TEST_CASE("Arpeggiator - AsPlayed mode preserves press order", "[arp][mode]")
{
    ana::Arpeggiator arp;
    arp.prepare(TEST_SR);
    arp.setMode(ana::ArpMode::AsPlayed);
    arp.setRate(0.25f);
    arp.setTempo(120.0);

    // Press notes out of pitch order
    arp.noteOn(67, 1.0f); // high first
    arp.noteOn(60, 1.0f);
    arp.noteOn(64, 1.0f);

    SECTION("plays in press order: 67, 60, 64")
    {
        advanceSteps(arp, 1);
        REQUIRE(arp.getCurrentNote() == 67);

        advanceSteps(arp, 1);
        REQUIRE(arp.getCurrentNote() == 60);

        advanceSteps(arp, 1);
        REQUIRE(arp.getCurrentNote() == 64);
    }
}

// ---------------------------------------------------------------------------
// UpDown mode
// ---------------------------------------------------------------------------

TEST_CASE("Arpeggiator - UpDown mode ascends then descends", "[arp][mode]")
{
    ana::Arpeggiator arp;
    arp.prepare(TEST_SR);
    arp.setMode(ana::ArpMode::UpDown);
    arp.setRate(0.25f);
    arp.setTempo(120.0);

    arp.noteOn(60, 1.0f);
    arp.noteOn(64, 1.0f);
    arp.noteOn(67, 1.0f);

    // Sequence: 60, 64, 67, 64, then wrap to 60
    SECTION("has ascending portion")
    {
        advanceSteps(arp, 1);
        REQUIRE(arp.getCurrentNote() == 60);

        advanceSteps(arp, 1);
        REQUIRE(arp.getCurrentNote() == 64);

        advanceSteps(arp, 1);
        REQUIRE(arp.getCurrentNote() == 67);
    }

    SECTION("descends after peak")
    {
        advanceSteps(arp, 4); // 60, 64, 67, 64
        REQUIRE(arp.getCurrentNote() == 64);
    }

    SECTION("wraps back to lowest after descent")
    {
        advanceSteps(arp, 5); // 60, 64, 67, 64, 60
        REQUIRE(arp.getCurrentNote() == 60);
    }
}
