#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ana {

//==============================================================================
/**
    About / Credits panel displayed as a CallOutBox from the status bar ⓘ button.

    Displays algorithm attributions, license notices, and framework credits
    in monospace cyber-styled text. Content is scrollable if it exceeds the
    viewport height.
*/
class CreditsPanel : public juce::Component
{
public:
    CreditsPanel();
    ~CreditsPanel() override = default;

    void resized() override;

    //==============================================================================
    /** A single credited project with author, license, and URL. */
    struct CreditItem
    {
        juce::String project;
        juce::String author;
        juce::String license;
        juce::String url;
    };

private:
    //==============================================================================
    /** Inner component that paints the actual credits content. */
    class ContentComponent : public juce::Component
    {
    public:
        ContentComponent();
        void paint(juce::Graphics& g) override;

        /** Recalculate layout metrics after construction. */
        void rebuildLayout(int targetWidth);

        /** Height of the painted content. */
        int getContentHeight() const { return contentHeight_; }

    private:
        //==============================================================================
        // Layout metrics (recomputed in rebuildLayout)
        int contentHeight_ = 0;
        int leftMargin_    = 0;
        int rightMargin_   = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContentComponent)
    };

    //==============================================================================
    juce::Viewport       viewport_;
    ContentComponent     content_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CreditsPanel)
};

} // namespace ana
