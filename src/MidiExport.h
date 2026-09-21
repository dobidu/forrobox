/* ============================================================================
   FORRÓ BOX — Standard MIDI File export

   `PLANNING.md:804-825` specifies the format completely and names `exportMIDI()`
   in `audio.js` as the reference implementation, so this is the same shape as
   the groove tables: a design source exists, and the C++ is cross-checked
   against it on every build rather than trusted. `scripts/verify-midi.py` runs
   the prototype's own function under Node and compares byte for byte.

   A MIDI file is a delta-encoded stream: one wrong VLQ shifts every event after
   it and the result still parses. That is why the check compares BYTES and
   reports the first diverging offset, rather than asserting an event count.

   The export is the STORED GRID, deliberately — no swing, no CACHAÇA jitter, no
   ghosts. `exportMIDI` places notes at `step * stepTicks` with no swing term,
   and `PLANNING.md:584` keeps ghosts out of the pattern entirely, so the writer
   excludes them by never seeing them. Live MIDI out (07-03) emits the humanised
   performance instead; the two are different data paths on purpose.
============================================================================ */
#pragma once

#include "ForroBoxState.h"

#include <array>
#include <cstdint>
#include <vector>

namespace forrobox
{

/** Which channels are muted, indexed the way `ids::channelInfos` is.

    Channels, not lanes: muting BATERIA removes BB, CX, HH and TOM together
    because all four map to the composite channel, which is what AC-2 asks for
    and what `VoiceEngine::channelForLane` already says. A per-LANE gate would
    have to re-state that grouping and could disagree with it.

    MUTE ONLY — deliberately NOT `VoiceEngine::ChannelSettings::audible`, which
    is the resolved mute/SOLO gate. `exportMIDI` reads `chans[id].mute` and never
    looks at solo, so folding solo in here would make the C++ diverge from the
    very thing the cross-check compares it against. A caller that already has
    `resolveChannelSettings()` in hand will reach for `! audible`; that is the
    wrong answer, and this is where it is written down. */
using ChannelGate = std::array<bool, static_cast<size_t> (State::kNumChannels)>;

/** The bytes of a type 0, PPQ 96, single-track Standard MIDI File.

    Takes the pieces rather than the processor — 03-02's law for the clock. A
    pure function of its arguments can be swept exhaustively offline with no
    processor, no host and no audio device, which is exactly what the
    cross-check's matrix does. */
std::vector<std::uint8_t> renderStandardMidiFile (const State& state, int bpm, int steps,
                                                  const ChannelGate& muted);

} // namespace forrobox
