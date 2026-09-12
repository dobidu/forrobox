#include "PluginProcessor.h"

#include <cmath>
#include "PluginEditor.h"

#include <array>
#include <cstdlib>
#include <type_traits>

ForroBoxAudioProcessor::ForroBoxAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
        // Instrument: declaring an input bus makes some hosts present this as
        // an effect, so there is deliberately none.
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Resolved once, here, so processBlock reads a float through a pointer
    // rather than doing a string-keyed lookup on the audio thread.
    bpmParam   = apvts.getRawParameterValue (forrobox::ids::bpm);
    swingParam = apvts.getRawParameterValue (forrobox::ids::swing);
    stepsParam = apvts.getRawParameterValue (forrobox::ids::steps);
    syncParam  = apvts.getRawParameterValue (forrobox::ids::sync);
    cachacaParam   = apvts.getRawParameterValue (forrobox::ids::cachaca);
    timbreParam    = apvts.getRawParameterValue (forrobox::ids::timbre);
    charMixParam   = apvts.getRawParameterValue (forrobox::ids::charMix);
    limiterOnParam = apvts.getRawParameterValue (forrobox::ids::limiterOn);
    masterParam    = apvts.getRawParameterValue (forrobox::ids::master);

    for (size_t c = 0; c < forrobox::ids::channelInfos.size(); ++c)
    {
        const auto* id = forrobox::ids::channelInfos[c].id;
        auto& pointers  = channelParamPointers[c];

        pointers.vol   = apvts.getRawParameterValue (forrobox::ids::channelParam (id, forrobox::ids::vol));
        pointers.pitch = apvts.getRawParameterValue (forrobox::ids::channelParam (id, forrobox::ids::pitch));
        pointers.decay = apvts.getRawParameterValue (forrobox::ids::channelParam (id, forrobox::ids::decay));
        pointers.pan   = apvts.getRawParameterValue (forrobox::ids::channelParam (id, forrobox::ids::pan));
        pointers.ghost = apvts.getRawParameterValue (forrobox::ids::channelParam (id, forrobox::ids::ghost));
        pointers.mute  = apvts.getRawParameterValue (forrobox::ids::channelParam (id, forrobox::ids::mute));
        pointers.solo  = apvts.getRawParameterValue (forrobox::ids::channelParam (id, forrobox::ids::solo));
    }

    parametersResolved = bpmParam != nullptr && swingParam != nullptr
                      && stepsParam != nullptr && syncParam != nullptr
                      && cachacaParam != nullptr && timbreParam != nullptr
                      && charMixParam != nullptr && limiterOnParam != nullptr
                      && masterParam != nullptr;

    for (const auto& pointers : channelParamPointers)
        parametersResolved = parametersResolved
                          && pointers.vol != nullptr && pointers.pitch != nullptr
                          && pointers.decay != nullptr && pointers.pan != nullptr
                          && pointers.ghost != nullptr
                          && pointers.mute != nullptr && pointers.solo != nullptr;

    jassert (parametersResolved);

    // Reported here as well as in prepareToPlay: a host that queries latency at
    // scan or instantiation time — before any prepare — would otherwise read 0
    // and leave the groove 32 ms late.
    setLatencySamples (engine.getLookaheadSamples() + forrobox::MixBus::kLatencySamples);
}

forrobox::VoiceEngine::Settings ForroBoxAudioProcessor::resolveChannelSettings() const noexcept
{
    forrobox::VoiceEngine::Settings settings {};

    if (! parametersResolved)
        return settings;

    // Solo is decided across ALL channels before any channel's gate is set:
    // "if any channel is soloed, non-soloed channels are silent" (PLANNING.md).
    // Resolving per channel in one pass would make the answer depend on the
    // order they were visited.
    auto anySoloed = false;

    for (const auto& pointers : channelParamPointers)
        if (pointers.solo->load (std::memory_order_relaxed) >= 0.5f)
            anySoloed = true;

    // A global, not a per-channel value: one knob for the whole instrument —
    // and normalised HERE, once, NaN-safely, rather than at each of the three
    // places downstream that used to read it raw.
    settings.cachaca = forrobox::ids::normalisedPercent (
                           cachacaParam->load (std::memory_order_relaxed));

    for (size_t c = 0; c < channelParamPointers.size(); ++c)
    {
        const auto& pointers = channelParamPointers[c];
        auto& channel = settings.channels[c];

        channel.vol   = pointers.vol  ->load (std::memory_order_relaxed);
        channel.pitch = pointers.pitch->load (std::memory_order_relaxed);
        channel.decay = pointers.decay->load (std::memory_order_relaxed);
        channel.pan   = pointers.pan  ->load (std::memory_order_relaxed);
        channel.ghost = pointers.ghost->load (std::memory_order_relaxed);

        const auto muted  = pointers.mute->load (std::memory_order_relaxed) >= 0.5f;
        const auto soloed = pointers.solo->load (std::memory_order_relaxed) >= 0.5f;

        // Solo overrides mute for the soloed channel: a channel that is both
        // muted and soloed sounds. That matches the prototype, where solo
        // decides which channels are in the mix at all.
        channel.audible = anySoloed ? soloed : ! muted;
    }

    return settings;
}

