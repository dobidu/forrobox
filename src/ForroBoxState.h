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

    juce::String activeProfile { "campina" };
    bool dirty { false };

    // ── bounded scalars ─────────────────────────────────────────────────────
    //  Private with clamping setters so an out-of-range value cannot exist in
    //  memory at all. Clamping only at the serialisation boundary left a window
    //  where a bad value sat in memory until the next save happened to catch it.
    int  getPresetIdx() const noexcept { return presetIdx; }
    void setPresetIdx (int v) noexcept { presetIdx = juce::jlimit (kMinPresetIdx, kMaxPresetIdx, v); }

    /** Stub pattern-variation slot for one channel, 1-8. */
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
