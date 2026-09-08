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
#include "MixBus.h"
#include "VoiceEngine.h"
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
    /** Was 0.0 while the plugin was silent. Now the engine's worst-case voice
        tail, so a host bouncing to disk renders the decay instead of cutting it
        at the last step. */
    double getTailLengthSeconds() const override        { return forrobox::VoiceEngine::kMaxTailSeconds; }

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
    int getCurrentStep() const noexcept { return currentStep.load (std::memory_order_acquire); }

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

    /** Diagnostics on the handover: what a test uses to prove the audio thread
        copies only when something was published, and what Phase 6's reload will
        want to confirm a swap actually reached the audio thread. */
    std::uint32_t getPatternPublicationCount() const noexcept { return patternPublisher.publicationCount(); }
    int getPatternCopyCount() const noexcept                  { return patternReader.copyCount(); }

    /** The velocities the most recently emitted step carried, one per lane.

        Written from the audio thread as each step fires, read by the message
        thread. Phase 5's per-channel LED and activity meter read exactly this;
        it is also how a test sees what the emitter read out of the snapshot.

        All eight live in ONE atomic word — eight lanes, one byte each, exactly
        64 bits — so they cannot straddle two steps among themselves. As eight
        independent relaxed atomics they could, which is half of the group
        consistency Phase 5 needs; the other half, consistency with the step
        index, needs the published struct logged in STATE. */
    std::uint8_t getLastStepVelocity (int lane) const noexcept
    {
        if (! juce::isPositiveAndBelow (lane, forrobox::State::kNumLanes))
            return 0;

        const auto packed = lastStepVelocities.load (std::memory_order_relaxed);
        return static_cast<std::uint8_t> ((packed >> (8 * lane)) & 0xffu);
    }

    /** The voice engine, for the tests and for Phase 5's activity meters.

        Exposed const: the engine is driven from processBlock and from nowhere
        else, and handing out a mutable reference is the mistake 01-02's
        `getPatternState()` made. */
    const forrobox::VoiceEngine& getVoiceEngine() const noexcept { return engine; }

    /** The per-channel values a block renders with, including the resolved
        mute/solo gate. Public so the mute/solo truth table can be swept
        without rendering audio for all 32 combinations. */
    forrobox::VoiceEngine::Settings resolveChannelSettings() const noexcept;

    /** The four global values the output stage reads.

        A second resolver rather than four more fields on the engine's Settings:
        each stage reads only what it uses, which is what keeps the engine/bus
        split honest rather than nominal. */
    forrobox::MixBus::Settings resolveBusSettings() const noexcept;

    /** The output stage, for the tests and for Phase 8's GR meter. */
    const forrobox::MixBus& getMixBus() const noexcept { return mixBus; }

    /** Renders the same pattern under a different humanisation realisation.

        For sampling a distribution rather than pinning one draw — 03-02's
        limiter design input was measured from a single realisation and came out
        12% low. Call before the first block. */
    void setHumanisationSeedOffset (std::uint64_t offset) noexcept
    {
        engine.setHumanisationSeedOffset (offset);
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

    /** Advances the clock and schedules this block's steps. Renders nothing:
        processBlock calls engine.render exactly once, unconditionally, so a
        later output stage cannot be added to some exits and not others. */
    void scheduleBlock (int numSamplesThisBlock) noexcept;

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

    /** Owns the voices, the sampler and the one seeded RNG, and lives for as
        long as the processor does.

        NOT a block-scoped object hanging off the step emitter, which is the
        shape that would fall out of the emitter already taking references:
        03-02's timing jitter can place a trigger past the end of the block that
        scheduled it, and the RNG, smoothers, character bus and limiter are all
        prepareToPlay lifetime. Recorded at 02-04's altitude review. */
    forrobox::VoiceEngine engine;

    /** The character bus, limiter and master — everything downstream of the
        voice sum. A SIBLING of the engine; see MixBus.h for why. */
    forrobox::MixBus mixBus;

    // Cached raw parameter pointers. Looked up once at construction so
    // processBlock reads a float through a pointer instead of doing a
    // string-keyed lookup on the audio thread.
    std::atomic<float>* bpmParam   { nullptr };
    std::atomic<float>* swingParam { nullptr };
    std::atomic<float>* stepsParam { nullptr };
    std::atomic<float>* syncParam   { nullptr };
    std::atomic<float>* cachacaParam { nullptr };
    std::atomic<float>* timbreParam    { nullptr };
    std::atomic<float>* charMixParam   { nullptr };
    std::atomic<float>* limiterOnParam { nullptr };
    std::atomic<float>* masterParam    { nullptr };

    /** The seven per-channel parameters the engine reads, cached for the same
        reason: `channelParam()` builds a juce::String, which must never happen
        on the audio thread. */
    struct ChannelParamPointers
    {
        std::atomic<float>* vol   { nullptr };
        std::atomic<float>* pitch { nullptr };
        std::atomic<float>* decay { nullptr };
        std::atomic<float>* pan   { nullptr };
        std::atomic<float>* ghost { nullptr };
        std::atomic<float>* mute  { nullptr };
        std::atomic<float>* solo  { nullptr };
    };

    std::array<ChannelParamPointers, static_cast<size_t> (forrobox::State::kNumChannels)>
        channelParamPointers {};

    /** True only when every one of the 40 pointers above resolved.

        One flag rather than a chain of null checks in processBlock. The check
        itself is not optional: getRawParameterValue returns nullptr for an
        unknown ID, so a parameter-ID rename leaves these null, and the
        constructor's jassert compiles away in Release — where the dereference
        takes the host down instead of failing visibly. A four-pointer version
        of this check was deleted during the 02-03 restructure and had to be
        restored by review. */
    bool parametersResolved { false };

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
    std::atomic<std::uint64_t> lastStepVelocities { 0 };

    static_assert (forrobox::State::kNumLanes == 8,
                   "the eight lane velocities are packed into one 64-bit word");
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free,
                   "the packed velocities are written on the audio thread");

    static_assert (std::atomic<double>::is_always_lock_free,
                   "atomic<double> must be lock-free — it is read on the audio thread");
    static_assert (std::atomic<int>::is_always_lock_free,
                   "atomic<int> must be lock-free — it is read on the audio thread");
    static_assert (std::atomic<bool>::is_always_lock_free,
                   "atomic<bool> must be lock-free — it is read on the audio thread");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForroBoxAudioProcessor)
};
