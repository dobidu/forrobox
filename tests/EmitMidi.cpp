/* ============================================================================
   FORRÓ BOX — `--emit-midi`, the C++ side of the MIDI cross-check

   Reads states as NDJSON on stdin — one JSON document per line — and writes one
   line of hex per state on stdout. `scripts/verify-midi.py` drives both this and
   `scripts/verify-midi.js` through the same protocol and compares the lines.

   BATCHED, one process for the whole matrix rather than one per state, and the
   numbers are why: measured, a spawn of this executable costs 4.2 ms and a spawn
   of Node 20.3 ms, so 24 states through 48 processes took the gate to 1351 ms of
   every relink — three times the four existing cross-checks combined, and three
   times the link it waits on. A gate that expensive is one somebody eventually
   switches off.

   The JSON schema is the PROTOTYPE's own state shape, not a convenient one, so
   `verify-midi.py` can hand the identical document to both sides and
   `verify-midi.js` can pass it straight to `exportMIDI` unchanged. Anything the
   two sides had to translate differently would be a difference the comparison
   could not see.

   Its own translation unit rather than a block inside TestMain.cpp: this is 80
   lines of prototype-state-shape knowledge, and `main()`'s job is dispatch.
============================================================================ */
#include <JuceHeader.h>

#include "ForroBoxState.h"
#include "MidiExport.h"
#include "ParameterIDs.h"
#include "VoiceEngine.h"

#include "TestSuites.h"

#include <iostream>
#include <string>

namespace
{

/** One state, in the prototype's shape, to the bytes the writer produces.

    Returns false with a reason on stderr rather than guessing: a document this
    cannot read is the DRIVER's mistake, and silently exporting something else
    would make the two sides disagree over that instead of over the writer. */
bool renderOneLine (const juce::var& parsed, juce::String& hexOut)
{
    if (! parsed.isObject())
    {
        std::cerr << "--emit-midi: a line is not a JSON object\n";
        return false;
    }

    const auto bpm   = static_cast<int> (parsed["bpm"]);
    const auto steps = static_cast<int> (parsed["steps"]);

    // Rejected rather than clamped. The writer guards its own division, but this
    // mode exists to be compared against the prototype, and the prototype's
    // answer to a missing BPM is an infinity truncated to a tempo meta of
    // 00 00 00.
    if (bpm < forrobox::ids::kMinBpm || bpm > forrobox::ids::kMaxBpm)
    {
        std::cerr << "--emit-midi: bpm " << bpm << " is outside ["
                  << forrobox::ids::kMinBpm << ", " << forrobox::ids::kMaxBpm << "]\n";
        return false;
    }

    const auto grid     = parsed["grid"];
    const auto channels = parsed["channels"];

    forrobox::State state;
    forrobox::ChannelGate muted {};

    for (size_t c = 0; c < forrobox::ids::channelInfos.size(); ++c)
        muted[c] = static_cast<bool> (channels[juce::Identifier (forrobox::ids::channelInfos[c].id)]["mute"]);

    // Which lanes nest under a channel object and which sit at the top level is
    // DERIVED, not written down: the composite channel is the one no lane
    // carries the id of, and `exportMIDI` nests exactly its lanes.
    const auto composite = forrobox::detail::compositeChannel();

    for (size_t lane = 0; lane < forrobox::ids::lanes.size(); ++lane)
    {
        const juce::Identifier laneId { forrobox::ids::lanes[lane] };
        const auto channel = forrobox::VoiceEngine::channelForLane (static_cast<int> (lane));

        const auto array = channel == composite
                             ? grid[juce::Identifier (forrobox::ids::channelInfos[static_cast<size_t> (channel)].id)][laneId]
                             : grid[laneId];

        if (auto* values = array.getArray())
            for (int step = 0; step < values->size() && step < forrobox::State::kMaxSteps; ++step)
                state.lanes[lane][static_cast<size_t> (step)]
                    = static_cast<std::uint8_t> (juce::jlimit (0, 255, static_cast<int> ((*values)[step])));
    }

    const auto bytes = forrobox::renderStandardMidiFile (state, bpm, steps, muted);

    hexOut = juce::String::toHexString (bytes.data(), static_cast<int> (bytes.size()), 0);
    return true;
}

} // namespace

int emitMidiHexFromStdin()
{
    std::string line;

    while (std::getline (std::cin, line))
    {
        if (line.empty())
            continue;

        juce::String hex;

        if (! renderOneLine (juce::JSON::parse (juce::String::fromUTF8 (line.c_str(),
                                                                       static_cast<int> (line.size()))),
                             hex))
            return 2;

        std::cout << hex << "\n";
    }

    std::cout.flush();
    return 0;
}