namespace
{
    /** The `steps` display strings, built from ids::stepWindows so the labels
        and the windows the clock runs come from one table. */
    juce::StringArray stepWindowChoices()
    {
        juce::StringArray choices;
        for (auto window : forrobox::ids::stepWindows)
            choices.add (juce::String (window));

        return choices;
    }

    /** The TIMBRE display strings, built from the same table the bus reads its
        cutoffs and drives from.

        It was a hand-written `StringArray { "HI-FI", "LO-FI", "CICLOTRON" }`
        beside a `timbreSpecs` array carrying its own `displayName` copy, with
        nothing tying the choice INDEX to a cutoff. That is the fifth instance
        of a pattern this codebase has already named three times —
        `ids::channelInfos`, `ids::profileInfos` ("id, display name, short name
        and code cannot drift apart") and `stepWindowChoices` above, which
        exists so "the host shows 32" and "the clock runs 32" cannot disagree.
        Phase 6 draws the timbre rows and Phase 8 adds CICLOTRON's trademark, so
        the two lists would have drifted at the first of those. */
    juce::StringArray timbreChoices()
    {
        juce::StringArray choices;

        for (const auto& timbre : forrobox::timbreSpecs)
            choices.add (timbre.displayName);

        return choices;
    }
}

int ForroBoxAudioProcessor::stepsForChoiceIndex (int choiceIndex) noexcept
{
    // Indexes the same table the parameter's display strings are built from,
    // so "the host shows 32" and "the clock runs 32" cannot drift apart.
    //
    // An out-of-range index falls back to the DEFAULT window, not the nearest
    // one: clamping would send a bad index to the widest window, quietly
    // doubling the pattern length. The default is the conservative wrong answer.
    jassert (juce::isPositiveAndBelow (choiceIndex, (int) forrobox::ids::stepWindows.size()));

    if (! juce::isPositiveAndBelow (choiceIndex, (int) forrobox::ids::stepWindows.size()))
        return forrobox::ids::stepWindows[0];

    return forrobox::ids::stepWindows[static_cast<size_t> (choiceIndex)];
}

void ForroBoxAudioProcessor::setPlaying (bool shouldPlay)
{
    if (playing.load (std::memory_order_relaxed) == shouldPlay)
        return;

    // Reset on BOTH edges: starting must not resume mid-pattern, and stopping
    // must clear the playhead — PLANNING.md: "Stopping clears the playhead and
    // all playing pad outlines, and resets the step counter to 0."
    //
    // The clock is NOT reset here. Its fields are plain doubles, and the audio
    // thread may be inside advance(). Request the reset and let processBlock
    // perform it; the release store below pairs with processBlock's acquire
    // load, so the audio thread cannot observe playing == true while still
    // seeing the pre-reset phase.
    currentStep.store (forrobox::Clock::kStoppedStep, std::memory_order_release);
    resetPending.store (true, std::memory_order_relaxed);
    playing.store (shouldPlay, std::memory_order_release);
}

void ForroBoxAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // The only place allocation is permitted. Later phases size their voice
    // pools, pattern buffers and FIFOs here.
    currentSampleRate.store (sampleRate,      std::memory_order_relaxed);
    currentBlockSize .store (samplesPerBlock, std::memory_order_relaxed);

    // prepareToPlay runs with the audio device stopped, so touching the clock
    // and the internal position directly is safe here.
    clock.reset();
    positionInSteps = 0.0;
    currentStep.store (forrobox::Clock::kStoppedStep, std::memory_order_release);

    // Allocates the voice pools, the per-voice filter state and the samples —
    // which is exactly what this callback is for.
    engine.prepare (sampleRate, samplesPerBlock);
    mixBus.prepare (sampleRate, samplesPerBlock);

    // Every trigger is delayed by the engine's lookahead so that CACHAÇA's
    // BIPOLAR timing jitter can place a hit earlier than its step at all. Tell
    // the host, so it shifts the recording back and the groove lands where the
    // grid says.
    //
    // Reported unconditionally, including at CACHAÇA 0. What must not vary is
    // the KNOB: a latency that grew as CACHAÇA was raised would force a host
    // re-negotiation mid-session, which many DAWs handle badly or ignore.
    //
    // It does still change with the sample RATE — 1536 at 48 kHz against 1411
    // at 44.1 — which is both unavoidable and something hosts expect across a
    // prepareToPlay. The engine seeds it from a nominal 48 kHz at construction
    // so that a host querying before the first prepare reads a sane figure
    // rather than 0.
    setLatencySamples (engine.getLookaheadSamples() + forrobox::MixBus::kLatencySamples);
}

void ForroBoxAudioProcessor::releaseResources()
{
    // Deliberately does NOT reset the cached rate/block size. A host may close
    // its audio device with the editor still open and querying; zeroing here
    // would hand out a 0.0 sample rate indefinitely.
}

bool ForroBoxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Stereo main output, no input. Multi-out is a post-v0.1 concern.
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet()  == juce::AudioChannelSet::disabled();
}

void ForroBoxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();
    midi.clear();

    // Scheduling is conditional and returns early from half a dozen places;
    // rendering is not, and happens exactly once, here.
    //
    // It used to be three `engine.render (buffer)` calls — one per early exit —
    // which was a landmine for 03-03. A character bus, limiter and master added
    // at the normal path alone would have left the ring-out after STOP
    // unlimited and at unity master: at the default master of 82, (0.82)^2 =
    // 0.672, so pressing Stop would make a decaying zabumba jump +3.5 dB and
    // lose the character bus's wet path. Audible, on the most common gesture in
    // the plugin, and the sort of thing found by ear at an A/B checkpoint
    // rather than by a test.
    scheduleBlock (buffer.getNumSamples());

    engine.render (buffer);

    // The output stage, immediately after the sum and once per block:
    // voices -> character bus -> limiter -> master, which is PLANNING.md's
    // chain. Unconditional, for the same reason engine.render is.
    mixBus.process (buffer, resolveBusSettings());
}

forrobox::MixBus::Settings ForroBoxAudioProcessor::resolveBusSettings() const noexcept
{
    forrobox::MixBus::Settings settings {};

    if (! parametersResolved)
        return settings;

    settings.timbreIndex = juce::roundToInt (timbreParam->load (std::memory_order_relaxed));
    settings.charMix     = charMixParam->load (std::memory_order_relaxed);
    settings.limiterOn   = limiterOnParam->load (std::memory_order_relaxed) >= 0.5f;
    settings.master      = masterParam->load (std::memory_order_relaxed);

    return settings;
}

/** Advances the clock and hands this block's steps to the engine.

    Returns early wherever there is nothing to schedule. Renders nothing — see
    processBlock. */
