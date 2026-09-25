/* ============================================================================
   FORRÓ BOX — test runner

   One executable, three suites. All always run: a suite that is built but never
   executed reports nothing while still looking like coverage, which is exactly
   what happened when the Windows script ran a single named binary.
============================================================================ */
#include <JuceHeader.h>

#include <future>

#include "ExitProbe.h"
#include "TestHarness.h"
#include "Settings.h"
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
    // Exit-stage markers, OFF unless FORROBOX_EXIT_PROBE is set — see ExitProbe.h.
    // Each `Stage` is declared immediately BEFORE the local it reports on, so it
    // is destroyed immediately AFTER it: the order of the locals themselves is
    // unchanged, and it is load-bearing (see below).
    fbtest::exitprobe::installLateMarkers();
    const fbtest::exitprobe::Stage afterJuceShutdown { "juceInit destroyed (shutdownJuce_GUI returned)" };
    juce::ScopedJuceInitialiser_GUI juceInit;

    const fbtest::exitprobe::Stage afterTimers { "keepTimersAlive destroyed" };
    const KeepTimersAlive keepTimersAlive;

    // ── THE SUITE NEVER TOUCHES THE DEVELOPER'S OWN PREFERENCES ────────────
    //
    // 08-02 gave the plugin a GLOBAL settings store, and `PluginEditor` seeds
    // the LookAndFeel from it — so the moment that landed, every test that
    // builds an editor started reading a file outside this repository. The
    // suite passed only because the machine that ran it happened to have no
    // stored theme: setting the theme to Light in the plugin and re-running
    // would have failed six light/dark render checks, for a reason no diff
    // could show.
    //
    // This is that whole class closed at the top rather than test by test. Every
    // suite from here down reads a temp file that starts empty, so the DEFAULTS
    // are what tests see unless a test says otherwise — and the settings tests
    // redirect again inside this. That nesting is why `ScopedTestFile` restores
    // the PREVIOUS file rather than reopening the default: the first version
    // reopened the default, so the first inner scope to close handed every later
    // test the real user's file, and this guard did nothing from that point on.
    // Caught by writing a Light theme into the real file and re-running: 13
    // checks failed with the guard in place.
    const auto settingsPath = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("forrobox-suite-settings-"
                                                 + juce::String (juce::Time::getHighResolutionTicks())
                                                 + ".settings");

    // ORDER IS LOAD-BEARING, the same way `ScopedSettingsFile` says it is:
    // locals are destroyed in REVERSE declaration order, so the redirect must be
    // declared LAST to be torn down FIRST. Declared the other way round, the
    // temp file was deleted while the store was still pointed at it.
    // /code-review.
    const fbtest::exitprobe::Stage afterRemove { "removeSettingsFile ran" };
    const juce::ScopeGuard removeSettingsFile { [&settingsPath] { settingsPath.deleteFile(); } };
    const fbtest::exitprobe::Stage afterIsolated { "isolatedSettings destroyed" };
    const forrobox::Settings::ScopedTestFile isolatedSettings (settingsPath);

    // `--render-audition <dir>` renders every GROOVE of every profile to a WAV and a MID instead of
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

            // reportSummary, NOT 0. 09-04 made the renders assert — finite, not
            // silent, not clipping, and carrying the groove they are named for —
            // and an assertion whose failure cannot reach the exit code is the
            // kind of check this project keeps finding. Returning 0 here would
            // hand a wrong audition to a listener with a clean exit.
            return fbtest::reportSummary();
        }
    }

    // `--exit-probe-selftest` proves the probe on the one case it exists for: a
    // local whose destructor never returns. With FORROBOX_EXIT_PROBE=1 the run
    // must end by the WATCHDOG — code 3, a stack naming this wait — rather than
    // hang or exit 0. An instrument that has never caught anything proves nothing.
    struct NeverReturns
    {
        bool armed = false;
        ~NeverReturns()
        {
            if (armed)
                std::promise<void>().get_future().wait();
        }
    };
    NeverReturns selfTestHang;

    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) != "--exit-probe-selftest")
            continue;

        // Without the probe nothing would ever end it: refuse rather than hang.
        if (! fbtest::exitprobe::enabled())
        {
            std::cerr << "--exit-probe-selftest needs FORROBOX_EXIT_PROBE=1\n";
            return 2;
        }

        selfTestHang.armed = true;
    }

    if (selfTestHang.armed)
    {
        fbtest::exitprobe::mark ("self-test: a destructor that never returns is next");
        fbtest::exitprobe::armWatchdog (5);
        return 0;
    }

    runStateTests();
    runClockTests();
    runVoiceTests();
    runUiTests();
    runMidiExportTests();

    const int result = fbtest::reportSummary();
    fbtest::exitprobe::mark ("summary printed; main's locals are destroyed next");
    fbtest::exitprobe::armWatchdog();
    return result;
}
