#include "PluginProcessor.h"

#include <utility>

#include <cmath>
#include "PluginEditor.h"

#include <array>
#include <cstdlib>
#include <type_traits>

/** Main stereo out, plus one stereo bus per channel.

    The five aux buses are named from `ids::channelInfos` rather than typed out,
    which is `profileCodes`' rule and the one that kept the OUTPUT toggle's
    labels honest at 04-05: a name a user reads in their host's routing panel and
    the channel it actually carries cannot drift apart.

    They are declared NOT enabled by default. A host that wants a plain stereo
    instrument gets one, and every session that predates multi-out opens
    unchanged — which is AC-1, and the regression surface this plan actually
    risks.

    PLANNING.md:845: "Route each channel to its own output bus (5 stereo buses or
    5 mono + master). Declare the extra buses in the VST3 bus layout." Stereo
    rather than mono, because every channel has a PAN a mono bus would discard. */
juce::AudioProcessor::BusesProperties ForroBoxAudioProcessor::makeBusesProperties()
{
    // Instrument: declaring an input bus makes some hosts present this as an
    // effect, so there is deliberately none.
    auto properties = BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);

    for (const auto& info : forrobox::ids::channelInfos)
        properties = properties.withOutput (juce::String (juce::CharPointer_UTF8 (info.displayName)),
                                            juce::AudioChannelSet::stereo(), false);

    return properties;
}

ForroBoxAudioProcessor::ForroBoxAudioProcessor()
    : juce::AudioProcessor (makeBusesProperties())
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
    outputModeParam = apvts.getRawParameterValue (forrobox::ids::outputMode);

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
                      && masterParam != nullptr && outputModeParam != nullptr;

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
    setLatencySamples (outputDelaySamples());

    // The window as it stands, so the first real CHANGE tiles and merely
    // observing the initial value does not.
    lastTiledWindow = currentStepWindow();
}

void ForroBoxAudioProcessor::timerCallback()
{
    applyPendingStepChange();
}

void ForroBoxAudioProcessor::applyPendingStepChange()
{
    // MESSAGE THREAD, and the edge detected here rather than delivered to it.
    const auto window = currentStepWindow();

    if (window == std::exchange (lastTiledWindow, window))
        return;

    // Widening only. Narrowing tiles nothing — see State::tileToFullWidth for
    // why that is a decision rather than a missing branch.
    if (window <= forrobox::ids::stepWindows.front())
        return;

    // Through the handle, so the publish to the audio thread happens on release
    // like every other writer's.
    lockPatternState()->tileToFullWidth();
}

namespace
{
/** One parameter to one denormalised value, as a complete host gesture.

    BRACKETED, and it was not. A bare `setValueNotifyingHost` changes the value
    audibly but writes no automation, because a host only records while a gesture
    is open — so a user with BPM armed in Touch or Latch who clicked a profile
    would have heard the tempo change and captured nothing. JUCE's VST3 wrapper
    also turns an unbracketed write into `performEdit` with no `beginEdit`, which
    Steinberg's validator flags. Every other write path in this plugin goes
    through `ParameterAttachment::setValueAsCompleteGesture`, which brackets;
    this one claimed "as a complete host gesture" in its own docstring and did
    not do it. /code-review.

    `setValueNotifyingHost` takes a NORMALISED value — a reload writing 132 into
    a 40..300 BPM parameter without converting would set it to the maximum and
    the groove would run at 300. */
void writeParameter (juce::AudioProcessorValueTreeState& apvts, juce::StringRef id, float value)
{
    auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id));

    // A renamed or typo'd id would otherwise make the reload skip that field in
    // silence. The test only caught that by the luck of its -1 sentinel not
    // matching any real profile value.
    jassert (parameter != nullptr);

    if (parameter == nullptr)
        return;

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    parameter->endChangeGesture();
}
} // namespace

