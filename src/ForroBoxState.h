/* ============================================================================
   FORRÓ BOX — non-parameter state

   The pattern grid, active profile, dirty flag, preset index and per-channel
   pattern slot. Deliberately NOT parameters: exposing 8 lanes x 32 steps as
   automation would put 256 lanes in front of the user for data that is edited
   by clicking pads, not by drawing envelopes.

   Lives in a ValueTree child of the APVTS state, so it saves with the host
   project and with presets through the same path.
============================================================================ */
#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "ParameterIDs.h"

#include <array>
#include <cstdint>

namespace forrobox
{

struct State
{
    static constexpr int kMaxSteps = 32;

    // Derived, not re-declared: the ID lists are the single source of truth, so
    // adding a lane or a channel there cannot leave these behind.
    static constexpr int kNumLanes    = static_cast<int> (ids::lanes.size());
    static constexpr int kNumChannels = static_cast<int> (ids::channelInfos.size());

    static constexpr std::uint8_t kMaxVelocity   = 127;

    /** Repeat the first half of every lane into the second — `PLANNING.md:606`,
        "switching TILES the existing pattern rather than clearing it:
        `newArray[i] = oldArray[i % oldLength]`, so 16→32 duplicates the bar".

        Here rather than in the caller so the law has one home and a test can
        drive it without a processor. It takes no step count: the only widening
        this plugin has is 16 -> 32, and a general `tile(from, to)` would be a
        parameterised version of a law with one instance.

        There is deliberately no inverse. The prototype TRUNCATES on 32 -> 16
        because its array length IS the step count; ours is always kMaxSteps
        slots with `steps` as a view (02-01, recorded in `expandPattern`'s doc),
        so narrowing merely stops reading the upper half — and because widening
        tiles over it, nothing that survives is ever observable. Truncating would
        destroy work for no reachable benefit. */
    void tileToFullWidth() noexcept;
    static constexpr int kMinPresetIdx   = 0;
    static constexpr int kMaxPresetIdx   = 7;
    static constexpr int kMinPatternSlot = 1;
    static constexpr int kMaxPatternSlot = 8;

    using Lane = std::array<std::uint8_t, static_cast<size_t> (kMaxSteps)>;

    /** Lanes are always stored at the full 32 slots regardless of the `steps`
        parameter, which selects the active window. A variable-length lane would
        make a 32 -> 16 -> 32 sequence lossy in the persistence layer, which is a
        different thing from the truncation the UI performs when the user
        deliberately changes the step count. */
    std::array<Lane, static_cast<size_t> (kNumLanes)> lanes {};

    /** The seven patterns a channel is NOT currently playing, per slot.

        `lanes` above holds what every channel plays RIGHT NOW, and that is
        deliberate: `PatternSnapshot.h:55` is `using PatternLanes =
        decltype (State::lanes)`, so the lock-free handover copies that array as
        one block. Keeping the active patterns there means 09-05 added eight
        storable patterns per channel WITHOUT the audio thread learning that
        slots exist — same type, same 256 bytes, same publish, and
        `processBlock` untouched since 02-04.

        Switching a channel's slot is a SWAP, and it lives in the processor
        (`ForroBoxAudioProcessor::selectPatternSlot`) because it must go through
        the LockedState handle that publishes on release, and because the
        lane-to-channel rule belongs to `VoiceEngine`.

        THE ENTRY FOR A CHANNEL'S ACTIVE SLOT IS STALE, BY DESIGN. It holds that
        slot's patterns as they were when the slot was last left; `lanes` is
        authoritative while the slot is active. Nothing may read the parked copy
        of an active slot — the swap always writes it before reading the next
        one, which is the invariant the whole model rests on and which
        `testPatternSlotSwap` asserts rather than assumes. */
    std::array<std::array<Lane, static_cast<size_t> (kNumLanes)>,
               static_cast<size_t> (kMaxPatternSlot)> parkedLanes {};

    juce::String activeProfile { ids::defaultProfile };

    /** Which groove of `activeProfile`'s bank is playing, by id.

        AN ID, NOT AN INDEX, for the reason `activeProfile` is one: a bank index
        would point at a different groove the moment a bank is reordered or one
        is inserted, and revising a groove is one JSON edit. `readFrom`
        preserves an unrecognised value verbatim, exactly as it does for the
        profile — 07-02's rule, that "a project saved by a newer build must not
        lose its profile", applied one level down.

        STORING THIS IS WHY `dirty` STAYS FALSE when the cycler moves. Two
        consecutive reviews flagged that applying a non-default groove left a
        state claiming "CAMPINA GRANDE, pristine" while playing something else.
        09-05 answered the same question for SLOTS by marking dirty, because a
        slot's CONTENTS are user-edited and have no factory identity to record.
        A groove has one, so the honest fix is to record it: the state says
        CAMPINA GRANDE / XOTE LENTO, which is what it is. Selecting a factory
        groove is not an edit. */
    juce::String activeGroove;

    /** The impulse response's path, as chosen. Empty when none is loaded.

        A PATH, and it is kept even when the file is gone. A project saved on
        another machine, or one whose IR moved, must load and play DRY while
        still naming what it wants — the same rule `activeProfile` (07-02) and
        `activeGroove` (09-06) follow, for the same reason: this data comes back
        from arbitrary host project files and losing it helps nobody. */
    juce::String impulseResponsePath;

    /** One user sample path per channel, empty for the built-in voice.

        Kept verbatim like the IR's and for the same reason: a project opened on
        another machine must still name what it wants. Validated as absolute
        before it becomes a `juce::File`, because a Windows path on Linux trips
        `parseAbsolutePath`'s assertion — 09-07's finding. */
    std::array<juce::String, static_cast<size_t> (kNumChannels)> samplePaths {};

    bool dirty { false };

    // ── bounded scalars ─────────────────────────────────────────────────────
    //  Private with clamping setters so an out-of-range value cannot exist in
    //  memory at all. Clamping only at the serialisation boundary left a window
    //  where a bad value sat in memory until the next save happened to catch it.
    /** NOT the groove selector — see `activeGroove`.

        This is persisted, clamped and round-trip tested, and has no production
        reader; it would fit a bank index exactly, which is why the next reader
        would assume it is one. It is the prototype's flat `state.presetIdx`
        (`app.js:562`), kept for parity. 09-06 considered repurposing it and
        refused: an index into a mutable bank is the fragility an id avoids. */
    int  getPresetIdx() const noexcept { return presetIdx; }
    void setPresetIdx (int v) noexcept { presetIdx = juce::jlimit (kMinPresetIdx, kMaxPresetIdx, v); }

    /** The pattern-variation slot a channel is playing, 1-8. Stub until 09-05. */
    int  getPatternSlot (size_t channel) const noexcept
    {
        return channel < patternSlots.size() ? patternSlots[channel] : kMinPatternSlot;
    }
    void setPatternSlot (size_t channel, int v) noexcept
    {
        if (channel < patternSlots.size())
            patternSlots[channel] = juce::jlimit (kMinPatternSlot, kMaxPatternSlot, v);
    }

    /** Replaces any existing state child under `parent`. */
    void writeTo (juce::ValueTree& parent) const;

private:
    int presetIdx { kMinPresetIdx };
    std::array<int, static_cast<size_t> (kNumChannels)> patternSlots { 1, 1, 1, 1, 1 };

public:
    /** Never throws and never asserts: this data comes back from arbitrary host
        project files, so anything missing, truncated or out of range degrades to
        a valid default rather than taking the host down. */
    static State readFrom (const juce::ValueTree& parent);
};

} // namespace forrobox
