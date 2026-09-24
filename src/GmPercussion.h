/* ============================================================================
   FORRÓ BOX — the General MIDI percussion map

   `PLANNING.md:815-822`, one row per lane, in `ids::lanes` order.

   ITS OWN HEADER because two callers need it and a second table is what
   `ids::channelInfos` forbids in its own comment: "one array of structs makes
   divergence impossible instead of detectable". It lived in `MidiExport.cpp`'s
   anonymous namespace while the file writer was the only caller; 07-03's live
   MIDI out is the second, and copying eight numbers into it would have been the
   parallel table that comment is about.

   The `static_assert` travels WITH the table. Enrolling a header only lets a
   reader find a name — what stops the rows drifting out of `ids::lanes` order
   is the assertion, and separating the two would leave a table that looks
   checked and is not.
============================================================================ */
#pragma once

#include "ParameterIDs.h"

#include <array>

namespace forrobox::gm
{

struct LaneNote
{
    const char* lane;   ///< must equal ids::lanes[i]
    int         note;   ///< GM percussion note, PLANNING.md:815-822
};

/** Zabumba and BB share note 36 deliberately — `PLANNING.md:815` and `:819` both
    say 36. That is the spec, not a collision to normalise away, and it is the
    reason `renderStandardMidiFile`'s sort has to be stable. */
inline constexpr std::array<LaneNote, ids::lanes.size()> laneNotes {{
    { "zabumba",   36 },   // Bass Drum 1
    { "triangulo", 81 },   // Open Triangle
    { "pandeiro",  54 },   // Tambourine
    { "ganza",     82 },   // Shaker
    { "bb",        36 },   // Bateria BB — 36 again, on purpose
    { "cx",        38 },   // Acoustic Snare
    { "hh",        42 },   // Closed Hi-Hat
    { "tom",       45 },   // Low Tom
}};

constexpr bool laneNotesFollowLaneOrder() noexcept
{
    for (size_t i = 0; i < ids::lanes.size(); ++i)
        if (! ids::detail::sameId (laneNotes[i].lane, ids::lanes[i]))
            return false;

    return true;
}

static_assert (laneNotesFollowLaneOrder(),
               "the GM map is indexed by lane — its rows must be ids::lanes, in order");

/** The GM note for a lane, or -1 for a lane outside the table.

    The bounds check is BELT AND BRACES, not a live guard — both callers have
    already validated the index against an array of the same size
    (`scheduleStep` before it reaches `playVelocity`, and the writer's own loop
    over `ids::lanes`). It is free at compile time and it keeps a named accessor
    honest for a test that calls it directly.

    The first version of this comment said the lane "has already travelled
    through a voice pool" — it has not: the live tap is the first statement of
    `playVelocity`, before any voice is claimed. */
constexpr int noteForLane (int lane) noexcept
{
    return (lane >= 0 && lane < static_cast<int> (laneNotes.size()))
             ? laneNotes[static_cast<size_t> (lane)].note
             : -1;
}

/** The lane a GM note plays, or -1 for a note this map does not carry.

    THE INVERSE OF `noteForLane`, derived by scanning `laneNotes` rather than
    written out again — the rule `read_timbre_index` and `writeLaneForRow`
    follow, and the reason is the same: a second table is a second thing to keep
    in step, and `laneNotes` already carries a `static_assert` binding it to
    `ids::lanes`.

    WHY THE INVERSE AT ALL. 09-09 made the plugin answer incoming notes, and
    accepting exactly what it EMITS is what makes the two symmetric: record this
    plugin's own MIDI output, play it back in, and the same lanes fire. Any other
    map would leave the instrument unable to play its own output.

    NOTE 36 HAS TWO OWNERS — zabumba and BB, deliberately, because
    `PLANNING.md:815` and `:819` both say 36. A forward map may be
    many-to-one; an inverse cannot be one-to-many, so this returns the FIRST,
    which is zabumba. That is a choice and not a derivation: a host playing 36
    gets the zabumba, and the BB is reachable from the grid and from its own
    kit row. /09-09. */
constexpr int laneForNote (int note) noexcept
{
    for (size_t i = 0; i < laneNotes.size(); ++i)
        if (laneNotes[i].note == note)
            return static_cast<int> (i);

    return -1;
}

/** A normalised velocity as a MIDI one, clamped to [1, 127].

    Here because BOTH MIDI paths need the rule and its reason: a note-on with
    velocity 0 IS a note-off in MIDI, so a quiet ghost must not round to
    silence-that-is-really-a-release. `renderStandardMidiFile` clamps a uint8
    grid velocity and the live path clamps a normalised float; this holds the
    ceiling and the floor that both share. */
constexpr int toMidiVelocity (int scaled) noexcept
{
    return scaled < 1 ? 1 : (scaled > 127 ? 127 : scaled);
}

/** Channel 10, the GM percussion channel — `PLANNING.md:808`. JUCE numbers MIDI
    channels from 1, so this is 10 to JUCE and the low nibble 9 on the wire. */
inline constexpr int kPercussionChannel = 10;

} // namespace forrobox::gm
