#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <cmath>
#include <vector>

struct ebur128_state;

namespace ana {

/**
 * ITU-R BS.1770-4 compliant LUFS metering engine.
 *
 * Wraps libebur128 (MIT, C library) to provide real-time loudness
 * measurements: Momentary (400 ms), Short-term (3 s), Integrated
 * (from start), True-peak (4x oversampled), and LRA.
 *
 * The engine is read-only — it never modifies the audio buffer.
 * All getters are lock-free atomics for audio-thread-safe reads.
 */
class MeteringEngine {
public:
    MeteringEngine();
    ~MeteringEngine();

    MeteringEngine(const MeteringEngine&) = delete;
    MeteringEngine& operator=(const MeteringEngine&) = delete;

    /** Initialise (or re-initialise) the internal libebur128 state.
        Safe to call multiple times — destroys any previous state. */
    void prepare(double sampleRate, int numChannels, int maxBlockSize);

    /** Feed a completed output buffer for analysis.
        Must be called after master gain / final limiting.
        Does NOT modify the buffer. */
    void process(const juce::AudioBuffer<float>& buffer);

    /** Reset all meters and destroy internal state. */
    void reset();

    // --- Lock-free getters ---

    /** Momentary loudness (400 ms integrated window), in LUFS. */
    double getMomentaryLUFS() const noexcept { return momentaryLUFS_.load(); }

    /** Short-term loudness (3 s sliding window), in LUFS. */
    double getShortTermLUFS() const noexcept { return shortTermLUFS_.load(); }

    /** Integrated loudness (entire session, gated), in LUFS. */
    double getIntegratedLUFS() const noexcept { return integratedLUFS_.load(); }

    /** Loudness Range (LRA, EBU TECH 3342), in LU. */
    double getLRA() const noexcept { return lra_.load(); }

    /** True-peak for a given channel (4x oversampled), in dBTP. */
    double getTruePeak(int channel) const noexcept;

private:
    ebur128_state* state_ = nullptr;
    int numChannels_ = 0;

    /** Pre-allocated interleave buffer (avoids heap in audio callback). */
    std::vector<float> interleaveBuffer_;

    // --- Cached meter values (updated every process() call) ---
    std::atomic<double> momentaryLUFS_{-HUGE_VAL};
    std::atomic<double> shortTermLUFS_{-HUGE_VAL};
    std::atomic<double> integratedLUFS_{-HUGE_VAL};
    std::atomic<double> lra_{-HUGE_VAL};

    static constexpr int kMaxChannels = 8;
    std::array<std::atomic<double>, kMaxChannels> truePeak_;
};

} // namespace ana