void ForroBoxAudioProcessor::scheduleBlock (int numSamplesThisBlock) noexcept
{

    // AUDIO-THREAD CONTRACT — no allocation, no locks, no I/O, no logging,
    // no juce::String construction. Every later phase inherits this rule.
    //
    // In particular: patternState is NOT read here. Looking up what a step
    // should trigger would mean taking the state lock on the audio thread,
    // which is the data race 02-04's double-buffer handover exists to solve.
    // `playing` is read FIRST, with acquire. setPlaying writes resetPending
    // before releasing playing, so acquiring playing here is what makes that
    // write visible; reading resetPending first could observe a stale false and
    // then a fresh true playing — starting without the reset for one block.
    const auto isPlayingNow = playing.load (std::memory_order_acquire);

    // Relaxed load first: the exchange is a lock xchg that takes the cache line
    // exclusive on every block, and a reset is pending on almost none of them.
    // The RMW itself must stay — a plain load-then-store would let a setPlaying
    // landing in between have its request swallowed.
    if (resetPending.load (std::memory_order_relaxed)
        && resetPending.exchange (false, std::memory_order_relaxed))
    {
        clock.reset();
        positionInSteps = 0.0;

        // The clock is reset; the VOICES are deliberately not.
        //
        // PLANNING.md's "stopping clears the playhead and all playing pad
        // outlines, and resets the step counter to 0" is about visible state.
        // Cutting a sounding voice mid-decay would click, and a zabumba tail
        // finishing after the transport stops is what every instrument does.
        // Voices are only hard-cleared in prepareToPlay, where the audio device
        // is stopping anyway.
    }

    // Restored after a review found it deleted in the 02-03 restructure, and
    // widened in 03-01 from four pointers to all 34. A parameter-ID rename
    // leaves these null, getRawParameterValue returns nullptr for an unknown
    // ID, and the constructor's jassert compiles away in Release — where the
    // dereference takes the host down instead of failing visibly. planBlock and
    // resolveChannelSettings both read through them, so the check must precede
    // both — which is why it now sits above the transport check rather than
    // below it.
    if (! parametersResolved)
        return;

    // Resolved once, and handed to the engine once. Everything downstream reads
    // it from there rather than being passed it again.
    engine.beginBlock (resolveChannelSettings());

    if (! isPlayingNow)
    {
        // Self-healing: a step emitted in the window between setPlaying's two
        // stores would otherwise leave the playhead parked on a live step.
        currentStep.store (forrobox::Clock::kStoppedStep, std::memory_order_release);

        // Nothing scheduled; the render at the bottom of processBlock still
        // drains whatever was already sounding.
        return;
    }

    const auto numSamples = numSamplesThisBlock;
    const auto sampleRate = currentSampleRate.load (std::memory_order_relaxed);
    const auto plan = planBlock (numSamples, sampleRate);

    if (plan.count == 0)
    {
        // The host's transport is stopped while synced. Report stopped rather
        // than leaving the playhead on whichever step fired last, and reset the
        // clock so its own reported step agrees. Voices still ring out.
        clock.reset();
        currentStep.store (forrobox::Clock::kStoppedStep, std::memory_order_release);
        return;
    }

    const forrobox::Clock::Params params {
        swingParam->load (std::memory_order_relaxed),
        stepsForChoiceIndex (juce::roundToInt (stepsParam->load (std::memory_order_relaxed)))
    };

    // The snapshot is taken ONCE here, not per step. Refreshing inside the
    // emitter would ask the publisher on every hit, which is both wasteful and
    // wrong: a table published mid-block would change under the steps already
    // placed in it, so the block would render two different patterns.
    patternReader.refresh (patternPublisher);

    // A stack emitter carrying this block's context in its own fields.
    //
    // Deliberately NOT a mutable member read inside the callback. 02-03 had one
    // — an offset the listener added — and /simplify removed it, because a
    // member set before advance() and read during it hides its lifetime and made
    // the clock's own documented offset contract false. Per-block context
    // belongs to a per-block object.
    //
    // It holds the LANES, not the reader. That is what makes "every step in a
    // block reads one table" structural: there is no refresh to call, so a step
    // cannot see a table its block-mate did not. It replaced a per-step
    // generation counter that existed only so a probabilistic test could hunt
    // for the bug this makes unrepresentable — and that test needed a measured
    // 300-publication threshold to detect anything at all.
    struct BlockEmitter final : forrobox::StepListener
    {
        BlockEmitter (const forrobox::PatternLanes& lanesToRead,
                      forrobox::VoiceEngine& engineToDrive,
                      ForroBoxAudioProcessor& p)
            : lanes (lanesToRead), voiceEngine (engineToDrive), owner (p) {}

        const forrobox::PatternLanes& lanes;
        forrobox::VoiceEngine& voiceEngine;
        ForroBoxAudioProcessor& owner;

        void stepTriggered (forrobox::StepEvent event) override
        {
            // Storage index, not the active window: 02-01 settled that the 32
            // slots are storage and `steps` is a view onto them, and the clock
            // already wraps the emitted index over the window. Indexing by
            // anything else would play the wrong half of a 32-step pattern.
            const auto index = static_cast<size_t> (
                juce::jlimit (0, forrobox::State::kMaxSteps - 1, event.step));

            // One pass over the lanes, feeding both consumers.
            //
            // Packed into one word so the eight cannot straddle two steps.
            std::uint64_t packed = 0;
            forrobox::VoiceEngine::StepVelocities velocities {};

            for (size_t lane = 0; lane < lanes.size(); ++lane)
            {
                const auto velocity = lanes[lane][index];

                velocities[lane] = velocity;
                packed |= static_cast<std::uint64_t> (velocity) << (8 * lane);
            }

            owner.lastStepVelocities.store (packed, std::memory_order_relaxed);

            // The one line of work this adapter does beyond bookkeeping: hand
            // the WHOLE STEP to the engine at the event's own sample offset. No
            // DSP here — synthesis inside a step callback would interleave
            // rendering with step placement, and would leave 03-02's jitter,
            // which can fire past the end of this block, nowhere to go.
            //
            // A step, not eight hits: `app.js` draws its CACHAÇA timing jitter
            // once per step and moves every lane of that step together, so a
            // per-lane call here would invite a per-lane draw and the lanes
            // would flam apart.
            //
            // event.sampleOffset is read here for the first time in the
            // project: Phase 2 built the field and never consumed it.
            voiceEngine.scheduleStep (velocities, event.sampleOffset);

            // Published at GRID time, while this step's audio leaves the plugin
            // `lookaheadSamples` later.
            //
            // Host latency compensation realigns the RECORDING, not live
            // monitoring, so Phase 5's playhead and pad highlights will run
            // 32 ms — plus up to 22 ms of jitter — ahead of what the user
            // hears. Phase 5 owns the UI and the fix (delay the published step,
            // or offset the playhead by getLatencySamples()), but the offset is
            // created here, so it is recorded here.
            //
            // RELEASE, and last: the velocities above must be visible to anyone
            // who acquires this step.
            owner.currentStep.store (event.step, std::memory_order_release);
            owner.emittedSteps.fetch_add (1, std::memory_order_relaxed);
        }
    };

    BlockEmitter emitter { patternReader.lanes(), engine, *this };

    for (int i = 0; i < plan.count; ++i)
        clock.advance (plan.spans[static_cast<size_t> (i)], params, emitter);

}

