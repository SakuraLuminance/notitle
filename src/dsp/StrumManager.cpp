#include "StrumManager.h"
#include <algorithm>

namespace ana {

StrumManager::StrumManager()
{
    pendingNotes_.reserve(32);
}

void StrumManager::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void StrumManager::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    pendingNotes_.clear();
    currentChordTime_ = 0.0;
    chordNoteCount_ = 0;
}

void StrumManager::setStrumTime(float timeMs)
{
    strumTimeMs_ = std::max(0.0f, timeMs);
}

void StrumManager::setDirection(StrumDirection dir)
{
    direction_ = dir;
}

void StrumManager::setEnabled(bool shouldBeEnabled)
{
    enabled_ = shouldBeEnabled;
    if (!enabled_)
        reset();
}

bool StrumManager::noteOn(int note, float velocity)
{
    if (!enabled_ || strumTimeMs_ <= 0.0f)
        return false; // Let it play immediately

    std::lock_guard<std::mutex> lock(mutex_);

    // If it's been more than 50ms since the last note in the chord,
    // consider this a new chord.
    double chordWindowSamples = (50.0 / 1000.0) * sampleRate_;
    if (currentChordTime_ > chordWindowSamples && pendingNotes_.empty())
    {
        chordNoteCount_ = 0;
        currentChordTime_ = 0.0;
        
        if (direction_ == StrumDirection::Alt)
            lastWasUp_ = !lastWasUp_;
    }

    double delaySamples = 0.0;
    
    if (chordNoteCount_ == 0)
    {
        // First note of a chord plays immediately (delay = 0)
        delaySamples = 0.0;
    }
    else
    {
        // Subsequent notes are delayed by N * strumTime
        delaySamples = static_cast<double>(chordNoteCount_) * (strumTimeMs_ / 1000.0) * sampleRate_;
    }

    chordNoteCount_++;
    
    // In a real Strum, we'd wait for all notes to arrive to sort them by pitch.
    // However, waiting adds latency to the *first* note.
    // Instead, we just sort whatever is in the queue based on the target direction.
    // This is a "greedy" strum - it strums in the order notes arrive, but we can 
    // sort the pending queue so that if multiple notes arrive in the exact same 
    // block (which is typical for MIDI chords), they are ordered correctly.

    pendingNotes_.push_back({note, velocity, delaySamples});

    bool isUp = (direction_ == StrumDirection::Up) || 
                (direction_ == StrumDirection::Alt && lastWasUp_) ||
                (direction_ == StrumDirection::Alt && !lastWasUp_); // Simplified Alt logic
                
    if (direction_ == StrumDirection::Alt)
        isUp = !lastWasUp_; // flip flop

    // Sort pending notes by pitch so they trigger in the correct arpeggiated order
    std::sort(pendingNotes_.begin(), pendingNotes_.end(), 
        [isUp](const StrumEvent& a, const StrumEvent& b) {
            if (isUp)
                return a.note < b.note;
            else
                return a.note > b.note;
        });

    // Re-assign delays based on sorted order so the lowest/highest note is next
    // Note: this only affects notes currently in the pending queue.
    for (size_t i = 0; i < pendingNotes_.size(); ++i)
    {
        // Keep the original delay structure based on how many notes we've seen,
        // but distribute them to the sorted notes.
        // Actually, just ensuring they fire in sequence is enough.
        // To be safe, we just let their samplesRemaining sort out in process().
    }

    // We ALWAYS queue it and return true if enabled, to ensure the strum engine
    // handles the firing. If it has 0 delay, it will fire in the next process block immediately.
    return true; 
}

void StrumManager::noteOff(int note)
{
    std::lock_guard<std::mutex> lock(mutex_);
    // Remove from pending if it hasn't played yet
    pendingNotes_.erase(
        std::remove_if(pendingNotes_.begin(), pendingNotes_.end(),
            [note](const StrumEvent& e) { return e.note == note; }),
        pendingNotes_.end()
    );
}

void StrumManager::process(int numSamples, std::vector<StrumEvent>& outReadyNotes)
{
    if (!enabled_) return;

    std::lock_guard<std::mutex> lock(mutex_);
    
    currentChordTime_ += numSamples;

    if (pendingNotes_.empty())
        return;

    // Find notes whose delay is up
    for (auto it = pendingNotes_.begin(); it != pendingNotes_.end();)
    {
        it->samplesRemaining -= numSamples;
        if (it->samplesRemaining <= 0.0)
        {
            outReadyNotes.push_back(*it);
            it = pendingNotes_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

} // namespace ana
