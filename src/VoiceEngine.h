/* ============================================================================
   FORRÓ BOX — the voice engine

   A PERSISTENT member of the processor, prepared with the sample rate and block
   size, owning the voice pools, the sampler and the one seeded RNG.

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

#include "Voices.h"
#include "ZabumbaSampler.h"
#include "ParameterIDs.h"

#include <array>
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
}

class VoiceEngine
{
public:
    static constexpr int kNumChannels = static_cast<int> (ids::channelInfos.size());

    /** Pool sizes, from the worst case the grooves can actually reach.

        Step interval at the fastest supported tempo (kMaxBpm = 300) is
        60/300/4 = 50 ms. The longest synthesised voice is the triângulo's open
        articulation at maximum DECAY: 0.45 s x 1.8 = 0.81 s, so 0.81/0.05 = 17
        can overlap on one lane. Seven synthesised lanes x 17 = 119, rounded up.

        The longest sampled voice is the 0.498 s layer pitched down an octave
        (playback rate 0.5), so 0.996 s = 20 overlapping hits, doubled to 40 by
        the velocity crossfade playing two layers per strike.

        03-02's ghost notes do NOT double these: a ghost only fires on a step
        where the lane has no programmed hit, so hits per lane per step stays at
        most one. */
    static constexpr int kSynthVoices  = 128;
    static constexpr int kSampleVoices = 48;

    /** The longest a single voice can ring, from the same arithmetic: the
        0.498 s zabumba layer pitched down an octave. Reported to the host as
        the plugin's tail so it renders the decay rather than truncating it when
        bouncing. */
    static constexpr double kMaxTailSeconds = 1.0;

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
        bool  audible { true };  ///< the resolved mute/solo gate
    };

    using Settings = std::array<ChannelSettings, static_cast<size_t> (kNumChannels)>;

    /** Allocates the pools, the filter state and the samples. Called from
        prepareToPlay, with the audio device stopped. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Silences everything and reseeds the RNG, so a render from a known state
        is reproducible. Audio thread safe. */
    void reset() noexcept;

    /** Starts a note. `sampleOffset` is where in the coming block it begins;
        offsets beyond the block are carried forward, which is the hook 03-02's
        jitter needs. Called from the clock's step callback. */
    void schedule (int lane, std::uint8_t velocity, int sampleOffset, const Settings& settings) noexcept;

    /** Renders every sounding voice into `buffer`, ADDING to it. Called once
        per block after the clock has advanced. */
    void render (juce::AudioBuffer<float>& buffer, const Settings& settings) noexcept;

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
    int getActiveVoiceCount() const noexcept;
    int getVoicesStolen() const noexcept { return voicesStolen; }
    int getVoicesDropped() const noexcept { return voicesDropped; }
    bool isPrepared() const noexcept { return prepared; }

    const ZabumbaSampler& getSampler() const noexcept { return sampler; }

private:
    /** One sounding layer of the sampled zabumba. Two of these make one strike
        while the velocity crossfade has it between layers. */
    struct SampleVoice
    {
        bool   active { false };
        int    slot { 0 };
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

    void scheduleSynth (int lane, float velocity, int sampleOffset, const ChannelSettings&) noexcept;
    void scheduleSample (float velocity, int sampleOffset, const ChannelSettings&) noexcept;

    SynthVoice*  claimSynthVoice (int lane) noexcept;
    SampleVoice* claimSampleVoice() noexcept;

    std::array<SynthVoice, static_cast<size_t> (kSynthVoices)> synthVoices;
    std::array<SampleVoice, static_cast<size_t> (kSampleVoices)> sampleVoices;

    ZabumbaSampler sampler;

    /** One generator for every random decision in the engine — the triângulo's
        detune, the noise sources, and 03-02's jitter and ghost notes. Seeded
        once so a render is reproducible; never a static or a thread_local. */
    juce::Random rng { kRngSeed };

    static constexpr int kRngSeed = 0x464f5252;   // 'FORR'

    double sampleRate { 44100.0 };
    int    maxBlockSize { 0 };
    bool   prepared { false };

    std::uint64_t nextStartOrder { 1 };
    int voicesStolen { 0 };
    int voicesDropped { 0 };
};

} // namespace forrobox