namespace
{
    /** The host's position expressed in steps, anchored on its bar.

        A pure function of PositionInfo, lifted out of planBlock so it can be
        exercised across meters directly. It was buried mid-function before, and
        the consequence was concrete: "step 0 only locks to the bar in 4/4
        starting at ppq 0" survived a whole review pass, because every 4/4 case
        agrees with the unanchored form and the rule could only be reached by
        driving a whole processor through a fake host.

        Anchoring needs the bar start, the meter AND the bar number. Deriving the
        bar number from the bar start assumes the current meter has held since
        position 0 — false in exactly the two situations anchoring exists for, a
        non-bar-aligned origin and a mid-project meter change. Bar parity is
        genuinely unknowable without meter history, so when the host does not
        count bars for us we fall back to the project origin rather than invent a
        number we cannot know. */
    double hostStepPosition (const juce::AudioPlayHead::PositionInfo& position, double ppq) noexcept
    {
        const auto fromOrigin = ppq * forrobox::Clock::kStepsPerBeat;

        const auto barStart = position.getPpqPositionOfLastBarStart();
        const auto meter    = position.getTimeSignature();
        const auto barCount = position.getBarCount();

        if (! barStart.hasValue() || ! std::isfinite (*barStart)
            || ! meter.hasValue() || meter->numerator <= 0 || meter->denominator <= 0
            || ! barCount.hasValue() || *barCount < 0)
            return fromOrigin;

        const auto beatsPerBar = static_cast<double> (meter->numerator)
                               * 4.0 / static_cast<double> (meter->denominator);

        if (! (beatsPerBar > 0.0))
            return fromOrigin;

        // A 16-step window then starts on every bar, and a 32-step window on
        // every other bar, which is what a two-bar pattern should do. Where the
        // bar is not a whole number of patterns — 3/4 gives 12 steps against a
        // 16-step window — the pattern cannot both fit the bar and stay 16
        // steps, so it rotates. Inherent, and pinned by tests.
        return static_cast<double> (*barCount) * beatsPerBar * forrobox::Clock::kStepsPerBeat
             + (ppq - *barStart) * forrobox::Clock::kStepsPerBeat;
    }

    /** Steps per sample at a tempo — the conversion the processor owns now that
        the clock knows nothing about tempo. */
    double stepsPerSampleAt (double bpm, double sampleRate) noexcept
    {
        return (bpm * forrobox::Clock::kStepsPerBeat) / (sampleRate * 60.0);
    }
}