void ForroBoxAudioProcessor::loadProfile (const forrobox::Profile& profile)
{
    // The law the docstring states, now enforced: this takes a lock and builds
    // juce::Strings through `channelParam`, whose own header says never to call
    // it from `processBlock`.
    JUCE_ASSERT_MESSAGE_THREAD

    // ── the parameter half FIRST, and the MUTES are why ────────────────────
    //
    // Publishing the pattern first hands the audio thread the new groove while
    // the old gates are still in force — and the gate is applied at SCHEDULE
    // time, so a step landing in that window is scheduled unmuted and then rings
    // out for its whole decay. Loading CAMPINA mid-transport is exactly that
    // case: it mutes bateria, and its four kit lanes are full. The reverse
    // window — the old pattern under the new gates, for at most one block — is
    // inaudible by comparison. /code-review.
    writeParameter (apvts, forrobox::ids::bpm,     static_cast<float> (profile.bpm));
    writeParameter (apvts, forrobox::ids::swing,   profile.swing);
    writeParameter (apvts, forrobox::ids::cachaca, profile.cachaca);
    writeParameter (apvts, forrobox::ids::timbre,  static_cast<float> (profile.timbreIndex));

    for (const auto& info : forrobox::ids::channelInfos)
    {
        // `bateriaMuted` is the only mute the tables carry — every other channel
        // is unmuted by a load, which is what `app.js:532-534` does when it reads
        // `p.muted[inst.id]` and finds nothing.
        const auto muted = profile.bateriaMuted
                        && juce::StringRef (info.id) == juce::StringRef ("bateria");

        writeParameter (apvts, forrobox::ids::channelParam (info.id, forrobox::ids::mute),
                        muted ? 1.0f : 0.0f);

        // SOLO IS CLEARED, and it is not profile data. `app.js:534` clears every
        // solo on load; leaving one set would silence the groove that was just
        // loaded and look like the reload had failed.
        writeParameter (apvts, forrobox::ids::channelParam (info.id, forrobox::ids::solo), 0.0f);
    }

    // ── then the pattern, under one lock ───────────────────────────────────
    //
    // `applyProfile` fills all 32 slots per lane from the 16-step source, which
    // is 02-01's storage model — the window selects which slots are READ and
    // never decides their contents. That is what makes a load at 32 steps repeat
    // the bar, the way `buildGroove (id, state.steps)` does in the prototype.
    //
    // The handle publishes on destruction, so the engine picks the new table up
    // through the mechanism it already follows — and by now it is already under
    // this profile's gates.
    //
    // Nothing marks the state dirty on a parameter change today, so the writes
    // above cannot undo the `dirty` this clears. PLANNING.md:601's "editing
    // anything marks the state dirty" is a recorded deferral, and whoever lands
    // it has to exempt this function explicitly — this ordering is not that
    // exemption.
    {
        auto handle = lockPatternState();

        forrobox::applyProfile (*handle, profile);
    }
}

bool ForroBoxAudioProcessor::isStateDirty()
{
    return lockPatternState()->dirty;
}

int ForroBoxAudioProcessor::selectedProfileIndex()
{
    juce::String stored;
    auto isDirty = false;

    {
        auto handle = lockPatternState();

        stored = handle->activeProfile;
        isDirty = handle->dirty;
    }

    // An EDITED state is not the profile it names — `app.js:555` is
    // `pid === state.activeProfile && !state.dirty`, and PLANNING.md:601 says
    // the highlight clears. Both readers went through `indexOfProfile` alone and
    // so kept the highlight lit over a state that had stopped being that groove.
    return isDirty ? -1 : forrobox::ChassisLayout::indexOfProfile (stored, -1);
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

    /** Built from `ids::outputModes`, the table the footer's OUTPUT toggle also
        reads — so the text a user sees and the index a saved project holds are
        one list, not two that agree today. */
    juce::StringArray outputModeChoices()
    {
        juce::StringArray choices;

        for (const auto* mode : forrobox::ids::outputModes)
            choices.add (mode);

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
    publishTransportStopped();
    resetPending.store (true, std::memory_order_relaxed);
    playing.store (shouldPlay, std::memory_order_release);
}

void ForroBoxAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Only while the plugin is PREPARED. It costs 0.27% of a core at 30 Hz
    // (/simplify measured JUCE's per-tick dispatch at 60-90 us), and a host
    // SCANNING the plugin, or holding a deactivated instance, should pay none of
    // it. Anything the window did while stopped is caught by the first tick,
    // because the drain compares against `lastTiledWindow` rather than
    // consuming an event.
    startTimerHz (kStepTilingPollHz);

    // The only place allocation is permitted. Later phases size their voice
    // pools, pattern buffers and FIFOs here.
    currentSampleRate.store (sampleRate,      std::memory_order_relaxed);
    currentBlockSize .store (samplesPerBlock, std::memory_order_relaxed);

    // prepareToPlay runs with the audio device stopped, so touching the clock
    // and the internal position directly is safe here.
    clock.reset();
    positionInSteps = 0.0;
    publishTransportStopped();

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
    setLatencySamples (outputDelaySamples());
}

