#include "UiAudit.h"
#include "../PluginProcessor.h"
#include "../PluginEditor.h"

// WavAudioFormat/AudioFormatWriter live in juce_audio_formats, which the plugin
// target links (the processor reads audio files) but which no header pulled in
// for this translation unit: "error C2065: 'WavAudioFormat': undeclared".
#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <typeinfo>
#include <utility>
#include <vector>

namespace ana::uiaudit
{

namespace
{

//==============================================================================
// Reporting
//==============================================================================

struct Finding
{
    juce::String kind;
    juce::String path;
    juce::String detail;
};

juce::String prettyType (const juce::Component& c)
{
    juce::String name (typeid (c).name());

    name = name.replace ("class ", "").replace ("struct ", "").replace ("ana::", "");

    // MSVC decorates template instantiations heavily; keep the report readable.
    if (name.startsWith ("juce::"))
        name = name.upToFirstOccurrenceOf ("<", false, false);

    return name;
}

bool isControlLike (const juce::Component& c)
{
    return dynamic_cast<const juce::Slider*> (&c) != nullptr
        || dynamic_cast<const juce::TextButton*> (&c) != nullptr
        || dynamic_cast<const juce::ToggleButton*> (&c) != nullptr
        || dynamic_cast<const juce::ComboBox*> (&c) != nullptr
        || dynamic_cast<const juce::DrawableButton*> (&c) != nullptr;
}

/** Tooltips come from TooltipClient, not from Component - a control that is not
    also a tooltip client cannot show one at all. */
bool hasTooltip (juce::Component& c)
{
    // TooltipClient::getTooltip() is not const in JUCE 8, so this cannot take a
    // const reference: "cannot convert 'this' pointer from 'const juce::TooltipClient'".
    if (auto* client = dynamic_cast<juce::TooltipClient*> (&c))
        return client->getTooltip().trim().isNotEmpty();

    return false;
}

/** Minimum size a usable control needs, by kind.  Labels are text and are only
    checked for a non-zero height. */
void minimumSize (const juce::Component& c, int& minW, int& minH)
{
    minW = 1;
    minH = 1;

    if (auto* slider = dynamic_cast<const juce::Slider*> (&c))
    {
        // A rotary knob is drawn as a circle inside its bounds, so a knob given
        // a 22-pixel control row is not merely small - it is a dot.  This is the
        // check that catches "the layout looks cramped" in the one place where it
        // cannot be argued about.
        const auto style = slider->getSliderStyle();
        const bool rotary = style == juce::Slider::Rotary
                         || style == juce::Slider::RotaryHorizontalDrag
                         || style == juce::Slider::RotaryVerticalDrag
                         || style == juce::Slider::RotaryHorizontalVerticalDrag;

        if (rotary) { minW = 24; minH = 24; }
        else        { minW = 8;  minH = 8;  }
    }
    else if (dynamic_cast<const juce::TextButton*> (&c) != nullptr)  { minW = 12; minH = 8;  }
    else if (dynamic_cast<const juce::ToggleButton*> (&c) != nullptr){ minW = 8;  minH = 8;  }
    else if (dynamic_cast<const juce::ComboBox*> (&c) != nullptr)    { minW = 24; minH = 8;  }
    else if (dynamic_cast<const juce::DrawableButton*> (&c) != nullptr) { minW = 8; minH = 8; }
    else if (dynamic_cast<const juce::Label*> (&c) != nullptr)       { minW = 1;  minH = 6;  }
}

juce::String rectToString (juce::Rectangle<int> r)
{
    return juce::String (r.getX()) + "," + juce::String (r.getY())
         + " " + juce::String (r.getWidth()) + "x" + juce::String (r.getHeight());
}

//==============================================================================
// The audit itself
//==============================================================================

class Auditor
{
public:
    void audit (juce::Component& root, const juce::String& label)
    {
        walk (root, label);
    }

    std::vector<Finding> findings;
    int visibleControls = 0;
    int visibleComponents = 0;

