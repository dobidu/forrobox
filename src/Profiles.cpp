#include "Profiles.h"

namespace forrobox
{

namespace
{
    // GENERATED FROM data.js — do not hand-edit a digit here.
    // scripts/verify-profiles.py compares the patterns, the numeric scalars, the
    // timbre index, the muted flag and every identity string against data.js,
    // and fails naming the profile and field if any diverge. Run it via the
    // `verify-profiles` CMake target; a plain build depends on it.
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
    // Compared through CharPointer to stay allocation-free and to avoid the
    // ambiguous StringRef/const char* operator==.
    for (const auto& p : kProfiles)
        if (id.text.compare (juce::CharPointer_UTF8 (p.id())) == 0)
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
