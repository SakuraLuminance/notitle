#include "UiAudit.h"

#include <cstdlib>
#include <iostream>

//==============================================================================
/**
    Entry point for the offscreen UI audit runner.

    CI has two ways to run the audit.  The first is the plugin's own Standalone
    build: it needs no host either, but it drags in JUCE's standalone wrapper, the
    audio-device panel and the settings window, and for several runs the audit step
    reported success while producing no report at all - the standalone artefact was
    never built.  This console application links the same shared code, so it is
    subject to none of that, and it makes ANAPLUG_UI_AUDIT work without depending on
    where the standalone wrapper calls createPluginFilter().

    The audit itself (UiAudit.cpp) is shared: it renders every page at every
    supported window size with the real editor, checks each control's geometry and
    writes summary.txt / report.txt / inkmap*.txt / index.html / the PNG snapshots.
*/
int main (int argc, char** argv)
{
    juce::String outputDir;

    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]).isNotEmpty())
            outputDir = juce::String (argv[i]);

    if (outputDir.isEmpty())
        if (const auto* fromEnv = std::getenv ("ANAPLUG_UI_AUDIT"))
            outputDir = juce::String (fromEnv);

    if (outputDir.isEmpty())
    {
        std::cout << "usage: AnaPlugUiAudit <output-directory>   (or set ANAPLUG_UI_AUDIT)"
                  << std::endl;
        return 2;
    }

    // runFromEnvironmentAndExit never returns: it writes the report and exits with
    // the number of findings, so CI can treat a clean audit as a passing step.
    ana::uiaudit::runFromEnvironmentAndExit (outputDir);
}
