#include "MidiExport.h"

#include "GmPercussion.h"
#include "VoiceEngine.h"

#include <algorithm>
#include <cmath>

namespace forrobox
{
namespace
{

// Here rather than in the header: nothing outside this file reads them, and
// tests/MidiExportTest.cpp carries a comment explaining why it deliberately
// must NOT — a check that read the writer's own constants could not fail. By
// 02-04's rule a public constant with no caller is a guarantee with no caller.
constexpr int kPPQ       = 96;
constexpr int kStepTicks = kPPQ / 4;   ///< a sixteenth

/** Gate length as a FRACTION, not the 19 ticks it currently works out to.
    `exportMIDI` writes `Math.floor(stepTicks * 0.8)`; a literal 19 would stop
    tracking PPQ the moment either constant moved.

    `ids::kStepMidiGateFraction`, not a second 0.8. 07-03 added that constant
    with a comment saying it was "named once so the two cannot drift" while this
    one stayed — two names for one number under a claim that there was one,
    which is the exact failure `ids::channelInfos` states its rule about. */
constexpr double kGateFraction = ids::kStepMidiGateFraction;

/** MIDI clocks per metronome click, for the time-signature meta.

    A literal 24, NOT `kPPQ / 4`. The MIDI clock is 24 per quarter note by
    definition, whatever a file's PPQ happens to be; the two are equal here only
    because kPPQ is 96. Writing it as a division claims a dependency that does
    not exist, so raising kPPQ would silently rewrite this byte while looking
    like it was keeping it in step — and the cross-check could not catch it,
    because the prototype writes `PPQ / 4` too. */
constexpr int kClocksPerMetronomeClick = 24;

struct Event
{
    int tick {};
    std::array<std::uint8_t, 3> data {};
};

void appendVariableLength (std::vector<std::uint8_t>& out, int value)
{
    // The prototype's `vlq`: seven bits at a time, most significant group
    // first, every group but the last carrying the continuation bit.
    auto n = static_cast<std::uint32_t> (value);

    std::array<std::uint8_t, 5> groups {};
    int count = 0;

    groups[static_cast<size_t> (count++)] = static_cast<std::uint8_t> (n & 0x7f);
    n >>= 7;

    while (n != 0)
    {
        groups[static_cast<size_t> (count++)] = static_cast<std::uint8_t> ((n & 0x7f) | 0x80);
        n >>= 7;
    }

    for (int i = count; i-- > 0;)
        out.push_back (groups[static_cast<size_t> (i)]);
}

void appendAscii (std::vector<std::uint8_t>& out, const char* text)
{
    for (auto* c = text; *c != '\0'; ++c)
        out.push_back (static_cast<std::uint8_t> (*c));
}

void appendBigEndian32 (std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back (static_cast<std::uint8_t> ((value >> 24) & 0xff));
    out.push_back (static_cast<std::uint8_t> ((value >> 16) & 0xff));
    out.push_back (static_cast<std::uint8_t> ((value >>  8) & 0xff));
    out.push_back (static_cast<std::uint8_t> ( value        & 0xff));
}

void appendBigEndian24 (std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back (static_cast<std::uint8_t> ((value >> 16) & 0xff));
    out.push_back (static_cast<std::uint8_t> ((value >>  8) & 0xff));
    out.push_back (static_cast<std::uint8_t> ( value        & 0xff));
}

void appendBigEndian16 (std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xff));
    out.push_back (static_cast<std::uint8_t> ( value       & 0xff));
}

} // namespace

