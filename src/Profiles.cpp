#include "Profiles.h"

namespace forrobox
{

namespace
{
    // GENERATED FROM assets/profiles.json — and since 09-01 that is a FACT
    // rather than an instruction. This banner said "GENERATED FROM data.js" from
    // Phase 2, while the table was in truth transcribed by hand and compared
    // back by a script that regex-parsed JavaScript. `scripts/build-profiles.py`
    // now writes these digits; `--verify` runs on every build and fails naming
    // the file, the line and the lane.
    //
    // The same JSON generates `data.js`'s PROFILES block and PROFILE_ORDER,
    // and the copies of both inlined in the standalone page — so one edit
    // reaches the plugin and both prototypes, and all three are checked.
    //
    // NOT everything about a profile, and the gap is named because 09-02 walks
    // into it: the identity strings and the three description lines still live
    // hand-written in `ids::profileInfos`, compared back by a regex parse of
    // this project's own C++. That is the arrangement this plan existed to end,
    // left standing for the one field the next plan edits. /simplify.
    //
    // Patterns are in ids::lanes order: zabumba, triangulo, pandeiro, ganza,
    // bb, cx, hh, tom.
    constexpr std::array<Profile, 4> kProfiles {{
    {
        &ids::profileInfos[0],   /* campina */
        132, 38.0f, 22.0f, 0, true,
        {{
          "9..5 ..6. 8..4 ..6."   /* zabumba */,
          "7474 7474 7474 7474"   /* triangulo */,
          "..6. 9..4 ..6. 9..5"   /* pandeiro */,
          "6363 6363 6363 6363"   /* ganza */,
          "9... .... 9... ...."   /* bb */,
          ".... 9... .... 9..."   /* cx */,
          ".5.5 .5.5 .5.5 .5.5"   /* hh */,
          ".... .... .... ..4."   /* tom */
        }}
    },
    {
        &ids::profileInfos[1],   /* caruaru */
        138, 54.0f, 32.0f, 0, false,
        {{
          "9..6 .57. 9..6 .47."   /* zabumba */,
          "7575 7575 7575 7575"   /* triangulo */,
          "..7. 9..5 ..7. 9..6"   /* pandeiro */,
          "7474 7474 7474 7474"   /* ganza */,
          "9... ..6. 9... ..6."   /* bb */,
          ".... 9..3 .... 9..4"   /* cx */,
          "6.6. 6.6. 6.6. 6.6."   /* hh */,
          ".... ...4 .... ..5."   /* tom */
        }}
    },
    {
        &ids::profileInfos[2],   /* petrolina */
        128, 26.0f, 16.0f, 1, false,
        {{
          "9... 9..4 9... 9..6"   /* zabumba */,
          "5.5. 5.5. 5.5. 5.5."   /* triangulo */,
          ".... 7..3 .... 7..4"   /* pandeiro */,
          "8484 8484 8484 8484"   /* ganza */,
          "9... ..5. 9..4 ...."   /* bb */,
          ".... 9... .... 9..."   /* cx */,
          "6868 6868 6868 6868"   /* hh */,
          ".... .... ...5 ..6."   /* tom */
        }}
    },
    {
        &ids::profileInfos[3],   /* sp */
        124, 16.0f, 6.0f, 0, false,
        {{
          "9... 6... 9... 6..."   /* zabumba */,
          "8888 8888 8888 8888"   /* triangulo */,
          "..7. ..7. ..7. ..7."   /* pandeiro */,
          "7575 7575 7575 7575"   /* ganza */,
          "9... .... 9... ...."   /* bb */,
          ".... 9... .... 9..."   /* cx */,
          ".7.7 .7.7 .7.7 .7.7"   /* hh */,
          ".... .... .... ...."   /* tom */
        }}
    },
    }};

    static_assert (kProfiles.size() == ids::profileInfos.size(),
                   "every profile needs a ProfileInfo entry");

    // Size alone does not prove the rows line up. Each Profile references its
    // identity rather than copying it, so a row pointing at the wrong entry
    // would compile and silently mislabel a whole groove — the C++ half of the
    // divergence that verify-profiles.py catches in the data. Prove it here so
    // it cannot even build, without waiting for the script to run.
    constexpr bool profilesAlignWithInfos()
    {
        for (size_t i = 0; i < kProfiles.size(); ++i)
            if (kProfiles[i].info != &ids::profileInfos[i])
                return false;

        return true;
    }

    static_assert (profilesAlignWithInfos(),
                   "kProfiles[i] must reference ids::profileInfos[i] — a row pointing at "
                   "another profile's identity would mislabel that groove");
}

bool decodePattern (juce::StringRef pattern, DecodedPattern& out)
{
    DecodedPattern decoded {};
    int count = 0;

    // StringRef::text is a CharPointerType value, not a pointer to iterate.
    auto c = pattern.text;
    while (! c.isEmpty())
    {
        const auto ch = c.getAndAdvance();

        if (juce::CharacterFunctions::isWhitespace (ch))
            continue;

        if (count >= kPatternLength)
            return false;                       // too many significant characters

        if (ch == '.')
        {
            decoded[static_cast<size_t> (count++)] = 0;
        }
        else if (ch >= '1' && ch <= '9')
        {
            const auto level = static_cast<int> (ch - '0');
            decoded[static_cast<size_t> (count++)] =
                static_cast<std::uint8_t> (juce::jmin (127, level * 14));
        }
        else
        {
            return false;                       // illegal character
        }
    }

    if (count != kPatternLength)
        return false;                           // too few

    out = decoded;
    return true;
}

// The profile pattern length and the sequencer's narrow window are the same 16,
// and nothing said so. `tileToFullWidth`'s asserts tie kMaxSteps to
// ids::stepWindows; this ties kPatternLength to the same table. Without it,
// changing kMaxSteps to 64 leaves expandPattern tiling a 16 into 64 while
// tileToFullWidth tiles a 32 into 64 — both compiling, both green, and the
// grooves different. Found by /simplify.
static_assert (kPatternLength == ids::stepWindows.front(),
               "a profile's pattern is exactly the narrow step window");

State::Lane expandPattern (const DecodedPattern& base)
{
    State::Lane lane {};

    // Every slot, always. No step count is consulted: see the header for why a
    // window-dependent expansion silently changes what a later STEPS switch plays.
    for (size_t i = 0; i < lane.size(); ++i)
        lane[i] = base[i % static_cast<size_t> (kPatternLength)];

    return lane;
}

juce::Span<const Profile> allProfiles()
{
    return { kProfiles.data(), kProfiles.size() };
}

const Profile* findProfile (juce::StringRef id)
{
    // The explicit StringRef on the right disambiguates the overload set
    // (StringRef == const char* is ambiguous) without allocating.
    for (const auto& p : kProfiles)
        if (id == juce::StringRef (p.id()))
            return &p;

    return nullptr;
}

void applyProfile (State& state, const Profile& profile)
{
    for (size_t lane = 0; lane < profile.patterns.size(); ++lane)
    {
        DecodedPattern decoded {};

        // A malformed string cannot reach here — the cross-check and the tests
        // both reject one — but silence the lane rather than leaving stale data
        // if it somehow did.
        if (! decodePattern (profile.patterns[lane], decoded))
            decoded.fill (0);

        state.lanes[lane] = expandPattern (decoded);
    }

    state.activeProfile = profile.id();

    // A freshly loaded profile is pristine. app.js loadProfile clears this too;
    // leaving it set shows CUSTOM over a state that is exactly a profile.
    state.dirty = false;
}

} // namespace forrobox
