#pragma once

#include "../CyberpunkTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace ana
{

class PageTabs : public juce::Component
{
public:
    PageTabs()
    {
        static const char* names[] = { "TIMBRE", "FILTER", "MOD", "SEQ", "FX", "MASTER" };
        for (int i = 0; i < 6; ++i)
        {
            auto b = std::make_unique<juce::TextButton>(names[i]);
            b->setClickingTogglesState(true);
            b->setRadioGroupId(2401);
            b->setColour(juce::TextButton::buttonColourId,
                         CyberpunkTheme::bg_.brighter(0.08f));
            b->setColour(juce::TextButton::textColourOffId,
                         CyberpunkTheme::fg_.withAlpha(0.55f));
            b->setColour(juce::TextButton::buttonOnColourId,
                         CyberpunkTheme::cyan_.withAlpha(0.35f));
            b->setColour(juce::TextButton::textColourOnId, CyberpunkTheme::cyan_);
            const int idx = i;
            b->onClick = [this, idx]
            {
                if (activeIndex_ != idx)
                    setActive(idx);
            };
            addAndMakeVisible(b.get());
            tabs_.add(std::move(b));
        }
        activeIndex_ = 0;
        tabs_.getUnchecked(0)->setToggleState(true, juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(CyberpunkTheme::bg_.withAlpha(0.5f));
        g.fillRect(getLocalBounds());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(2, 1);
        const int w = juce::jmax(1, area.getWidth() / 6);
        for (int i = 0; i < 6; ++i)
            tabs_.getUnchecked(i)->setBounds(area.removeFromLeft(w).reduced(1, 1));
    }

    int getActiveIndex() const noexcept { return activeIndex_; }

    void setActive(int page)
    {
        if (page < 0 || page > 5)
            return;
        activeIndex_ = page;
        for (int i = 0; i < 6; ++i)
            tabs_.getUnchecked(i)->setToggleState(i == page, juce::dontSendNotification);
        repaint();
        if (onTabChanged)
            onTabChanged(page);
    }

    std::function<void(int)> onTabChanged;

private:
    int activeIndex_ = 0;
    juce::OwnedArray<juce::TextButton> tabs_;
};

} // namespace ana
