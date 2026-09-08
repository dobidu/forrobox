/* ============================================================================
   FORRÓ BOX — plugin processor

   Skeleton only: silent, no parameters, no sequencer. What it does establish
   is the audio-thread contract every later phase inherits — processBlock does
   no allocation, no locking and no I/O.
============================================================================ */
#pragma once

#include <JuceHeader.h>

#include "Clock.h"
#include "PatternSnapshot.h"
#include "ForroBoxState.h"
#include "ParameterIDs.h"

#include <array>
#include <atomic>
#include <optional>

class ForroBoxAudioProcessor final : public juce::AudioProcessor
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

    /** How many steps have been emitted since the last reset.

        Observability, not test scaffolding: the emitted count is what Phase 5's
        per-channel hit visualiser and activity meter read at frame rate, and the
        `getCurrentStep()`
        exposes only the last step of a block, so a step emitted twice within one
        block is invisible through it — which is exactly how the duplicate
        hazard went unnoticed. */
    int getEmittedStepCount() const noexcept   { return emittedSteps.load (std::memory_order_relaxed); }

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
        LockedState (juce::CriticalSection& sectionToLock,
                     forrobox::State& stateToUse,
                     forrobox::PatternPublisher& publisherToUse)
            : lock (sectionToLock), state (stateToUse), publisher (publisherToUse) {}

        /** Publishes the lanes for the audio thread when the handle goes out of
            scope — automatically, so a writer cannot forget.

            "Every writer must remember to publish" is exactly the kind of
            invariant that fails, and this project has already been bitten by a
            partial guarantee: 01-02's `getPatternState()` handed out a mutable
            reference and the lock covered only some paths, which advertised
            protection it did not provide.

            publishIfChanged, not publish: this handle is taken for READ access
            too, and an unconditional publish would make every read force the
            audio thread into a pointless copy. */
        ~LockedState() { publisher.publishIfChanged (state.lanes); }

        forrobox::State* operator->() const noexcept { return &state; }
        forrobox::State& operator*()  const noexcept { return state; }

        LockedState (const LockedState&) = delete;
        LockedState& operator= (const LockedState&) = delete;

    private:
        const juce::ScopedLock lock;
        forrobox::State& state;
        forrobox::PatternPublisher& publisher;
    };

    /** Locks the non-automatable state for the lifetime of the returned handle,
        and publishes any change to the audio thread when it is released. */
    LockedState lockPatternState() { return { stateLock, patternState, patternPublisher }; }

    /** Diagnostics on the handover. Both are what a test uses to prove the
        audio thread is reading the grid once per block and not per step, and the
        publication count is what Phase 6's reload will want to confirm a swap
        actually reached the audio thread. */
    std::uint32_t getPatternPublicationCount() const noexcept { return patternPublisher.publicationCount(); }
    int getPatternCopyCount() const noexcept                  { return patternReader.copyCount(); }
    std::uint32_t getHeldPatternGeneration() const noexcept   { return patternReader.heldGeneration(); }

    /** How many times two steps WITHIN one block read different pattern
        generations.

        Must always be 0. This is the property that matters about taking the
        snapshot once per block: if the table could change between two steps of
        the same block, the block renders two different patterns. Counting
        copies does not test it — refresh is idempotent once the generation is
        held, so calling it per step instead of per block copies exactly as
        often and looks identical. */
    int getIntraBlockGenerationChanges() const noexcept
    {
        return intraBlockGenerationChanges.load (std::memory_order_relaxed);
    }

    /** The velocities the most recently emitted step carried, one per lane.

        Written from the audio thread as each step fires, read by the message
        thread. Phase 5's per-channel LED and activity meter read exactly this;
        it is also how a test can see what the emitter read out of the snapshot. */
    std::uint8_t getLastStepVelocity (int lane) const noexcept
    {
        return juce::isPositiveAndBelow (lane, forrobox::State::kNumLanes)
             ? lastStepVelocities[static_cast<size_t> (lane)].load (std::memory_order_relaxed)
             : 0;
    }

    // Last values seen by prepareToPlay. 0 only before the first prepare —
    // releaseResources deliberately retains them, so a host closing its audio
    // device while the editor stays open cannot hand callers a zero to divide by.
    double getCurrentSampleRate() const noexcept        { return currentSampleRate.load (std::memory_order_relaxed); }
    int    getCurrentBlockSize()  const noexcept        { return currentBlockSize.load  (std::memory_order_relaxed); }

private:
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

    // The handover. The publisher is written only from the message thread; the
    // reader is the audio thread's private snapshot and is touched nowhere else.
    forrobox::PatternPublisher patternPublisher;
    forrobox::PatternReader patternReader;

    /** What a block covers: one span, or two when the host's loop end falls
        inside it — the timeline is not contiguous there, and treating it as if
        it were drops the step at the loop start, the DOWNBEAT, on every
        repetition, because that step sits behind the next block's start
        position. Traced with a one-bar loop at 120 BPM and 512-sample blocks.

        `count` is 0 when nothing should be emitted: the host's transport is
        stopped. An array and a count rather than a vector or optionals — this is
        built on the audio thread every block. */
    struct BlockPlan
    {
        std::array<forrobox::Clock::Span, 2> spans {};
        int count { 0 };
    };

    /** Decides where this block sits on the musical timeline, splitting it if
        the host loops within it. Called once per block on the audio thread;
        reads the playhead at most once. */
    BlockPlan planBlock (int numSamples, double sampleRate) noexcept;

    /** The next position not yet emitted, in steps.

        ONE field, deliberately. It was two — an internal-path position plus a
        watermark of the last step emitted — with the synced path writing the
        internal path's variable to keep them in step. Two representations of one
        concept, and their disagreement is what made a duplicate filter
        necessary at all.

        Now both paths own it: the synced path re-anchors it when the host has
        genuinely moved elsewhere, and otherwise lets it tile forward. Spans then
        tile by construction, so no step is emitted twice AND none falls in a
        gap. The old filter caught only the duplicate case; the gap case — the
        host's next position overshooting the computed end, so the steps in
        between are never emitted — was invisible to it and to every counter. */
    double positionInSteps { 0.0 };

    /** Beyond this much disagreement between our position and the host's, the
        host has moved rather than merely reported a tempo we integrated
        slightly differently.

        One step. Below it the difference is integration error from the single
        tempo a host reports per block, and tiling forward absorbs it. Above
        it — a loop, a scrub, a jump — the host is authoritative and we
        re-anchor, without emitting the steps in between: no catch-up burst. */
    static constexpr double kReanchorThresholdInSteps = 1.0;

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
    std::array<std::atomic<std::uint8_t>, static_cast<size_t> (forrobox::State::kNumLanes)>
                        lastStepVelocities {};
    std::atomic<int>    intraBlockGenerationChanges { 0 };

    static_assert (std::atomic<double>::is_always_lock_free,
                   "atomic<double> must be lock-free — it is read on the audio thread");
    static_assert (std::atomic<int>::is_always_lock_free,
                   "atomic<int> must be lock-free — it is read on the audio thread");
    static_assert (std::atomic<bool>::is_always_lock_free,
                   "atomic<bool> must be lock-free — it is read on the audio thread");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForroBoxAudioProcessor)
};
