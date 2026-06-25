// Catch2 test runner — JUCE static initializer for production code compatibility
#include <catch2/catch_all.hpp>

// Some production DSP sources use JUCE's message thread or create JUCE objects
// during static initialization. This ensures JUCE's GUI infrastructure is available
// before any static initializers run.
struct JuceInitialiser {
    JuceInitialiser() {
        juce::MessageManager::getInstance();
    }
} juceInit;
