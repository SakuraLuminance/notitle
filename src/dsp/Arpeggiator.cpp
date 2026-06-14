#include "Arpeggiator.h"
#include <algorithm>
#include <random>
#include <numeric>

namespace ana {

// ============================================================================
// Construction / Life-cycle
// ============================================================================

Arpeggiator::Arpeggiator()
{
    // Default pattern: all 16 steps active with full velocity and 50% gate
    for (int i = 0; i < 16; ++i)
    {
        steps[i].active   = true;
        steps[i].velocity = 1.0f;
        steps[i].gate     = 0.5f;
    }
}

void Arpeggiator::prepare(double sr)
{
    sampleRate = sr;
    stepDurationSamples = (60.0 / bpm) * rateBeats * sampleRate;
    reset();
}

void Arpeggiator::reset()
{
    currentStep         = -1;
    sampleCounter       = 0.0;
    noteInStep          = 0;
    direction           = 1;
    playing             = false;
    noteActive          = false;
    currentNote         = -1;
    currentVelocity     = 0.0f;
    gateCounter         = 0.0;
    gateDurationSamples = 0.0;
    heldNotes.clear();
    noteOrder.clear();
    arpSequence.clear();
}

// ============================================================================
// Note input
// ============================================================================

void Arpeggiator::noteOn(int note, float velocity)
{
    juce::ignoreUnused(velocity);

    // Only add if not already held
    if (std::find(heldNotes.begin(), heldNotes.end(), note) == heldNotes.end())
    {
        heldNotes.push_back(note);
        noteOrder.push_back(note);
    }

    rebuildArpSequence();
}

void Arpeggiator::noteOff(int note)
{
    auto it = std::find(heldNotes.begin(), heldNotes.end(), note);
    if (it != heldNotes.end())
    {
        heldNotes.erase(it);

        // Also remove from noteOrder (only first occurrence)
        auto orderIt = std::find(noteOrder.begin(), noteOrder.end(), note);
        if (orderIt != noteOrder.end())
            noteOrder.erase(orderIt);
    }

    rebuildArpSequence();
}

// ============================================================================
// Configuration
// ============================================================================

void Arpeggiator::setMode(ArpMode m)
{
    if (mode != m)
    {
        mode = m;
        rebuildArpSequence();
    }
}

void Arpeggiator::setRate(float beats)
{
    rateBeats = std::max(0.03125f, beats); // min 1/32 note
    stepDurationSamples = (60.0 / bpm) * rateBeats * sampleRate;
}

void Arpeggiator::setGate(float percent)
{
    gatePercent = std::clamp(percent, 10.0f, 200.0f);
}

void Arpeggiator::setOctaveRange(int octaves)
{
    octaveRange = std::clamp(octaves, 1, 4);
    rebuildArpSequence();
}

void Arpeggiator::setSwing(float percent)
{
    swingPercent = std::clamp(percent, 0.0f, 100.0f);
}

void Arpeggiator::setTempo(double bpmVal)
{
    bpm = std::max(1.0, bpmVal);
    stepDurationSamples = (60.0 / bpm) * rateBeats * sampleRate;
}

// ============================================================================
// Pattern editing
// ============================================================================

void Arpeggiator::setStep(int index, bool active, float velocity, float gate)
{
    if (index >= 0 && index < 16)
    {
        steps[index].active   = active;
        steps[index].velocity = std::clamp(velocity, 0.0f, 1.0f);
        steps[index].gate     = std::clamp(gate, 0.0f, 1.0f);
    }
}

const ArpStep& Arpeggiator::getStep(int index) const
{
    static const ArpStep empty;
    if (index >= 0 && index < 16)
        return steps[index];
    return empty;
}

// ============================================================================
// Processing
// ============================================================================

void Arpeggiator::process(int numSamples)
{
    // Nothing to play
    if (!playing)
    {
        noteActive  = false;
        currentNote = -1;
        return;
    }

    // Pattern mode has its own step logic even if arpSequence is empty;
    // other modes need a non-empty sequence.
    if (mode != ArpMode::Pattern && arpSequence.empty())
    {
        noteActive  = false;
        currentNote = -1;
        return;
    }

    if (stepDurationSamples <= 0.0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        // ---- Step advance ------------------------------------------------
        if (sampleCounter >= getCurrentStepDuration() || currentStep < 0)
        {
            if (currentStep >= 0)
                sampleCounter -= getCurrentStepDuration();
            else
                sampleCounter = 0.0;

            advanceStep();
        }

        // ---- Gate off ----------------------------------------------------
        if (noteActive && gateCounter >= gateDurationSamples)
        {
            noteActive  = false;
            currentNote = -1;
        }

        sampleCounter += 1.0;
        gateCounter   += 1.0;
    }
}

// ============================================================================
// Output / State accessors
// ============================================================================

int Arpeggiator::getCurrentNote() const      { return currentNote; }
float Arpeggiator::getCurrentVelocity() const { return currentVelocity; }
bool Arpeggiator::isNoteActive() const       { return noteActive; }
int Arpeggiator::getNumHeldNotes() const     { return static_cast<int>(heldNotes.size()); }
std::vector<int> Arpeggiator::getHeldNotes() const { return heldNotes; }
bool Arpeggiator::isPlaying() const          { return playing; }

// ============================================================================
// Private helpers
// ============================================================================

void Arpeggiator::advanceStep()
{
    noteActive   = true;
    gateCounter  = 0.0;

    if (mode == ArpMode::Pattern)
    {
        // ---- Pattern mode: walk steps[0..15] -----------------------------
        currentStep = (currentStep + 1) % 16;

        // Skip inactive steps (give up after scanning all 16)
        for (int i = 0; i < 16; ++i)
        {
            if (steps[currentStep].active)
                break;
            currentStep = (currentStep + 1) % 16;
        }

        if (steps[currentStep].active)
        {
            // Cycle through held notes across octaves
            int nHeld = std::max(1, static_cast<int>(heldNotes.size()));
            int noteIdx = currentStep % nHeld;
            int oct     = (currentStep / nHeld) % octaveRange;
            currentNote = heldNotes.empty() ? -1 : heldNotes[noteIdx] + oct * 12;
            currentVelocity = steps[currentStep].velocity;
            gateDurationSamples = stepDurationSamples * steps[currentStep].gate;
        }
        else
        {
            // No active steps found
            currentNote = -1;
            currentVelocity = 0.0f;
            noteActive = false;
        }
    }
    else
    {
        // ---- Non-pattern modes: walk arpSequence -------------------------
        if (arpSequence.empty())
        {
            currentNote = -1;
            noteActive  = false;
            return;
        }

        currentStep = (currentStep + 1) % static_cast<int>(arpSequence.size());
        currentNote     = arpSequence[currentStep];
        currentVelocity = 1.0f;
        gateDurationSamples = stepDurationSamples * (gatePercent / 100.0);
    }
}

int Arpeggiator::getNoteForStep(int stepIndex) const
{
    juce::ignoreUnused(stepIndex);

    if (heldNotes.empty())
        return -1;

    if (mode == ArpMode::Pattern)
    {
        int nHeld   = static_cast<int>(heldNotes.size());
        int noteIdx = stepIndex % nHeld;
        int oct     = (stepIndex / nHeld) % octaveRange;
        return heldNotes[noteIdx] + oct * 12;
    }

    if (arpSequence.empty())
        return -1;

    return arpSequence[stepIndex % static_cast<int>(arpSequence.size())];
}

void Arpeggiator::rebuildArpSequence()
{
    arpSequence.clear();
    noteInStep = 0;
    direction  = 1;
    currentStep = -1;

    if (heldNotes.empty())
    {
        playing = false;
        currentNote = -1;
        noteActive  = false;
        return;
    }

    playing = true;

    switch (mode)
    {
    case ArpMode::Up:
    {
        // Ascending, each octave block in order
        std::vector<int> sorted = heldNotes;
        std::sort(sorted.begin(), sorted.end());
        for (int oct = 0; oct < octaveRange; ++oct)
            for (int n : sorted)
                arpSequence.push_back(n + oct * 12);
        break;
    }

    case ArpMode::Down:
    {
        // Descending, each octave block in order
        std::vector<int> sorted = heldNotes;
        std::sort(sorted.begin(), sorted.end(), std::greater<int>());
        for (int oct = 0; oct < octaveRange; ++oct)
            for (int n : sorted)
                arpSequence.push_back(n + oct * 12);
        break;
    }

    case ArpMode::UpDown:
    {
        // Up then back down, omitting the endpoints to avoid repeats
        std::vector<int> sorted = heldNotes;
        std::sort(sorted.begin(), sorted.end());

        std::vector<int> seq;
        for (int oct = 0; oct < octaveRange; ++oct)
            for (int n : sorted)
                seq.push_back(n + oct * 12);

        // Add down portion (exclude first and last of seq to prevent double-tap)
        int sz = static_cast<int>(seq.size());
        for (int i = sz - 2; i >= 1; --i)
            seq.push_back(seq[i]);

        arpSequence = seq;
        break;
    }

    case ArpMode::Random:
    {
        // Shuffled notes across the octave range
        std::vector<int> notes;
        for (int oct = 0; oct < octaveRange; ++oct)
            for (int n : heldNotes)
                notes.push_back(n + oct * 12);

        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(notes.begin(), notes.end(), g);
        arpSequence = notes;
        break;
    }

    case ArpMode::AsPlayed:
    {
        // Preserve the order notes were pressed
        for (int oct = 0; oct < octaveRange; ++oct)
            for (int n : noteOrder)
                arpSequence.push_back(n + oct * 12);
        break;
    }

    case ArpMode::Pattern:
    {
        // Pattern mode uses steps[] directly; just store held notes
        // for cycling.  The sequence length is always 16 virtual positions.
        arpSequence = heldNotes;
        break;
    }
    }
}

double Arpeggiator::getCurrentStepDuration() const
{
    double dur = stepDurationSamples;

    if (swingPercent > 0.0f && currentStep >= 0)
    {
        // Swing: even-indexed steps are lengthened (downbeat held longer),
        // odd-indexed steps are shortened (offbeat delayed).
        if (currentStep % 2 == 0)
            dur = stepDurationSamples * (1.0 + swingPercent / 100.0);
        else
            dur = stepDurationSamples * (1.0 - swingPercent / 100.0);
    }

    return std::max(dur, 1.0);
}

} // namespace ana
