// Catch2 test runner
//
// NOTE: JUCE (MessageManager) must NOT be touched from a static initializer
// here. The previous JuceInitialiser static object called
// juce::MessageManager::getInstance() during CRT static init, which raced
// against the class-static juce::SingletonHolder<InternalMessageQueue,
// CriticalSection> constructor in another translation unit and segfaulted
// pre-main (NULL critical section). Initialising in main() is order-safe.
#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

int main(int argc, char* argv[])
{
    juce::MessageManager::getInstance();

    int result = Catch::Session().run(argc, argv);

    juce::MessageManager::deleteInstance();
    return result;
}
