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

#include <algorithm>
#include <cmath>
#include <cstdint>
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
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

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

    return reportSummary();
}