ForroBoxAudioProcessor::BlockPlan
ForroBoxAudioProcessor::planBlock (int numSamples, double sampleRate) noexcept
{
    if (! std::isfinite (sampleRate) || sampleRate <= 0.0)
        return {};

    const auto internalBpm = static_cast<double> (
        juce::jlimit (forrobox::ids::kMinBpm, forrobox::ids::kMaxBpm,
                      juce::roundToInt (bpmParam->load (std::memory_order_relaxed))));

    // Every path ends here: one span starting where we left off, advancing the
    // watermark by exactly what it covers. Spans therefore tile the timeline by
    // construction, which is what makes duplicates and gaps impossible rather
    // than merely filtered.
    const auto tileForward = [this, numSamples] (double rate)
    {
        BlockPlan plan;
        plan.spans[0] = { positionInSteps, rate, numSamples, 0 };
        plan.count = 1;
        positionInSteps += rate * static_cast<double> (numSamples);
        return plan;
    };

    if (syncParam->load (std::memory_order_relaxed) < 0.5f)
        return tileForward (stepsPerSampleAt (internalBpm, sampleRate));

    // ── SYNC is on ──────────────────────────────────────────────────────────
    // getPosition() is called exactly once per block. Every PositionInfo field
    // is Optional and hosts populate them inconsistently, so an absent field
    // means "cannot sync this block", never zero.
    auto* hostPlayHead = getPlayHead();

    if (hostPlayHead == nullptr)
        return tileForward (stepsPerSampleAt (internalBpm, sampleRate));

    const auto position = hostPlayHead->getPosition();

    if (! position.hasValue())
        return tileForward (stepsPerSampleAt (internalBpm, sampleRate));

    // Host transport governs while synced, and it is checked FIRST: getIsPlaying
    // is a plain bool that is always present, whereas the position may be
    // absent. Checking the position first meant a host reporting PositionInfo
    // without a ppq left the sequencer free-running when the user pressed stop.
    if (! position->getIsPlaying())
        return {};

    const auto ppq = position->getPpqPosition();

    // NEGATIVE positions are accepted, deliberately, which differs from what
    // this plan asked for. A host count-in reports a negative ppq and the
    // windowed mapping handles it, so the groove plays through the count-in on
    // the same grid. Only non-finite values are refused here; magnitude is
    // checked below.
    if (! ppq.hasValue() || ! std::isfinite (*ppq))
        return tileForward (stepsPerSampleAt (internalBpm, sampleRate));

    const auto reportedBpm = position->getBpm();
    const auto hostReportedTempo = reportedBpm.hasValue() && std::isfinite (*reportedBpm)
                                && *reportedBpm > 0.0;

    const auto bpm = hostReportedTempo
                   ? juce::jlimit (static_cast<double> (forrobox::ids::kMinBpm),
                                   static_cast<double> (forrobox::ids::kMaxBpm), *reportedBpm)
                   : internalBpm;

    // Published for the header's BPM field, which shows this instead of the
    // parameter while SYNC is on. One relaxed store; see getHostBpm.
    //
    // The CLAMPED value, not the raw one, because that is the tempo this
    // plugin is actually running at — a field showing 900 while the groove
    // plays at 300 would be a readout of something that is not happening.
    hostBpm.store (hostReportedTempo ? static_cast<float> (bpm) : 0.0f,
                   std::memory_order_relaxed);

    const auto rate = stepsPerSampleAt (bpm, sampleRate);
    const auto hostPosition = hostStepPosition (*position, *ppq);

    // Refuse a position the clock could not use anyway, rather than storing it
    // and having every later span refused for the rest of the session.
    if (! std::isfinite (hostPosition) || std::abs (hostPosition) > forrobox::Clock::kMaxPosition)
        return tileForward (rate);

    // Re-anchor only when the host has genuinely moved. Below the threshold the
    // difference is integration error from the one tempo the host reports per
    // block, and tiling forward absorbs it — no duplicate, and no gap either.
    if (std::abs (hostPosition - positionInSteps) > kReanchorThresholdInSteps)
        positionInSteps = hostPosition;

    // ── does the host's loop end fall inside this block? ────────────────────
    // If it does the timeline is NOT contiguous across the block, and treating
    // it as one span drops the step at the loop start on every repetition: that
    // step sits behind the next block's start position, so neither block emits
    // it. With a one-bar loop at 120 BPM and 512-sample blocks the wrap lands
    // mid-block most of the time, so the groove loses its downbeat every bar.
    //
    // Only possible when the host reports loop points; many do not, and there
    // the re-anchor above at least keeps the wrap from duplicating steps.
    const auto loopPoints = position->getLoopPoints();

    if (position->getIsLooping() && loopPoints.hasValue()
        && std::isfinite (loopPoints->ppqStart) && std::isfinite (loopPoints->ppqEnd)
        && loopPoints->ppqEnd > loopPoints->ppqStart)
    {
        const auto stepsToWrap   = (loopPoints->ppqEnd - *ppq) * forrobox::Clock::kStepsPerBeat;
        const auto samplesToWrap = static_cast<int> (std::floor (stepsToWrap / rate));

        if (samplesToWrap > 0 && samplesToWrap < numSamples)
        {
            const auto loopStart = hostPosition
                                 + (loopPoints->ppqStart - *ppq) * forrobox::Clock::kStepsPerBeat;

            BlockPlan plan;
            plan.spans[0] = { positionInSteps, rate, samplesToWrap,              0 };
            plan.spans[1] = { loopStart,       rate, numSamples - samplesToWrap, samplesToWrap };
            plan.count = 2;

            // The watermark follows the wrap: after this block we are inside the
            // loop, not past its end.
            positionInSteps = loopStart + rate * static_cast<double> (numSamples - samplesToWrap);
            return plan;
        }
    }

    return tileForward (rate);
}