    /** Control heights, bucketed: <12, 12-15, 16-19, 20-27, >=28 pixels.

        The design system says a control row is kControlHeight (20) tall, so this
        turns "the layout looks cramped" into a number: a UI built out of 8-pixel
        controls is not a matter of taste.  It is deliberately a report line rather
        than a finding, because genuinely small controls do exist (sequencer step
        cells, page tabs) and flagging each one would bury the real findings. */
    int heightBuckets[5] { 0, 0, 0, 0, 0 };

private:
    void add (const juce::String& kind, const juce::String& path, const juce::String& detail)
    {
        findings.push_back ({ kind, path, detail });
    }

    void walk (juce::Component& parent, const juce::String& path)
    {
        // A Viewport deliberately holds a component that is larger than itself
        // (that is what scrolls), and its scrollbars sit inside its own bounds.
        const bool parentIsViewport = dynamic_cast<juce::Viewport*> (&parent) != nullptr;

        std::vector<juce::Component*> visible;

        for (auto* child : parent.getChildren())
        {
            if (child == nullptr || ! child->isVisible())
                continue;

            ++visibleComponents;

            const auto childPath = path + "/" + prettyType (*child);

            if (dynamic_cast<juce::TooltipWindow*> (child) == nullptr)
                visible.push_back (child);

            // (1) zero size - the layout ran out of room and gave it nothing
            if (child->getWidth() <= 0 || child->getHeight() <= 0)
                add ("zero-size", childPath,
                     "visible but " + rectToString (child->getBounds()));

            // (2) outside the parent - clipped away, unreachable
            if (! parentIsViewport
                && ! parent.getLocalBounds().contains (child->getBounds()))
            {
                add ("outside-parent", childPath,
                     "child " + rectToString (child->getBounds())
                     + " parent " + rectToString (parent.getLocalBounds()));
            }

            // (3) too small to use
            int minW = 1, minH = 1;
            minimumSize (*child, minW, minH);

            if (isControlLike (*child))
            {
                ++visibleControls;

                const int h = child->getHeight();
                heightBuckets[h < 12 ? 0 : h < 16 ? 1 : h < 20 ? 2 : h < 28 ? 3 : 4]++;

                if (child->getWidth() < minW || child->getHeight() < minH)
                    add ("too-small", childPath,
                         rectToString (child->getBounds())
                         + " needs at least " + juce::String (minW) + "x" + juce::String (minH));

                // (4) the project rule: every control explains itself
                if (! hasTooltip (*child))
                    add ("no-tooltip", childPath, rectToString (child->getBounds()));
            }
            else if (auto* label = dynamic_cast<juce::Label*> (child))
            {
                if (label->getHeight() < minH)
                    add ("too-small", childPath, rectToString (child->getBounds()));

                // A label narrower than its own text is the classic cramped
                // layout defect: JUCE squeezes the glyphs or clips them.
                const auto text = label->getText();

                if (text.isNotEmpty())
                {
                    // JUCE 8 removed Font::getStringWidth; the replacement lives on
                    // GlyphArrangement (modules/juce_graphics/fonts/juce_GlyphArrangement.h,
                    // pinned 8.0.13: static int getStringWidthInt (const Font&, StringRef)).
                    const auto needed = juce::GlyphArrangement::getStringWidthInt (label->getFont(), text);
                    const auto room   = child->getWidth();

                    // JUCE squeezes label text horizontally down to
                    // minimumHorizontalScale (0.7 by default) before it clips, so
                    // needing more than room / 0.7 is genuinely illegible.
                    if (needed * 10 > room * 15)
                        add ("text-overflow", childPath,
                             "\"" + text + "\" needs " + juce::String (needed)
                             + " px, label is " + juce::String (room) + " px");
                    else if (needed > room + 1)
                        add ("text-tight", childPath,
                             "\"" + text + "\" needs " + juce::String (needed)
                             + " px, label is " + juce::String (room) + " px");
                }
            }

            walk (*child, childPath);
        }

        // (5) visible siblings that intersect: one is drawn over the other
        for (size_t i = 0; i < visible.size(); ++i)
        {
            for (size_t j = i + 1; j < visible.size(); ++j)
            {
                const auto a = visible[i]->getBounds();
                const auto b = visible[j]->getBounds();
                const auto overlap = a.getIntersection (b);

                if (overlap.getWidth() * overlap.getHeight() <= 4)
                    continue;

                add ("overlap", path,
                     prettyType (*visible[i]) + " " + rectToString (a)
                     + " x " + prettyType (*visible[j]) + " " + rectToString (b)
                     + " -> " + rectToString (overlap));
            }
        }
    }
};

//==============================================================================
// Snapshots
//==============================================================================

bool savePng (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
    {
        juce::PNGImageFormat png;
        return png.writeImageToStream (image, *stream);
    }

    return false;
}

/** What a rendered snapshot actually contains.  The PNGs themselves are only
    reachable through the CI artifact, so the numbers (and the ASCII ink map)
    are what makes a snapshot reviewable from the log alone. */
struct SnapshotStats
{
    juce::String status = "ok";
    double inkRatio = 0.0;      // fraction of pixels that differ from the background
    int distinctColours = 0;
    juce::String inkMap;        // downsampled view of what was drawn, one line per row
};

SnapshotStats renderSnapshot (juce::Component& component, const juce::File& file)
{
    SnapshotStats stats;

    if (component.getWidth() <= 0 || component.getHeight() <= 0)
    {
        stats.status = "skipped (no size)";
        return stats;
    }

    juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);

