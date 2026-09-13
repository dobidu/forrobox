/* ============================================================================
   FORRÓ BOX — the voice engine

   A PERSISTENT member of the processor, prepared with the sample rate and block
   size, owning the voice pools, the sampler and the noise generator.

   WHY PERSISTENT, and not the block emitter that feeds it — recorded at 02-04's
   altitude review, before any of this existed:

     - `CACHAÇA` timing jitter (03-02) can place a trigger AFTER the end of the
       block that scheduled it. A stack object destroyed at block end has
       nowhere to carry "this one fires 3 ms from now"
     - the RNG, the smoothers, the character bus and the limiter are all
       `prepareToPlay` lifetime. An RNG rebuilt per block would either reseed
       every block or need a mutable member — reintroducing the channel
       `/simplify` has already removed twice
     - `BlockEmitter` stays a thin adapter: it translates a StepEvent plus lane
       velocities into `schedule()` calls and does no DSP. Synthesis inside
       `stepTriggered` would interleave rendering with step placement, which is
       the shape 02-03's review removed once already

   `schedule` is called from inside `clock.advance`; `render` exactly once per
   block, afterwards.
============================================================================ */
#pragma once

#include "Humanisation.h"
#include "Voices.h"
#include "ZabumbaSampler.h"
#include "ParameterIDs.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace forrobox
{

/** Lane -> channel, derived rather than written down.

    Four of the eight lanes (BB, CX, HH, TOM) share the single BATERIA channel's
    VOL/PITCH/DECAY/PAN/mute/solo. The mapping is computed from the id lists at
    compile time by the same argument `State::kNumLanes` is derived: the channel
    whose id no lane carries is the composite one, so adding a lane or a channel
    cannot leave a hand-written table behind. */
namespace detail
{
    constexpr bool sameId (const char* a, const char* b) noexcept
    {
        while (*a != '\0' && *a == *b) { ++a; ++b; }
        return *a == *b;
    }

    constexpr int compositeChannel() noexcept
    {
        for (size_t c = 0; c < ids::channelInfos.size(); ++c)
        {
            auto claimed = false;

            for (size_t l = 0; l < ids::lanes.size(); ++l)
                if (sameId (ids::lanes[l], ids::channelInfos[c].id))
                    claimed = true;

            if (! claimed)
                return static_cast<int> (c);
        }

        return -1;
    }

    constexpr std::array<int, ids::lanes.size()> makeLaneToChannel() noexcept
    {
        std::array<int, ids::lanes.size()> map {};
        const auto composite = compositeChannel();

        for (size_t l = 0; l < ids::lanes.size(); ++l)
        {
            map[l] = composite;

            for (size_t c = 0; c < ids::channelInfos.size(); ++c)
                if (sameId (ids::lanes[l], ids::channelInfos[c].id))
                    map[l] = static_cast<int> (c);
        }

        return map;
    }

    inline constexpr auto laneToChannel = makeLaneToChannel();

    static_assert (compositeChannel() >= 0,
                   "one channel must carry no lane of its own — it is the composite kit channel");

    /** The hi-hat lane's index, found by name rather than written down.

        PLANNING.md: "For bateria, ghosts are generated on the hi-hat (HH)
        only." The composite kit's other three lanes never ghost, so the rule
        needs to know which one HH is — derived, for the same reason the
        lane→channel map is derived. */
    constexpr int ghostingKitLane() noexcept
    {
        for (size_t l = 0; l < ids::lanes.size(); ++l)
            if (sameId (ids::lanes[l], "hh"))
                return static_cast<int> (l);

        return -1;
    }

    static_assert (ghostingKitLane() >= 0,
                   "the kit lane that ghosts must exist — PLANNING.md names it as hh");

    /** Whether a lane can produce ghost notes at all.

        Every lane with a channel of its own can; among the composite kit's
        lanes, only HH. */
    constexpr bool laneCanGhost (int lane) noexcept
    {
        if (lane < 0 || lane >= static_cast<int> (ids::lanes.size()))
            return false;

        return laneToChannel[static_cast<size_t> (lane)] != compositeChannel()
            || lane == ghostingKitLane();
    }
}

class VoiceEngine
{
public:
    static constexpr int kNumChannels = static_cast<int> (ids::channelInfos.size());

    /** Pool sizes.

        Step interval at the fastest supported tempo (kMaxBpm = 300) is
        60/300/4 = 50 ms, so a lane holds ceil(duration / 50 ms) voices at once.
        Each synthesised lane's longest voice, at maximum DECAY (decayScale 1.8)
        and full velocity:

          triângulo  0.45 x 1.8 = 0.810 s  -> 17
          pandeiro   0.20 x 1.8 = 0.360 s  ->  8
          tom        0.20 x 1.8 = 0.360 s  ->  8
          bb         0.14 x 1.8 = 0.252 s  ->  6
          cx         0.14 x 1.8 = 0.252 s  ->  6
          ganzá      0.065 x 1.8 = 0.117 s ->  3
          hh         0.070 x 1.8 = 0.126 s ->  3
                                              --
                                              51

        The first version of this comment said "seven lanes x 17 = 119", which
        was wrong by more than a factor of two: only the triângulo reaches 17.
        The sum is what matters, and it is 51.

        The sampled zabumba's longest layer is 0.498 s, doubled to 0.996 s by
        PITCH -12 (read rate 0.5) -> 20 overlapping hits, and doubled again to
        40 by the velocity crossfade sounding two layers per strike. The
        crossfade collapses to one layer only at the very ends of the velocity
        range.

        MEASURED, 8 lanes x 32 steps at 300 BPM with every DECAY at 100
        (tests/VoiceTest.cpp, testVoicePoolUnderPressure):

          velocity  40 ->  48 voices      velocity  90 ->  70 voices
          velocity  64 ->  53 voices      velocity 127 ->  61 voices

        Re-measured after 03-02 added humanisation, with per-pool counters: the
        synthetic worst case reaches 77 voices (56 synth, 22 sample) at CACHAÇA
        100 with every GHOST at 100, and the four real profiles reach 20. Ghosts
        cost about +2 voices; the 32 ms lookahead holds a further 18 pending at
        CACHAÇA 100 against 9 at 0, which never exceeds one step's worth because
        32 ms sits inside the 50 ms step at the fastest supported tempo. Zero
        steals and zero drops across 21 configurations, so the margin is 2.3x on
        synth and 2.2x on sample.

        77 is the worst observed, against a combined capacity of 176. The
        margin is deliberate rather than leftover: 03-02's timing jitter can
        push a hit up to 22 ms past its step, which at a 50 ms step overlaps the
        next one and raises concurrency in a way this arithmetic does not model.
        Its ghost notes do NOT, though — a ghost only fires on a step where the
        lane has no programmed hit, so hits per lane per step stays at most one.

        The test asserts nothing is stolen at that worst case, and that the peak
        stays inside the pools. It previously asserted getVoicesDropped() == 0,
        which could never fail: claimSynthVoice and claimSampleVoice steal
        rather than return null, so the drop counter was unreachable and the
        pool arithmetic went unverified in both directions. */
    static constexpr int kSynthVoices  = 128;
    static constexpr int kSampleVoices = 48;

    /** The longest a single voice can ring, from the same arithmetic: the
        0.498 s zabumba layer pitched down an octave, so 0.996 s.

        PLUS the humanisation's own reach. A host bouncing to disk stops
        rendering after the last EVENT plus this figure, and a trigger can land
        up to 22 ms (step jitter) or 32 ms (a ghost, jitter and its own offset
        together) after the event that scheduled it. At 1.0 s flat — which is
        what this was — the final zabumba decay was truncated by up to 30 ms.

        The 32 ms lookahead itself needs no allowance: the host compensates it.
        Spelled as the two excursions rather than as kLookaheadSeconds, which is
        numerically the same only because the lookahead IS their sum — the
        sentence above is about the excursions, so the code says so. */
    static constexpr double kMaxVoiceSeconds = 0.996;
    static constexpr double kMaxTailSeconds  = kMaxVoiceSeconds
                                             + kMaxJitterSeconds + kGhostJitterSeconds;

    /** Per-channel values, resolved once per block on the audio thread.

        VOL and PAN are read at render time so moving a knob affects notes
        already sounding, which is what the prototype's graph does — the gain
        node is downstream of the envelope. PITCH and DECAY are captured at
        trigger time instead, because they determine the note's duration and
        cannot change under a voice that is already running. */
    struct ChannelSettings
    {
        float vol { 80.0f };     ///< 0-100, linear gain
        float pitch { 0.0f };    ///< semitones
        float decay { 50.0f };   ///< 0-100
        float pan { 0.0f };      ///< -100 (left) to +100 (right)
        float ghost { 0.0f };    ///< 0-100, the ghost-note probability
        bool  audible { true };  ///< the resolved mute/solo gate
    };

    /** Everything a block schedules and renders with.

        A struct rather than a bare array of channels, because CACHAÇA is a
        GLOBAL: one value for the whole instrument, not one per channel.
        Duplicating it across five channels would invite five different values
        for one knob. */
    struct Settings
    {
        std::array<ChannelSettings, static_cast<size_t> (kNumChannels)> channels {};

        /** CACHAÇA as 0..1, normalised and NaN-guarded ONCE where the
            parameter enters, not at each of the sites that read it.

            It was stored raw and normalised at three use sites, one of which
            carried a `>= 0.0f` guard that looked dead — jlimit already bounds
            it — but was in fact catching NaN, which jlimit passes through
            unchanged. The other two sites had no such guard, so a non-finite
            CACHAÇA reached the velocity multiplier. Clock.cpp does this
            correctly for swing (`isfinite ? value : 0.0`), and a guard living
            at one of three use sites is the shape that makes the other two
            possible and invisible. */
        float cachaca { 0.0f };
    };

    /** Allocates the pools, the filter state and the samples. Called from
        prepareToPlay, with the audio device stopped. */
    void prepare (double sampleRate, int maxBlockSize);

    /** How far every trigger is delayed, in samples at the prepared rate.

        The processor reports this to the host with setLatencySamples, so the
        host shifts the recording back and the groove lands where it should.
        See kLookaheadSeconds for why it is 32 ms rather than 22. */
    int getLookaheadSamples() const noexcept { return lookaheadSamples; }

    /** Silences everything and rewinds the humanisation, so a render from a
        known state is reproducible. Audio thread safe. */
    void reset() noexcept;

    /** Renders the same pattern under a different humanisation realisation.

        Call between prepare and the first block. For sampling a distribution —
        peak level, ghost rate — rather than pinning one draw. */
    void setHumanisationSeedOffset (std::uint64_t offset) noexcept { seedOffset = offset; }

    /** The per-channel values this block will schedule and render with.

        Called once at the top of processBlock. The engine HOLDS them rather
        than taking them as an argument in three places — as a schedule
        parameter, a render parameter, and a reference member of the step
        emitter. The property that matters is that the settings the mute gate
        saw at schedule time are the settings the gain uses at render time, and
        that was true only because one local happened to be passed to all
        three. Now it is structural. */
    void beginBlock (const Settings& newSettings) noexcept { blockSettings = newSettings; }

    /** All eight lanes' velocities for ONE step, at one sample offset. */
    using StepVelocities = std::array<std::uint8_t, ids::lanes.size()>;

    /** Starts every non-silent lane of a step.

        STEP-shaped, not hit-shaped, and that matters for 03-02. `app.js`'s
        scheduler draws its `CACHAÇA` timing jitter ONCE per step and adds it to
        that step's time, so every lane of the step moves together —
        `const t = nextNoteTime + swingDelay + jitter`. A per-lane seam invites a
        per-lane draw, which is not a compile error and not a test failure: it is
        the whole step breathing against the lanes flamming apart. One call per
        step puts the draw where the spec draws it.

        It also hands the engine the fact that a lane had NO hit, which is
        exactly where ghost notes fire (`if (v > 0) play(…) else ghost(…)`) —
        and the engine is where the seeded RNG lives.

        `sampleOffset` may point past the end of the block; the offset is
        carried forward, which is what lets a late-jittered hit survive.

The offset may point past the end of the block; it is carried
        forward, which is what lets a late-jittered hit survive. Early ones are
        representable because scheduleStep delays every trigger by the lookahead
        first — see kLookaheadSeconds. */
    void scheduleStep (const StepVelocities& velocities, int sampleOffset) noexcept;

    /** Where a voice's output goes, besides the main buffer.

        One stereo buffer per channel, or nullptr for "do not split". Default
        constructed to all-nullptr, so every caller that just wants the sum —
        which is every caller in STEREO mode, and every existing test — passes
        nothing and gets exactly what it got before.

        The engine does not know what a BUS is: the processor hands it buffers
        and keeps `getBusBuffer` to itself. That is also what keeps this class
        testable without a host. 04-06. */
    struct RenderTargets
    {
        std::array<juce::AudioBuffer<float>*, static_cast<size_t> (kNumChannels)> perChannel {};
    };

    /** Renders into `buffer`, and additionally into any per-channel target.

        `buffer` is the MAIN bus's buffer, not the host's whole multi-bus one —
        this reads `getNumChannels()` to decide whether there is a right channel
        to pan into, and with six buses enabled that number is 12. */
    void render (juce::AudioBuffer<float>& buffer, const RenderTargets& targets) noexcept;

    /** Which channel's parameters a lane reads. */
    static constexpr int channelForLane (int lane) noexcept
    {
        // Bounds checked by hand: juce::isPositiveAndBelow is not constexpr, and
        // calling it here made this function constexpr in name only.
        return (lane >= 0 && lane < static_cast<int> (detail::laneToChannel.size()))
                 ? detail::laneToChannel[static_cast<size_t> (lane)]
                 : 0;
    }

    // ── observability ───────────────────────────────────────────────────────
    //
    //  Atomic, because the header used to promise these to "Phase 5's activity
    //  meters" — the message thread, at frame rate — while they were plain ints
    //  and plain bools written on the audio thread. That is precisely the data
    //  race PluginProcessor spends three static_asserts and a comment ruling
    //  out ("plain scalars across threads are a data race, not merely a stale
    //  read"). Only tests read them today, so it was latent; the first editor
    //  meter would have made it UB.
    //
    //  Relaxed throughout: these are display and diagnostic values, and a
    //  one-frame-stale read of a meter is invisible where a lock would not be.

    /** Voices sounding at the end of the last render. */
    int getActiveVoiceCount() const noexcept { return activeVoices.load (std::memory_order_relaxed); }

    /** The most that have ever sounded at once since the last reset.

        The number that makes the pool arithmetic above checkable. Without it
        the only pool metric was a drop counter that could never fire — see
        getVoicesDropped. */
    int getPeakActiveVoices() const noexcept { return peakActiveVoices.load (std::memory_order_relaxed); }

    /** How often a sounding voice had to be cut short to free a slot. This is
        the real exhaustion signal. */
    int getVoicesStolen() const noexcept { return voicesStolen.load (std::memory_order_relaxed); }

    /** Triggers abandoned outright.

        NOT a capacity signal, and not a substitute for one: claimSynthVoice and
        claimSampleVoice steal rather than fail, so neither returns null while
        the pools are non-empty. It fires only where a trigger is unplayable —
        an empty slot, a non-positive read rate, or a voice that refused to
        configure. Assert getVoicesStolen and getPeakActiveVoices for capacity.

        Twice now this counter has carried a false claim. First a test asserted
        it was zero as proof the pool arithmetic held, which it could never
        disprove. Then this comment said scheduleSample "CAN reach it
        legitimately" while the two paths named `continue`d without
        incrementing. They increment now, so the sentence is true. */
    int getVoicesDropped() const noexcept { return voicesDropped.load (std::memory_order_relaxed); }

    const ZabumbaSampler& getSampler() const noexcept { return sampler; }

private:
    /** One sounding layer of the sampled zabumba. Two of these make one strike
        while the velocity crossfade has it between layers. */
    struct SampleVoice
    {
        bool   active { false };
        int    slot { 0 };
        /** Which channel's VOL and PAN this voice reads.

            Stored rather than assumed. render used to hardcode
            channelForLane (0) for every sample voice, while the actual selector
            is voiceSpecs[lane].usesSample — so marking a second lane sampled
            (which ZabumbaSampler.h already anticipates, for the pá hit) or
            reordering ids::lanes would have had it silently inherit ZABUMBA's
            VOL and PAN, with no compile error and no failing test. */
        int    channel { 0 };
        /** Whether the source holds two distinct channels.

            Decides the PAN law. A mono source through the stereo law comes out
            at 2x amplitude when hard-panned, because that law folds the
            opposite side inwards and a mono file's two reads are identical. */
        bool   sourceIsStereo { true };
        double position { 0.0 };
        double readRate { 1.0 };
        double lengthSamples { 0.0 };
        double envSamples { 0.0 };      ///< where DECAY truncates it
        double releaseSamples { 0.0 };  ///< linear fade so truncation cannot click
        double pos { 0.0 };
        float  gain { 0.0f };
        int    samplesUntilStart { 0 };
        std::uint64_t startOrder { 0 };
    };

    /** Sounds an already-normalised velocity. The one place a velocity becomes
        a voice, whichever source it came from — the grid, or a ghost roll. */
    void playVelocity (int lane, float velocity, int sampleOffset, const ChannelSettings&) noexcept;
    void scheduleSynth (int lane, float velocity, int sampleOffset, const ChannelSettings&) noexcept;
    void scheduleSample (int lane, float velocity, int sampleOffset, const ChannelSettings&) noexcept;

    SynthVoice*  claimSynthVoice (int lane) noexcept;
    SampleVoice* claimSampleVoice() noexcept;

    std::array<SynthVoice, static_cast<size_t> (kSynthVoices)> synthVoices;
    std::array<SampleVoice, static_cast<size_t> (kSampleVoices)> sampleVoices;

    ZabumbaSampler sampler;

    /** This block's resolved per-channel values, set by beginBlock. */
    Settings blockSettings {};

    /** ONE stream, and it is only for noise.

        Every humanisation value — the step's timing jitter, each hit's velocity
        multiplier, every ghost roll, offset and velocity, and the triângulo's
        per-partial detune — is KEYED on (seed, step, lane, purpose, index) by
        `humanisedValue` in Humanisation.h, not drawn from a stream.

        Streams cannot hold the property this needs. It has to be true that
        muting a channel, editing one lane's pattern or moving a GHOST knob does
        not re-time or re-level any OTHER channel, and with a shared stream that
        is a discipline rather than a guarantee. The discipline failed twice in
        one plan: first the velocity and ghost draws were conditional (measured:
        muting the ganzá moved BB's hits by up to 21 ms), and then the fix —
        drawing unconditionally — was itself incomplete, because
        `SynthVoice::trigger` drew five more values for one lane, after the
        audibility gate, only for a voice that was actually claimed. Muting the
        triângulo still re-levelled everything else, and the comment asserting
        the invariant listed that very detune as part of the stream.

        Keyed, a value is a function of its key. There is no order to preserve,
        so there is nothing to be disciplined about.

        `noiseRng` stays a stream deliberately: it is consumed per sample, where
        keying would buy nothing and cost a hash per sample. Nothing else reads
        it, so it cannot couple anything.

        A deviation from 03-02's AC-6, which said all randomness comes from one
        generator. Reproducibility — what that criterion protected — is stronger
        now: the values do not depend on execution order at all.

        Measured, for the record: the keyed hash costs 2.460 ns against
        juce::Random::nextFloat's 2.346, so this is not a performance change in
        either direction — at 20 steps a second the whole per-step humanisation
        is 16 ns of a 10 667 us block budget. */
    juce::Random noiseRng { kNoiseSeed };

    static constexpr int kNoiseSeed = 0x4e4f4953;   // 'NOIS'

    /** Monotonic count of steps scheduled since the last reset, and the second
        half of every humanisation key.

        The step INDEX would not do: it wraps at the pattern length, so step 0 of
        bar 2 would humanise exactly like step 0 of bar 1 and the groove would
        repeat its deviations every bar — audible as a loop, which is the
        opposite of the intent. */
    std::uint64_t stepCounter { 0 };

    /** Offsets the humanisation key, so the same pattern can be rendered under
        a different realisation.

        Exists for the tests: 03-02 measured the profiles' peak levels from a
        single realisation and handed the figure to 03-03's limiter, and a
        review measured 1.408 where the suite measured 1.206. A distribution
        cannot be sampled without a seam, and there was none. */
    std::uint64_t seedOffset { 0 };


    double sampleRate { 44100.0 };
    int    maxBlockSize { 0 };
    /** Seeded from a nominal 48 kHz rather than 0.

        getLatencySamples() is read before the first prepareToPlay by hosts that
        query at scan or instantiation time, and a 0 there leaves the groove
        32 ms late in exactly the hosts that cache it. It still changes when the
        sample RATE changes (1536 at 48 kHz, 1411 at 44.1) — the invariant that
        matters, and the one the comment on setLatencySamples claims, is that it
        does not change with CACHAÇA. */
    int    lookaheadSamples { lookaheadSamplesFor (48000.0) };
    bool   prepared { false };

    std::uint64_t nextStartOrder { 1 };

    std::atomic<int> activeVoices { 0 };
    std::atomic<int> peakActiveVoices { 0 };
    std::atomic<int> voicesStolen { 0 };
    std::atomic<int> voicesDropped { 0 };

    static_assert (std::atomic<int>::is_always_lock_free,
                   "the observability counters are written on the audio thread");
};

} // namespace forrobox