void ForroBoxAudioProcessor::releaseResources()
{
    stopTimer();

    // Deliberately does NOT reset the cached rate/block size. A host may close
    // its audio device with the editor still open and querying; zeroing here
    // would hand out a 0.0 sample rate indefinitely.
}

bool ForroBoxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Still an instrument: no input bus, ever.
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled()
        || ! layouts.inputBuses.isEmpty())
        return false;

    // The MAIN bus must be stereo and must be present.
    //
    // Refusing a disabled main bus is the point of stating this separately: a
    // host allowed to turn it off would give a user a plugin that is silent on
    // the output they are actually listening to, and the obvious diagnosis
    // ("multi-out is broken") would be wrong.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Each aux bus is stereo or disabled, independently — a host may enable
    // three of five, and several do.
    for (int bus = 1; bus < layouts.outputBuses.size(); ++bus)
    {
        const auto& set = layouts.outputBuses.getReference (bus);

        if (set != juce::AudioChannelSet::stereo() && set != juce::AudioChannelSet::disabled())
            return false;
    }

    // And no more buses than were declared.
    return layouts.outputBuses.size() <= kNumOutputBuses;
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

    // Every bus this block writes, addressed as buses rather than as raw
    // channel numbers.
    //
    // With six buses enabled `buffer.getNumChannels()` is 12, and VoiceEngine
    // reads that number to decide whether there is a right channel to pan into.
    // Writing getWritePointer(0)/(1) still happened to hit main's left and right
    // — correct by accident, and the accident would have survived every existing
    // test, because they all render through a single stereo bus.
    forrobox::VoiceEngine::Stems stems;
    juce::AudioBuffer<float> mainBus;

    resolveRenderTargets (buffer, mainBus, stems);

    // The aux buses need no explicit clear: `buffer.clear()` at the top of this
    // method covers EVERY enabled bus, because `buffer` is the host's whole
    // multi-bus buffer. Said here rather than adding a second clear that would
    // look load-bearing and would not be — AC-4 is that they are silent because
    // they were cleared, and this is where that happens.

    // A GATE, not a request. Stems bypass the mix bus, and a host applies the
    // plugin's reported latency to every output bus alike — so a non-zero mix-bus
    // latency would make all five stems arrive that many samples EARLY relative to
    // the main bus and the host grid. That is a sub-millisecond flam visible only
    // to someone printing stems, which is the least likely class of bug to be
    // caught by ear. MixBus.h carries the same warning on the constant; this
    // breaks the build from the other end.
    static_assert (forrobox::MixBus::kLatencySamples == 0,
                   "stems bypass the mix bus, so a non-zero mix-bus latency must be matched "
                   "by an equal delay on the per-channel stem path");

    engine.render (mainBus, stems);

    // The output stage, immediately after the sum and once per block:
    // voices -> character bus -> limiter -> master, which is PLANNING.md's
    // chain. Unconditional, for the same reason engine.render is — and on the
    // MAIN bus only. Stems are pre-character, pre-limiter, pre-master by
    // decision at 04-06 planning, so they never enter it.
    mixBus.process (mainBus, resolveBusSettings());
}