    {
        juce::Graphics g (image);
        component.paintEntireComponent (g, true);
    }

    stats.status = savePng (image, file) ? "ok" : "png write failed";

    // ---- ink statistics + ASCII map -------------------------------------
    const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);

    constexpr int cols = 48;
    constexpr int rows = 14;

    // The background is whatever colour the top-left corner is painted with.
    const auto background = pixels.getPixelColour (0, 0);

    juce::Array<double> cellInk;
    cellInk.resize (cols * rows);
    cellInk.fill (0.0);

    const auto stepX = juce::jmax (1, image.getWidth() / cols);
    const auto stepY = juce::jmax (1, image.getHeight() / rows);

    long long total = 0;
    long long inked = 0;

    // Distinct colours are sampled coarsely: a per-pixel set would cost more than
    // the render itself, and the number is only used to spot a flat snapshot.
    std::vector<juce::uint32> sampledColours;
    sampledColours.reserve (static_cast<size_t> (image.getWidth() / 8 + 1)
                            * static_cast<size_t> (image.getHeight() / 8 + 1));

    for (int y = 0; y < image.getHeight(); y += 2)
    {
        for (int x = 0; x < image.getWidth(); x += 2)
        {
            const auto c = pixels.getPixelColour (x, y);
            ++total;

            if ((x % 8) == 0 && (y % 8) == 0)
                sampledColours.push_back (c.getARGB());

            const auto differs = std::abs (static_cast<int> (c.getRed())   - static_cast<int> (background.getRed()))
                               + std::abs (static_cast<int> (c.getGreen()) - static_cast<int> (background.getGreen()))
                               + std::abs (static_cast<int> (c.getBlue())  - static_cast<int> (background.getBlue()));

            if (differs > 24)
            {
                ++inked;

                const auto cell = juce::jlimit (0, rows - 1, y / stepY) * cols
                                + juce::jlimit (0, cols - 1, x / stepX);
                cellInk.setUnchecked (cell, cellInk[cell] + 1.0);
            }
        }
    }

    stats.inkRatio = total > 0 ? static_cast<double> (inked) / static_cast<double> (total) : 0.0;
    std::sort (sampledColours.begin(), sampledColours.end());
    sampledColours.erase (std::unique (sampledColours.begin(), sampledColours.end()),
                          sampledColours.end());

