#pragma once
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
 * - processMidi() is called from the audio thread (it reads/writes
 *   mappings_ which is only mutated on the message thread)
 * - Learned mappings are created by startLearn() from the message thread
 * - addMapping() / removeMapping() must be called from the message thread
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
    bool isLearning() const { return learning_; }

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
    void addMappingInternal(int cc, const juce::String& paramId,
                            std::atomic<float>* target,
                            std::function<void(float)> setter,
                            std::function<float()> getter,
                            float min, float max);

    std::vector<MidiMapping> mappings_;
    bool learning_ = false;
    juce::String learnParamId_;
    std::atomic<float>* learnTarget_ = nullptr;
    std::function<void(float)> learnSetter_;
    std::function<float()> learnGetter_;
    float learnMin_ = 0.0f;
    float learnMax_ = 1.0f;
};

} // namespace ana
