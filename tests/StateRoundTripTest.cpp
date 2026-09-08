/* ============================================================================
   FORRÓ BOX — parameter and state tests

   Runs headless: no GUI, no audio device. Every case records its own failures
   and the process returns non-zero at the end, so one run reports everything
   that is wrong rather than stopping at the first problem.
============================================================================ */
#include <JuceHeader.h>

#include "ForroBoxState.h"
#include "ParameterIDs.h"
#include "Profiles.h"
#include "PluginProcessor.h"

#include "TestHarness.h"
#include "TestSuites.h"
#include "FakePlayHead.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <iostream>
#include <type_traits>
#include <memory>
#include <utility>
#include <vector>

using namespace fbtest;

namespace
{
    // ── shared scaffolding ──────────────────────────────────────────────────
    /** Saves a donor processor's state, lets `edit` mutate the resulting tree,
        then loads it into a fresh processor. Hoisted to file scope because three
        malformed-input cases and every hardening case need the same eight-step
        save/parse/edit/reload sequence — inline copies had already drifted
        (some guarded `xml != nullptr`, others did not). */
    template <typename PrepareDonor, typename EditTree>
    std::unique_ptr<ForroBoxAudioProcessor> reloadWithEdit (PrepareDonor prepare, EditTree edit)
    {
        ForroBoxAudioProcessor donor;
        prepare (donor);

        juce::MemoryBlock blob;
        donor.getStateInformation (blob);

        auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), static_cast<int> (blob.getSize()));
        if (xml == nullptr)
            return nullptr;

        auto tree = juce::ValueTree::fromXml (*xml);
        edit (tree);

        const auto edited = tree.createXml();
        juce::MemoryBlock out;
        juce::AudioProcessor::copyXmlToBinary (*edited, out);

        auto restored = std::make_unique<ForroBoxAudioProcessor>();
        restored->setStateInformation (out.getData(), static_cast<int> (out.getSize()));
        return restored;
    }

    /** Every lane must stay the declared length with every value in range. */
    bool lanesAreValid (const forrobox::State& st)
    {
        for (const auto& lane : st.lanes)
        {
            if (lane.size() != static_cast<size_t> (forrobox::State::kMaxSteps))
                return false;
            for (auto v : lane)
                if (v > forrobox::State::kMaxVelocity)
                    return false;
        }
        return true;
    }

    constexpr auto noPrep = [] (ForroBoxAudioProcessor&) {};

    // ── case 1: parameter inventory ─────────────────────────────────────────
    void testInventory (ForroBoxAudioProcessor& p)
    {
        section ("inventory");
        namespace ids = forrobox::ids;

        const auto& params = p.getParameters();
        checkEqual (params.size(), 45, "total parameter count");

        // Groups: GLOBAL + 5 instruments, with 10 / 7 / 7 / 7 / 7 / 7 members.
        const auto& tree = p.getParameterTree();
        const auto groups = tree.getSubgroups (false);
        checkEqual (groups.size(), 6, "group count");

        int grouped = 0;
        for (auto* g : groups)
            grouped += g->getParameters (false).size();
        checkEqual (grouped, 45, "parameters living inside a group");

        if (groups.size() == 6)
        {
            checkEqual (groups[0]->getParameters (false).size(), 10, "GLOBAL group size");
            for (int i = 1; i < 6; ++i)
                checkEqual (groups[i]->getParameters (false).size(), 7,
                            "channel group size: " + groups[i]->getID());
        }

        // No duplicate IDs.
        juce::StringArray seen;
        for (auto* raw : params)
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (raw))
                seen.add (withId->paramID);
        checkEqual (seen.size(), 45, "parameters exposing an ID");
        auto unique = seen;
        unique.removeDuplicates (false);
        checkEqual (unique.size(), seen.size(), "no duplicate parameter IDs");

        // Defaults, straight from PLANNING.md's tables.
        auto& apvts = p.getAPVTS();
        auto value = [&apvts] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };

        checkEqual (value (ids::bpm),       132.0f, "default bpm");
        checkEqual (value (ids::swing),      38.0f, "default swing");
        checkEqual (value (ids::cachaca),    22.0f, "default cachaca");
        checkEqual (value (ids::charMix),    40.0f, "default char_mix");
        checkEqual (value (ids::master),     82.0f, "default master");
        checkEqual (value (ids::limiterOn),   1.0f, "default limiter_on");
        checkEqual (value (ids::sync),        0.0f, "default sync");
        checkEqual (value (ids::steps),       0.0f, "default steps (16)");
        checkEqual (value (ids::timbre),      0.0f, "default timbre (HI-FI)");
        checkEqual (value (ids::outputMode),  0.0f, "default output_mode (STEREO)");

        const float vols[]   = { 82, 68, 72, 64, 74 };
        const float decays[] = { 58, 40, 46, 30, 50 };
        const float pans[]   = {  0, 22, -18, 12, 0 };
        const float ghosts[] = { 12,  8, 14,  6, 10 };

        for (size_t i = 0; i < ids::channelInfos.size(); ++i)
        {
            const auto* ch = ids::channelInfos[i].id;
            checkEqual (value (ids::channelParam (ch, ids::vol)),   vols[i],   juce::String (ch) + " default vol");
            checkEqual (value (ids::channelParam (ch, ids::decay)), decays[i], juce::String (ch) + " default decay");
            checkEqual (value (ids::channelParam (ch, ids::pan)),   pans[i],   juce::String (ch) + " default pan");
            checkEqual (value (ids::channelParam (ch, ids::ghost)), ghosts[i], juce::String (ch) + " default ghost");
            checkEqual (value (ids::channelParam (ch, ids::pitch)),     0.0f,  juce::String (ch) + " default pitch");
            checkEqual (value (ids::channelParam (ch, ids::mute)),      0.0f,  juce::String (ch) + " default mute");
            checkEqual (value (ids::channelParam (ch, ids::solo)),      0.0f,  juce::String (ch) + " default solo");
        }
    }

    // ── case 2: display conversions, and their inverses ─────────────────────
    void testDisplayConversions (ForroBoxAudioProcessor& p)
    {
        section ("display conversions");
        namespace ids = forrobox::ids;

        auto* pan   = p.getAPVTS().getParameter (ids::channelParam ("zabumba", ids::pan));
        auto* pitch = p.getAPVTS().getParameter (ids::channelParam ("zabumba", ids::pitch));
        auto* ghost = p.getAPVTS().getParameter (ids::channelParam ("zabumba", ids::ghost));
        auto* bpm   = p.getAPVTS().getParameter (ids::bpm);

        check (pan != nullptr && pitch != nullptr && ghost != nullptr && bpm != nullptr,
               "the four formatted parameters exist");
        if (pan == nullptr || pitch == nullptr || ghost == nullptr || bpm == nullptr)
            return;

        auto textAt = [] (juce::RangedAudioParameter* param, float plain)
        {
            return param->getText (param->convertTo0to1 (plain), 0);
        };

        checkEqual (textAt (pan,   0.0f).toStdString(), std::string ("C"),   "pan 0 reads C");
        checkEqual (textAt (pan, -18.0f).toStdString(), std::string ("L18"), "pan -18 reads L18");
        checkEqual (textAt (pan,  22.0f).toStdString(), std::string ("R22"), "pan +22 reads R22");

        checkEqual (textAt (pitch,  5.0f).toStdString(), std::string ("+5"), "pitch +5 reads +5");
        checkEqual (textAt (pitch,  0.0f).toStdString(), std::string ("0"),  "pitch 0 reads 0");
        checkEqual (textAt (pitch, -5.0f).toStdString(), std::string ("-5"), "pitch -5 reads -5");

        checkEqual (textAt (ghost, 12.0f).toStdString(), std::string ("12%"), "ghost 12 reads 12%");
        checkEqual (textAt (bpm,  132.0f).toStdString(), std::string ("132"), "bpm 132 reads 132");

        // Invertibility across each full range.
        struct Sweep { juce::RangedAudioParameter* param; float lo; float hi; const char* name; };
        const Sweep sweeps[] {
            { pan,   -50.0f,  50.0f, "pan"   },
            { pitch, -12.0f,  12.0f, "pitch" },
            { ghost,   0.0f, 100.0f, "ghost" },
            { bpm,    40.0f, 300.0f, "bpm"   },
        };

        for (const auto& s : sweeps)
        {
            int mismatches = 0;
            for (float v = s.lo; v <= s.hi; v += 1.0f)
            {
                // getValueForText returns a NORMALISED 0..1 value, so it has to
                // be converted back before comparing against a plain value.
                const auto text      = s.param->getText (s.param->convertTo0to1 (v), 0);
                const auto roundTrip = s.param->convertFrom0to1 (s.param->getValueForText (text));
                if (std::abs (roundTrip - v) > 0.001f)
                    ++mismatches;
            }
            checkEqual (mismatches, 0, juce::String (s.name) + " text->value is the inverse of value->text");
        }
    }

    // ── shared helper: drive every parameter off its default ────────────────
    /** Deterministic non-default value for each parameter, derived from its
        index so a swap between two parameters cannot go unnoticed. */
    float offDefaultValue (juce::RangedAudioParameter& param, int index)
    {
        const auto& range = param.getNormalisableRange();
        const auto  span  = range.end - range.start;

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&param))
            return static_cast<float> ((choice->getIndex() + 1) % choice->choices.size());

        if (auto* flag = dynamic_cast<juce::AudioParameterBool*> (&param))
            return flag->get() ? 0.0f : 1.0f;   // flip: limiter_on defaults true, the rest false

        // Spread values across the range by index so no two share a value.
        const auto fraction = 0.15f + 0.7f * (static_cast<float> (index % 17) / 17.0f);
        auto v = range.start + span * fraction;
        if (range.interval > 0.0f)
            v = range.snapToLegalValue (v);
        return v;
    }

    /** A pattern unique per lane AND per step, so a lane swap or an off-by-one
        both show up as a mismatch instead of cancelling out. */
    std::uint8_t syntheticVelocity (size_t lane, int step)
    {
        return static_cast<std::uint8_t> ((lane * 13 + static_cast<size_t> (step) * 7 + 1) % 128);
    }

    // ── case 3: full round-trip ─────────────────────────────────────────────
    void testRoundTrip()
    {
        section ("round-trip");
        namespace ids = forrobox::ids;

        ForroBoxAudioProcessor source;

        // Move all 45 parameters off their defaults to distinct values.
        std::vector<std::pair<juce::String, float>> expected;
        int index = 0;
        for (auto* raw : source.getParameters())
        {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (raw))
            {
                const auto plain = offDefaultValue (*ranged, index++);
                ranged->setValueNotifyingHost (ranged->convertTo0to1 (plain));
                expected.emplace_back (ranged->paramID, plain);
            }
        }
        checkEqual (static_cast<int> (expected.size()), 45, "parameters driven off default");

        // Fill the grid and every scalar.
        auto st = source.lockPatternState();
        for (size_t lane = 0; lane < st->lanes.size(); ++lane)
            for (int step = 0; step < forrobox::State::kMaxSteps; ++step)
                st->lanes[lane][static_cast<size_t> (step)] = syntheticVelocity (lane, step);

        st->activeProfile = "caruaru";
        st->dirty = true;
        st->setPresetIdx (5);
        for (size_t i = 0; i < ids::channelInfos.size(); ++i)
            st->setPatternSlot (i, static_cast<int> (i) + 1);

        juce::MemoryBlock blob;
        source.getStateInformation (blob);
        check (blob.getSize() > 0, "getStateInformation produced data");

        // Fresh instance, fed the bytes.
        ForroBoxAudioProcessor restored;
        restored.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));

        int paramMismatches = 0;
        for (const auto& [id, plain] : expected)
        {
            auto* param = restored.getAPVTS().getParameter (id);
            if (param == nullptr)
            {
                ++paramMismatches;
                continue;
            }
            const auto actual = param->convertFrom0to1 (param->getValue());
            if (std::abs (actual - plain) > 0.001f)
            {
                ++paramMismatches;
                std::cout << "        param drift: " << id << " expected " << plain
                          << " got " << actual << std::endl;
            }
        }
        checkEqual (paramMismatches, 0, "all 45 parameter values survived");

        const auto rs = restored.lockPatternState();
        int laneMismatches = 0;
        for (size_t lane = 0; lane < rs->lanes.size(); ++lane)
            for (int step = 0; step < forrobox::State::kMaxSteps; ++step)
                if (rs->lanes[lane][static_cast<size_t> (step)] != syntheticVelocity (lane, step))
                    ++laneMismatches;
        checkEqual (laneMismatches, 0, "all 8 x 32 grid values survived");

        checkEqual (rs->activeProfile.toStdString(), std::string ("caruaru"), "activeProfile survived");
        check      (rs->dirty,                                                "dirty survived");
        checkEqual (rs->getPresetIdx(), 5,                                    "presetIdx survived");
        for (size_t i = 0; i < ids::channelInfos.size(); ++i)
            checkEqual (rs->getPatternSlot (i), static_cast<int> (i) + 1,
                        juce::String ("pattern slot survived: ") + ids::channelInfos[i].id);
    }

    // ── case 4: malformed input degrades, never crashes ─────────────────────
    void testMalformedInput()
    {
        section ("malformed input");
        namespace ids = forrobox::ids;

        {   // empty data, then random bytes — neither goes through reloadWithEdit
            // because the point is that they never parse as a tree at all.
            ForroBoxAudioProcessor p;
            p.setStateInformation (nullptr, 0);
            check (lanesAreValid (*p.lockPatternState()), "empty data: lanes valid");
            checkEqual (p.getAPVTS().getRawParameterValue (ids::bpm)->load(), 132.0f,
                        "empty data: bpm still default");
        }

        {
            ForroBoxAudioProcessor p;
            juce::Random rng { 1234 };
            juce::MemoryBlock junk (64);
            for (size_t i = 0; i < junk.getSize(); ++i)
                static_cast<char*> (junk.getData())[i] = static_cast<char> (rng.nextInt (256));
            p.setStateInformation (junk.getData(), static_cast<int> (junk.getSize()));
            check (lanesAreValid (*p.lockPatternState()), "random bytes: lanes valid");
            checkEqual (p.getAPVTS().getRawParameterValue (ids::bpm)->load(), 132.0f,
                        "random bytes: bpm still default");
        }

        {   // valid tree, state node removed
            auto p = reloadWithEdit (
                [] (ForroBoxAudioProcessor& d) { d.lockPatternState()->activeProfile = "petrolina"; },
                [&] (juce::ValueTree& tree)
                {
                    tree.removeChild (tree.getChildWithName (ids::stateNode), nullptr);
                });
            check (p != nullptr, "missing state node: reload produced a processor");
            if (p != nullptr)
            {
                check (lanesAreValid (*p->lockPatternState()), "missing state node: lanes valid");
                checkEqual (p->lockPatternState()->activeProfile.toStdString(), std::string ("campina"),
                            "missing state node: profile falls back to default");
            }
        }

        {   // truncated base64, out-of-range velocities, and a non-string lane
            auto p = reloadWithEdit (
                [] (ForroBoxAudioProcessor& d)
                {
                    auto ds = d.lockPatternState();
                    for (auto& lane : ds->lanes)
                        lane.fill (64);
                },
                [&] (juce::ValueTree& tree)
                {
                    auto grid = tree.getChildWithName (ids::stateNode).getChildWithName (ids::gridNode);

                    const auto whole = grid.getProperty (ids::lanes[0]).toString();
                    grid.setProperty (ids::lanes[0], whole.substring (0, whole.length() / 2), nullptr);

                    std::array<std::uint8_t, forrobox::State::kMaxSteps> hot;
                    hot.fill (250);
                    grid.setProperty (ids::lanes[1], juce::Base64::toBase64 (hot.data(), hot.size()), nullptr);

                    grid.setProperty (ids::lanes[2], 12345, nullptr);
                });
            check (p != nullptr, "corrupt lanes: reload produced a processor");
            if (p != nullptr)
            {
                const auto ps = p->lockPatternState();
                check (lanesAreValid (*ps), "corrupt lanes: all lanes valid length and range");

                std::uint8_t highest = 0;
                for (auto v : ps->lanes[1])
                    highest = juce::jmax (highest, v);
                checkEqual (static_cast<int> (highest), 127, "out-of-range velocities clamped to 127");

                bool silent = true;
                for (auto v : ps->lanes[2])
                    silent = silent && (v == 0);
                check (silent, "non-string lane became silent");

                bool intact = true;
                for (auto v : ps->lanes[7])
                    intact = intact && (v == 64);
                check (intact, "untouched lane unaffected by neighbours' corruption");
            }
        }

        {   // scalars out of range in the file are clamped on load
            auto p = reloadWithEdit (noPrep, [&] (juce::ValueTree& tree)
            {
                auto node = tree.getChildWithName (ids::stateNode);
                node.setProperty (ids::presetIdx, 99, nullptr);
                node.setProperty (ids::patternSlot (ids::channelInfos[0].id), -4, nullptr);
            });
            check (p != nullptr, "out-of-range scalars: reload produced a processor");
            if (p != nullptr)
            {
                checkEqual (p->lockPatternState()->getPresetIdx(), 7, "presetIdx 99 clamped to 7");
                checkEqual (p->lockPatternState()->getPatternSlot (0), 1, "pattern slot -4 clamped to 1");
            }
        }
    }

    // ── case 5: the declared ID lists must match the built layout ───────────
    /** ids::globalParams and ids::channelParams exist to keep the ID list and
        the parameter layout in step, but the layout enumerates its parameters
        independently. Asserting over the arrays is what gives them teeth: add
        a parameter to the layout without declaring its ID (or vice versa) and
        this fails, naming the one that drifted. */
    void testDeclaredIdsMatchLayout (ForroBoxAudioProcessor& p)
    {
        section ("declared IDs vs layout");
        namespace ids = forrobox::ids;

        const auto declared = ids::globalParams.size()
                            + ids::channelInfos.size() * ids::channelParams.size();
        checkEqual (static_cast<int> (declared), 45, "declared ID count");
        checkEqual (p.getParameters().size(), static_cast<int> (declared),
                    "layout size equals declared ID count");

        for (const auto* id : ids::globalParams)
            check (p.getAPVTS().getParameter (id) != nullptr,
                   juce::String ("declared global exists in layout: ") + id);

        for (const auto& info : ids::channelInfos)
            for (const auto* suffix : ids::channelParams)
            {
                const auto id = ids::channelParam (info.id, suffix);
                check (p.getAPVTS().getParameter (id) != nullptr,
                       "declared channel param exists in layout: " + id);
            }

        // And nothing in the layout that is not declared.
        juce::StringArray declaredIds;
        for (const auto* id : ids::globalParams)
            declaredIds.add (id);
        for (const auto& info : ids::channelInfos)
            for (const auto* suffix : ids::channelParams)
                declaredIds.add (ids::channelParam (info.id, suffix));

        int undeclared = 0;
        for (auto* raw : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (raw))
                if (! declaredIds.contains (withId->paramID))
                {
                    ++undeclared;
                    std::cout << "        undeclared parameter in layout: "
                              << withId->paramID << std::endl;
                }
        checkEqual (undeclared, 0, "no parameter in the layout is undeclared");
    }

    // ── case 6: typed text entry, the direction the sweep cannot reach ──────
    /** The value->text->value sweep only ever feeds back text this plugin
        generated ("L18", "R22"). A host's typed-value field passes whatever the
        user typed, so plain signed numbers need their own coverage. */
    void testTypedTextEntry (ForroBoxAudioProcessor& p)
    {
        section ("typed text entry");
        namespace ids = forrobox::ids;

        auto* pan = p.getAPVTS().getParameter (ids::channelParam ("zabumba", ids::pan));
        check (pan != nullptr, "pan parameter exists");
        if (pan == nullptr)
            return;

        // Takes the parameter as an argument rather than capturing it, so a
        // third formatted parameter is one more call site, not another lambda.
        auto typed = [] (juce::RangedAudioParameter* param, const char* text)
        {
            return juce::roundToInt (param->convertFrom0to1 (param->getValueForText (text)));
        };

        checkEqual (typed (pan, "C"),    0,   "typed \"C\" is centre");
        checkEqual (typed (pan, "L20"), -20,  "typed \"L20\" is left 20");
        checkEqual (typed (pan, "R20"),  20,  "typed \"R20\" is right 20");
        checkEqual (typed (pan, "-20"), -20,  "typed \"-20\" stays LEFT, not right");
        checkEqual (typed (pan, "20"),   20,  "typed \"20\" is right 20");
        checkEqual (typed (pan, "0"),    0,   "typed \"0\" is centre");

        auto* pitch = p.getAPVTS().getParameter (ids::channelParam ("zabumba", ids::pitch));
        if (pitch != nullptr)
        {
            checkEqual (typed (pitch, "-5"), -5, "typed pitch \"-5\"");
            checkEqual (typed (pitch, "+5"),  5, "typed pitch \"+5\"");
            checkEqual (typed (pitch, "5"),   5, "typed pitch \"5\"");
        }
    }

    // ── case 7: the hardening the review asked for ─────────────────────────
    void testHardening()
    {
        section ("hardening");
        namespace ids = forrobox::ids;

        {   // empty profile string must not survive as the active profile
            auto p = reloadWithEdit (noPrep, [&] (juce::ValueTree& tree)
            {
                tree.getChildWithName (ids::stateNode).setProperty (ids::activeProfile, "", nullptr);
            });
            if (p != nullptr)
                checkEqual (p->lockPatternState()->activeProfile.toStdString(), std::string ("campina"),
                            "empty activeProfile falls back to the default");
        }

        {   // an absurdly long lane payload is rejected, not decoded
            auto p = reloadWithEdit (noPrep, [&] (juce::ValueTree& tree)
            {
                auto grid = tree.getChildWithName (ids::stateNode).getChildWithName (ids::gridNode);
                grid.setProperty (ids::lanes[0], juce::String::repeatedString ("QUJD", 200000), nullptr);
            });
            if (p != nullptr)
            {
                bool silent = true;
                for (auto v : p->lockPatternState()->lanes[0])
                    silent = silent && (v == 0);
                check (silent, "oversized lane payload rejected rather than decoded");
            }
        }

        {   // the setters clamp, so an out-of-range value never reaches the file
            ForroBoxAudioProcessor donor;
            donor.lockPatternState()->setPresetIdx (12);
            donor.lockPatternState()->setPatternSlot (0, 0);
            checkEqual (donor.lockPatternState()->getPresetIdx(), 7,
                        "setPresetIdx clamps in memory, not just on save");
            checkEqual (donor.lockPatternState()->getPatternSlot (0), 1,
                        "setPatternSlot clamps in memory");

            juce::MemoryBlock blob;
            donor.getStateInformation (blob);
            auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), static_cast<int> (blob.getSize()));
            if (xml != nullptr)
            {
                auto node = juce::ValueTree::fromXml (*xml).getChildWithName (ids::stateNode);
                checkEqual (static_cast<int> (node.getProperty (ids::presetIdx)), 7,
                            "clamped value is what reaches the written file");
            }
        }

        {   // after a load, the live APVTS tree must not carry a grid child
            ForroBoxAudioProcessor donor;
            donor.lockPatternState()->lanes[0].fill (99);
            juce::MemoryBlock blob;
            donor.getStateInformation (blob);

            ForroBoxAudioProcessor p;
            p.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
            check (! p.getAPVTS().state.getChildWithName (ids::stateNode).isValid(),
                   "no FORROBOX_STATE child on the live APVTS tree");
            checkEqual (static_cast<int> (p.lockPatternState()->lanes[0][0]), 99,
                        "grid still loaded correctly despite the child never being assigned");
        }
    }


    // ── case 8: the velocity notation ───────────────────────────────────────
    void testPatternDecoder()
    {
        section ("pattern decoder");

        forrobox::DecodedPattern out {};

        // The worked example: campina's zabumba line.
        check (forrobox::decodePattern ("9..5 ..6. 8..4 ..6.", out), "campina zabumba decodes");
        const std::array<int, 16> expected { 126,0,0,70, 0,0,84,0, 112,0,0,56, 0,0,84,0 };
        int mismatches = 0;
        for (size_t i = 0; i < expected.size(); ++i)
            if (out[i] != expected[i]) ++mismatches;
        checkEqual (mismatches, 0, "9..5 ..6. 8..4 ..6. decodes to the specified velocities");

        // The rule itself: '.' is 0, digit d is min(127, d*14), so 9 is 126.
        check (forrobox::decodePattern ("9999999999999999", out), "all-nines decodes");
        checkEqual (static_cast<int> (out[0]), 126, "'9' is 126, not 127");
        check (forrobox::decodePattern ("................", out), "all-rests decodes");
        checkEqual (static_cast<int> (out[7]), 0, "'.' is 0");
        check (forrobox::decodePattern ("1234567891234567", out), "every digit decodes");
        for (int d = 1; d <= 9; ++d)
        {
            forrobox::DecodedPattern one {};
            const auto str = juce::String (d) + "...............";
            check (forrobox::decodePattern (str, one), "single digit " + juce::String (d));
            checkEqual (static_cast<int> (one[0]), juce::jmin (127, d * 14),
                        "digit " + juce::String (d) + " is min(127, d*14)");
        }

        // Whitespace is cosmetic and must not shift a step.
        forrobox::DecodedPattern spaced {}, tight {};
        check (forrobox::decodePattern ("9..5 ..6. 8..4 ..6.", spaced), "spaced form decodes");
        check (forrobox::decodePattern ("9..5..6.8..4..6.",    tight),  "tight form decodes");
        checkEqual (std::equal (spaced.begin(), spaced.end(), tight.begin()), true,
                    "whitespace does not shift any step");

        // Malformed input is rejected, never padded — a short pattern would tile
        // wrongly and produce a groove that is merely subtly wrong.
        forrobox::DecodedPattern untouched {};
        untouched.fill (42);
        auto probe = untouched;
        check (! forrobox::decodePattern ("9..5 ..6. 8..4 ..6", probe),  "15 characters rejected");
        check (! forrobox::decodePattern ("9..5 ..6. 8..4 ..6.9", probe), "17 characters rejected");
        check (! forrobox::decodePattern ("9..5 ..6. 8..4 ..6X", probe), "illegal character rejected");
        check (! forrobox::decodePattern ("0..5 ..6. 8..4 ..6.", probe), "'0' rejected (not in 1-9)");
        check (! forrobox::decodePattern ("", probe),                    "empty string rejected");
        checkEqual (static_cast<int> (probe[0]), 42, "a rejected decode leaves the output untouched");
    }

    // ── case 9: the four profiles carry their specified scalars ─────────────
    void testProfileScalars()
    {
        section ("profile scalars");
        namespace ids = forrobox::ids;

        const auto profiles = forrobox::allProfiles();
        checkEqual (static_cast<int> (profiles.size()), 4, "four profiles");
        checkEqual (static_cast<int> (ids::profileInfos.size()), 4, "four ProfileInfo entries");

        struct Expected { const char* id; int bpm; float swing; float cachaca; int timbre; bool muted; const char* code; };
        const std::array<Expected, 4> want {{
            { "campina",   132, 38.0f, 22.0f, 0, true,  "CAM" },
            { "caruaru",   138, 54.0f, 32.0f, 0, false, "CAR" },
            { "petrolina", 128, 26.0f, 16.0f, 1, false, "PET" },
            { "sp",        124, 16.0f,  6.0f, 0, false, "UNI" },
        }};

        for (const auto& w : want)
        {
            const auto* p = forrobox::findProfile (w.id);
            check (p != nullptr, juce::String ("profile resolves: ") + w.id);
            if (p == nullptr) continue;

            checkEqual (p->bpm,          w.bpm,     juce::String (w.id) + " bpm");
            checkEqual (p->swing,        w.swing,   juce::String (w.id) + " swing");
            checkEqual (p->cachaca,      w.cachaca, juce::String (w.id) + " cachaca");
            checkEqual (p->timbreIndex,  w.timbre,  juce::String (w.id) + " timbre index");
            checkEqual (p->bateriaMuted, w.muted,   juce::String (w.id) + " bateria muted flag");
            checkEqual (juce::String (p->code()).toStdString(), std::string (w.code),
                        juce::String (w.id) + " code");
            checkEqual (static_cast<int> (p->patterns.size()), forrobox::State::kNumLanes,
                        juce::String (w.id) + " has one pattern per lane");

            // every pattern must decode — a malformed one would silence a lane
            for (size_t lane = 0; lane < p->patterns.size(); ++lane)
            {
                forrobox::DecodedPattern d {};
                check (forrobox::decodePattern (p->patterns[lane], d),
                       juce::String (w.id) + " lane decodes: " + ids::lanes[lane]);
            }
        }

        checkEqual (juce::String (ids::defaultProfile).toStdString(), std::string ("campina"),
                    "campina is the default, matching the prototype");
        check (forrobox::findProfile ("nonexistent") == nullptr,
               "an unknown id returns nullptr rather than a default");
        check (forrobox::findProfile ("") == nullptr, "empty id returns nullptr");
    }

    // ── case 10: tiling and profile application ─────────────────────────────
    void testExpansionAndApply()
    {
        section ("tiling and profile load");
        namespace ids = forrobox::ids;

        forrobox::DecodedPattern base {};
        for (size_t i = 0; i < base.size(); ++i)
            base[i] = static_cast<std::uint8_t> (i + 1);   // distinct per step

        const auto filled = forrobox::expandPattern (base);
        // Every slot filled, window-independent: zeroing past 16 would make a
        // later STEPS switch play silence where the prototype repeats bar 1.
        int mismatched = 0;
        for (size_t i = 0; i < filled.size(); ++i)
            if (filled[i] != base[i % 16]) ++mismatched;
        checkEqual (mismatched, 0, "every slot equals base[i mod 16]");

        int zeros = 0;
        for (auto v : filled) if (v == 0) ++zeros;
        checkEqual (zeros, 0, "no slot left silent — base has no rests here");
        checkEqual (static_cast<int> (filled.size()), forrobox::State::kMaxSteps,
                    "the lane is always its full 32 slots");

        // Applying a profile fills every lane.
        forrobox::State st;
        const auto* caruaru = forrobox::findProfile ("caruaru");
        check (caruaru != nullptr, "caruaru resolves");
        if (caruaru == nullptr) return;

        forrobox::applyProfile (st, *caruaru);
        checkEqual (st.activeProfile.toStdString(), std::string ("caruaru"), "activeProfile set");

        int silentLanes = 0;
        for (const auto& lane : st.lanes)
        {
            bool any = false;
            for (auto v : lane) any = any || (v != 0);
            if (! any) ++silentLanes;
        }
        checkEqual (silentLanes, 0, "all 8 lanes filled — caruaru has no all-rest lane");

        // Bateria's four kit pieces are distinct, not one shared lane.
        const auto bb = st.lanes[4], cx = st.lanes[5], hh = st.lanes[6], tom = st.lanes[7];
        check (! std::equal (bb.begin(), bb.end(), cx.begin()), "bb differs from cx");
        check (! std::equal (bb.begin(), bb.end(), hh.begin()), "bb differs from hh");
        check (! std::equal (cx.begin(), cx.end(), tom.begin()), "cx differs from tom");

        // A second load must fully replace the first, leaving nothing behind.
        // sp's tom lane is all rests, so it is the strictest case.
        const auto* sp = forrobox::findProfile ("sp");
        check (sp != nullptr, "sp resolves");
        if (sp == nullptr) return;
        forrobox::applyProfile (st, *sp);
        checkEqual (st.activeProfile.toStdString(), std::string ("sp"), "activeProfile replaced");

        bool tomSilent = true;
        for (auto v : st.lanes[7]) tomSilent = tomSilent && (v == 0);
        check (tomSilent, "sp's all-rest tom lane overwrote caruaru's — no bleed-through");

        // Finding 1: a freshly loaded profile is pristine.
        st.dirty = true;
        forrobox::applyProfile (st, *sp);
        check (! st.dirty, "applyProfile clears dirty — a loaded profile is not CUSTOM");

        // Finding 6: State's default agrees with the declared constant.
        forrobox::State fresh;
        checkEqual (fresh.activeProfile.toStdString(),
                    std::string (forrobox::ids::defaultProfile),
                    "a fresh State's activeProfile equals ids::defaultProfile");

        // Finding 5: Profile's identity comes from profileInfos, not a copy.
        for (size_t i = 0; i < ids::profileInfos.size(); ++i)
        {
            const auto* p = forrobox::findProfile (ids::profileInfos[i].id);
            check (p != nullptr, juce::String ("profileInfos[") + juce::String ((int) i)
                                 + "].id resolves to a Profile");
            if (p != nullptr)
                checkEqual (juce::String (p->id()).toStdString(),
                            std::string (ids::profileInfos[i].id),
                            "Profile identity is the ProfileInfo entry, not a duplicate");
        }

        int stale = 0;
        forrobox::DecodedPattern spZab {};
        forrobox::decodePattern (sp->patterns[0], spZab);
        for (int i = 0; i < 16; ++i)
            if (st.lanes[0][static_cast<size_t> (i)] != spZab[static_cast<size_t> (i)]) ++stale;
        checkEqual (stale, 0, "zabumba lane matches sp, not caruaru");
    }

    // ── transport (02-02) ───────────────────────────────────────────────────
    /** Renders `blocks` blocks of `blockSize` and returns the processor's
        reported step afterwards. */
    int renderAndReportStep (ForroBoxAudioProcessor& processor, int blockSize, int blocks)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int i = 0; i < blocks; ++i)
        {
            buffer.clear();
            midi.clear();
            processor.processBlock (buffer, midi);
        }

        return processor.getCurrentStep();
    }

    void testTransport()
    {
        section ("transport");

        checkEqual (ForroBoxAudioProcessor::stepsForChoiceIndex (0), 16, "steps choice 0 is a 16-step window");
        checkEqual (ForroBoxAudioProcessor::stepsForChoiceIndex (1), 32, "steps choice 1 is a 32-step window");
        checkEqual (ForroBoxAudioProcessor::stepsForChoiceIndex (7), 16,
                    "an out-of-range steps choice falls back to 16 rather than passing the index through");

        ForroBoxAudioProcessor processor;
        processor.prepareToPlay (48000.0, 512);

        check (! processor.isPlaying(), "a fresh processor is not playing");
        checkEqual (processor.getCurrentStep(), -1, "a stopped processor reports step -1");

        // Not playing: the clock must not advance at all.
        checkEqual (renderAndReportStep (processor, 512, 200), -1,
                    "processBlock does not advance the clock while stopped");

        processor.setPlaying (true);
        check (processor.isPlaying(), "setPlaying(true) starts the transport");

        const auto stepWhilePlaying = renderAndReportStep (processor, 512, 200);
        check (stepWhilePlaying >= 0, "processBlock advances the clock while playing");
        check (stepWhilePlaying < 32, "the reported step stays inside the 32-slot storage");

        processor.setPlaying (false);
        checkEqual (processor.getCurrentStep(), -1, "stopping resets the reported step to -1");
        checkEqual (renderAndReportStep (processor, 512, 50), -1, "no steps are emitted after stopping");

        // Restarting begins at step 0 rather than resuming mid-pattern.
        processor.setPlaying (true);
        checkEqual (renderAndReportStep (processor, 64, 1), 0,
                    "restarting emits step 0 first rather than resuming mid-pattern");

        // PLANNING.md lists `playing` as global state, but it is deliberately
        // neither a parameter nor persisted: a plugin that resumes playing when
        // a project is reopened is hostile.
        ForroBoxAudioProcessor donor;
        donor.prepareToPlay (48000.0, 512);
        donor.setPlaying (true);
        check (donor.isPlaying(), "the donor is playing before its state is saved");

        juce::MemoryBlock blob;
        donor.getStateInformation (blob);

        ForroBoxAudioProcessor restored;
        restored.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));

        check (! restored.isPlaying(), "playing does not survive a state round-trip");
        checkEqual (restored.getCurrentStep(), -1, "a restored processor reports the stopped step");

        // The play state is not on the parameter surface either.
        bool foundPlayingParam = false;
        for (auto* parameter : restored.getParameters())
            if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
                if (withID->paramID.containsIgnoreCase ("play"))
                    foundPlayingParam = true;

        check (! foundPlayingParam, "no automatable parameter exposes the play state");
    }

    /** The `steps` window the clock runs must agree with the choice the host
        displays. getRawParameterValue returns the denormalised but UNSNAPPED
        value, so a choice parameter's raw value is a continuous float — every
        normalised value in [0.5, 1) reads back as "32" from the parameter while
        truncation would give the clock a 16-step window. A MIDI-CC map, an
        automation lane or a generic host slider all land there. */
    void testStepWindowAgreesWithTheHost()
    {
        section ("step window vs the host's choice");

        ForroBoxAudioProcessor processor;
        processor.prepareToPlay (48000.0, 256);

        auto* stepsParam = dynamic_cast<juce::AudioParameterChoice*> (
            processor.getAPVTS().getParameter (forrobox::ids::steps));
        check (stepsParam != nullptr, "the steps parameter is an AudioParameterChoice");

        if (stepsParam == nullptr)
            return;

        // Normalised values a host can genuinely produce, including the ones
        // that are neither 0 nor 1.
        for (const float norm : { 0.0f, 0.4f, 0.5f, 0.606f, 0.75f, 0.99f, 1.0f })
        {
            stepsParam->setValueNotifyingHost (norm);

            const auto reportedByHost = stepsParam->getIndex() == 1 ? 32 : 16;

            // Deliberately NOT compared against a test-side recomputation of
            // the plugin's own conversion — that would key both sides off the
            // same logic and pass whatever processBlock did. The only honest
            // question is what the clock actually emits.
            processor.setPlaying (false);
            processor.setPlaying (true);

            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            int widestStep = -1;

            for (int block = 0; block < 900; ++block)
            {
                buffer.clear();
                midi.clear();
                processor.processBlock (buffer, midi);
                widestStep = juce::jmax (widestStep, processor.getCurrentStep());
            }

            check (widestStep < reportedByHost,
                   juce::String ("every emitted step fits the displayed window at normalised ")
                       + juce::String (norm, 3));
            check (widestStep >= reportedByHost / 2,
                   juce::String ("the run covered enough of the window to be meaningful at normalised ")
                       + juce::String (norm, 3));
        }
    }

    // ── host sync (02-03) ───────────────────────────────────────────────────
    /** Renders blocks through a scripted host, collecting every step the
        processor reports. The processor's currentStep atomic is the only
        observable, so the host is advanced one block at a time and sampled. */
    struct SyncRun
    {
        std::vector<int> steps;   // every distinct step reported, in order
        int blocksRendered = 0;
    };

    SyncRun renderWithHost (ForroBoxAudioProcessor& processor,
                            fbtest::FakePlayHead& host,
                            int blockSize,
                            int blocks,
                            double sampleRate = 48000.0)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        SyncRun run;
        int previous = -2;

        for (int i = 0; i < blocks; ++i)
        {
            buffer.clear();
            midi.clear();
            processor.processBlock (buffer, midi);

            const auto step = processor.getCurrentStep();
            if (step != previous)
            {
                run.steps.push_back (step);
                previous = step;
            }

            host.advance (blockSize, sampleRate);
            ++run.blocksRendered;
        }

        return run;
    }

    /** A processor wired to a scripted host, with SYNC on and playing. */
    struct SyncedProcessor
    {
        ForroBoxAudioProcessor processor;
        fbtest::FakePlayHead   host;

        explicit SyncedProcessor (bool syncOn = true, int stepsChoice = 0)
        {
            processor.setPlayHead (&host);
            processor.prepareToPlay (48000.0, 256);

            if (auto* sync = processor.getAPVTS().getParameter (forrobox::ids::sync))
                sync->setValueNotifyingHost (syncOn ? 1.0f : 0.0f);

            if (auto* steps = processor.getAPVTS().getParameter (forrobox::ids::steps))
                steps->setValueNotifyingHost (stepsChoice == 1 ? 1.0f : 0.0f);

            if (auto* swing = processor.getAPVTS().getParameter (forrobox::ids::swing))
                swing->setValueNotifyingHost (0.0f);

            processor.setPlaying (true);
        }
    };

    void testHostBarLock()
    {
        section ("host sync: step 0 on the host bar");

        // At a bar start the emitted step must be 0 — derived from the host's
        // absolute position, not from a local counter.
        for (const int bar : { 0, 1, 7, 100 })
        {
            SyncedProcessor rig;
            rig.host.seekToBar (bar);

            const auto run = renderWithHost (rig.processor, rig.host, 64, 1);
            check (! run.steps.empty(), juce::String ("a step is reported at bar ") + juce::String (bar));
            checkEqual (run.steps.empty() ? -99 : run.steps.front(), 0,
                        juce::String ("step 0 lands on bar ") + juce::String (bar)
                            + " (no drift after " + juce::String (bar) + " bars)");
        }

        // A 32-step window spans two bars: step 0 on even bars, 16 on odd.
        for (const int bar : { 0, 1, 2, 3 })
        {
            SyncedProcessor wide { true, 1 };
            wide.host.seekToBar (bar);

            const auto run = renderWithHost (wide.processor, wide.host, 64, 1);
            const auto expected = (bar % 2 == 0) ? 0 : 16;
            checkEqual (run.steps.empty() ? -99 : run.steps.front(), expected,
                        juce::String ("a 32-step window puts step ") + juce::String (expected)
                            + " on bar " + juce::String (bar));
        }

        // A weak version of this case would pass for a clock that ignores the
        // host entirely, so prove the plugin is really following: with SYNC OFF
        // the same seek is ignored and the pattern starts from 0 regardless.
        SyncedProcessor unsynced { false };
        unsynced.host.seekToBar (7);
        const auto free = renderWithHost (unsynced.processor, unsynced.host, 64, 1);
        checkEqual (free.steps.empty() ? -99 : free.steps.front(), 0,
                    "with SYNC off the host's bar position is ignored");
    }

    void testHostTempo()
    {
        section ("host sync: host tempo drives the clock");

        // Host at 90 while the bpm parameter says 132: the host must win.
        SyncedProcessor rig;
        rig.host.bpm = 90.0;

        if (auto* bpm = rig.processor.getAPVTS().getParameter (forrobox::ids::bpm))
            bpm->setValueNotifyingHost (bpm->convertTo0to1 (132.0f));

        // 90 BPM at 48 kHz: a sixteenth is 8000 samples. 32000 samples (125
        // blocks of 256) crosses four boundaries, and step 0 fires at the start,
        // so five distinct steps are reported.
        const auto run = renderWithHost (rig.processor, rig.host, 256, 125);
        checkEqual (static_cast<int> (run.steps.size()), 5,
                    "host 90 BPM gives 5 reported sixteenths in 32000 samples, not the parameter's 132");

        // With SYNC off the parameter drives it again: 132 BPM is a sixteenth
        // every 5454.5 samples, so 32000 samples is 6 steps.
        SyncedProcessor unsynced { false };
        unsynced.host.bpm = 90.0;
        if (auto* bpm = unsynced.processor.getAPVTS().getParameter (forrobox::ids::bpm))
            bpm->setValueNotifyingHost (bpm->convertTo0to1 (132.0f));

        const auto freeRun = renderWithHost (unsynced.processor, unsynced.host, 256, 125);
        checkEqual (static_cast<int> (freeRun.steps.size()), 6,
                    "with SYNC off the bpm parameter drives the clock again");

        // A tempo ramp across blocks stays in order and unbroken.
        SyncedProcessor ramp;
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        int previous = -1, breaks = 0, reported = 0;

        for (int i = 0; i < 400; ++i)
        {
            ramp.host.bpm = 60.0 + static_cast<double> (i) * 0.5;   // 60 -> 260
            buffer.clear();
            midi.clear();
            ramp.processor.processBlock (buffer, midi);

            const auto step = ramp.processor.getCurrentStep();
            if (step != previous && step >= 0)
            {
                if (previous >= 0 && step != (previous + 1) % 16)
                    ++breaks;
                previous = step;
                ++reported;
            }

            ramp.host.advance (256, 48000.0);
        }

        check (reported > 10, "the tempo ramp emitted a meaningful number of steps");
        checkEqual (breaks, 0, "a host tempo ramp produces no gap and no repeat");
    }

    void testHostTransport()
    {
        section ("host sync: host transport governs");

        // Host stopped: nothing is emitted even though the plugin is playing.
        SyncedProcessor stopped;
        stopped.host.hostPlaying = false;
        stopped.host.seekToBar (4);

        const auto silent = renderWithHost (stopped.processor, stopped.host, 256, 50);
        checkEqual (stopped.processor.getCurrentStep(), -1,
                    "a stopped host reports the stopped step even while the plugin is playing");
        for (auto step : silent.steps)
            checkEqual (step, -1, "no step is emitted while the host is stopped");

        // The host starts mid-timeline: the plugin joins at the host's position,
        // not at step 0.
        SyncedProcessor joining;
        joining.host.hostPlaying = false;
        joining.host.seekToBar (2);
        renderWithHost (joining.processor, joining.host, 256, 4);

        // Set the position absolutely rather than nudging it: the stopped blocks
        // above advanced the host, and a block only contains a step if a step
        // boundary falls inside it. ppq 9.5 is step position 38, exactly.
        joining.host.hostPlaying = true;
        joining.host.ppq = 9.5;              // a beat and a half into bar 2
        joining.host.lastBarStartPpq = 8.0;
        const auto joined = renderWithHost (joining.processor, joining.host, 64, 1);

        check (! joined.steps.empty(), "a step is reported once the host starts");
        checkEqual (joined.steps.empty() ? -99 : joined.steps.front(), 6,
                    "joining 1.5 beats into a bar emits step 6, not step 0");

        // With SYNC off the plugin's own transport governs, as in 02-02.
        SyncedProcessor own { false };
        own.host.hostPlaying = false;
        const auto ownRun = renderWithHost (own.processor, own.host, 256, 30);
        check (! ownRun.steps.empty() && ownRun.steps.front() >= 0,
               "with SYNC off a stopped host does not silence the plugin");
    }

    void testHostLoopsAndJumps()
    {
        section ("host sync: loops, jumps and scrubs");

        // PLANNING.md does not specify these. Decision: re-derive from the
        // host's absolute position, and never catch up.
        SyncedProcessor rig;
        rig.host.hostLooping = true;
        rig.host.seekToBar (0);

        renderWithHost (rig.processor, rig.host, 256, 40);

        // Loop back to the top mid-pattern.
        rig.host.seekToBar (0);
        const auto looped = renderWithHost (rig.processor, rig.host, 64, 1);
        checkEqual (looped.steps.empty() ? -99 : looped.steps.front(), 0,
                    "looping back to bar 0 re-derives step 0");

        // A large forward jump: the step follows the host, and only one step is
        // reported for the block — no catch-up burst.
        SyncedProcessor jumper;
        renderWithHost (jumper.processor, jumper.host, 256, 10);
        jumper.host.seekToBar (64);

        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer midi;
        jumper.processor.processBlock (buffer, midi);
        checkEqual (jumper.processor.getCurrentStep(), 0,
                    "a 64-bar forward jump lands on step 0, derived from the host");

        // A backwards scrub to an arbitrary, non-bar position.
        SyncedProcessor scrubbed;
        renderWithHost (scrubbed.processor, scrubbed.host, 256, 20);
        scrubbed.host.ppq = 2.25;            // step position 9, exactly
        scrubbed.host.lastBarStartPpq = 0.0;
        const auto after = renderWithHost (scrubbed.processor, scrubbed.host, 64, 1);
        checkEqual (after.steps.empty() ? -99 : after.steps.front(), 9,
                    "a scrub to ppq 2.25 emits step 9, derived from the position");

        // Repeated renders at the SAME host position must not report a
        // different step each time — the mapping is a pure function of position.
        SyncedProcessor frozen;
        frozen.host.ppq = 5.5;
        juce::AudioBuffer<float> frozenBuffer (2, 64);
        juce::MidiBuffer frozenMidi;
        std::vector<int> seen;

        for (int i = 0; i < 8; ++i)
        {
            frozenBuffer.clear();
            frozenMidi.clear();
            frozen.processor.processBlock (frozenBuffer, frozenMidi);
            seen.push_back (frozen.processor.getCurrentStep());
        }

        int varied = 0;
        for (size_t i = 1; i < seen.size(); ++i)
            if (seen[i] != seen[0]) ++varied;

        checkEqual (varied, 0, "the same host position always maps to the same step");
    }

    void testPlayheadDegradation()
    {
        section ("host sync: missing playhead information");

        // No playhead at all: fall back to internal tempo rather than silence.
        {
            ForroBoxAudioProcessor bare;
            bare.prepareToPlay (48000.0, 256);
            if (auto* sync = bare.getAPVTS().getParameter (forrobox::ids::sync))
                sync->setValueNotifyingHost (1.0f);
            bare.setPlaying (true);

            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            int reported = -1;
            for (int i = 0; i < 60; ++i)
            {
                buffer.clear();
                midi.clear();
                bare.processBlock (buffer, midi);
                reported = juce::jmax (reported, bare.getCurrentStep());
            }

            check (reported >= 0, "SYNC on with no playhead falls back to internal tempo");
        }

        // Each field independently absent.
        struct Missing { const char* name; bool position; bool ppq; bool bpm; bool bar; bool sig; };
        const Missing cases[] {
            { "no position at all", false, true,  true,  true,  true  },
            { "no ppq",             true,  false, true,  true,  true  },
            { "no tempo",           true,  true,  false, true,  true  },
            { "no bar start",       true,  true,  true,  false, true  },
            { "no time signature",  true,  true,  true,  true,  false },
            { "ppq but no bar",     true,  true,  true,  false, false },
            { "bar but no ppq",     true,  false, true,  true,  true  },
        };

        for (const auto& c : cases)
        {
            SyncedProcessor rig;
            rig.host.providePosition       = c.position;
            rig.host.providePpq            = c.ppq;
            rig.host.provideBpm            = c.bpm;
            rig.host.provideBarStart       = c.bar;
            rig.host.provideTimeSignature  = c.sig;

            const auto run = renderWithHost (rig.processor, rig.host, 256, 60);
            check (run.blocksRendered == 60,
                   juce::String ("rendering completes with ") + c.name + " (no hang)");
            check (! run.steps.empty(),
                   juce::String ("something sensible is reported with ") + c.name);
        }

        // Non-finite and negative host positions.
        for (const double badPpq : { std::numeric_limits<double>::quiet_NaN(),
                                     std::numeric_limits<double>::infinity(),
                                     -1.0, -1000.0 })
        {
            SyncedProcessor rig;
            rig.host.ppq = badPpq;

            const auto run = renderWithHost (rig.processor, rig.host, 256, 20);
            check (run.blocksRendered == 20, "a non-finite or negative host position does not hang");
        }

        // A non-finite host tempo.
        for (const double badBpm : { std::numeric_limits<double>::quiet_NaN(), 0.0, -120.0,
                                     std::numeric_limits<double>::infinity() })
        {
            SyncedProcessor rig;
            rig.host.bpm = badBpm;

            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            for (int i = 0; i < 20; ++i) { buffer.clear(); midi.clear(); rig.processor.processBlock (buffer, midi); }
            check (true, "a non-finite or non-positive host tempo does not hang");
        }
    }

    void testPlayheadQueriedOncePerBlock()
    {
        section ("host sync: playhead read once per block");

        SyncedProcessor rig;
        rig.host.resetQueryCount();

        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (int i = 0; i < 25; ++i)
        {
            buffer.clear();
            midi.clear();
            rig.processor.processBlock (buffer, midi);
        }

        check (rig.host.queryCount() <= 25,
               juce::String ("getPosition() called at most once per block (")
                   + juce::String (rig.host.queryCount()) + " for 25 blocks)");
        check (rig.host.queryCount() >= 25, "getPosition() is actually being called");
    }

} // namespace

void runStateTests()
{
    std::cout << "Forro Box — parameter and state tests" << std::endl;

    ForroBoxAudioProcessor processor;
    testInventory (processor);
    testDisplayConversions (processor);
    testDeclaredIdsMatchLayout (processor);
    testTypedTextEntry (processor);
    testRoundTrip();
    testMalformedInput();
    testHardening();
    testPatternDecoder();
    testProfileScalars();
    testExpansionAndApply();
    testTransport();
    testStepWindowAgreesWithTheHost();
    testHostBarLock();
    testHostTempo();
    testHostTransport();
    testHostLoopsAndJumps();
    testPlayheadDegradation();
    testPlayheadQueriedOncePerBlock();
}
