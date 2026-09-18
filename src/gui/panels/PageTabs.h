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
        static const char* names[] = { "TIMBRE", "FILTER", "MOD", "SEQ", "FX", "MASTER", "ENV", "EVO", "GRAIN" };
        static const char* tips[] =
        {
            "Timbre A/B, BLEND, XY pad and the image / generative layers",
            "Filter cutoff, resonance and the live frequency response",
            "Macros, the modulation matrix and both LFOs",
            "Step sequencer: 16 gates and values, tempo synced",
            "Effect rack, effect presets, spectral freeze and the character buttons",
            "Voicing, portamento, arpeggiator, master volume and pan",
            "Envelope slots VOL/ENV1-3 on the editable canvas",
            "Evolve spectral DNA, then promote the fittest genome to a timbre",
            "Granular layer: cloud, source envelope and the ten grain controls"
        };
        for (int i = 0; i < kNumTabs; ++i)
        {
            auto b = std::make_unique<juce::TextButton>(names[i]);
            b->setTooltip(tips[i]);
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
        const int w = juce::jmax(1, area.getWidth() / kNumTabs);
        for (int i = 0; i < kNumTabs; ++i)
            tabs_.getUnchecked(i)->setBounds(area.removeFromLeft(w).reduced(1, 1));
    }

    int getActiveIndex() const noexcept { return activeIndex_; }

    void setActive(int page)
    {
        if (page < 0 || page >= kNumTabs)
            return;
        activeIndex_ = page;
        for (int i = 0; i < kNumTabs; ++i)
            tabs_.getUnchecked(i)->setToggleState(i == page, juce::dontSendNotification);
        repaint();
        if (onTabChanged)
            onTabChanged(page);
    }

    std::function<void(int)> onTabChanged;

    /** Number of tabs in the strip (TIMBRE..GRAIN). */
    static constexpr int kNumTabs = 9;

private:
    int activeIndex_ = 0;
    juce::OwnedArray<juce::TextButton> tabs_;
};

} // namespace ana