    stats.distinctColours = static_cast<int> (sampledColours.size());

    static const char* ramp = " .:-=+*#%@";
    const auto cellSamples = static_cast<double> (juce::jmax (1, stepX * stepY / 4));

    juce::StringArray mapRows;

    for (int row = 0; row < rows; ++row)
    {
        juce::String line;

        for (int col = 0; col < cols; ++col)
        {
            const auto ratio = juce::jlimit (0.0, 1.0, cellInk[row * cols + col] / cellSamples);
            const auto index = juce::jlimit (0, 9, static_cast<int> (std::sqrt (ratio) * 9.99));
            line += ramp[index];
        }

        mapRows.add (line);
    }

    stats.inkMap = mapRows.joinIntoString ("\n");

    return stats;
}

} // namespace

//==============================================================================
bool loadTestSample (AnaPlugAudioProcessor& processor, const juce::File& dir)
{
    const auto file = dir.getChildFile ("ui-audit-sample.wav");
    file.deleteFile();

    constexpr double sampleRate = 44100.0;
    constexpr int    numSamples = static_cast<int> (sampleRate * 1.5);
    constexpr int    channels   = 2;

    juce::AudioBuffer<float> buffer (channels, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const double t       = static_cast<double> (i) / sampleRate;
        const double vibrato = 1.0 + 0.01 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t);
        const double env     = std::exp (-2.2 * t) * (1.0 - std::exp (-40.0 * t));

        double value = 0.0;

        for (int h = 1; h <= 12; ++h)
            value += (1.0 / static_cast<double> (h))
                   * std::sin (2.0 * juce::MathConstants<double>::pi
                               * 220.0 * static_cast<double> (h) * vibrato * t);

        value *= env * 0.25;

        for (int ch = 0; ch < channels; ++ch)
            buffer.setSample (ch, i, static_cast<float> (value));
    }

    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate, static_cast<unsigned int> (channels), 24, {}, 0));

    if (writer == nullptr)
        return false;

    stream.release();   // the writer owns it now
    writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
    writer.reset();

    return processor.loadFile (file);
}

