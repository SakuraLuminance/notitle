#include <catch2/catch_all.hpp>
#include "dsp/EffectsChain.h"
#include "dsp/effects/EffectParamRegistry.h"
#include "gui/EffectParamPanel.h"

#include <cmath>

// ---------------------------------------------------------------------------
// P2 leftover: enumeration parameters become real menus instead of knobs.
// ---------------------------------------------------------------------------

namespace
{
ana::EffectParamSpec makeSpec(const char* values, bool isInt, float max = 4.0f)
{
    ana::EffectParamSpec s;
    s.id     = "p";
    s.label  = "P";
    s.min    = 0.0f;
    s.max    = max;
    s.def    = 0.0f;
    s.skew   = 1.0f;
    s.isInt  = isInt;
    s.values = values;
    return s;
}
}

TEST_CASE("EffectParamSpec parses menu labels", "[effects][ui]")
{
    SECTION("declared indices give the label order")
    {
        auto spec = makeSpec("0=SOFT 1=HARD 2=TUBE", true);
        REQUIRE(spec.isChoice());

        const auto labels = spec.getChoiceLabels();
        REQUIRE(labels.size() == 3);
        REQUIRE(labels[0] == "SOFT");
        REQUIRE(labels[1] == "HARD");
        REQUIRE(labels[2] == "TUBE");
    }

    SECTION("labels are sorted by index, not by position in the string")
    {
        const auto labels = makeSpec("2=SQUARE 0=SINE 1=TRI", true).getChoiceLabels();
        REQUIRE(labels.size() == 3);
        REQUIRE(labels[0] == "SINE");
        REQUIRE(labels[1] == "TRI");
        REQUIRE(labels[2] == "SQUARE");
    }

    SECTION("a continuous parameter keeps its anchors as a tooltip only")
    {
        auto spec = makeSpec("0=MONO 0.5=100% 1=200%", false, 1.0f);
        REQUIRE_FALSE(spec.isChoice());

        const auto labels = spec.getChoiceLabels();
        REQUIRE(labels.size() == 3);
        REQUIRE(labels[1] == "100%");
    }

    SECTION("no labels means no menu")
    {
        auto spec = makeSpec(nullptr, true);
        REQUIRE_FALSE(spec.isChoice());
        REQUIRE(spec.getChoiceLabels().isEmpty());
    }

    SECTION("malformed entries are skipped instead of producing blank items")
    {
        REQUIRE(makeSpec("", true).getChoiceLabels().isEmpty());
        REQUIRE(makeSpec("garbage", true).getChoiceLabels().isEmpty());
        REQUIRE(makeSpec("=SINE 0=", true).getChoiceLabels().isEmpty());
        REQUIRE(makeSpec("0=  1=ON", true).getChoiceLabels().size() == 1);
        REQUIRE(makeSpec("1=ON", true).getChoiceLabels()[0] == "ON");
    }
}

TEST_CASE("Every effect enumeration exposes usable menu entries", "[effects][ui]")
{
    int choiceParams = 0;

    for (const auto& typeName : ana::EffectParamRegistry::getTypeNames())
    {
        auto effect = ana::EffectParamRegistry::create(typeName);
        REQUIRE(effect != nullptr);

        for (int i = 0; i < effect->getNumParams(); ++i)
        {
            const auto& spec = effect->getParamSpec(i);
            if (! spec.isChoice())
                continue;

            ++choiceParams;

            const auto labels = spec.getChoiceLabels();
            INFO(typeName << " param " << i << " (" << (spec.id != nullptr ? spec.id : "?") << ")");

            // A menu needs at least two entries, one per declared integer step.
            REQUIRE(labels.size() > 1);
            REQUIRE(labels.size() == static_cast<int>(std::lround(spec.max)) + 1);

            // ... and every entry must survive a round trip through the effect,
            // otherwise picking it in the menu would store a different value.
            for (int c = 0; c < labels.size(); ++c)
            {
                effect->setParamValue(i, static_cast<float>(c));
                REQUIRE(effect->getParamValue(i) == Catch::Approx(static_cast<float>(c)));
            }

            // Menu entries are never blank or duplicated.
            for (int c = 0; c < labels.size(); ++c)
            {
                REQUIRE(labels[c].trim().isNotEmpty());
                for (int d = c + 1; d < labels.size(); ++d)
                    REQUIRE(labels[c] != labels[d]);
            }
        }
    }

    REQUIRE(choiceParams >= 5);
}