// ── parameter layout ────────────────────────────────────────────────────────
namespace
{
    using namespace juce;

    /** 0-100 with unit steps. Integer steps keep text<->value strictly
        invertible, which host-typed automation values and the Phase 4
        type-to-set knob both depend on. */
    NormalisableRange<float> percentRange() { return { 0.0f, 100.0f, 1.0f }; }

    /** "12%" <-> 12. */
    AudioParameterFloatAttributes percentAttributes()
    {
        return AudioParameterFloatAttributes()
            // No withValueFromStringFunction: AudioParameterFloat's default is
            // String::getFloatValue(), which is strtod-based and already stops at
            // the '%'. A custom parser here would only restate it.
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; });
    }

    /** "+5" / "0" / "-5" — the explicit plus is how the prototype shows pitch. */
    AudioParameterIntAttributes pitchAttributes()
    {
        return AudioParameterIntAttributes()
            // Only the display half is custom; AudioParameterInt's default parser
            // is already String::getIntValue().
            .withStringFromValueFunction ([] (int v, int) { return v > 0 ? "+" + String (v) : String (v); });
    }

    /** "C" at centre, else "L##" / "R##". */
    AudioParameterIntAttributes panAttributes()
    {
        return AudioParameterIntAttributes()
            .withStringFromValueFunction ([] (int v, int)
            {
                if (v == 0) return String ("C");
                return (v < 0 ? String ("L") : String ("R")) + String (std::abs (v));
            })
            .withValueFromStringFunction ([] (const String& t) -> int
            {
                const auto s = t.trim();
                if (s.startsWithIgnoreCase ("C")) return 0;

                const auto magnitude = s.retainCharacters ("0123456789").getIntValue();
                if (s.startsWithIgnoreCase ("L")) return -magnitude;
                if (s.startsWithIgnoreCase ("R")) return  magnitude;

                // Plain numeric entry keeps whatever sign it was typed with.
                // retainCharacters must keep the '-' here, or a host's typed
                // "-20" would come back as R20 — the opposite channel.
                return s.retainCharacters ("-0123456789").getIntValue();
            });
    }

    std::unique_ptr<AudioProcessorParameterGroup> makeGlobalGroup()
    {
        namespace ids = forrobox::ids;

        auto group = std::make_unique<AudioProcessorParameterGroup> (ids::groupGlobal, "GLOBAL", "|");

        group->addChild (
            std::make_unique<AudioParameterInt>    (ParameterID { ids::bpm, 1 },        "BPM", forrobox::ids::kMinBpm, forrobox::ids::kMaxBpm, 132),
            std::make_unique<AudioParameterBool>   (ParameterID { ids::sync, 1 },       "SYNC", false),
            std::make_unique<AudioParameterFloat>  (ParameterID { ids::swing, 1 },      "SWING",   percentRange(), 38.0f, percentAttributes()),
            std::make_unique<AudioParameterFloat>  (ParameterID { ids::cachaca, 1 },    String::fromUTF8 ("CACHA\xc3\x87" "A")   /* CACHAÇA — split so \x87 does not swallow the A */, percentRange(), 22.0f, percentAttributes()),
            std::make_unique<AudioParameterChoice> (ParameterID { ids::steps, 1 },      "STEPS",  stepWindowChoices(), 0),
            std::make_unique<AudioParameterChoice> (ParameterID { ids::timbre, 1 },     "TIMBRE", timbreChoices(), 0),
            std::make_unique<AudioParameterFloat>  (ParameterID { ids::charMix, 1 },    "MIX",    percentRange(), 40.0f, percentAttributes()),
            std::make_unique<AudioParameterBool>   (ParameterID { ids::limiterOn, 1 },  "LIMITER", true),
            std::make_unique<AudioParameterFloat>  (ParameterID { ids::master, 1 },     "MASTER", percentRange(), 82.0f, percentAttributes()),
            std::make_unique<AudioParameterChoice> (ParameterID { ids::outputMode, 1 }, "OUTPUT", StringArray { "STEREO", "MULTI-OUT" }, 0));

        return group;
    }

    std::unique_ptr<AudioProcessorParameterGroup> makeChannelGroup (size_t index)
    {
        namespace ids = forrobox::ids;

        const auto& info    = ids::channelInfos[index];
        const auto* channel = info.id;

        auto group = std::make_unique<AudioProcessorParameterGroup> (
                         channel, String::fromUTF8 (info.displayName), "|");

        group->addChild (
            std::make_unique<AudioParameterFloat> (ParameterID { ids::channelParam (channel, ids::vol),   1 }, "VOL",   percentRange(), info.vol,   percentAttributes()),
            std::make_unique<AudioParameterInt>   (ParameterID { ids::channelParam (channel, ids::pitch), 1 }, "PITCH", -12, 12, 0,              pitchAttributes()),
            std::make_unique<AudioParameterFloat> (ParameterID { ids::channelParam (channel, ids::decay), 1 }, "DECAY", percentRange(), info.decay, percentAttributes()),
            std::make_unique<AudioParameterInt>   (ParameterID { ids::channelParam (channel, ids::pan),   1 }, "PAN",   -ids::kPanExtent, ids::kPanExtent, info.pan, panAttributes()),
            std::make_unique<AudioParameterFloat> (ParameterID { ids::channelParam (channel, ids::ghost), 1 }, "GHOST", percentRange(), info.ghost, percentAttributes()),
            std::make_unique<AudioParameterBool>  (ParameterID { ids::channelParam (channel, ids::mute),  1 }, "MUTE",  false),
            std::make_unique<AudioParameterBool>  (ParameterID { ids::channelParam (channel, ids::solo),  1 }, "SOLO",  false));

        return group;
    }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout ForroBoxAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (makeGlobalGroup());

    for (size_t i = 0; i < forrobox::ids::channelInfos.size(); ++i)
        layout.add (makeChannelGroup (i));

    return layout;
}