//==============================================================================
int runAudit (AnaPlugAudioProcessor& processor, const juce::File& outputDir, juce::String& reportText)
{
    outputDir.createDirectory();

    const auto sampleLoaded = loadTestSample (processor, outputDir);

    std::unique_ptr<juce::AudioProcessorEditor> editorOwner (processor.createEditor());
    auto* editor = dynamic_cast<AnaPlugAudioProcessorEditor*> (editorOwner.get());

    if (editor == nullptr)
    {
        reportText = "UI AUDIT: could not create the editor\n";
        outputDir.getChildFile ("summary.txt").replaceWithText (reportText);
        return 1;
    }

    // Below the editor's own resize floor JUCE clamps silently, so the audit
    // covers the documented minimum and a few realistic sizes.
    const std::pair<int, int> sizes[] =
    {
        { 900, 660 },     // documented minimum
        { 1100, 780 },    // default
        { 1280, 860 },
        { 1600, 1000 },
        { 1920, 1200 }    // maximum
    };

    const int numPages = AnaPlugAudioProcessorEditor::getNumPages();

    std::vector<Finding> all;
    juce::StringArray lines;
    int snapshots = 0;
    int visibleControls = 0;
    int visibleComponents = 0;
    int heightBuckets[5] { 0, 0, 0, 0, 0 };

    lines.add ("AnaPlug UI audit");
    lines.add ("================");
    lines.add ("sample loaded: " + juce::String (sampleLoaded ? "yes" : "no"));
    lines.add ("");

    // One snapshot per (size, page) plus one per spectrum view mode, since the
    // view area swaps in a different tool row for IMAGE and different canvases
    // for the rest.
    juce::StringArray inkMaps;

    // Every snapshot is also listed in an HTML page, so the whole UI can be
    // reviewed one page at a time by opening a single file from the artifact.
    struct Snapshot { juce::String tag, png, summary; bool blank = false; };
    std::vector<Snapshot> gallery;

    auto capture = [&] (const juce::String& tag, const juce::String& fileName)
    {
        const auto pngName = juce::File::createLegalFileName (fileName) + ".png";
        const auto file = outputDir.getChildFile (pngName);
        const auto stats = renderSnapshot (*editor, file);
        ++snapshots;

        Auditor auditor;
        auditor.audit (*editor, tag);

        visibleControls   += auditor.visibleControls;
        visibleComponents += auditor.visibleComponents;

        for (int b = 0; b < 5; ++b)
            heightBuckets[b] += auditor.heightBuckets[b];

        const auto inkPercent = stats.inkRatio * 100.0;

        const auto controlCount  = juce::String (auditor.visibleControls);
        const auto componentCount = juce::String (auditor.visibleComponents);
        const auto inkText       = juce::String (inkPercent, 2);
        const auto findingCount  = juce::String (static_cast<int> (auditor.findings.size()));

        lines.add (tag
                   + "  controls=" + controlCount
                   + "  components=" + componentCount
                   + "  ink=" + inkText + "%"
                   + "  colours=" + juce::String (stats.distinctColours)
                   + "  findings=" + findingCount
                   + "  png=" + stats.status);

        // A page that paints almost nothing is broken however clean its geometry
        // is - this is what caught an EVO page whose panel was never created.
        const bool blank = (stats.inkRatio < 0.002 && stats.status == "ok");

        if (blank)
            all.push_back ({ "blank-snapshot", tag,
                             "only " + inkText + "% of the pixels differ from the background" });

        gallery.push_back ({ tag, pngName,
                             "controls " + controlCount + " | components " + componentCount
                             + " | ink " + inkText + "% | findings " + findingCount,
                             blank });

        for (const auto& f : auditor.findings)
            all.push_back (f);

        inkMaps.add ("--- " + tag + "  ink=" + juce::String (inkPercent, 2) + "%");
        inkMaps.add (stats.inkMap);
    };

    for (const auto& size : sizes)
    {
        editor->setSize (size.first, size.second);

        const auto actualW = editor->getWidth();
        const auto actualH = editor->getHeight();
        const auto sizeTag = juce::String (actualW) + "x" + juce::String (actualH);

        for (int page = 0; page < numPages; ++page)
        {
            editor->showPage (page);

            // The editor paints live state; one manual timer tick fills every
            // page's read-outs and canvases before anything is rendered.
            editor->timerCallback();

            const auto pageName = AnaPlugAudioProcessorEditor::getPageName (page);

            capture (sizeTag + "  " + pageName,
                     juce::String (page) + "-" + pageName + "_" + sizeTag);
        }
    }

    // Spectrum view modes at the default and the minimum size (the view area is
    // shared by every page, and IMAGE adds a tool row that only exists there).
    for (const auto& size : { std::pair<int, int> { 900, 660 },
                              std::pair<int, int> { 1100, 780 } })
    {
        editor->setSize (size.first, size.second);
        editor->showPage (0);

        const auto sizeTag = juce::String (editor->getWidth()) + "x"
                           + juce::String (editor->getHeight());

        const int numModes = editor->getNumViewModes();

        for (int mode = 0; mode < numModes; ++mode)
        {
            // The 3D view draws through an OpenGL context that replaces component
            // painting entirely (setComponentPaintingEnabled(false)), so an offscreen
            // software snapshot of it would be empty - and attaching a context that
            // has no peer is not something to do in a headless job.
            if (editor->getViewModeName (mode).containsIgnoreCase ("3D"))
            {
                lines.add (sizeTag + "  VIEW " + editor->getViewModeName (mode)
                           + "  skipped (OpenGL surface, not renderable offscreen)");
                continue;
            }

            editor->showViewMode (mode);
            editor->timerCallback();

            capture (sizeTag + "  VIEW " + editor->getViewModeName (mode),
                     "view-" + editor->getViewModeName (mode) + "_" + sizeTag);
        }

        // Back to a software-painted view before anything else is rendered.
        editor->showViewMode (3);
        editor->showViewMode (0);
    }

    // ---- summarise by kind -------------------------------------------------
    // Counts are taken from the complete list; the printed list keeps at most 12
    // entries per kind so that a noisy check cannot bury the interesting ones.
    std::map<juce::String, int> byKind;
    std::map<juce::String, int> printedPerKind;
    std::vector<Finding> printed;

    for (const auto& f : all)
    {
        ++byKind[f.kind];

        if (printedPerKind[f.kind] < 12)
        {
            ++printedPerKind[f.kind];
            printed.push_back (f);
        }
    }

    juce::String summary;
    summary << "UI AUDIT: " << snapshots << " snapshots, "
            << visibleControls << " visible controls, "
            << static_cast<int> (all.size()) << " findings";

    for (const auto& entry : byKind)
        summary << " | " << entry.first << "=" << entry.second;

    // How cramped is it really?  One line, so it survives into the CI annotation.
    const juce::String heightLine =
        "control heights: <12px=" + juce::String (heightBuckets[0])
      + " 12-15px=" + juce::String (heightBuckets[1])
      + " 16-19px=" + juce::String (heightBuckets[2])
      + " 20-27px=" + juce::String (heightBuckets[3])
      + " >=28px=" + juce::String (heightBuckets[4])
      + " (design row = " + juce::String (CyberpunkTheme::kControlHeight) + "px)";

    lines.add ("");
    lines.add (heightLine);
    lines.add (summary);
    lines.add ("");
    lines.add ("findings (max 12 per kind, counts above are complete):");

    for (const auto& f : printed)
        lines.add ("  [" + f.kind + "] " + f.path + " :: " + f.detail);

    reportText = lines.joinIntoString ("\n") + "\n";

    outputDir.getChildFile ("report.txt").replaceWithText (reportText);
    outputDir.getChildFile ("summary.txt").replaceWithText (heightLine + "\n" + summary + "\n");
    outputDir.getChildFile ("inkmap.txt").replaceWithText (inkMaps.joinIntoString ("\n"));

    // The minimum supported window is where a cramped layout shows up first, so
    // those maps get published into the CI annotation (the artifact is not always
    // reachable from a dev machine).
    juce::StringArray smallMaps;

    for (int i = 0; i < inkMaps.size(); ++i)
        if (inkMaps[i].startsWith ("--- 900x660"))
            smallMaps.add (inkMaps[i] + "\n" + (i + 1 < inkMaps.size() ? inkMaps[i + 1] : juce::String()));

    outputDir.getChildFile ("inkmap-minimum.txt").replaceWithText (smallMaps.joinIntoString ("\n"));

    // Machine-readable copy for tooling that should not parse prose.
    juce::DynamicObject::Ptr root (new juce::DynamicObject());
    root->setProperty ("snapshots", snapshots);
    root->setProperty ("visibleControls", visibleControls);
    root->setProperty ("visibleComponents", visibleComponents);
    root->setProperty ("findings", static_cast<int> (all.size()));

    juce::Array<juce::var> jsonFindings;

    for (const auto& f : all)
    {
        juce::DynamicObject::Ptr o (new juce::DynamicObject());
        o->setProperty ("kind", f.kind);
        o->setProperty ("path", f.path);
        o->setProperty ("detail", f.detail);
        jsonFindings.add (juce::var (o.get()));
    }

    root->setProperty ("items", jsonFindings);

    outputDir.getChildFile ("report.json")
             .replaceWithText (juce::JSON::toString (juce::var (root.get()), false));

    // ---- HTML gallery ------------------------------------------------------
    // "Show me the UI page by page": one self-contained file, every snapshot,
    // every finding, no script and no external assets.
    {
        juce::String html;
        html << "<!doctype html><meta charset='utf-8'><title>AnaPlug UI audit</title>"
             << "<style>body{background:#0b0d12;color:#d8e0f0;font:13px/1.5 Consolas,monospace;margin:16px}"
             << "h1{font-size:16px;color:#5ff} h2{font-size:14px;color:#f5f;margin-top:22px}"
             << ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:10px}"
             << ".card{border:1px solid #263;border-radius:4px;padding:6px;background:#11141b}"
             << ".card img{width:100%;height:auto;display:block;border:1px solid #222}"
             << ".tag{color:#5ff} .bad{color:#f66} .muted{color:#889} table{border-collapse:collapse}"
             << "td,th{border:1px solid #263;padding:3px 6px;text-align:left;vertical-align:top}</style>";
        html << "<h1>AnaPlug UI audit</h1>";
        html << "<p>" << summary.replace ("&", "&amp;").replace ("<", "&lt;") << "</p>";
        html << "<p>sample loaded: " << (sampleLoaded ? "yes" : "no") << "</p>";

        if (! byKind.empty())
        {
            html << "<h2>findings by kind</h2><table><tr><th>kind</th><th>count</th></tr>";

            for (const auto& entry : byKind)
                html << "<tr><td>" << entry.first << "</td><td>" << entry.second << "</td></tr>";

            html << "</table>";
        }

        if (! printed.empty())
        {
            html << "<h2>findings</h2><table><tr><th>kind</th><th>where</th><th>detail</th></tr>";

            for (const auto& fnd : printed)
                html << "<tr><td>" << fnd.kind << "</td><td>" << fnd.path.replace ("<", "&lt;")
                     << "</td><td>" << fnd.detail.replace ("<", "&lt;") << "</td></tr>";

            html << "</table>";
        }

        html << "<h2>snapshots (" << snapshots << ")</h2><div class='grid'>";

        for (const auto& s : gallery)
            html << "<div class='card'><div class='tag'>" << s.tag.replace ("<", "&lt;")
                 << "</div><img src='" << s.png << "' alt=''><div class='"
                 << (s.blank ? "bad" : "muted") << "'>" << s.summary << "</div></div>";

        html << "</div>";

        outputDir.getChildFile ("index.html").replaceWithText (html);
    }

    return static_cast<int> (all.size());
}