/** Points `view` at `count` channels of `buffer` starting at `first`, or makes it
    EMPTY if that range does not fit.

    `setDataToReferTo` rather than assigning a returned buffer, and that is the
    whole reason this is a void function: AudioBuffer's MOVE assignment is
    allocation-free but its COPY assignment calls setSize and mallocs — so
    assigning a returned buffer was correct only while the result stayed a prvalue,
    and hoisting it into a named local would have put five mallocs per block on the
    audio thread AND silently detached each stem from the host's buffer. This form
    cannot be broken that way. */
void ForroBoxAudioProcessor::pointStemAt (juce::AudioBuffer<float>& view,
                                          juce::AudioBuffer<float>& buffer,
                                          int first, int count) noexcept
{
    if (count <= 0 || first < 0 || first + count > buffer.getNumChannels())
    {
        view.setSize (0, 0);
        return;
    }

    view.setDataToReferTo (buffer.getArrayOfWritePointers() + first, count,
                           buffer.getNumSamples());
}

/** Whether MULTI-OUT is selected.

    Read once per block, which QUANTISES a mode change to the block boundary
    rather than removing it: automating MULTI-OUT -> STEREO while a zabumba hit is
    ringing drops the aux buses from full amplitude to zero at that boundary, which
    is a click in whatever is recording the stems. Named by /code-review at 04-06
    and left as it is — a stem-side fade is a real feature with a real time
    constant to choose, and inventing one here would be worse than saying plainly
    that it is not done. The main bus, which is what a user is listening to, is
    unaffected either way. */
bool ForroBoxAudioProcessor::isMultiOut() const noexcept
{
    return parametersResolved
        && outputModeParam->load (std::memory_order_relaxed) >= 0.5f;
}

/** Points `mainBus` and each entry of `stems` at the buses they render into.

    A sibling of `resolveBusSettings`, and for its reason: both derive a per-block
    value from parameters and layout so `processBlock` reads at one altitude. That
    method is deliberately three lines of intent — `scheduleBlock`, `render`,
    `mixBus.process` — and the comment above `scheduleBlock` explains why that
    shape is load-bearing. Thirty lines of bus arithmetic in the middle of it made
    the shape unreadable.

    Both are passed in rather than returned because the views must outlive this
    call; nothing here allocates.

    A view with zero channels means "no audio here", which is what `pointStemAt`
    leaves for a bus that is disabled, absent, or wider than the buffer the caller
    supplied. ONE rule, in one place — it used to be tested again in the loop below
    and a third time inside VoiceEngine. */
