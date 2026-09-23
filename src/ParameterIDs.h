/* ============================================================================
   FORRÓ BOX — parameter and state identifiers

   Every ID the plugin persists is declared here, once. Parameter IDs and group
   IDs become part of the state path a host writes into a saved project, so a
   literal duplicated between the layout, the state code and the tests is how a
   later rename silently invalidates every session a user has saved.

   All IDs are ASCII, lowercase, underscore-separated. Display names may be
   accented; identifiers never are.
============================================================================ */
#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace forrobox::ids
{
// ── global parameters ───────────────────────────────────────────────────────
inline constexpr const char* bpm         = "bpm";
inline constexpr const char* sync        = "sync";
inline constexpr const char* swing       = "swing";
inline constexpr const char* cachaca     = "cachaca";
inline constexpr const char* steps       = "steps";
inline constexpr const char* timbre      = "timbre";
inline constexpr const char* charMix     = "char_mix";
inline constexpr const char* limiterOn   = "limiter_on";
inline constexpr const char* master      = "master";
inline constexpr const char* outputMode  = "output_mode";
inline constexpr const char* midiGate    = "midi_gate";

inline constexpr std::array<const char*, 11> globalParams {
    bpm, sync, swing, cachaca, steps, timbre, charMix, limiterOn, master, outputMode, midiGate
};

/** `output_mode`'s choices, in index order.

    ONE table, beside the id it belongs to. The processor declares the parameter
    from it and the footer's OUTPUT toggle labels its segments from it, so the
    text a user reads and the index a saved project holds cannot drift apart —
    `profileInfos`' rule, and the reason that one exists. Its index IS the
    segment index, which is what lets the toggle read the parameter directly.

    04-06 gives MULTI-OUT its five extra stereo buses; until then the parameter
    is real, automatable and persisted, and the control that shows it is
    read-only. */
inline constexpr std::array<const char*, 2> outputModes { "STEREO", "MULTI-OUT" };

/** `midi_gate`'s choices, in index order — how long a LIVE MIDI note lasts.

    The file export gets its gate for free: `exportMIDI` writes
    `floor(stepTicks * 0.8)`, 19 of 24 ticks, because every exported note sits
    on the grid. A live note does not — it is jittered off the grid by CACHAÇA,
    and a ghost is jittered again from there — so there is no step boundary to
    measure against and two answers are defensible. Both ship, decided with the
    user at 07-03 planning.

    FIXED is the default and is what hardware drum machines send: a short,
    tempo-independent note-off that nothing can strand. STEP reuses the export's
    80%, so a live note and an exported note agree at a steady tempo, at the
    cost of a length that moves with BPM and SYNC.

    NO UI, deliberately. `PLANNING.md` specifies no MIDI-gate control and the
    design mandate forbids inventing one, so hosts expose it in their generic
    parameter panel and Phase 8's settings menu can attach to it later. A
    control was not forgotten. */
inline constexpr std::array<const char*, 2> midiGateModes { "FIXED", "STEP" };

/** The same two, named — so no site indexes the table with a bare literal. */
enum class MidiGate { fixed = 0, step = 1 };

/** FIXED's length. Short enough to be a formality for the one-shot samplers
    that receive percussion, long enough that a host recording the output shows
    a note a human can see. */
inline constexpr double kFixedMidiGateSeconds = 0.040;

/** STEP's fraction — the same 0.8 `exportMIDI` uses, named once so the two
    cannot drift. */
inline constexpr double kStepMidiGateFraction = 0.8;

// ── per-channel parameter suffixes ──────────────────────────────────────────
inline constexpr const char* vol   = "vol";
inline constexpr const char* pitch = "pitch";
inline constexpr const char* decay = "decay";
inline constexpr const char* pan   = "pan";
inline constexpr const char* ghost = "ghost";
inline constexpr const char* mute  = "mute";
inline constexpr const char* solo  = "solo";

inline constexpr std::array<const char*, 7> channelParams {
    vol, pitch, decay, pan, ghost, mute, solo
};

// ── channels / groups ───────────────────────────────────────────────────────
inline constexpr const char* groupGlobal = "global";

/** Everything that varies per channel, in one place.

    Previously the id, the display name and the four defaults lived in three
    separately-sized arrays across two files, held in step only by static_asserts.
    One array of structs makes divergence impossible instead of detectable. */
struct ChannelInfo
{
    const char* id;            ///< ASCII parameter/group identifier
    const char* displayName;   ///< UTF-8, accented where the design calls for it
    float vol;
    float decay;
    int   pan;
    float ghost;
};

inline constexpr std::array<ChannelInfo, 5> channelInfos {{
    { "zabumba",   "ZABUMBA",              82.0f, 58.0f,   0, 12.0f },
    { "triangulo", "TRIÂNGULO",            68.0f, 40.0f,  22,  8.0f },
    { "pandeiro",  "PANDEIRO",             72.0f, 46.0f, -18, 14.0f },
    { "ganza",     "GANZÁ",                64.0f, 30.0f,  12,  6.0f },
    { "bateria",   "BATERIA",              74.0f, 50.0f,   0, 10.0f },
}};

/** Composes a per-channel parameter ID, e.g. ("zabumba", "vol") -> "zabumba_vol".

    Builds a juce::String, so it belongs to construction and (de)serialisation
    only. Never call it from processBlock or any per-block path. */
inline juce::String channelParam (juce::StringRef channel, juce::StringRef param)
{
    return juce::String (channel) + "_" + juce::String (param);
}


// ── regional profiles ───────────────────────────────────────────────────────
/** The four regional grooves. Same one-array-of-structs shape as channelInfos,
    for the same reason: id, display name, short name and code cannot drift apart.

    Ids are verbatim from data.js — note `sp`, whose display name is
    UNIVERSITÁRIO. State::activeProfile already defaults to "campina" and a saved
    project may carry any of these strings, so none of them may be renamed. */
struct ProfileInfo
{
    const char* id;
    const char* displayName;   ///< UTF-8, accented
    const char* shortName;
    const char* code;          ///< 3-letter header switch label

    /** The three lines the side panel shows under the ACTIVE profile — css:400
        hides them on the others.

        Here rather than in a fifth array, for the reason `channelInfos` states
        in its own comment: "one array of structs makes divergence impossible
        instead of detectable". They are `data.js`'s own words and
        `verify-profiles.py` compares them against it line by line, which is what
        stops a retyped accent from drifting. Three lines because the design's
        box is sized for three — `app.js:281` joins them with a space and lets
        the box wrap, which is the same result at this width. */
    std::array<const char*, 3> description;
};

inline constexpr std::array<ProfileInfo, 4> profileInfos {{
    { "campina",    "CAMPINA GRANDE",          "CAMPINA",    "CAM",
      { "Pé-de-serra puro — sanfona, zabumba e triângulo.",
        "Swing médio, balanço solto.",
        "Timbre HI-FI, bateria em silêncio." } },
    { "caruaru",    "CARUARU",                 "CARUARU",    "CAR",
      { "Forró tradicional pernambucano.",
        "Peso extra na zabumba, swing alto.",
        "Timbre HI-FI, balanço pesado." } },
    { "petrolina",  "PETROLINA",               "PETROLINA",  "PET",
      { "Forró eletrônico do São Francisco.",
        "Bateria presente, groove seco.",
        "Timbre LO-FI, cachaça baixa." } },
    { "sp",         "UNIVERSITÁRIO",           "UNIV",       "UNI",
      { "Forró universitário, limpo e pop.",
        "Quantizado, cachaça quase zero.",
        "Timbre HI-FI, pulso reto." } },
}};

/** What a fresh instance loads, and therefore what it plays.

    `State` default-initialises `activeProfile` to this, and since 08-01
    `ForroBoxAudioProcessor`'s constructor also LOADS it — the grid and BATERIA's
    mute — so the name and the groove agree.

    They did not for five phases. This comment carried three paragraphs
    explaining that nothing applied the pattern at construction, that a fresh
    plugin lit CAMPINA over an empty grid and was silent, and that "Phase 6 owns
    the fix". Phase 6 closed without it; 08-01 fixed it and this is the self that
    deletes the note, which 03-02's reconciliation made a rule after a docstring
    described its own plan's shipped fix as "NOT yet solved". */
inline constexpr const char* defaultProfile = "campina";

// ── non-parameter state (a ValueTree child of the APVTS state) ──────────────
inline constexpr const char* stateNode = "FORROBOX_STATE";
inline constexpr const char* gridNode  = "GRID";

/** The seven patterns a channel is not playing. A SEPARATE child from
    `gridNode`, which keeps meaning "the lanes that are active" — so a project
    saved before 09-05 has a grid, no parked node, and loads with its pattern
    intact in the active slot. Backward compatibility by structure rather than
    by a version number. */
inline constexpr const char* parkedNode = "parked";

/** The tempo range the BPM parameter offers. Lived on Clock until the clock
    stopped knowing about tempo at all — the span it is given carries it now. */
inline constexpr int kMinBpm = 40;
inline constexpr int kMaxBpm = 300;

/** PAN's extent, per PLANNING.md's state table: -50..+50, bipolar, displayed
    `L##` / `C` / `R##`. `kPercentMax` is the maximum of every percentage
    parameter (VOL, DECAY, GHOST, SWING, CACHACA, MASTER).

    Both named because the 50 was previously a literal inside
    createParameterLayout and nowhere else, so the voice engine normalised PAN
    by 100 instead: every pan came out at half strength and hard left was only
    -0.5. A range only one place knows is a range the next reader guesses. */
inline constexpr int   kPanExtent  = 50;
inline constexpr float kPercentMax = 100.0f;

/** A percentage parameter as 0..1, bounded and NaN-safe.

    `jlimit` alone is not enough: it passes NaN through unchanged, which is
    documented in Clock.cpp for `swing` and was the reason a `>= 0.0f` test that
    looked dead was actually load-bearing. Written out inline at six sites
    before this existed — the drift ParameterIDs.h's own note above warns
    about. */
inline float normalisedPercent (float percent) noexcept
{
    return std::isfinite (percent) ? juce::jlimit (0.0f, kPercentMax, percent) / kPercentMax
                                   : 0.0f;
}

/** PAN as -1..+1, bounded and NaN-safe, by the same argument. */
inline float normalisedPan (float panParameter) noexcept
{
    const auto extent = static_cast<float> (kPanExtent);

    return std::isfinite (panParameter)
             ? juce::jlimit (-extent, extent, panParameter) / extent
             : 0.0f;
}

/** PROTOTYPE FIDELITY versus PLUGIN CORRECTNESS — the tie-breaker.

    PROJECT.md's rule is that "correct plugin practice wins wherever the two
    conflict", and its own parenthetical scopes that to threading,
    sample-accurate timing, automation and state persistence. Two Phase 3
    decisions were tonal, outside that scope, and resolved in OPPOSITE
    directions:

      - the character bus keeps Web Audio's default lowpass Q of 1.0, an
        unchosen default, because reproducing it is free and audible
      - it does NOT reproduce WaveShaperNode's clamp outside [-1, 1], an
        unchosen artefact, because the grooves reach 1.454 and the clamp would
        put a hard ceiling at an arbitrary input level

    The discriminator both in fact follow, recorded once here rather than
    re-argued per case: **reproduce an unchosen prototype default unless
    reproducing it would make the plugin audibly worse at levels the plugin
    actually reaches.** It recurs in Phase 8's Ciclotron work and in every
    future voice tweak. */

/** The step windows the `steps` CHOICE parameter offers, in index order. The
    parameter's display strings are built from these, and the clock's window is
    looked up by the same index, so the two cannot disagree. */
inline constexpr std::array<int, 2> stepWindows { 16, 32 };

/** Compile-time string equality, for the id tables in this file.

    Here rather than in `VoiceEngine.h`, where it was written: it is a generic
    constexpr strcmp with nothing to do with voices, and 07-03's GM table needed
    it — which made a header of eight MIDI note numbers include the whole synth
    engine, and gave `VoiceEngine.cpp` a header that includes it back. Its three
    original callers are unaffected; they are in the same namespace tree. */
namespace detail
{
    constexpr bool sameId (const char* a, const char* b) noexcept
    {
        while (*a != '\0' && *a == *b) { ++a; ++b; }
        return *a == *b;
    }
}

/** The 8 sequencer lanes. Bateria expands into its four kit pieces. */
inline constexpr std::array<const char*, 8> lanes {
    "zabumba", "triangulo", "pandeiro", "ganza", "bb", "cx", "hh", "tom"
};

inline constexpr const char* activeProfile = "activeProfile";
inline constexpr const char* dirty         = "dirty";
inline constexpr const char* presetIdx     = "presetIdx";
inline constexpr const char* patternPrefix = "pattern";

/** Per-channel stub pattern slot, e.g. "pattern_zabumba".
    Allocates a juce::String — (de)serialisation only, never the audio thread. */
inline juce::String patternSlot (juce::StringRef channel)
{
    return juce::String (patternPrefix) + "_" + juce::String (channel);
}

} // namespace forrobox::ids