TEST_CASE("EffectParamPanel shows a menu for a choice parameter", "[effects][ui]")
{
    auto effect = ana::EffectParamRegistry::create("RingModulator");
    REQUIRE(effect != nullptr);

    int choiceIndex = -1;
    int choiceCount = 0;
    for (int i = 0; i < effect->getNumParams(); ++i)
        if (effect->getParamSpec(i).isChoice())
        {
            if (choiceIndex < 0)
                choiceIndex = i;
            ++choiceCount;
        }

    REQUIRE(choiceCount >= 1);

    ana::EffectParamPanel panel(effect.get());

    // Choice parameters become menus, everything else stays a knob.
    REQUIRE(panel.getNumMenus() == choiceCount);
    REQUIRE(panel.getNumKnobs() == effect->getNumParams() - choiceCount);

    int seen = 0;
    panel.visitMenus([&](int paramIndex, juce::ComboBox& menu)
    {
        ++seen;
        REQUIRE(paramIndex == choiceIndex);
        REQUIRE(menu.getNumItems() == 3);
        REQUIRE(menu.getItemText(0) == "SINE");
        REQUIRE(menu.getItemText(2) == "SQUARE");
        REQUIRE(menu.getSelectedId() == 1);

        // Picking an entry writes the parameter (index, not the menu id).
        menu.setSelectedId(3, juce::sendNotificationSync);
    });

    REQUIRE(seen == choiceCount);
    REQUIRE(effect->getParamValue(choiceIndex) == Catch::Approx(2.0f));

    // The menu rows need real space above the knob grid.
    REQUIRE(panel.getPreferredHeight(300) > choiceCount * 20);
}
// ---------------------------------------------------------------------------
// The rack reserves getParamPanelPreferredHeight(width) for an expanded slot and
// then lays the panel out at that same width.  If the two disagree by even one
// knob column the last row lands outside the reserved area and silently
// disappears, so the height formula and the layout are checked against each
// other here at every width the rack can hand out.
// ---------------------------------------------------------------------------

TEST_CASE("EffectParamPanel reserves the height its layout needs", "[effects][ui]")
{
    int checkedEffects = 0;

    for (const auto& typeName : ana::EffectParamRegistry::getTypeNames())
    {
        auto effect = ana::EffectParamRegistry::create(typeName);
        REQUIRE(effect != nullptr);

        ana::EffectParamPanel panel(effect.get());
        ++checkedEffects;

        // Widths that straddle knob-column boundaries, including the exact
        // multiples of 46 that used to shift a knob onto an extra row.
        for (int width : { 46, 47, 91, 92, 93, 137, 138, 184, 230, 231, 232,
                           275, 276, 277, 322, 414, 460, 690, 1080 })
        {
            const int height = panel.getPreferredHeight(width);
            REQUIRE(height > 0);

            panel.setBounds(0, 0, width, height);
            panel.resized();

            const auto bounds = panel.getLocalBounds();

            panel.visitKnobs([&](int, juce::Slider& knob)
            {
                INFO(typeName << " knob at width " << width);
                REQUIRE(bounds.contains(knob.getBounds()));
            });

            panel.visitMenus([&](int, juce::ComboBox& menu)
            {
                INFO(typeName << " menu at width " << width);
                REQUIRE(bounds.contains(menu.getBounds()));
            });

            // Half the reserved height must not push a knob out of the panel
            // either: the rows squeeze, they never fall off the bottom.
            panel.setBounds(0, 0, width, juce::jmax(20, height / 2));
            panel.resized();

            const auto squeezed = panel.getLocalBounds();

            panel.visitKnobs([&](int, juce::Slider& knob)
            {
                INFO(typeName << " squeezed knob at width " << width);
                REQUIRE(squeezed.contains(knob.getBounds()));
            });
        }
    }

    REQUIRE(checkedEffects >= 10);
}

