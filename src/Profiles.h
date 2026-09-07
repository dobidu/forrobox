/* ============================================================================
   FORRÓ BOX — regional groove profiles

   The musical content, ported verbatim from data.js. PLANNING.md calls this the
   heart of the product, and it is the one place where a wrong digit produces no
   crash, no failed build and no failing test — only a groove that is subtly
   wrong, with no way to tell which digit. So the pattern strings here were
   generated mechanically from data.js, and scripts/verify-profiles.py proves
   they still match it.

   Notation: '.' is a rest, '1'-'9' is level x 14 (so 9 = 126), spaces cosmetic.
============================================================================ */
#pragma once

#include <juce_core/juce_core.h>

#include "ForroBoxState.h"
#include "ParameterIDs.h"

#include <array>
#include <cstdint>

namespace forrobox
{

/** One regional groove: its identity, its feel settings, and one 16-step
    pattern string per lane in `ids::lanes` order. */
struct Profile
{
    /** Identity lives in ids::profileInfos, referenced not copied. Re-declaring
        id/displayName/shortName/code here would defeat the very "cannot drift
        apart" rationale that array exists for. */
    const ids::ProfileInfo* info;

    int   bpm;            ///< 40..300
    float swing;          ///< 0..100
    float cachaca;        ///< 0..100
    int   timbreIndex;    ///< 0 HI-FI, 1 LO-FI, 2 CICLOTRON — the parameter's choice index
    bool  bateriaMuted;   ///< data only; muting behaviour is Phase 3

    std::array<const char*, static_cast<size_t> (State::kNumLanes)> patterns;

    const char* id()          const noexcept { return info->id; }
    const char* displayName() const noexcept { return info->displayName; }
    const char* shortName()   const noexcept { return info->shortName; }
    const char* code()        const noexcept { return info->code; }
};

/** Number of significant characters every pattern string must contain. */
inline constexpr int kPatternLength = 16;

using DecodedPattern = std::array<std::uint8_t, static_cast<size_t> (kPatternLength)>;

/** Decodes the velocity notation.

    Returns false and leaves `out` untouched for anything malformed: a character
    that is not '.', '1'-'9' or whitespace, or a significant-character count
    other than kPatternLength. Deliberately strict rather than padding — a short
    pattern would tile wrongly and yield a groove that is merely subtly wrong,
    which is the failure mode hardest to notice and hardest to trace. */
bool decodePattern (juce::StringRef pattern, DecodedPattern& out);

/** Fills every one of the lane's 32 slots with `base[i % kPatternLength]`.

    Deliberately takes no step count. Zeroing beyond a 16-step window would make
    a profile load depend on whatever STEPS happened to be set, so switching to
    32 later would give a silent second bar where the prototype (`app.js`
    setSteps) repeats bar 1. The window selects which slots are *read*; it never
    decides their contents. */
State::Lane expandPattern (const DecodedPattern& base);

/** All four profiles, in the prototype's PROFILE_ORDER. */
juce::Span<const Profile> allProfiles();

/** Nullptr for an unknown id, so a project saved by a newer build degrades
    rather than silently resolving to the wrong groove. */
const Profile* findProfile (juce::StringRef id);

/** Fills all 8 lanes from `profile` and sets activeProfile to its id.

    Every lane is written unconditionally, including all-rest ones: otherwise a
    profile switch would leave the previous groove bleeding through. Does NOT
    touch any parameter — moving bpm/swing/cachaça/timbre is Phase 6's full
    reload, and reaching into the APVTS from here would be the wrong layer. */
void applyProfile (State& state, const Profile& profile);

} // namespace forrobox
