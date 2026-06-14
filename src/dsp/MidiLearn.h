#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
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
    void removeMapping(int cc);
    void removeAllMappings();

    // --- MIDI processing (audio thread) ---
    void processMidi(const juce::MidiMessage& msg);

    // --- Learn mode (message thread) ---
    void startLearn(const juce::String& paramId, std::atomic<float>* target,
                    float min = 0.0f, float max = 1.0f);
    void stopLearn();
    bool isLearning() const { return learning_; }

    // --- Reconnect a target pointer after state load ---
    void reconnectTarget(const juce::String& paramId, std::atomic<float>* target);

    // --- Utility ---
    const std::vector<MidiMapping>& getMappings() const { return mappings_; }

    // --- Persistence ---
    juce::ValueTree saveState() const;
    void loadState(const juce::ValueTree& state);

private:
    std::vector<MidiMapping> mappings_;
    bool learning_ = false;
    juce::String learnParamId_;
    std::atomic<float>* learnTarget_ = nullptr;
    float learnMin_ = 0.0f;
    float learnMax_ = 1.0f;
};

} // namespace ana
