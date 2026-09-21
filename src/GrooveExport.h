/* ============================================================================
   FORRÓ BOX — the current groove, as a file

   The adapter between the processor and `MidiExport`'s pure writer: it gathers
   the pattern, the BPM, the step window and the mute gate, and answers both
   halves of a question the drag and the save dialog must not answer differently
   — the BYTES and the FILENAME.

   Its own file rather than `MidiExport.h`, deliberately. That header is a pure
   function of its arguments (03-02's law for the clock), which is what lets
   `scripts/verify-midi.py` sweep it across 24 states with no processor, no host
   and no audio device. Including `PluginProcessor.h` there would end that.
============================================================================ */
#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

class ForroBoxAudioProcessor;

namespace forrobox
{

struct GrooveExport
{
    std::vector<std::uint8_t> bytes;

    /** `forrobox_<profile>_<bpm>bpm.mid` — `app.js:454`. */
    juce::String filename;
};

/** What we would export right now.

    Message thread only: it takes `lockPatternState()` and reads parameters
    through the APVTS. */
GrooveExport renderCurrentGroove (ForroBoxAudioProcessor&);

} // namespace forrobox
