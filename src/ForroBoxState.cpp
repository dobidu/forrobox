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

void State::writeTo (juce::ValueTree& parent) const
{
    namespace ids = forrobox::ids;

    parent.removeChild (parent.getChildWithName (ids::stateNode), nullptr);

    juce::ValueTree node { ids::stateNode };
    node.setProperty (ids::activeProfile, activeProfile, nullptr);
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

    result.dirty = static_cast<bool> (node.getProperty (ids::dirty, false));

    // The setters clamp, so no bounds appear here.
    result.setPresetIdx (static_cast<int> (node.getProperty (ids::presetIdx, kMinPresetIdx)));

    for (size_t i = 0; i < ids::channelInfos.size(); ++i)
        result.setPatternSlot (i, static_cast<int> (
            node.getProperty (ids::patternSlot (ids::channelInfos[i].id), kMinPatternSlot)));

    const auto grid = node.getChildWithName (ids::gridNode);
    if (! grid.isValid())
        return result;   // node without a grid — lanes stay silent

    for (size_t i = 0; i < ids::lanes.size(); ++i)
        decodeLane (grid.getProperty (ids::lanes[i]), result.lanes[i]);

    return result;
}

} // namespace forrobox
