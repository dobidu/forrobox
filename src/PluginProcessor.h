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
#include "Profiles.h"
#include "StepSnapshot.h"
#include "VoiceEngine.h"
#include "ForroBoxState.h"
#include "ParameterIDs.h"

#include <array>
#include <atomic>
#include <optional>

class ForroBoxAudioProcessor final : public juce::AudioProcessor,
                                     private juce::Timer
{
public:
    ForroBoxAudioProcessor();

private:
    /** Main stereo out plus one stereo bus per channel, named from the channel
        table. Static because the constructor's initialiser list calls it before
        any member exists. */
    static BusesProperties makeBusesProperties();

public:
    ~ForroBoxAudioProcessor() override = default;

    // ── lifecycle ───────────────────────────────────────────────────────────
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    /** How many output buses this plugin declares: main plus one per channel. */
    static constexpr int kNumOutputBuses = 1 + static_cast<int> (forrobox::ids::channelInfos.size());

    /** The bus index carrying one channel's stem. Main is 0. */
    static constexpr int busForChannel (int channelIndex) noexcept { return 1 + channelIndex; }

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
    /** Was 0.0 while the plugin was silent. Now the CHAIN's worst-case tail, so
        a host bouncing to disk renders the decay instead of cutting it at the
        last step.

        A sum, not the engine's figure alone: the bus contributes 0, verified in
        the JUCE source rather than assumed (see MixBus::kTailSeconds), and
        writing it as a sum is what gives a later stage added to MixBus a
        structural reminder that this is host-facing. */
    double getTailLengthSeconds() const override
    {
        return forrobox::VoiceEngine::kMaxTailSeconds + forrobox::MixBus::kTailSeconds;
    }

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

    /** The tempo the host last reported, or 0 when it has reported none.

        Published for ONE reader: under `SYNC` the header's BPM field must show
        the host's tempo rather than the `bpm` parameter's, which
        `PLANNING.md:400` and its stub table at `:838` both state. The value was
        already computed inside `processBlock` and used locally; this publishes
        it and nothing more.

        A relaxed store per block, which is neither an allocation nor a lock.
        Relaxed is enough because it is a display: the UI reads it on a timer,
        no other value is ordered against it, and a reader that sees the
        previous block's tempo is one frame stale — invisible, and the next
        frame corrects it.

        0 rather than the parameter's value when the host reports nothing, so
        the caller can tell "no host tempo" from "the host says 120". */
    float getHostBpm() const noexcept { return hostBpm.load (std::memory_order_relaxed); }

    /** True when SYNC is on AND the host's transport is rolling.

        While synced the host's transport is the only one that matters — see
        processBlock's transport gate — so this is what the header's Play button
        shows, and `isPlaying()` becomes the plugin's own clock's state rather
        than a claim about whether anything is sounding.

        One relaxed store per block, like getHostBpm and for the same reader. */
    bool isHostTransportRolling() const noexcept
    {
        return hostTransportRolling.load (std::memory_order_relaxed);
    }

    /** How many steps have been emitted since the last reset.

        Observability, not test scaffolding: the emitted count is what Phase 5's
        per-channel hit visualiser and activity meter read at frame rate, and the
        `getCurrentStep()`
        exposes only the last step of a block, so a step emitted twice within one
        block is invisible through it — which is exactly how the duplicate
        hazard went unnoticed. */

    /** The step most recently triggered, or -1 while stopped. Phase 5's
        playhead reads this at frame rate. Relaxed on purpose: it is a display
        value, and a one-frame-stale read is invisible where a lock would not
        be. */
    int getCurrentStep() const noexcept { return stepPublisher.read().step; }

public:
    /** Run a pending step-window tiling now, on the calling thread.

        CALLED, never waited for. The plugin's hop happens on the message loop;
        the tests drive it directly, so nothing in the suite depends on a
        message loop running — 04-04's lesson, where three checks failed on
        MSVC's clock rather than on the code.

        Public because the behaviour must be reachable without one, which is the
        same reason `SequencerGrid::refreshFromState` and `updatePlayhead` are. */
    void applyPendingStepChange();

private:
    /** A STEP-WINDOW change tiles the pattern — `PLANNING.md:606`.

        Owned by the PROCESSOR, decided with the user at planning. The prototype
        has one path to a step change (`setSteps`, a button click); a plugin has
        two, and the second is host automation, which can arrive with no editor
        open. A UI-owned tiling would make the same automation produce a
        different groove depending on whether a window happened to be open.

        POLLED, not pushed. The window is read on the timer's own tick and
        compared against the last one tiled — the same edge-detect
        `SequencerGrid` does for its pad count, against a baseline this class
        already had to keep.

        There was an `APVTS::Listener` here, setting a flag for the timer to
        drain. It worked, and it was scaffolding for delivering an edge the
        drain can detect for itself: two base classes, an atomic flag, two
        static_asserts, a listener registration, and a destructor whose only job
        was ordering their teardown — all so a 30 Hz consumer could learn
        something it could have looked up. Deleting it also deletes the only
        thing this plugin did on the audio thread outside `processBlock`, so the
        argument about whether `triggerAsyncUpdate` may block there becomes moot
        rather than won. Found by /simplify.

        The one case a poll cannot see is a 16 -> 32 -> 16 -> 32 round trip
        completed inside one 33 ms tick. No hand produces that, and nothing can
        have been edited in between, so there is nothing for the tiling to
        carry. */
    void timerCallback() override;

    /** The step window the tiling last acted on — the edge detector, and the
        only piece of this that is genuinely irreducible. */
    int lastTiledWindow { 0 };

    /** 15 Hz, and running only while the plugin is prepared.

        This is not free, and /simplify measured what it costs: JUCE dispatches
        every tick through the message queue at ~60-90 us of CPU per tick, so a
        30 Hz timer was +2.7 ms/s — 0.27% of a core, per instance, forever.
        Worse than the raw number, `AudioProcessorValueTreeState`'s own timer
        backs off to a 500 ms period when nothing is updating, so the plugin's
        idle wake rate was about 2 Hz and a 30 Hz timer pinned it 15x higher.

        15 Hz is 67 ms worst case, still under a 16th note at 132 BPM (114 ms),
        and only the TILING of slots 16-31 waits for it — the step count itself
        reaches the audio thread immediately through the raw parameter. 10 Hz
        was measured as nearly a whole step and rejected.

        A one-shot armed on demand is not available: `Timer::startTimer` takes
        the TimerThread lock, so it cannot be armed from the audio thread. */
    static constexpr int kStepTilingPollHz = 15;

public:
    /** The plugin's own output delay: what the host is told with
        setLatencySamples, and the amount the playhead has to be pulled back by.

        ONE definition, because it was two. The correction at `scheduleBlock`'s
        end used `engine.getLookaheadSamples()` alone while the host was told
        that PLUS `MixBus::kLatencySamples`. The bus contributes 0 today, so
        there was no live bug — but `MixBus.h:105-111` already carries a list of
        what a future non-zero value owes, and this site was not on it. Raising
        it silently re-introduces the playhead lead this exists to remove.
        Found by /code-review. */
    int outputDelaySamples() const noexcept
    {
        return engine.getLookaheadSamples() + forrobox::MixBus::kLatencySamples;
    }

    /** AUDIO THREAD. The transport is not running: publish the stopped step AND
        park the display position.

        One call, because they are one fact. Four sites published the stopped
        step and left `displayPositionInSteps` holding its last playing value —
        so a stopped transport reported step -1 beside a live position, and a
        playhead reading its documented "only input" would have drawn itself
        frozen mid-sweep instead of hiding. Found by /code-review. */
    void publishTransportStopped() noexcept
    {
        stepPublisher.publishStopped();
        displayPositionInSteps.store (kStoppedPosition, std::memory_order_relaxed);
    }

    /** The position that means "not running". Negative, and below any real
        position including the negative ones a host count-in produces — those
        are bounded by one block's worth of steps, never by -1000. */
    static constexpr double kStoppedPosition = -1000.0;

    /** Is the transport stopped, asked of the SAME channel the playhead reads.

        `publishTransportStopped` was added to make "stopped" one fact on both
        channels, and then both consumers asked `getCurrentStep()` instead — so
        the sentinel had no production reader and "is it running" still had two
        answers that a later change could desynchronise, which is the failure the
        sentinel exists to prevent. Found by /simplify. */
    bool isTransportStopped() const noexcept
    {
        return getDisplayPositionInSteps() <= kStoppedPosition;
    }

    /** The step and its velocities together — what the LEDs and the meters read.

        One load, so the index and the velocities are always the SAME step's.
        The three accessors around it are kept as forwarders: 33 call sites across
        the suite read them, and leaving them unchanged is what makes "the
        publication is equivalent" a demonstrated claim rather than an asserted
        one. */
    forrobox::StepSnapshot getStepSnapshot() const noexcept { return stepPublisher.read(); }

    /** The active step window. ONE reader.

        The expression `stepsForChoiceIndex (roundToInt (stepsParam->load (...)))`
        was written out three times in this file — and a fourth time in
        `SequencerGrid`, with a DIFFERENT fallback. They agreed only because
        `stepWindows[0] == stepWindows.front()`, which nothing said. 05-03
        extracted exactly this reader in the grid, for exactly this reason, and
        then did not apply it one file over. Found by /simplify. */
    int currentStepWindow() const noexcept
    {
        return stepsForChoiceIndex (
            juce::roundToInt (stepsParam != nullptr
                                ? stepsParam->load (std::memory_order_relaxed)
                                : 0.0f));
    }

    /** How many steps have been published, monotonic.

        `getEmittedStepCount()` was a second spelling of this, narrowing the
        count to `int`. It was kept through Task 1 so the refactor's 33 call
        sites stayed untouched — which is what made the swap's equivalence
        demonstrable — but two names for one counter is not a thing to ship.
        Its 13 call sites, all in tests, now read this. Found by /simplify.

        The visualisers trigger off a CHANGE in this rather than off the
        snapshot's contents: the snapshot holds the last step's velocities
        continuously, so a UI that read them every frame would re-trigger 60
        times a second and nothing would ever decay. */
    int getStepPublicationCount() const noexcept
    {
        // INT, not the publisher's own uint32_t. Every caller compares or
        // prints it, so an unsigned return pushed a static_cast to each of
        // them — four signedness warnings the moment the int-returning
        // duplicate was removed. Wrap-safety is unchanged: the only ordering
        // test is `!=`, and both types wrap at the same order of magnitude
        // (decades of continuous play at ~9 steps a second).
        return static_cast<int> (stepPublisher.publicationCount());
    }

    /** Where the groove is in steps, fractional, already corrected for the
        plugin's own lookahead — see `displayPositionInSteps`. The playhead's
        only input. */
    double getDisplayPositionInSteps() const noexcept
    {
        return displayPositionInSteps.load (std::memory_order_relaxed);
    }

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

    /** How many times the pattern has actually CHANGED.

        No longer only a diagnostic. 05-03 made it the signal the editor follows:
        `publishIfChanged` increments it when the lanes differ and not otherwise,
        and `~LockedState` calls that for every writer — `toggleCell`,
        `setStateInformation`, Phase 6's profile load — so a UI that polls this
        sees every pattern change and no read.

        That is deliberate rather than convenient. "Every writer must remember"
        is the invariant this project has already been bitten by: 01-02's two
        state methods both bypassed the publish, which is why the handle
        publishes from its destructor. Hanging the refresh off the same seam
        means Phase 6's reload needs no new call.

        A second counter was planned and would have been duplication: this one
        already counts exactly the right events, and it also does NOT fire on the
        reads the handle is taken for — which an unconditional bump would have,
        sixty times a second, from the grid's own poll. */
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

        return stepPublisher.read().velocities[static_cast<size_t> (lane)];
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

    /** Load a regional profile: the FULL state reload `PLANNING.md:612-616`
        specifies.

        The pattern half and the parameter half, in one call, because they are
        one action. `applyProfile` has written the lanes, `activeProfile` and
        `dirty` since Phase 2 and touches no parameter at all — so bpm, swing,
        cachaça and the timbre character, all of which `Profile` carries, had
        never reached the APVTS.

        ONE operation with two callers rather than two that agree today: the side
        panel's list and the header's `STYLE` control must not be able to load
        the same profile into two different states.

        Message thread only. Each parameter moves as a complete host gesture, and
        the audio thread picks the pattern up through the publication it already
        follows — `processBlock` is untouched. */
    void loadProfile (const forrobox::Profile&);

    /** Whether the stored state still IS the profile it names.

        `PLANNING.md:601-602` — an edited state stops being the profile it came
        from, so the highlight clears even though `activeProfile` still names it.
        One predicate, because the side panel and the header's `STYLE` control
        must not disagree about which groove is selected. */
    //  NOT const:  is not, because the handle publishes to the
    //  audio thread when it is released. A read that takes that door is a write
    //  as far as the type system is concerned, and saying so is more honest than
    //  a const_cast.
    struct ProfileSelection
    {
        /** The entry of `ids::profileInfos` the state IS, or -1 — because it has
            been EDITED (`PLANNING.md:601-602`: the highlight clears even though
            `activeProfile` still names it), or because the id came from a newer
            build. */
        int index { -1 };

        /** `PLANNING.md:670` keeps this in the PERSISTED state, not in view
            state, and `:706` requires it to round-trip. */
        bool dirty { false };
    };

    /** Both facts from ONE lock.

        They used to be two calls, and the -1 collapsed them: the side panel took
        the lock a second time purely to tell "edited" from "unknown id" apart
        again. Because `dirty` is the steady state after any edit, that second
        take fired on nearly every 30 Hz tick — which is the cost the comment
        there claimed to have removed. /simplify. */
    ProfileSelection profileSelection();

    /** The index alone, for callers with no CUSTOM tag to show. */
    int selectedProfileIndex() { return profileSelection().index; }


    /** Whether `output_mode` selects MULTI-OUT, read from the resolved pointer.

        Read ONCE per block and used for the whole block: a mode that changed
        per sample would be a discontinuity in the middle of a groove. */
    bool isMultiOut() const noexcept;

    /** Points an existing buffer at part of another's channels, or empties it. */
    static void pointStemAt (juce::AudioBuffer<float>& view, juce::AudioBuffer<float>&,
                             int first, int count) noexcept;

    /** Points the main-bus view and each stem at the buses they render into. */
    void resolveRenderTargets (juce::AudioBuffer<float>& buffer,
                               juce::AudioBuffer<float>& mainBus,
                               forrobox::VoiceEngine::Stems& stems) noexcept;

    /** The output stage, for the tests. */
    const forrobox::MixBus& getMixBus() const noexcept { return mixBus; }

    /** The limiter's gain reduction since the LAST CALL, in dB, and zero once
        it has been taken.

        **There can be exactly ONE reader of this.** `MixBus::processBlock`
        accumulates the reduction into an ATOMIC MAX every block and this read
        `exchange`s it back to zero, which makes it a peak-hold rather than a
        sample: a caller polling slower than the audio thread cannot miss a
        peak, and a SECOND caller would take half of them so that neither reader
        ever sees the true maximum. The footer's gain-reduction meter is that
        reader. A test must build its own processor rather than read one that
        has a live editor attached to it.

        Forwarded rather than reached through `getMixBus()` — which would work,
        the accessor being const — so that the contract above has one place to
        be stated and one place to be found. */
    float takeGainReductionDb() const noexcept { return mixBus.takeGainReductionDb(); }

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
    BlockPlan planBlock (int numSamples, double sampleRate,
                         const juce::Optional<juce::AudioPlayHead::PositionInfo>&) noexcept;

    /** The CLAMPED host tempo, or 0 when the host reports none.

        ONE definition, and a pure function of the PositionInfo rather than a
        second getPosition() call: planBlock takes the clock's rate from it and
        processBlock publishes it, so the tempo the header displays is the tempo
        the groove is running at. */
    static float hostBpmFrom (const juce::Optional<juce::AudioPlayHead::PositionInfo>&);

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
    std::atomic<float>* outputModeParam { nullptr };
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

    /** Written by processBlock, read by the editor's timer. See getHostBpm. */
    std::atomic<float>  hostBpm           { 0.0f };

    /** Whether the HOST's transport is rolling AND sync is on — the state the
        header's Play button shows while synced. See isHostTransportRolling. */
    std::atomic<bool>   hostTransportRolling { false };

    // Set by setPlaying on the message thread, consumed by processBlock on the
    // audio thread. The clock's own fields are plain doubles and ints, so
    // resetting it from the message thread while the audio thread is inside
    // advance() is a data race on non-atomic memory — not a benign stale read.
    // The reset therefore happens where the clock is actually used.
    std::atomic<bool>   resetPending      { false };
    /** The step and its velocities, as ONE value. Replaced `currentStep`,
        `emittedSteps` and `lastStepVelocities` at 05-02 — three atomics that
        were ordered but not group-atomic, so a reader could hold step N beside
        step N+1's velocities. Nothing read them until Phase 5 gave the
        publication three readers at frame rate. */
    forrobox::StepPublisher stepPublisher;

    /** Where the groove is, in steps, for the PLAYHEAD — fractional, so the
        sweep is continuous rather than a jump per step.

        Separate from the snapshot on purpose. The playhead asks "where is the
        groove now" and the LEDs ask "what did the last step play"; they are
        different questions about different instants and need no consistency
        with each other, so binding them would cost the single-word publication
        for nothing.

        LOOKAHEAD-CORRECTED by `outputDelaySamples()`. `BlockEmitter` publishes
        at GRID time while the audio leaves the plugin that many samples later,
        so an uncorrected playhead leads what the user hears by 32 ms — about
        28% of a sixteenth at 132 BPM. `PluginProcessor.cpp`'s emitter predicted
        exactly this and said Phase 5 owns the fix.

        TWO THINGS A CONSUMER MUST HANDLE, both named by /code-review at 05-02:

        It can be NEGATIVE, and not only at a count-in. For the first
        `outputDelaySamples()` after Play it is below zero by construction — the
        correction has pulled it behind the origin — and the same happens just
        after a host loop wrap. Wrap it the way `Clock.cpp:104` does,
        `((n % window) + window) % window`, or it lands on the wrong pad;
        `fmod` alone does not.

        The STEP publication is NOT corrected — only this is. So the snapshot's
        step, which the LEDs read, is `outputDelaySamples()` AHEAD of this
        position. They agreed before the correction and they disagree after it.
        Correcting the step too would mean delaying its publication on the audio
        thread; the cheaper answer is for the LED to fire when this position
        reaches the step, which is where 05-02's Task 3 does it. */
    std::atomic<double> displayPositionInSteps { kStoppedPosition };


    static_assert (std::atomic<double>::is_always_lock_free,
                   "atomic<double> must be lock-free — it is read on the audio thread");
    static_assert (std::atomic<int>::is_always_lock_free,
                   "atomic<int> must be lock-free — it is read on the audio thread");
    static_assert (std::atomic<bool>::is_always_lock_free,
                   "atomic<bool> must be lock-free — it is read on the audio thread");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForroBoxAudioProcessor)
};