void ForroBoxAudioProcessor::resolveRenderTargets (juce::AudioBuffer<float>& buffer,
                                                   juce::AudioBuffer<float>& mainBus,
                                                   forrobox::VoiceEngine::Stems& stems) noexcept
{
    if (isMultiOut())
        for (int c = 0; c < forrobox::VoiceEngine::kNumChannels; ++c)
        {
            const auto bus = busForChannel (c);

            // Where this bus's channels start in the host's interleaved
            // multi-bus buffer. JUCE's own function, not a hand-rolled sum of
            // getChannelCountOfBus over the earlier buses — which is what this
            // was, character for character, including the rule that a disabled
            // bus contributes zero.
            pointStemAt (stems[static_cast<size_t> (c)], buffer,
                         getChannelIndexInProcessBlockBuffer (false, bus, 0),
                         getChannelCountOfBus (false, bus));
        }

    // Main is bus 0, so its offset is exactly 0 and needs no lookup. Its width is
    // ASKED of the bus rather than written as the literal 2 the rest of this
    // file's stereo assumption would allow: VoiceEngine.cpp already warns it is
    // "a trap for whoever relaxes that check", and a hard-coded 2 against a mono
    // main would have aux bus 1 starting at host channel 1 while this view still
    // claimed channels 0 and 1 — writing the full mix, limiter and master into
    // ZABUMBA's stem.
    pointStemAt (mainBus, buffer, 0, getChannelCountOfBus (false, 0));
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

    // ── the host's tempo, published for the header's BPM field ─────────────
    //
    // HERE, above every early return, and unconditionally. It used to sit
    // inside planBlock beside the read that produces it, which put it below
    // four returns and the `if (! position->getIsPlaying())` at :550 — so a
    // stopped host or a stopped plugin left the last value frozen and the
    // field went on showing a tempo that was no longer running. The 0 sentinel
    // was only ever written on the one narrow path where a ppq existed but a
    // bpm did not. Found by /code-review.
    //
    // One relaxed store per block, which is neither an allocation nor a lock.
    //
    // The position is fetched ONCE, here, and handed to planBlock — getPosition
    // is called exactly once per block and a test counts it.
    auto* head = getPlayHead();
    const auto hostPosition = head != nullptr ? head->getPosition()
                                              : juce::Optional<juce::AudioPlayHead::PositionInfo>();

    hostBpm.store (hostBpmFrom (hostPosition), std::memory_order_relaxed);

    // ── which transport governs ────────────────────────────────────────────
    //
    // While SYNC is on, the HOST's. `PLANNING.md:838` says SYNC should "follow
    // host tempo and transport", and 02-03's own summary says "the host's
    // transport decides whether anything plays" — but the plugin's `playing`
    // gated here as well, so a synced plugin stayed silent against a rolling
    // host until its own Play was pressed too.
    //
    // It shipped because every host-sync test builds its rig with
    // setPlaying(true), so "host rolling, plugin stopped" was never exercised.
    // Reported at 04-04's checkpoint; the fix touches Phase 2's closed code and
    // was confirmed before it was made.
    //
    // planBlock already refuses to emit while the host is stopped, so under
    // SYNC this gate simply steps aside and the `plan.count == 0` branch below
    // reports stopped exactly as it does for a host that pauses mid-session.
    const auto syncedToHost = syncParam->load (std::memory_order_relaxed) > 0.5f;

    hostTransportRolling.store (syncedToHost && hostPosition.hasValue()
                                    && hostPosition->getIsPlaying(),
                                std::memory_order_relaxed);

    if (! syncedToHost && ! isPlayingNow)
    {
        // Self-healing: a step emitted in the window between setPlaying's two
        // stores would otherwise leave the playhead parked on a live step.
        publishTransportStopped();

        // Nothing scheduled; the render at the bottom of processBlock still
        // drains whatever was already sounding.
        return;
    }

    const auto numSamples = numSamplesThisBlock;
    const auto sampleRate = currentSampleRate.load (std::memory_order_relaxed);
    const auto plan = planBlock (numSamples, sampleRate, hostPosition);

    if (plan.count == 0)
    {
        // The host's transport is stopped while synced. Report stopped rather
        // than leaving the playhead on whichever step fired last, and reset the
        // clock so its own reported step agrees. Voices still ring out.
        clock.reset();
        publishTransportStopped();
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
            forrobox::VoiceEngine::StepVelocities velocities {};

            for (size_t lane = 0; lane < lanes.size(); ++lane)
                velocities[lane] = lanes[lane][index];

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
            // ONE publication, carrying the step and its velocities together.
            // Three stores before this — the velocities, then the step with
            // release ordering, then the count — which made a reader's two
            // loads able to straddle two steps. See StepSnapshot.h.
            owner.stepPublisher.publish (event.step, velocities);
        }
    };

    BlockEmitter emitter { patternReader.lanes(), engine, *this };

    for (int i = 0; i < plan.count; ++i)
        clock.advance (plan.spans[static_cast<size_t> (i)], params, emitter);

    // ── where the groove is, for the playhead ───────────────────────────────
    //
    // The END of the last span is where the timeline has reached. Derived from
    // the span's own rate rather than re-read from the host, so the playhead
    // and the steps cannot disagree about where the block sat.
    //
    // MINUS the lookahead. The emitter above publishes at GRID time while this
    // block's audio leaves the plugin `getLookaheadSamples()` later, so an
    // uncorrected position leads what the user hears by 32 ms — roughly 28% of
    // a sixteenth at 132 BPM, which is a playhead visibly ahead of the groove.
    // The emitter's own comment predicted this and named Phase 5 as the owner.
    //
    // The jitter is NOT corrected for: it is per-step and bipolar by design, so
    // there is no single offset that answers it, and a playhead that jittered
    // with the audio would read as a broken playhead rather than as humanised
    // timing. The fixed part is the part that is a lie.
    {
        const auto& last = plan.spans[static_cast<size_t> (plan.count - 1)];

        const auto endInSteps = last.startInSteps + last.stepsPerSample * last.numSamples;
        const auto lookahead  = last.stepsPerSample
                              * static_cast<double> (outputDelaySamples());

        displayPositionInSteps.store (endInSteps - lookahead, std::memory_order_relaxed);
    }
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

