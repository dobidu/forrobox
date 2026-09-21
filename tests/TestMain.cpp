/* ============================================================================
   FORRÓ BOX — test runner

   One executable, three suites. All always run: a suite that is built but never
   executed reports nothing while still looking like coverage, which is exactly
   what happened when the Windows script ran a single named binary.
============================================================================ */
#include <JuceHeader.h>

#include "TestHarness.h"
#include "TestSuites.h"

/** Holds JUCE's shared timer thread open for the whole run.

    Not scaffolding — it is worth 37% of the voice suite's wall time.

    APVTS is a juce::Timer, so every processor the tests build and destroy is a
    timer. Measured per lifecycle: constructing one is 0.169 ms and
    prepareToPlay 0.567 ms, but the DESTRUCTOR takes 2.179 ms — and it is not
    work, it is a wait. When the rig is the only timer alive, destroying it tears
    down the shared timer thread and the next construction recreates it.
    Confirmed by control: one bare juce::Timer held for the process drops the
    cost from 2.650 ms to 0.472 ms per lifecycle, and keeping a whole extra
    processor alive instead gives the same 0.479 ms — so it is the thread, not
    the APVTS.

    183 lifecycles interleaved measured 493 ms; constructing all 183 first and
    then destroying them measured 130 ms. This closes that gap: the voice suite
    went 1023 ms -> 647 ms and the executable 1.25 s -> 0.87 s, with no change
    to what any test does. */
struct KeepTimersAlive final : juce::Timer
{
    KeepTimersAlive() { startTimer (60'000); }
    ~KeepTimersAlive() override { stopTimer(); }
    void timerCallback() override {}
};

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const KeepTimersAlive keepTimersAlive;

    // `--render-audition <dir>` renders the four profiles to WAVs instead of
    // running the suites. Folded into this executable rather than given a
    // target of its own, for the same reason the three suites share one: a
    // second target re-compiles the entire JUCE module set.
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--emit-midi")
            return emitMidiHexFromStdin();

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
    runUiTests();
    runMidiExportTests();

    return fbtest::reportSummary();
}
