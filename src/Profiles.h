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
#include <span>
#include <cstdint>

namespace forrobox
{

/** The most grooves a profile's bank can hold.

    EIGHT because that is what the design source gives the cycler that reaches
    them: `PLANNING.md:843` lists eight preset labels and `:844` gives each
    channel eight pattern slots. A bank larger than any control can select is
    data nothing can reach, which is the kind of stub this phase exists to
    remove rather than add. */
inline constexpr int kMaxGroovesPerProfile = 8;

/** One playable groove: eight lane patterns, and the name a cycler shows.

    Added at 09-02. Before it, a profile WAS its patterns — one groove each, and
    the `PAT 01` and preset cyclers both drew a label over data that did not
    exist. The bank is the storage those two controls need; 09-03 fills it and
    09-05 reaches it. */
struct Groove
{
    const char* id;            ///< stable, lowercase-kebab; a saved state may hold it
    const char* name;          ///< UTF-8, accented — what the cycler screen shows

    /** The FEEL, which 09-03 moved here from `Profile`.

        A groove carried only its patterns for one plan, so every entry in a
        bank played at its profile's tempo — which made a bank offering
        `XOTE LENTO` at CAMPINA's 132 bpm a name that lies, and the eight rhythm
        labels `PLANNING.md:843` lists unusable. The user chose full feel per
        groove.

        Bounded by the PARAMETER ranges, not by taste: 09-06 writes these into
        `ids::bpm`, `ids::swing` and `ids::cachaca`, so a value outside
        `ids::kMinBpm`..`kMaxBpm` or `0`..`ids::kPercentMax` would be silently
        clamped on load. `verify-profiles.py` refuses one. */
    int   bpm;                 ///< ids::kMinBpm .. ids::kMaxBpm
    float swing;               ///< 0 .. ids::kPercentMax
    float cachaca;             ///< 0 .. ids::kPercentMax

    std::array<const char*, static_cast<size_t> (State::kNumLanes)> patterns;
};

/** One regional profile: its identity, its feel settings, and a BANK of grooves.

    It used to be "one 16-step pattern string per lane in `ids::lanes` order",
    which is now the description of a `Groove` rather than of a profile — 09-02
    moved the patterns into the bank and left `patterns()` as the accessor for
    the one a profile loads. The old wording survived the move as a comment
    orphaned onto `kMaxGroovesPerProfile`, describing neither. /code-review. */
struct Profile
{
    /** Identity lives in ids::profileInfos, referenced not copied. Re-declaring
        id/displayName/shortName/code here would defeat the very "cannot drift
        apart" rationale that array exists for. */
    const ids::ProfileInfo* info;

    /** REGIONAL CHARACTER, which stays here while the feel moved to the groove.

        PETROLINA is LO-FI because the São Francisco forró eletrônico is, and no
        groove within a region changes that. `check_descriptions` also ties each
        profile's prose to both of these — "Timbre LO-FI", "bateria em silêncio"
        — so moving them would make every claim ambiguous about which groove it
        describes. */
    int   timbreIndex;    ///< 0 HI-FI, 1 LO-FI, 2 CICLOTRON — the parameter's choice index
    bool  bateriaMuted;   ///< data only; muting behaviour is Phase 3

    /** The bank. Trailing entries are value-initialised and MUST NOT be read —
        `grooveCount` is how many are real. Iterate with `grooves()`, never over
        the raw array, so a bank of one and a bank of eight take one path. */
    std::array<Groove, static_cast<size_t> (kMaxGroovesPerProfile)> grooveBank;
    int grooveCount;

    constexpr std::span<const Groove> grooves() const noexcept
    {
        return { grooveBank.data(), static_cast<size_t> (grooveCount) };
    }

    /** The groove selecting this profile loads. `applyProfile` uses it, and it
        is `grooves[0]` by definition rather than by a flag — the bank is
        ordered and the first entry is the profile's own. */
    constexpr const Groove& defaultGroove() const noexcept { return grooveBank[0]; }

    /** The default groove's eight patterns.

        An ACCESSOR, not a member. Storing the same eight strings both here and
        in the bank would be the duplication 09-01 and 09-02 exist to remove,
        and a `patterns` member that disagreed with `grooveBank[0]` is exactly
        the drift this project keeps finding. */
    constexpr const std::array<const char*, static_cast<size_t> (State::kNumLanes)>&
    patterns() const noexcept { return defaultGroove().patterns; }

    /** The default groove's feel, which IS the profile's.

        Accessors for the same reason `patterns()` is one: a `bpm` member beside
        `grooveBank[0].bpm` is two numbers that can disagree. Selecting a profile
        loads its default groove, so the profile's tempo is that groove's by
        definition rather than by a rule something has to enforce. */
    constexpr int   bpm()     const noexcept { return defaultGroove().bpm; }
    constexpr float swing()   const noexcept { return defaultGroove().swing; }
    constexpr float cachaca() const noexcept { return defaultGroove().cachaca; }

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

/** Fills all 8 lanes from `groove` and sets activeProfile to `profile`'s id.

    Every lane is written unconditionally, including all-rest ones: otherwise a
    profile switch would leave the previous groove bleeding through. Does NOT
    touch any parameter — moving bpm/swing/cachaça/timbre is Phase 6's full
    reload, and reaching into the APVTS from here would be the wrong layer.
    (That contract described `applyProfile`, whose body this now is. 09-04 left
    it orphaned above the new declaration, describing neither — the same slip
    the `Profile` comment 90 lines up records. /code-review.)

    WRITTEN AT 09-04 BECAUSE IT FINALLY HAS A CALLER. 09-02 and 09-03 both
    wanted it and both declined: a function with no caller is not a guarantee,
    and this project has refused that shape twice before. The audition renderer
    is the first caller; 09-06's cycler is the second.

    **09-06 MUST FIX THIS BEFORE IT CALLS IT WITH A NON-DEFAULT GROOVE.** The
    state records `profile.id()` and clears `dirty`, and `State` has no groove
    field at all — so applying `campina/xote-lento` leaves a state that says
    "CAMPINA GRANDE, pristine" while playing something else. `SidePanel` would
    light CAMPINA as unedited, and save-then-reload would silently restore
    `grooveBank[0]`. `Groove::id` says "a saved state may hold it" and nothing
    stores it yet.

    Latent, not live: the only caller today is the audition renderer, which uses
    a throwaway rig and persists nothing. Not fixed here because the fix is
    either a `dirty` rule or a new persisted field, and both are decisions that
    belong to the plan with the UI that makes them observable. /code-review. */
/** A profile's groove by id, or its default when the id is unrecognised.

    Degrades rather than failing: the id may have come from a project saved by a
    build whose bank has a groove this one lacks. */
const Groove& grooveInProfile (const Profile& profile, juce::StringRef id);

void applyGroove (State& state, const Profile& profile, const Groove& groove);

/** The profile's DEFAULT groove, which is what selecting a profile loads.

    A delegation to `applyGroove`; behaviour is unchanged from when this
    carried the body. */
void applyProfile (State& state, const Profile& profile);

} // namespace forrobox