float ForroBoxAudioProcessor::hostBpmFrom (const juce::Optional<juce::AudioPlayHead::PositionInfo>& position)
{
    // The CLAMPED host tempo, or 0 when there is no host tempo to have. ONE
    // definition, read by planBlock for the clock's rate and published for the
    // BPM field, so the number the field shows is the number the groove runs
    // at rather than a second reading of the same source.
    //
    // A pure function of a PositionInfo, NOT a second getPosition() call.
    // Fetching the position again cost three calls per block where the whole
    // sync path is built on exactly one — hosts populate PositionInfo
    // inconsistently and some of them are expensive there, which is why a test
    // counts the calls.
    if (! position.hasValue())
        return 0.0f;

    const auto reported = position->getBpm();

    if (! reported.hasValue() || ! std::isfinite (*reported) || *reported <= 0.0)
        return 0.0f;

    return static_cast<float> (juce::jlimit (static_cast<double> (forrobox::ids::kMinBpm),
                                             static_cast<double> (forrobox::ids::kMaxBpm),
                                             *reported));
}

ForroBoxAudioProcessor::BlockPlan
ForroBoxAudioProcessor::planBlock (int numSamples, double sampleRate,
                                  const juce::Optional<juce::AudioPlayHead::PositionInfo>& position) noexcept
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
    // getPosition() is called exactly once per block — by processBlock, which
    // hands the result down. Every PositionInfo field is Optional and hosts
    // populate them inconsistently, so an absent field means "cannot sync this
    // block", never zero.
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

    // hostBpmFrom is the ONE definition of "the host's tempo, clamped", and
    // processBlock publishes the same function's result from the same
    // PositionInfo — so the number the header displays is the number the clock
    // is running at, not a second reading.
    const auto publishedHostBpm = hostBpmFrom (position);
    const auto bpm = publishedHostBpm > 0.0f ? static_cast<double> (publishedHostBpm)
                                             : internalBpm;

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
            std::make_unique<AudioParameterChoice> (ParameterID { ids::outputMode, 1 }, "OUTPUT", outputModeChoices(), 0));

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

    // A RESTORE IS NOT A STEP CHANGE, and this line is why the user's second bar
    // still exists.
    //
    // `replaceState` drives every restored parameter through
    // `setValueNotifyingHost`, which calls `parameterChanged` SYNCHRONOUSLY. A
    // project saved at 32 steps therefore looked to the listener exactly like
    // somebody switching from 16 to 32 — so the tiling fired and overwrote slots
    // 16-31, which the restore had just filled correctly, with a copy of 0-15.
    // Every reload silently destroyed the second bar.
    //
    // Re-baselined AFTER the replace, so the window the restore established is
    // the one a later change is measured against, and the flag is cleared
    // because the lanes on disk are already what they should be. Found by
    // /code-review; the test that should have caught it did not drain the
    // pending update, so it was asserting against something a real host applies.
    lastTiledWindow = currentStepWindow();
}

// Entry point the plugin wrappers call.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ForroBoxAudioProcessor();
}
