#include "CreditsPanel.h"
#include "CyberpunkTheme.h"

namespace ana {

//==============================================================================
// Static credit data builders
//==============================================================================
static juce::Array<CreditsPanel::CreditItem> buildAlgorithmCredits()
{
    juce::Array<CreditsPanel::CreditItem> arr;
    arr.add({ "WDF Circuit Modeling",       "jerryuhoo (Fire)",          "AGPL-3.0",     "https://github.com/jerryuhoo/Fire" });
    arr.add({ "KFR SIMD Math Library",       "kfrlib/kfr",               "GPL-2.0",      "https://github.com/kfrlib/kfr" });
    arr.add({ "FAUST DSP Compilation",       "grame-cncm/faust",         "Custom (GPL)",  "https://github.com/grame-cncm/faust" });
    arr.add({ "BYOD Dynamic Rack Pattern",   "Chowdhury-DSP/BYOD",       "GPL-3.0",      "https://github.com/Chowdhury-DSP/BYOD" });
    arr.add({ "JUCE Framework",              "juce-framework/JUCE",      "ISC / GPL-3.0","https://github.com/juce-framework/JUCE" });
    return arr;
}

static juce::StringArray buildLicenseNotices()
{
    juce::StringArray arr;
    arr.add("AnaPlug uses the JUCE framework (ISC / GPL-3.0).");
    arr.add("Source: https://github.com/juce-framework/JUCE");
    arr.add("");
    arr.add("DSP algorithm inspirations and their respective licenses:");
    arr.add("");
    arr.add("  WDF Circuit Modeling \u2014 Fire (AGPL-3.0)");
    arr.add("    https://github.com/jerryuhoo/Fire");
    arr.add("");
    arr.add("  SIMD Math Library \u2014 KFR (GPL-2.0)");
    arr.add("    https://github.com/kfrlib/kfr");
    arr.add("");
    arr.add("  DSP Compilation \u2014 FAUST (Custom GPL-compatible)");
    arr.add("    https://github.com/grame-cncm/faust");
    arr.add("");
    arr.add("  Dynamic Effect Rack \u2014 BYOD (GPL-3.0)");
    arr.add("    https://github.com/Chowdhury-DSP/BYOD");
    arr.add("");
    arr.add("All original AnaPlug code is distributed under the");
    arr.add("GNU General Public License v3.0 unless otherwise noted");
    arr.add("in individual source file headers.");
    arr.add("");
    arr.add("No icon assets are used from third-party sources.");
    return arr;
}

//==============================================================================
// CreditsPanel
//==============================================================================
CreditsPanel::CreditsPanel()
{
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,
        CyberpunkTheme::cyan_.withAlpha(0.5f));
    viewport_.getVerticalScrollBar().setColour(juce::ScrollBar::trackColourId,
        CyberpunkTheme::bg_.brighter(0.1f));
    addAndMakeVisible(viewport_);
}

void CreditsPanel::resized()
{
    auto bounds = getLocalBounds();
    content_.rebuildLayout(bounds.getWidth() - 6);
    content_.setSize(bounds.getWidth() - 6, content_.getContentHeight());
    viewport_.setBounds(bounds);
}

//==============================================================================
// ContentComponent
//==============================================================================
CreditsPanel::ContentComponent::ContentComponent()
{
    rebuildLayout(420);
}

void CreditsPanel::ContentComponent::rebuildLayout(int targetWidth)
{
    leftMargin_  = 14;
    rightMargin_ = 14;

    const int  pad   = 10;
    const int  lineH = 14;

    int y = pad;

    // Title
    y += lineH;       // "// ALGORITHM ATTRIBUTIONS"
    y += lineH + 4;   // subtitle
    // Underline
    y += 6;

    // Section: Algorithm Inspirations
    y += lineH + 4;   // section header
    y += 6;            // sub-divider

    auto credits = buildAlgorithmCredits();
    for (int i = 0; i < credits.size(); ++i)
    {
        y += lineH + 1;  // project name + number
        y += lineH;       // author
        y += lineH;       // license
        y += lineH + 6;   // URL + gap
        y += 3;           // item separator
    }

    // Section: License Notices
    y += 8;            // gap + divider
    y += 8;
    y += lineH + 4;    // section header
    y += 6;            // sub-divider

    auto notices = buildLicenseNotices();
    for (int i = 0; i < notices.size(); ++i)
        y += (notices[i].isEmpty() ? 6 : (lineH - 2));

    // Footer
    y += 8;            // divider + gap
    y += 8;
    y += lineH - 2;    // "AnaPlug  —  Spectral Synthesizer"
    y += lineH - 2;    // "No icon assets..."
    y += lineH - 2;    // "See individual source headers..."

    y += pad;
    contentHeight_ = juce::jmax(y, 400);
    setSize(targetWidth, contentHeight_);
}

