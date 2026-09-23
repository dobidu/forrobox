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
    // `ids::profileInfos` IS GENERATED TOO, since 09-02 — the gap this banner
    // named when 09-01 closed is closed. Nothing about a profile is transcribed
    // by hand any more: the identity strings, the three description lines, the
    // scalars and every groove all come from that one JSON.
    //
    // Each profile carries a BANK of grooves rather than one set of patterns.
    // `grooveCount` is what bounds it — the array's tail is value-initialised —
    // so read it through `grooves()`, never by indexing `grooveBank`.
    //
    // Patterns are in ids::lanes order: zabumba, triangulo, pandeiro, ganza,
    // bb, cx, hh, tom.
    constexpr std::array<Profile, 4> kProfiles {{
    {
        &ids::profileInfos[0],   /* campina */
        0, true,
        {{   /* 4 of 8 grooves */
          {
            "pe-de-serra-01", "PÉ-DE-SERRA 01",
            132, 38.0f, 22.0f,
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
            "baiao-seco", "BAIÃO SECO",
            116, 30.0f, 18.0f,
            {{
              "9... ..6. 9... ..5."   /* zabumba */,
              "74.4 74.4 74.4 74.4"   /* triangulo */,
              "..5. 8... ..5. 8..3"   /* pandeiro */,
              "5.53 5.53 5.53 5.53"   /* ganza */,
              "9... .... 9... ...."   /* bb */,
              ".... 8... .... 8..."   /* cx */,
              ".4.4 .4.4 .4.4 .4.4"   /* hh */,
              ".... .... .... ..3."   /* tom */
            }}
          },
          {
            "xote-lento", "XOTE LENTO",
            92, 44.0f, 26.0f,
            {{
              "9..4 .5.. 8..4 .5.."   /* zabumba */,
              "7.5. 7.5. 7.5. 7.5."   /* triangulo */,
              "..6. ...4 ..6. ...5"   /* pandeiro */,
              "6.4. 6.4. 6.4. 6.4."   /* ganza */,
              "9... .... 8... ...."   /* bb */,
              ".... 7... .... 7..."   /* cx */,
              ".4.. .4.. .4.. .4.."   /* hh */,
              ".... .... .... .3.."   /* tom */
            }}
          },
          {
            "arrasta-pe", "ARRASTA-PÉ",
            160, 22.0f, 30.0f,
            {{
              "9.6. 9.5. 9.6. 9.4."   /* zabumba */,
              "8888 8888 8888 8888"   /* triangulo */,
              ".7.5 .7.5 .7.5 .7.6"   /* pandeiro */,
              "7676 7676 7676 7676"   /* ganza */,
              "9... 9... 9... 9..."   /* bb */,
              "..8. ..8. ..8. ..8."   /* cx */,
              "6.6. 6.6. 6.6. 6.6."   /* hh */,
              ".... ...4 .... ...5"   /* tom */
            }}
          }
        }},
        4,
    },
    {
        &ids::profileInfos[1],   /* caruaru */
        0, false,
        {{   /* 4 of 8 grooves */
          {
            "tradicional-01", "TRADICIONAL 01",
            138, 54.0f, 32.0f,
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
            "xaxado-88", "XAXADO 88",
            108, 18.0f, 20.0f,
            {{
              "9..6 9..5 9..6 9..4"   /* zabumba */,
              "7.7. 7.7. 7.7. 7.7."   /* triangulo */,
              ".... 8..4 .... 8..5"   /* pandeiro */,
              "6.6. 6.6. 6.6. 6.6."   /* ganza */,
              "9... 9... 9... 9..."   /* bb */,
              "..7. ..7. ..7. ..7."   /* cx */,
              "5.5. 5.5. 5.5. 5.5."   /* hh */,
              ".... .... ..4. ...."   /* tom */
            }}
          },
          {
            "baiao-pesado", "BAIÃO PESADO",
            132, 50.0f, 36.0f,
            {{
              "9..7 .68. 9..7 .58."   /* zabumba */,
              "7676 7676 7676 7676"   /* triangulo */,
              "..8. 9..6 ..8. 9..7"   /* pandeiro */,
              "7575 7575 7575 7575"   /* ganza */,
              "9..4 ..7. 9..4 ..6."   /* bb */,
              ".... 9..4 .... 9..5"   /* cx */,
              "7.7. 7.7. 7.7. 7.7."   /* hh */,
              ".... ...5 .... ..6."   /* tom */
            }}
          },
          {
            "quadrilha", "QUADRILHA",
            152, 20.0f, 24.0f,
            {{
              "9.5. 9.5. 9.5. 9.6."   /* zabumba */,
              "8.8. 8.8. 8.8. 8.8."   /* triangulo */,
              ".6.6 .6.6 .6.6 .6.7"   /* pandeiro */,
              "8686 8686 8686 8686"   /* ganza */,
              "9... 9... 9... 9..."   /* bb */,
              ".... 8... .... 8..."   /* cx */,
              "6666 6666 6666 6666"   /* hh */,
              ".... .... .... .5.."   /* tom */
            }}
          }
        }},
        4,
    },
    {
        &ids::profileInfos[2],   /* petrolina */
        1, false,
        {{   /* 4 of 8 grooves */
          {
            "forro-eletrico", "FORRÓ ELÉTRICO",
            128, 26.0f, 16.0f,
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
            "pisadinha", "PISADINHA",
            136, 14.0f, 10.0f,
            {{
              "9..4 .9.. 9..4 .9.5"   /* zabumba */,
              "5..5 5..5 5..5 5..5"   /* triangulo */,
              ".... 6..4 .... 6..5"   /* pandeiro */,
              "8.8. 8.8. 8.8. 8.8."   /* ganza */,
              "9... 9... 9... 9..."   /* bb */,
              ".... 9... .... 9..."   /* cx */,
              "7878 7878 7878 7878"   /* hh */,
              ".... ...4 .... ..5."   /* tom */
            }}
          },
          {
            "xote-eletrico", "XOTE ELÉTRICO",
            104, 34.0f, 18.0f,
            {{
              "9..5 .6.. 8..5 .6.."   /* zabumba */,
              "6.6. 6.6. 6.6. 6.6."   /* triangulo */,
              "..5. ...3 ..5. ...4"   /* pandeiro */,
              "7.5. 7.5. 7.5. 7.5."   /* ganza */,
              "9... .... 8... ...."   /* bb */,
              ".... 8... .... 8..."   /* cx */,
              "5.5. 5.5. 5.5. 5.5."   /* hh */,
              ".... .... ...4 ...."   /* tom */
            }}
          },
          {
            "vaquejada", "VAQUEJADA",
            140, 22.0f, 14.0f,
            {{
              "9.69 ..5. 9.69 ..4."   /* zabumba */,
              "7.77 7.77 7.77 7.77"   /* triangulo */,
              ".5.. 8..4 .5.. 8..5"   /* pandeiro */,
              "8484 8484 8484 8484"   /* ganza */,
              "9..4 .... 9..4 ...."   /* bb */,
              ".... 9..3 .... 9..4"   /* cx */,
              "6.66 6.66 6.66 6.66"   /* hh */,
              ".... .... .... ..5."   /* tom */
            }}
          }
        }},
        4,
    },
    {
        &ids::profileInfos[3],   /* sp */
        0, false,
        {{   /* 4 of 8 grooves */
          {
            "universitario-01", "UNIVERSITÁRIO 01",
            124, 16.0f, 6.0f,
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
          {
            "xote-pop", "XOTE POP",
            100, 24.0f, 10.0f,
            {{
              "9..3 7... 9..3 7..."   /* zabumba */,
              "8.8. 8.8. 8.8. 8.8."   /* triangulo */,
              "..6. ..6. ..6. ..7."   /* pandeiro */,
              "6.6. 6.6. 6.6. 6.6."   /* ganza */,
              "9... .... 9... ...."   /* bb */,
              ".... 8... .... 8..."   /* cx */,
              ".6.6 .6.6 .6.6 .6.6"   /* hh */,
              ".... .... .... ...."   /* tom */
            }}
          },
          {
            "forro-pop", "FORRÓ POP",
            132, 12.0f, 8.0f,
            {{
              "9.5. 9.5. 9.5. 9.5."   /* zabumba */,
              "88.8 88.8 88.8 88.8"   /* triangulo */,
              ".7.5 .7.5 .7.5 .7.5"   /* pandeiro */,
              "7777 7777 7777 7777"   /* ganza */,
              "9... 9... 9... 9..."   /* bb */,
              ".... 9... .... 9..."   /* cx */,
              "7.7. 7.7. 7.7. 7.7."   /* hh */,
              ".... .... .... ...4"   /* tom */
            }}
          },
          {
            "pe-de-serra-pop", "PÉ-DE-SERRA POP",
            118, 20.0f, 12.0f,
            {{
              "9..4 ..7. 8..4 ..6."   /* zabumba */,
              "7.74 7.74 7.74 7.74"   /* triangulo */,
              "..6. 8..4 ..6. 8..5"   /* pandeiro */,
              "6464 6464 6464 6464"   /* ganza */,
              "9... .... 9... ...."   /* bb */,
              ".... 8... .... 8..."   /* cx */,
              ".6.6 .6.6 .6.6 .6.6"   /* hh */,
              ".... .... .... ..4."   /* tom */
            }}
          }
        }},
        4,
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

void applyGroove (State& state, const Profile& profile, const Groove& groove)
{
    for (size_t lane = 0; lane < groove.patterns.size(); ++lane)
    {
        DecodedPattern decoded {};

        // A malformed string cannot reach here — the cross-check and the tests
        // both reject one — but silence the lane rather than leaving stale data
        // if it somehow did.
        if (! decodePattern (groove.patterns[lane], decoded))
            decoded.fill (0);

        state.lanes[lane] = expandPattern (decoded);
    }

    state.activeProfile = profile.id();

    // A freshly loaded profile is pristine. app.js loadProfile clears this too;
    // leaving it set shows CUSTOM over a state that is exactly a profile.
    state.dirty = false;
}

void applyProfile (State& state, const Profile& profile)
{
    applyGroove (state, profile, profile.defaultGroove());

    // A PROFILE LOAD IS A FULL RELOAD, and that has to include the slots.
    //
    // `applyGroove` writes the active lanes and nothing else, so loading a
    // profile used to leave the PREVIOUS profile's parked patterns and slot
    // indices in place: load CAMPINA, press the cycler on zabumba, load
    // CARUARU, press it back — and that lane played CAMPINA's groove while the
    // side panel highlighted CARUARU as pristine, and the cross-profile content
    // survived save and reload. /code-review.
    //
    // Phase 6's headline is "selecting a profile performs a correct FULL state
    // reload"; seven stored patterns per channel are part of that state now.
    // Every channel returns to slot 1 and the parked storage is cleared, which
    // is also what makes the strip's `PAT 01` true after a load.
    state.parkedLanes = {};

    for (size_t channel = 0; channel < static_cast<size_t> (State::kNumChannels); ++channel)
        state.setPatternSlot (channel, State::kMinPatternSlot);
}

} // namespace forrobox
