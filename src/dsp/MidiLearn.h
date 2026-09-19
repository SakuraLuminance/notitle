#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace ana {

/**
 * A single MIDI CC → parameter mapping.
 * targetParam is updated atomically from the audio thread when
 * the mapped CC is received.
 */
struct MidiMapping {
    int ccNumber = -1;
    juce::String parameterId;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    std::atomic<float>* targetParam = nullptr;

    // Non-atomic targets (effect parameters live behind
    // EffectBase::get/setParamValue) are driven through these callbacks.
    // targetParam wins when both are set, so the historical atomic path is
    // bit-for-bit unchanged.  Callbacks are runtime-only and are never
    // serialised; the editor reconnects them after a state load.
    std::function<void(float)> targetSetter;
    std::function<float()> targetGetter;

    bool isGlobal = false;  // true = survives preset changes
};

/**
 * MIDI Learn system: maps incoming MIDI Continuous Controller messages
 * to atomic float parameters for real-time hardware control.
 *
 * Thread safety:
 * - processMidi() runs on the audio thread and takes lock_ with a *try* lock, so it
 *   never blocks: when the message thread is editing the table it drops that CC for
 *   one block instead of iterating a vector that is being reallocated underneath it.
 * - Every other method runs on the message thread and takes lock_ outright.
 * - In learn mode the audio thread only records the captured CC number; the mapping
 *   itself is created by applyPendingLearn() / stopLearn() on the message thread,
 *   because creating one allocates.
 * - addMapping() / removeMapping() must be called from the message thread.
 */
class MidiLearn {
public:
    MidiLearn() = default;

    // --- Mapping management (message thread only) ---
    void addMapping(int cc, const juce::String& paramId,
                    std::atomic<float>* target, float min, float max);

    /** Callback-target flavour: used for parameters that are not atomics
        (effect parameters).  @a setter receives the scaled value. */
    void addMapping(int cc, const juce::String& paramId,
                    std::function<void(float)> setter,
                    std::function<float()> getter,
                    float min, float max);
    void removeMapping(int cc);
    void removeAllMappings();
    void setMappingGlobal(const juce::String& paramId, bool isGlobal);

    // --- MIDI processing (audio thread) ---
    void processMidi(const juce::MidiMessage& msg);

    // --- Learn mode (message thread) ---
    void startLearn(const juce::String& paramId, std::atomic<float>* target,
                    float min = 0.0f, float max = 1.0f);

    /** Learn a mapping whose target is a callback instead of an atomic. */
    void startLearn(const juce::String& paramId,
                    std::function<void(float)> setter,
                    std::function<float()> getter,
                    float min = 0.0f, float max = 1.0f);

    void stopLearn();
    bool isLearning() const { return learning_.load(); }

    /** Turns a CC captured by the audio thread during learn mode into a mapping.
        Called from the message thread (the editor timer and stopLearn()); returns
        true when a capture was waiting. */
    bool applyPendingLearn();

    // --- Reconnect a target pointer after state load ---
    void reconnectTarget(const juce::String& paramId, std::atomic<float>* target);

    /** Reconnect an existing mapping to a callback target (replaces any
        previously connected atomic or callback target). */
    void reconnectTarget(const juce::String& paramId,
                         std::function<void(float)> setter,
                         std::function<float()> getter);

    /** Current value of a mapping's target (atomic or callback).
        Returns false when the mapping has no usable target. */
    bool getMappingValue(const juce::String& paramId, float& outValue) const;

    // --- Utility ---
    const std::vector<MidiMapping>& getMappings() const { return mappings_; }

    // --- Persistence ---
    // Full processor state: saves/loads ALL mappings (global + per-preset)
    juce::ValueTree saveProcessorState() const;
    void loadProcessorState(const juce::ValueTree& state);

    // Preset state: saves/loads only non-global (per-preset) mappings
    juce::ValueTree savePresetState() const;
    void loadPresetState(const juce::ValueTree& state);

    // Backward-compatible aliases
    juce::ValueTree saveState() const { return saveProcessorState(); }
    void loadState(const juce::ValueTree& state) { loadProcessorState(state); }

private:
    /** Lock-free body of the public addMapping() overloads: lock_ must be held. */
    void addMappingInternal(int cc, const juce::String& paramId,
                            std::atomic<float>* target,
                            std::function<void(float)> setter,
                            std::function<float()> getter,
                            float min, float max);

    /** Guards mappings_ and the learn fields.  The message thread takes it outright;
        processMidi() (audio thread) only ever try-locks it. */
    mutable juce::SpinLock lock_;
    std::vector<MidiMapping> mappings_;

    std::atomic<bool> learning_ { false };
    std::atomic<int>  pendingLearnCc_ { -1 };   // CC captured by the audio thread
    juce::String learnParamId_;
    std::atomic<float>* learnTarget_ = nullptr;
    std::function<void(float)> learnSetter_;
    std::function<float()> learnGetter_;
    float learnMin_ = 0.0f;
    float learnMax_ = 1.0f;
};

} // namespace ana
