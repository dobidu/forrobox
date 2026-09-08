/* ============================================================================
   FORRÓ BOX — test runner

   One executable, three suites. All always run: a suite that is built but never
   executed reports nothing while still looking like coverage, which is exactly
   what happened when the Windows script ran a single named binary.
============================================================================ */
#include <JuceHeader.h>

#include "TestHarness.h"
#include "TestSuites.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // `--render-audition <dir>` renders the four profiles to WAVs instead of
    // running the suites. Folded into this executable rather than given a
    // target of its own, for the same reason the three suites share one: a
    // second target re-compiles the entire JUCE module set.
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--render-audition")
        {
            renderAuditionFiles (i + 1 < argc ? juce::String (argv[i + 1])
                                              : juce::String ("audition"));
            return 0;
        }
    }

    runStateTests();
    runClockTests();
    runVoiceTests();

    return fbtest::reportSummary();
}
