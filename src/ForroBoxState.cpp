#include "ForroBoxState.h"

#include "ParameterIDs.h"

namespace forrobox
{

namespace
{
    juce::String encodeLane (const State::Lane& lane)
    {
        return juce::Base64::toBase64 (lane.data(), lane.size());
    }

    /** Decodes into `lane`, leaving anything it cannot fill at zero. Values above
        the MIDI maximum are clamped rather than wrapped. */
    void decodeLane (const juce::var& property, State::Lane& lane)
    {
        lane.fill (0);

        if (! property.isString())
            return;

        // 32 raw bytes encode to 44 base64 characters. Anything far longer is
        // not our data, and decoding it first would let a project file dictate
        // a multi-megabyte allocation per lane before we discarded all but 32.
        const auto encoded = property.toString();
        if (encoded.length() > 64)
            return;

        juce::MemoryOutputStream decoded;
        if (! juce::Base64::convertFromBase64 (decoded, encoded))
            return;   // malformed base64 — leave the lane silent

        const auto* bytes = static_cast<const std::uint8_t*> (decoded.getData());
        const auto  count = juce::jmin (decoded.getDataSize(), lane.size());

        for (size_t i = 0; i < count; ++i)
            lane[i] = juce::jmin (bytes[i], State::kMaxVelocity);
    }
}

void State::tileToFullWidth() noexcept
{
    // Half of storage, and the relation to the step WINDOWS asserted rather than
    // assumed. What stood here was `kMaxSteps % 2 == 0` — a literal against a
    // literal, vacuously true — under a message claiming a relation between the
    // windows that nothing checked, and a comment saying `half` was "derived
    // from the window table" when it is derived from `kMaxSteps`. Adding an
    // 8-step window would have left it green while the tiling started widening
    // a 16-step selection against a 16-slot source. Found by /code-review.
    static_assert (ids::stepWindows.size() == 2,
                   "this widens ONE narrow window into ONE wide one");
    static_assert (ids::stepWindows.back() == kMaxSteps,
                   "the wide window must be the whole of storage");
    static_assert (ids::stepWindows.front() * 2 == kMaxSteps,
                   "and the narrow one exactly half of it, which is what is copied");

    constexpr auto half = static_cast<size_t> (kMaxSteps) / 2;

    const auto widen = [] (Lane& lane)
    {
        for (size_t i = half; i < lane.size(); ++i)
            lane[i] = lane[i % half];
    };

    for (auto& lane : lanes)
        widen (lane);

    // THE PARKED SLOTS TOO, since 09-05. Tiling only the active lanes left a
    // parked pattern's second bar holding whatever was there before it was
    // parked — so editing at 16 steps, parking, widening to 32 and switching
    // back restored a lane whose upper half was the PRE-EDIT content. The law
    // this function exists for (`PLANNING.md:606`) applies to every pattern the
    // state holds, not only the ones currently playing. /code-review.
    for (auto& slot : parkedLanes)
        for (auto& lane : slot)
            widen (lane);
}

namespace
{
/** `"<lane>_<slot>"` — the parked storage's property name.

    One function so the writer and the reader cannot spell it differently, which
    is the failure `ids::patternSlot` exists to prevent one field over. */
juce::Identifier slotLaneProperty (size_t slot, size_t lane)
{
    return juce::Identifier (juce::String (ids::lanes[lane]) + "_"
                               + juce::String (static_cast<int> (slot) + State::kMinPatternSlot));
}
}

void State::writeTo (juce::ValueTree& parent) const
{
    namespace ids = forrobox::ids;

    parent.removeChild (parent.getChildWithName (ids::stateNode), nullptr);

    juce::ValueTree node { ids::stateNode };
    node.setProperty (ids::activeProfile, activeProfile, nullptr);
    node.setProperty (ids::activeGroove, activeGroove, nullptr);
    node.setProperty (ids::irPath, impulseResponsePath, nullptr);
    node.setProperty (ids::dirty,         dirty,         nullptr);
    // Clamped on the way out as well as the way in. Writing verbatim and only
    // clamping on load means an out-of-range value survives in memory and in the
    // saved file, then silently changes on the next load.
    // Already clamped by the setters; written through the getters, which cannot
    // return an out-of-range value. Defence in depth, not the only enforcement.
    node.setProperty (ids::presetIdx, getPresetIdx(), nullptr);

    for (size_t i = 0; i < ids::channelInfos.size(); ++i)
        node.setProperty (ids::patternSlot (ids::channelInfos[i].id),
                          getPatternSlot (i), nullptr);

    juce::ValueTree grid { ids::gridNode };
    for (size_t i = 0; i < ids::lanes.size(); ++i)
        grid.setProperty (ids::lanes[i], encodeLane (lanes[i]), nullptr);

    node.addChild (grid, -1, nullptr);

    // The parked slots, one property per (slot, lane). Written for every slot
    // including each channel's active one, whose entry is stale by design —
    // `grid` above is authoritative for what is playing, and the swap always
    // writes a slot's parking before reading the next one.
    juce::ValueTree parked { ids::parkedNode };

    for (size_t slot = 0; slot < parkedLanes.size(); ++slot)
        for (size_t i = 0; i < ids::lanes.size(); ++i)
            parked.setProperty (slotLaneProperty (slot, i),
                                encodeLane (parkedLanes[slot][i]), nullptr);

    node.addChild (parked, -1, nullptr);
    parent.addChild (node, -1, nullptr);
}

State State::readFrom (const juce::ValueTree& parent)
{
    namespace ids = forrobox::ids;

    State result;   // every field already at its default

    const auto node = parent.getChildWithName (ids::stateNode);
    if (! node.isValid())
        return result;   // no state saved (or a foreign tree) — defaults stand

    // An unknown profile string is preserved verbatim rather than discarded: a
    // project saved by a newer build must not lose its profile just because this
    // build does not recognise the name yet.
    // An unknown profile name is kept verbatim, so a project saved by a newer
    // build does not lose its profile. An empty string is not a name any build
    // could resolve, so that falls back to the default.
    if (const auto profile = node.getProperty (ids::activeProfile).toString(); profile.isNotEmpty())
        result.activeProfile = profile;

    // VERBATIM, like the profile above and for the same reason: a project saved
    // by a newer build may name a groove this one has never heard of, and
    // `grooveInProfile` degrades to the default rather than losing the string.
    // Empty is left empty — `applyProfile` fills it on the next load.
    result.activeGroove = node.getProperty (ids::activeGroove).toString();

    // Verbatim, and NOT checked for existence here: whether the file is
    // still there is the processor's question at load time, not the
    // deserialiser's. See the member's comment.
    result.impulseResponsePath = node.getProperty (ids::irPath).toString();

    result.dirty = static_cast<bool> (node.getProperty (ids::dirty, false));

    // The setters clamp, so no bounds appear here.
    result.setPresetIdx (static_cast<int> (node.getProperty (ids::presetIdx, kMinPresetIdx)));

    for (size_t i = 0; i < ids::channelInfos.size(); ++i)
        result.setPatternSlot (i, static_cast<int> (
            node.getProperty (ids::patternSlot (ids::channelInfos[i].id), kMinPatternSlot)));

    // THE PARKED SLOTS FIRST, so a node carrying them without a GRID does not
    // lose all eight. The early return below is about the ACTIVE lanes having
    // nothing to read; it should not take the stored ones with it.
    // /code-review.
    // MISSING IS NOT BROKEN. A project saved before 09-05 has a grid and no
    // parked node: its lanes are the active slot, the other seven start empty,
    // and nothing throws. Same rule as every other field here — see the
    // docstring — and the same care 07-02 took in the other direction.
    const auto parked = node.getChildWithName (ids::parkedNode);

    if (parked.isValid())
        for (size_t slot = 0; slot < result.parkedLanes.size(); ++slot)
            for (size_t i = 0; i < ids::lanes.size(); ++i)
                decodeLane (parked.getProperty (slotLaneProperty (slot, i)),
                            result.parkedLanes[slot][i]);

    const auto grid = node.getChildWithName (ids::gridNode);
    if (! grid.isValid())
        return result;   // node without a grid — lanes stay silent

    for (size_t i = 0; i < ids::lanes.size(); ++i)
        decodeLane (grid.getProperty (ids::lanes[i]), result.lanes[i]);

    return result;
}

} // namespace forrobox