juce::AudioProcessorEditor* ForroBoxAudioProcessor::createEditor()
{
    return new ForroBoxAudioProcessorEditor (*this);
}

void ForroBoxAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Serialise from a copy so the live parameter tree is never disturbed.
    //
    // Through the handle, not a bare ScopedLock. Both state methods used to take
    // stateLock directly, which left lockPatternState() with ZERO production
    // callers — so the "every writer publishes automatically" guarantee was
    // enforced at neither of its two production sites, and the next person to
    // add a writer would have copied the bypass. The handle's scope covers
    // apvts.copyState() exactly as the bare lock did.
    auto state = lockPatternState();

    auto tree = apvts.copyState();
    state->writeTo (tree);

    if (const auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void ForroBoxAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    // Absent, unparseable, or belonging to some other plugin — leave every
    // parameter at whatever it currently holds rather than half-applying.
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    // Through the handle: it publishes the restored grid on release, so the
    // audio thread cannot keep reading the pre-load one. That used to be an
    // explicit publishIfChanged call here — the one place the guarantee was not
    // automatic — and "the one place it is manual" is where it eventually gets
    // forgotten.
    auto state = lockPatternState();

    *state = forrobox::State::readFrom (tree);

    // Strip the grid child from a copy BEFORE handing the tree over, so the live
    // APVTS tree never holds a node it does not own. Stripping afterwards would
    // mean assigning an invalid tree and then repairing it.
    auto strippedTree = tree.createCopy();
    strippedTree.removeChild (strippedTree.getChildWithName (forrobox::ids::stateNode), nullptr);
    apvts.replaceState (strippedTree);
}

// Entry point the plugin wrappers call.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ForroBoxAudioProcessor();
}
