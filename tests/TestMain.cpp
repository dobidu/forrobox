/* ============================================================================
   FORRÓ BOX — test runner

   One executable, two suites. Both always run: a suite that is built but never
   executed reports nothing while still looking like coverage, which is exactly
   what happened when the Windows script ran a single named binary.
============================================================================ */
#include <JuceHeader.h>

#include "TestHarness.h"
#include "TestSuites.h"

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    runStateTests();
    runClockTests();

    return fbtest::reportSummary();
}
