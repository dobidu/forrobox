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

    jassert (bpmParam != nullptr && swingParam != nullptr
             && stepsParam != nullptr && syncParam != nullptr);
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
    currentStep.store (forrobox::Clock::kStoppedStep, std::memory_order_relaxed);
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
    currentStep.store (forrobox::Clock::kStoppedStep, std::memory_order_relaxed);
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

    // AUDIO-THREAD CONTRACT — no allocation, no locks, no I/O, no logging,
    // no juce::String construction. Every later phase inherits this rule.
    //
    // In particular: patternState is NOT read here. Looking up what a step
    // should trigger would mean taking the state lock on the audio thread,
    // which is the data race 02-04's double-buffer handover exists to solve.
    buffer.clear();
    midi.clear();

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
    }

    if (! isPlayingNow)
    {
        // Self-healing: a step emitted in the window between setPlaying's two
        // stores would otherwise leave the playhead parked on a live step.
        currentStep.store (forrobox::Clock::kStoppedStep, std::memory_order_relaxed);
        return;
    }

    // Restored after a review found it deleted in the 02-03 restructure. A
    // parameter-ID rename leaves these null, getRawParameterValue returns
    // nullptr for an unknown ID, and the constructor's jassert compiles away in
    // Release — where the dereference takes the host down instead of failing
    // visibly. planBlock reads two of them, so the check must precede it.
    if (bpmParam == nullptr || swingParam == nullptr
        || stepsParam == nullptr || syncParam == nullptr)
        return;

    const auto numSamples = buffer.getNumSamples();
    const auto sampleRate = currentSampleRate.load (std::memory_order_relaxed);
    const auto plan = planBlock (numSamples, sampleRate);

    if (plan.count == 0)
    {
        // The host's transport is stopped while synced. Report stopped rather
        // than leaving the playhead on whichever step fired last, and reset the
        // clock so its own reported step agrees.
        clock.reset();
        currentStep.store (forrobox::Clock::kStoppedStep, std::memory_order_relaxed);
        return;
    }

    const forrobox::Clock::Params params {
        swingParam->load (std::memory_order_relaxed),
        stepsForChoiceIndex (juce::roundToInt (stepsParam->load (std::memory_order_relaxed)))
    };

    for (int i = 0; i < plan.count; ++i)
        clock.advance (plan.spans[static_cast<size_t> (i)], params, *this);
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

    const auto hostBpm = position->getBpm();
    const auto bpm = (hostBpm.hasValue() && std::isfinite (*hostBpm) && *hostBpm > 0.0)
                   ? juce::jlimit (static_cast<double> (forrobox::ids::kMinBpm),
                                   static_cast<double> (forrobox::ids::kMaxBpm), *hostBpm)
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

void ForroBoxAudioProcessor::stepTriggered (forrobox::StepEvent event)
{
    // One job. It previously also filtered duplicate steps and shifted the
    // sample offset into the block's frame; spans tiling by construction
    // removed the need for the first, and the clock owning its span's offset
    // removed the second.
    //
    // Nothing consumes the step until Phase 3 gives it a voice to trigger. The
    // stores are what Phase 5's playhead and activity meter will read.
    currentStep.store (event.step, std::memory_order_relaxed);
    emittedSteps.fetch_add (1, std::memory_order_relaxed);
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
            std::make_unique<AudioParameterChoice> (ParameterID { ids::timbre, 1 },     "TIMBRE", StringArray { "HI-FI", "LO-FI", "CICLOTRON" }, 0),
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
            std::make_unique<AudioParameterInt>   (ParameterID { ids::channelParam (channel, ids::pan),   1 }, "PAN",   -50, 50, info.pan,          panAttributes()),
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
    // Takes stateLock directly rather than via lockPatternState() because the
    // lock must also cover apvts.copyState(); the effect is the same lock.
    const juce::ScopedLock lock (stateLock);

    auto tree = apvts.copyState();
    patternState.writeTo (tree);

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

    const juce::ScopedLock lock (stateLock);

    patternState = forrobox::State::readFrom (tree);

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
