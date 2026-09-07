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

inline constexpr std::array<const char*, 10> globalParams {
    bpm, sync, swing, cachaca, steps, timbre, charMix, limiterOn, master, outputMode
};

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
    { "triangulo", "TRI\xc3\x82NGULO",     68.0f, 40.0f,  22,  8.0f },
    { "pandeiro",  "PANDEIRO",             72.0f, 46.0f, -18, 14.0f },
    { "ganza",     "GANZ\xc3\x81",         64.0f, 30.0f,  12,  6.0f },
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
};

inline constexpr std::array<ProfileInfo, 4> profileInfos {{
    { "campina",    "CAMPINA GRANDE",          "CAMPINA",    "CAM" },
    { "caruaru",    "CARUARU",                 "CARUARU",    "CAR" },
    { "petrolina",  "PETROLINA",               "PETROLINA",  "PET" },
    { "sp",         "UNIVERSIT\xc3\x81RIO",    "UNIV",       "UNI" },
}};

/** Loaded on a fresh instance, matching the prototype. */
inline constexpr const char* defaultProfile = "campina";

// ── non-parameter state (a ValueTree child of the APVTS state) ──────────────
inline constexpr const char* stateNode = "FORROBOX_STATE";
inline constexpr const char* gridNode  = "GRID";

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
