#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ana { class AnaPlugAudioProcessor; }

namespace ana::uiaudit
{

/**
    Offscreen UI audit: renders every editor page at several window sizes and
    checks the geometry of every visible control.

    The checks cover the failure modes that are invisible in a compile:
      * a visible control whose bounds fall outside its parent (clipped away),
      * a visible control squeezed to zero width or height,
      * sibling controls whose bounds intersect (visual occlusion),
      * interactive controls below a usable minimum size (cramped layout),
      * interactive controls without a tooltip (project rule).

    It runs inside the plugin process (see createPluginFilter()) because the
    editor needs the plugin's own classes, and because a plugin target is the
    only place where the JUCE plugin wrappers are available.  Output:

        <out>/report.txt                 full human-readable report
        <out>/report.json                machine-readable findings
        <out>/summary.txt                one-paragraph summary for CI
        <out>/<page>_<width>x<height>.png  one snapshot per page and size

    @returns the number of findings (0 = clean).
*/
int runAudit (AnaPlugAudioProcessor& processor,
              const juce::File& outputDir,
              juce::String& reportText);

/** Writes a short deterministic test sample and loads it into the processor so
    every page has real analysed data to draw. */
bool loadTestSample (AnaPlugAudioProcessor& processor, const juce::File& dir);

/** Entry point for createPluginFilter(): reads ANAPLUG_UI_AUDIT, runs the audit
    and terminates the process with 0 (clean) or 1 (findings).  Never returns. */
[[noreturn]] void runFromEnvironmentAndExit (const juce::String& outputDirPath);

} // namespace ana::uiaudit