std::vector<std::uint8_t> renderStandardMidiFile (const State& state, int bpm, int steps,
                                                  const ChannelGate& muted)
{
    const auto window = std::clamp (steps, 0, State::kMaxSteps);
    const auto gateTicks = static_cast<int> (std::floor (kStepTicks * kGateFraction));

    std::vector<Event> events;
    events.reserve (static_cast<size_t> (2 * ids::lanes.size() * static_cast<size_t> (window)));

    // Lane order is `ids::lanes` order, which is the order `exportMIDI`
    // iterates: the four regional lanes, then the kit's four. Equal ticks keep
    // that order through the stable sort below, and the byte comparison would
    // flake if they did not.
    for (size_t lane = 0; lane < ids::lanes.size(); ++lane)
    {
        const auto channel = VoiceEngine::channelForLane (static_cast<int> (lane));

        if (muted[static_cast<size_t> (channel)])
            continue;

        const auto note = static_cast<std::uint8_t> (gm::laneNotes[lane].note);

        for (int step = 0; step < window; ++step)
        {
            const auto velocity = state.lanes[lane][static_cast<size_t> (step)];

            if (velocity == 0)
                continue;

            const auto on = step * kStepTicks;

            // Clamped to [1, 127] like the prototype's
            // `max(1, min(127, round(vel)))`. Load-bearing, not cosmetic: a
            // note-on with velocity 0 IS a note-off in MIDI, so a zero here
            // would silently drop the note in every DAW that reads the file.
            const auto clamped = static_cast<std::uint8_t> (std::clamp<int> (velocity, 1, 127));

            events.push_back ({ on,             { 0x99, note, clamped } });
            events.push_back ({ on + gateTicks, { 0x89, note, 0 } });
        }
    }

    // STABLE, and it matters — measured, not assumed. Two lanes can land on one
    // tick (zabumba and BB share note 36, and any two lanes can share a step),
    // and `Array.prototype.sort` has been stable since ES2019, so the reference
    // keeps equal-tick events in insertion order. Swapping this for std::sort
    // makes EVERY state in scripts/verify-midi.py diverge on libstdc++.
    std::stable_sort (events.begin(), events.end(),
                      [] (const Event& a, const Event& b) { return a.tick < b.tick; });

    std::vector<std::uint8_t> track;

    // Guarded, because this takes a plain int. The BPM parameter cannot leave
    // [kMinBpm, kMaxBpm], but a hand-built caller passing 0 would divide by zero
    // and hand std::llround an infinity, which is undefined behaviour — and the
    // prototype's own answer there (Infinity truncated through `>> 16 & 255`, so
    // a tempo meta of 00 00 00) is not a behaviour worth reproducing. Same
    // argument as the velocity clamp above: the invariant holds elsewhere, and
    // the boundary still guards it.
    const auto safeBpm = juce::jlimit (ids::kMinBpm, ids::kMaxBpm, bpm);

    const auto microsecondsPerBeat = static_cast<std::uint32_t> (
        std::llround (60'000'000.0 / static_cast<double> (safeBpm)));

    appendVariableLength (track, 0);
    track.insert (track.end(), { 0xff, 0x51, 0x03 });
    appendBigEndian24 (track, microsecondsPerBeat);

    appendVariableLength (track, 0);
    track.insert (track.end(), { 0xff, 0x58, 0x04, 4, 2,
                                 static_cast<std::uint8_t> (kClocksPerMetronomeClick),
                                 8 });   // 4/4

    int last = 0;

    for (const auto& event : events)
    {
        appendVariableLength (track, event.tick - last);
        track.insert (track.end(), event.data.begin(), event.data.end());
        last = event.tick;
    }

    appendVariableLength (track, 0);
    track.insert (track.end(), { 0xff, 0x2f, 0x00 });   // end of track

    std::vector<std::uint8_t> bytes;

    appendAscii (bytes, "MThd");
    appendBigEndian32 (bytes, 6);
    appendBigEndian16 (bytes, 0);    // format 0
    appendBigEndian16 (bytes, 1);    // one track
    appendBigEndian16 (bytes, kPPQ);

    appendAscii (bytes, "MTrk");
    appendBigEndian32 (bytes, static_cast<std::uint32_t> (track.size()));
    bytes.insert (bytes.end(), track.begin(), track.end());

    return bytes;
}

} // namespace forrobox
