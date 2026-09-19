#include "UiAudit.h"
#include "../PluginProcessor.h"
#include "../PluginEditor.h"

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

bool isSliderLike (const juce::Component& c)
{
    return dynamic_cast<const juce::Slider*> (&c) != nullptr
        || dynamic_cast<const juce::TextButton*> (&c) != nullptr
        || dynamic_cast<const juce::ToggleButton*> (&c) != nullptr
        || dynamic_cast<const juce::ComboBox*> (&c) != nullptr
        || dynamic_cast<const juce::DrawableButton*> (&c) != nullptr;
}

/** Minimum size a usable control needs, by kind.  Labels are text and are only
    checked for a non-zero height. */
void minimumSize (const juce::Component& c, int& minW, int& minH)
{
    minW = 1;
    minH = 1;

    if (dynamic_cast<const juce::Slider*> (&c) != nullptr)          { minW = 8;  minH = 8;  }
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

            if (isSliderLike (*child))
            {
                ++visibleControls;

                if (child->getWidth() < minW || child->getHeight() < minH)
                    add ("too-small", childPath,
                         rectToString (child->getBounds())
                         + " needs at least " + juce::String (minW) + "x" + juce::String (minH));

                // (4) the project rule: every control explains itself
                if (child->getTooltip().trim().isEmpty())
                    add ("no-tooltip", childPath, rectToString (child->getBounds()));
            }
            else if (dynamic_cast<juce::Label*> (child) != nullptr
                     && child->getHeight() < minH)
            {
                add ("too-small", childPath, rectToString (child->getBounds()));
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

juce::String renderSnapshot (juce::Component& component, const juce::File& file)
{
    if (component.getWidth() <= 0 || component.getHeight() <= 0)
        return "skipped (no size)";

    juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);

    {
        juce::Graphics g (image);
        component.paintEntireComponent (g, true);
    }

    return savePng (image, file) ? "ok" : "png write failed";
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

    lines.add ("AnaPlug UI audit");
    lines.add ("================");
    lines.add ("sample loaded: " + juce::String (sampleLoaded ? "yes" : "no"));
    lines.add ("");

    // One snapshot per (size, page) plus one per spectrum view mode, since the
    // view area swaps in a different tool row for IMAGE and different canvases
    // for the rest.
    auto capture = [&] (const juce::String& tag, const juce::String& fileName)
    {
        const auto file = outputDir.getChildFile (juce::File::createLegalFileName (fileName) + ".png");
        const auto status = renderSnapshot (*editor, file);
        ++snapshots;

        Auditor auditor;
        auditor.audit (*editor, tag);

        visibleControls   += auditor.visibleControls;
        visibleComponents += auditor.visibleComponents;

        lines.add (tag
                   + "  controls=" + juce::String (auditor.visibleControls)
                   + "  components=" + juce::String (auditor.visibleComponents)
                   + "  findings=" + juce::String (static_cast<int> (auditor.findings.size()))
                   + "  png=" + status);

        for (const auto& f : auditor.findings)
            all.push_back (f);
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
            editor->showViewMode (mode);
            editor->timerCallback();

            capture (sizeTag + "  VIEW " + editor->getViewModeName (mode),
                     "view-" + editor->getViewModeName (mode) + "_" + sizeTag);
        }

        editor->showViewMode (0);
    }

    // ---- summarise by kind -------------------------------------------------
    std::map<juce::String, int> byKind;

    for (const auto& f : all)
        ++byKind[f.kind];

    juce::String summary;
    summary << "UI AUDIT: " << snapshots << " snapshots, "
            << visibleControls << " visible controls, "
            << static_cast<int> (all.size()) << " findings";

    for (const auto& entry : byKind)
        summary << " | " << entry.first << "=" << entry.second;

    lines.add ("");
    lines.add (summary);
    lines.add ("");
    lines.add ("findings (first 60):");

    int shown = 0;
    for (const auto& f : all)
    {
        if (shown++ >= 60)
        {
            lines.add ("  ... " + juce::String (static_cast<int> (all.size()) - 60) + " more");
            break;
        }

        lines.add ("  [" + f.kind + "] " + f.path + " :: " + f.detail);
    }

    reportText = lines.joinIntoString ("\n") + "\n";

    outputDir.getChildFile ("report.txt").replaceWithText (reportText);
    outputDir.getChildFile ("summary.txt").replaceWithText (summary + "\n");

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

    AnaPlugAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    juce::String report;
    const int findings = runAudit (processor, outputDir, report);

    std::cout << report << std::flush;
    std::cout << "UI AUDIT EXIT: " << findings << " findings" << std::endl;

    // _Exit skips static destruction: the plugin wrappers install singletons that
    // are not meant to be torn down from this early entry point.
    std::_Exit (findings == 0 ? 0 : 1);
}

} // namespace ana::uiaudit