void CreditsPanel::ContentComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.fillAll(CyberpunkTheme::bg_);

    const auto fontBold   = CyberpunkTheme::getCyberFont(12.0f, true);
    const auto fontNormal = CyberpunkTheme::getCyberFont(11.0f, false);
    const auto fontSmall  = CyberpunkTheme::getCyberFont(10.0f, false);
    const auto fontURL    = CyberpunkTheme::getCyberFont(9.0f,  false);
    const int  pad        = 10;
    const int  lineH      = 14;
    const int  lm         = leftMargin_;
    const int  textW      = bounds.getWidth() - lm - rightMargin_;

    // -- Cyber grid background --
    CyberpunkTheme::drawGridBackground(g, bounds);

    // -- Title --
    {
        int y = pad;

        g.setFont(fontBold);
        g.setColour(CyberpunkTheme::cyan_);
        g.drawText("// ALGORITHM ATTRIBUTIONS", lm, y, textW, lineH,
                   juce::Justification::centredLeft);

        y += lineH;
        g.setFont(fontSmall);
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.45f));
        g.drawText("Credits, licenses, and inspirations", lm, y, textW, lineH,
                   juce::Justification::centredLeft);
        y += lineH + 4;

        g.setColour(CyberpunkTheme::cyan_.withAlpha(0.2f));
        g.drawHorizontalLine(y, lm, bounds.getWidth() - rightMargin_);
        y += 6;
    }

    // -- Section: Algorithm Inspirations --
    {
        int y = pad + lineH + lineH + 4 + 6;

        g.setFont(fontBold);
        g.setColour(CyberpunkTheme::magenta_);
        g.drawText(">> ALGORITHM INSPIRATIONS <<", lm, y, textW, lineH,
                   juce::Justification::centredLeft);
        y += lineH + 4;

        g.setColour(CyberpunkTheme::magenta_.withAlpha(0.2f));
        g.drawHorizontalLine(y, lm, bounds.getWidth() - rightMargin_);
        y += 6;

        auto credits = buildAlgorithmCredits();
        for (int i = 0; i < credits.size(); ++i)
        {
            const auto& c = credits[i];

            // Project name
            g.setFont(fontBold);
            g.setColour(CyberpunkTheme::cyan_);
            g.drawText(juce::String(i + 1) + ". " + c.project, lm + 4, y,
                       textW - 4, lineH,
                       juce::Justification::centredLeft);
            y += lineH + 1;

            // Author
            g.setFont(fontNormal);
            g.setColour(CyberpunkTheme::fg_);
            g.drawText("     Author:  " + c.author, lm + 4, y,
                       textW - 4, lineH,
                       juce::Justification::centredLeft);
            y += lineH;

            // License
            g.setFont(fontSmall);
            g.setColour(CyberpunkTheme::yellow_);
            g.drawText("     License: " + c.license, lm + 4, y,
                       textW - 4, lineH,
                       juce::Justification::centredLeft);
            y += lineH;

            // URL
            g.setFont(fontURL);
            g.setColour(CyberpunkTheme::fg_.withAlpha(0.5f));
            g.drawText("     " + c.url, lm + 4, y,
                       textW - 4, lineH,
                       juce::Justification::centredLeft);
            y += lineH + 6;

            // Item separator
            if (i < credits.size() - 1)
            {
                g.setColour(CyberpunkTheme::fg_.withAlpha(0.08f));
                g.drawHorizontalLine(y - 3, lm + 8, bounds.getWidth() - rightMargin_ - 8);
            }
        }
    }

    // -- Section: License Notices --
    {
        auto credits = buildAlgorithmCredits();
        int y = pad + lineH + lineH + 4 + 6;
        y += lineH + 4 + 6;
        for (int i = 0; i < credits.size(); ++i)
            y += lineH + 1 + lineH + lineH + lineH + 6 + 3;

        y += 8;

        // Section divider
        g.setColour(CyberpunkTheme::cyan_.withAlpha(0.15f));
        g.drawHorizontalLine(y, lm, bounds.getWidth() - rightMargin_);
        y += 8;

        g.setFont(fontBold);
        g.setColour(CyberpunkTheme::magenta_);
        g.drawText(">> LICENSE NOTICES <<", lm, y, textW, lineH,
                   juce::Justification::centredLeft);
        y += lineH + 4;

        g.setColour(CyberpunkTheme::magenta_.withAlpha(0.2f));
        g.drawHorizontalLine(y, lm, bounds.getWidth() - rightMargin_);
        y += 6;

        auto notices = buildLicenseNotices();
        for (int i = 0; i < notices.size(); ++i)
        {
            const auto& line = notices[i];

            if (line.isEmpty())
            {
                y += 6;
                continue;
            }

            if (line.trimStart().startsWith("http"))
            {
                g.setFont(fontURL);
                g.setColour(CyberpunkTheme::fg_.withAlpha(0.5f));
            }
            else
            {
                g.setFont(fontNormal);
                g.setColour(CyberpunkTheme::fg_);
            }

            g.drawText("  " + line.trimStart(), lm, y, textW, lineH - 2,
                       juce::Justification::centredLeft);
            y += lineH - 2;
        }

        // -- Footer --
        y += 8;
        g.setColour(CyberpunkTheme::cyan_.withAlpha(0.15f));
        g.drawHorizontalLine(y, lm, bounds.getWidth() - rightMargin_);
        y += 8;

        g.setFont(fontSmall);
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.35f));
        g.drawText("AnaPlug  \u2014  Spectral Synthesizer", lm, y, textW, lineH,
                   juce::Justification::centredLeft);
        y += lineH - 2;
        g.drawText("No icon assets from third-party sources.", lm, y,
                   textW, lineH - 2,
                   juce::Justification::centredLeft);
        y += lineH - 2;
        g.drawText("See individual source headers for detailed licensing.",
                   lm, y, textW, lineH - 2,
                   juce::Justification::centredLeft);
    }
}

} // namespace ana
