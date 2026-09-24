#pragma once

/*  The impulse-response stage — PLANNING.md:841's "convolution stage".

    WHERE IT SITS: on the summed voices, BEFORE MixBus. `MixBus::process` is a
    per-sample loop with the character bus, the limiter and the master
    interleaved, and `juce::dsp::Convolution` is block-based — it cannot go
    inside that loop, and splitting the one audio path this project has kept
    proven since 03-03 to make room is not worth the risk.

    So the chain is: voices -> convolution -> character -> limiter -> master.
    Two consequences, both deliberate:

      - the character bus colours the convolved signal rather than the reverse,
        which is a tape machine after a room mic and is ordinary, though it is
        not the literal reading of the side panel's `TIMBRE / CONVOLUTION`
        ordering;
      - THE LIMITER STILL PROTECTS THE OUTPUT. An impulse response with gain
        cannot push past it. That is the property that decided the placement.

    ITS WET IS `conv_mix`, NOT `MIX`. `PLANNING.md:841` says "MIX becomes the
    convolution wet amount", which would make `char_mix` — live and automatable
    since 03-03 — mean two different things depending on whether a file happened
    to load. The user chose a separate parameter at 09-07 planning, which is the
    resolution PROJECT.md names for prototype/plugin conflicts and the precedent
    07-03 set exactly: `midi_gate` is a parameter with no UI, because the design
    source specifies no control and inventing one is forbidden.
*/

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <memory>

namespace forrobox
{

class Convolver
{
public:
    /** Allocates. Message/prepare thread only. */
    void prepare (double sampleRate, int maximumBlockSize, int numChannels);

    void reset() noexcept;

    /** Mixes the convolved signal in at `wetPercent`, 0..100.

        AT 0 THIS DOES NO WORK: it returns before the FFT, which is the whole
        cost of the stage. The OUTPUT would be bit-identical either way — the
        blend's operations are exact at zero — so this is a CPU decision, not a
        correctness one, and the comment in the .cpp records how an overclaim to
        the contrary was found.

        Real-time safe: no allocation, no lock, no file access. */
    void process (juce::AudioBuffer<float>& buffer, float wetPercent) noexcept;

    /** Reads `file` and hands it to the convolution engine.

        NEVER FROM THE AUDIO THREAD. `juce::dsp::Convolution` does its own work
        in the background, but this opens a file to get there. Returns false and
        changes nothing if the file cannot be read — a missing IR costs a reverb,
        not a session. */
    bool loadImpulseResponse (const juce::File& file);

    /** Goes dry. Does NOT destroy the engine — see `engineReady`. */
    void clear() noexcept;

    bool hasImpulseResponse() const noexcept
    { return engineReady.load (std::memory_order_acquire); }

    /** Samples of latency this stage adds, for `setLatencySamples`.

        ZERO, because the engine is built in its zero-latency mode — a plugin
        that adds unreported latency plays late against every other track in the
        host, and a drum machine is the worst case for that. Kept as a function
        rather than a constant so the claim is checked against the engine rather
        than asserted by a comment. */
    int latencySamples() const noexcept;

    /** Forces `latencySamples()` to report `samples`, for tests only.

        THE CHECK COULD NOT FAIL WITHOUT THIS. The engine is built with
        `Latency { 0 }`, so `getLatency()` is structurally zero and AC-4's
        assertion — "what the host is told equals CACHAÇA's delay plus the
        stage's" — degenerated to "latency is unchanged from a fresh instance".
        It caught the first draft only because that draft discarded CACHAÇA's
        delay entirely; it could NOT catch a second writer clobbering the sum,
        which is exactly what `/code-review` then found in `prepareToPlay`.

        A seam rather than a constant, so the sum is exercised with a non-zero
        term on both sides. /code-review. */
    void setLatencyOverrideForTest (int samples) noexcept { latencyOverride = samples; }

private:
    /** BUILT ON FIRST LOAD, not on construction.

        `juce::dsp::Convolution` owns a background thread for its loading work
        and starts one whether or not an impulse response ever arrives. The test
        suite builds a processor about ninety-five times; holding this by value
        took the suite from 5 seconds to over two minutes — ninety-five threads
        nobody asked for.

        So a plugin with no IR loaded pays NOTHING for this stage: no thread, no
        FFT buffers, and `process` returns before touching the buffer. Measured,
        not assumed — the regression is what found it.

        ONCE BUILT IT IS NEVER DESTROYED, and that is the fix for a
        use-after-free rather than a preference. `clear()` used to run
        `convolution.reset()` from the message thread — reachable through
        `setStateInformation`, which hosts call DURING PLAYBACK on a project
        load, a preset change or an undo. The audio thread could be inside
        `convolution->process` when the object was freed underneath it.

        Written on the message thread only, and never read directly by the audio
        thread: `engineReady` below is the gate. /code-review. */
    std::unique_ptr<juce::dsp::Convolution> convolution;

    /** Whether the audio thread may use `convolution`.

        THE HANDOVER. The pointer is written and the engine prepared BEFORE this
        is set with release ordering, and the audio thread acquires it before
        dereferencing — so it can never observe a half-built engine. The first
        version published the pointer at `make_unique` and prepared it on the
        next line, leaving a window where `process` could call an engine whose
        `isActive` was still false.

        `clear()` lowers this instead of destroying anything, so going dry is a
        flag flip the audio thread reads safely. /code-review. */
    std::atomic<bool> engineReady { false };

    /** -1 when unset. See `setLatencyOverrideForTest`. */
    int latencyOverride { -1 };

    /** The spec `prepare` was given, so a late-built engine can be prepared
        with the same one. */
    juce::dsp::ProcessSpec specFor() const noexcept;

    juce::AudioBuffer<float> wetScratch;

    double preparedRate { 0.0 };
    int preparedBlock { 0 };
    int preparedChannels { 0 };

    bool prepared { false };
};

} // namespace forrobox
