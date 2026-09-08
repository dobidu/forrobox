/* ============================================================================
   FORRÓ BOX — plugin processor

   Skeleton only: silent, no parameters, no sequencer. What it does establish
   is the audio-thread contract every later phase inherits — processBlock does
   no allocation, no locking and no I/O.
============================================================================ */
#pragma once

#include <JuceHeader.h>

#include "Clock.h"
#include "ForroBoxState.h"
#include "ParameterIDs.h"

#include <array>
#include <atomic>
#include <optional>

class ForroBoxAudioProcessor final : public juce::AudioProcessor,
                                     private forrobox::StepListener
{
public:
    ForroBoxAudioProcessor();
    ~ForroBoxAudioProcessor() override = default;

    // ── lifecycle ───────────────────────────────────────────────────────────
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // ── audio thread ────────────────────────────────────────────────────────
    // Pull the double-precision overload into scope so declaring only the
    // float one does not hide it (-Woverloaded-virtual).
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    // ── editor ──────────────────────────────────────────────────────────────
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    // ── identity and capabilities ───────────────────────────────────────────
    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return true; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    // ── programs (a single default; real presets are a post-v0.1 concern) ───
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    // ── parameters ──────────────────────────────────────────────────────────
    /** The full automatable surface: 10 globals + 7 params x 5 channels = 45,
        arranged into a GLOBAL group plus one group per instrument. */
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    // ── transport ───────────────────────────────────────────────────────────
    /** The header's play/stop button (Phase 4) drives this.

        Deliberately NOT an APVTS parameter and NOT persisted:
          - not a parameter, because PLANNING.md's VST3 parameter-mapping list
            omits it, and a play toggle on an automation lane would fight the
            host's own transport;
          - not persisted, because a plugin that resumes playing when a project
            is reopened is hostile. This is the opposite call from `dirty` and
            `activeProfile`, which are persisted state.

        Starting resets the clock, so it never resumes mid-pattern. */
    void setPlaying (bool shouldPlay);
    bool isPlaying() const noexcept { return playing.load (std::memory_order_relaxed); }

    /** How many steps have been emitted, and how many were dropped as
        duplicates of one already emitted.

        Observability, not test scaffolding: the emitted count is what Phase 5's
        per-channel hit visualiser and activity meter read at frame rate, and the
        dropped count is the only way to notice a host whose reported tempo
        disagrees with the position it then advances to. `getCurrentStep()`
        exposes only the last step of a block, so a step emitted twice within one
        block is invisible through it — which is exactly how the duplicate
        hazard went unnoticed. */
    int getEmittedStepCount() const noexcept   { return emittedSteps.load (std::memory_order_relaxed); }
    int getDroppedStepCount() const noexcept   { return droppedSteps.load (std::memory_order_relaxed); }

    /** The step most recently triggered, or -1 while stopped. Phase 5's
        playhead reads this at frame rate. Relaxed on purpose: it is a display
        value, and a one-frame-stale read is invisible where a lock would not
        be. */
    int getCurrentStep() const noexcept { return currentStep.load (std::memory_order_relaxed); }

    /** The step window for a `steps` CHOICE index, from ids::stepWindows.

        Named rather than inlined because forwarding the choice index where a
        step count belongs is exactly the trap 02-01 removed from expandPattern,
        and it would be silent: the clock would run a 1-step window and the
        groove would simply be wrong. */
    static int stepsForChoiceIndex (int choiceIndex) noexcept;

    // ── state ───────────────────────────────────────────────────────────────
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** RAII handle to the non-automatable state: pattern grid, profile, dirty
        flag, preset index, pattern slots.

        Access goes through this rather than a raw reference so that EVERY touch
        is serialised against a host calling get/setStateInformation from a loader
        thread. A lock that only the state methods respected would have been worse
        than none: the header would advertise protection while the primary access
        path — the editor holding a reference — stayed unguarded.

        Hold it for as short a scope as possible, and never on the audio thread. */
    class LockedState
    {
    public:
        LockedState (juce::CriticalSection& sectionToLock, forrobox::State& stateToUse)
            : lock (sectionToLock), state (stateToUse) {}

        forrobox::State* operator->() const noexcept { return &state; }
        forrobox::State& operator*()  const noexcept { return state; }

        LockedState (const LockedState&) = delete;
        LockedState& operator= (const LockedState&) = delete;

    private:
        const juce::ScopedLock lock;
        forrobox::State& state;
    };

    /** Locks the non-automatable state for the lifetime of the returned handle. */
    LockedState lockPatternState() { return { stateLock, patternState }; }

    // Last values seen by prepareToPlay. 0 only before the first prepare —
    // releaseResources deliberately retains them, so a host closing its audio
    // device while the editor stays open cannot hand callers a zero to divide by.
    double getCurrentSampleRate() const noexcept        { return currentSampleRate.load (std::memory_order_relaxed); }
    int    getCurrentBlockSize()  const noexcept        { return currentBlockSize.load  (std::memory_order_relaxed); }

private:
    // Receives each step the clock places. Private: the clock is an
    // implementation detail, not part of the processor's public surface.
    void stepTriggered (forrobox::StepEvent event) override;

    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMETERS", createParameterLayout() };

    // Guards patternState. Taken by lockPatternState() and by both state
    // methods, so there is no unguarded path to the data. Never taken on the
    // audio thread — processBlock does not touch patternState.
    juce::CriticalSection stateLock;
    forrobox::State patternState;

    // The sequencer clock. It holds no position: processBlock tells it which
    // musical span this block covers. Its step events drive nothing until
    // Phase 3.
    forrobox::Clock clock;

    /** One contiguous stretch of timeline, rendered over part of a block. */
    struct Segment
    {
        double start { 0.0 };          // position in steps
        double stepsPerSample { 0.0 }; // rate, so the conversion is partition-stable
        int    numSamples { 0 };       // how much of the block this covers
        int    sampleOffset { 0 };     // where in the block it begins
        bool   afterLoopWrap { false }; // the timeline jumped backwards to get here
    };

    /** What a block covers. Two segments when the host's loop end falls inside
        the block: the timeline is not contiguous there, and treating it as if it
        were drops the step at the loop start — the DOWNBEAT — on every
        repetition, because that step sits behind the next block's start
        position. Measured with a one-bar loop at 120 BPM and 512-sample blocks.

        `count` is 0 when nothing should be emitted: the host's transport is
        stopped. */
    struct BlockPlan
    {
        std::array<Segment, 2> segments {};
        int count { 0 };
    };

    /** Decides where this block sits on the musical timeline, splitting it if
        the host loops within it. Called once per block on the audio thread;
        reads the playhead at most once. */
    BlockPlan planBlock (int numSamples) noexcept;

    // Position for the INTERNAL clock path, in steps. Carried across blocks
    // because without a host there is nothing else to derive it from. The
    // synced path never reads it: its position comes from the playhead, which
    // is what lets a host loop or jump simply produce a different span.
    double internalPositionInSteps { 0.0 };

    // The absolute position of the last step handed to stepTriggered, and the
    // previous block's span start.
    //
    // A position-driven clock is a pure function of its span, so if two
    // consecutive spans OVERLAP the same step is emitted twice. They can: the
    // span end is computed from the one tempo the host reported for this block,
    // while the next span's start is the host's own advanced position, which
    // reflects the real tempo curve. Under acceleration the computed end
    // overshoots — in Phase 3 that is two voice triggers on one musical step, a
    // flam. Hosts with quantised or momentarily non-monotonic ppq do the same.
    //
    // So the processor filters: a step at or behind the last one is dropped
    // while the timeline is moving forward. A genuine jump backwards — a loop,
    // a scrub — resets the filter, because re-playing an earlier step is then
    // exactly right.
    double lastEmittedStepPosition { -1.0e18 };
    double previousSpanStart { 0.0 };
    bool   havePreviousSpan { false };

    // Added to every sampleOffset the clock reports, so a segment rendered from
    // the middle of a block places its steps where they actually belong.
    int currentSegmentOffset { 0 };

    // Cached raw parameter pointers. Looked up once at construction so
    // processBlock reads a float through a pointer instead of doing a
    // string-keyed lookup on the audio thread.
    std::atomic<float>* bpmParam   { nullptr };
    std::atomic<float>* swingParam { nullptr };
    std::atomic<float>* stepsParam { nullptr };
    std::atomic<float>* syncParam   { nullptr };

    // Written on the message/prepare thread, read on the audio thread and by the
    // editor. Atomic because plain scalars across threads are a data race, not
    // merely a stale read — and this file is the template later phases inherit.
    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int>    currentBlockSize  { 0 };
    std::atomic<bool>   playing           { false };

    // Set by setPlaying on the message thread, consumed by processBlock on the
    // audio thread. The clock's own fields are plain doubles and ints, so
    // resetting it from the message thread while the audio thread is inside
    // advance() is a data race on non-atomic memory — not a benign stale read.
    // The reset therefore happens where the clock is actually used.
    std::atomic<bool>   resetPending      { false };
    std::atomic<int>    currentStep       { forrobox::Clock::kStoppedStep };
    std::atomic<int>    emittedSteps      { 0 };
    std::atomic<int>    droppedSteps      { 0 };

    static_assert (std::atomic<double>::is_always_lock_free,
                   "atomic<double> must be lock-free — it is read on the audio thread");
    static_assert (std::atomic<int>::is_always_lock_free,
                   "atomic<int> must be lock-free — it is read on the audio thread");
    static_assert (std::atomic<bool>::is_always_lock_free,
                   "atomic<bool> must be lock-free — it is read on the audio thread");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForroBoxAudioProcessor)
};