//==============================================================================
[[noreturn]] void runFromEnvironmentAndExit (const juce::String& outputDirPath)
{
    const juce::File outputDir (outputDirPath);

    // The host application owns the MessageManager; RenderTarget-free offscreen
    // painting only needs one to exist.
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> initialiser;

    if (juce::MessageManager::getInstanceWithoutCreating() == nullptr)
        initialiser = std::make_unique<juce::ScopedJuceInitialiser_GUI>();

    // A crash here would leave CI with no report and no explanation, which is the
    // one outcome that cannot be diagnosed from this machine (no artifact, no log
    // download).  Whatever goes wrong, write something readable first.
    int findings = 1;

    try
    {
        AnaPlugAudioProcessor processor;
        processor.prepareToPlay (44100.0, 512);

        juce::String report;
        findings = runAudit (processor, outputDir, report);

        std::cout << report << std::flush;
    }
    catch (const std::exception& e)
    {
        const juce::String message = juce::String ("UI AUDIT CRASHED: ") + e.what();
        outputDir.createDirectory();   // the crash may have happened before runAudit did
        outputDir.getChildFile ("summary.txt").replaceWithText (message + "\n");
        std::cout << message << std::endl;
        findings = 1;
    }
    catch (...)
    {
        const juce::String message = "UI AUDIT CRASHED: unknown exception";
        outputDir.createDirectory();
        outputDir.getChildFile ("summary.txt").replaceWithText (message + "\n");
        std::cout << message << std::endl;
        findings = 1;
    }

    std::cout << "UI AUDIT EXIT: " << findings << " findings" << std::endl;

    // _Exit skips static destruction: the plugin wrappers install singletons that
    // are not meant to be torn down from this early entry point.
    std::_Exit (findings == 0 ? 0 : 1);
}

} // namespace ana::uiaudit
