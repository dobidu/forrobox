#pragma once

/* ============================================================================
   FORRÓ BOX — exit probe (test binary only)

   Built for one question: WHERE does `ForroBoxTests.exe` stop, when it prints
   its summary and then never exits? From 09-06 until 10-01 that happened under
   MSVC, blocked at 0% CPU, and nothing in the process said where.

   Off unless asked for — `FORROBOX_EXIT_PROBE=1` in the environment — so a
   normal run prints and times exactly what it did before. When on:

   - `mark()` writes one line per exit stage straight to the stderr HANDLE,
     unbuffered: the question is precisely what did not get flushed, so the
     markers must not sit in a stream buffer that a hang would strand.
   - `armWatchdog()` starts a detached thread. If the process is still alive
     `seconds` later, it walks the stack of every other thread, prints the
     frames, and ends the process with `kWatchdogExitCode` — distinct from the
     suite's 0 and 1, so a hang can never read as a pass.

   The watchdog can only see a hang that happens while threads still run, i.e.
   inside `main`'s own local destructors. Once `main` returns and the CRT calls
   `ExitProcess`, Windows terminates every other thread — the watchdog
   included — before static destructors and DLL detach run. A hang THERE
   prints no stacks at all, and that silence, with the last marker that did
   print, is itself the measurement.

   Elsewhere than Windows every call is a no-op: the Linux binaries have always
   exited cleanly, and the probe exists for the platform that did not.
============================================================================ */

namespace fbtest::exitprobe
{
    /** The exit code the watchdog ends a hung process with. */
    inline constexpr int kWatchdogExitCode = 3;

    /** True when `FORROBOX_EXIT_PROBE` is set to a non-empty value other than "0". */
    bool enabled();

    /** One line, `[exit-probe] <stage>`, straight to stderr. No-op unless enabled. */
    void mark (const char* stage);

    /** Registers the two markers that run after `main` returns: an `atexit`
        handler and a function-local static's destructor. Call it FIRST in
        `main`, so the handler runs last among handlers and the static is
        destroyed after every static constructed later. No-op unless enabled. */
    void installLateMarkers();

    /** Starts the watchdog. No-op unless enabled, and on anything but Windows. */
    void armWatchdog (unsigned seconds = 20);

    /** Declared beside a local, its destructor marks the moment that local's
        neighbour has been destroyed. Locals die in reverse declaration order,
        so a stage declared immediately BEFORE a local prints immediately AFTER
        that local is gone — without reordering anything `main` already owns. */
    struct Stage
    {
        const char* name;
        ~Stage() { mark (name); }
    };
}
