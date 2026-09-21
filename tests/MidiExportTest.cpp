/* ============================================================================
   FORRÓ BOX — Standard MIDI File export tests

   `scripts/verify-midi.py` already proves the bytes are IDENTICAL to the ones
   the prototype's own `exportMIDI` produces, across a matrix covering every
   profile, both step windows, both BPMs and every mute. This
   suite exists for what that comparison cannot do: it cannot prove the
   prototype's format is what `PLANNING.md` specifies, and it cannot fail if BOTH
   sides are wrong the same way.

   So these checks READ THE FILE BACK and assert the spec directly, against
   numbers taken from `PLANNING.md` with its own line numbers beside them —
   never from `MidiExport.h`'s constants, because a table that reads the
   writer's own constants would move with any mutation of them and could not
   fail.
============================================================================ */
#include <JuceHeader.h>

#include "ForroBoxState.h"
#include "DragMidiButton.h"
#include "GrooveExport.h"
#include "MidiExport.h"
#include "ParameterIDs.h"
#include "PluginProcessor.h"
#include "VoiceEngine.h"

#include "TestHarness.h"
#include "TestSuites.h"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace fbtest;

namespace
{
    // ── the spec, transcribed from PLANNING.md with its line numbers ────────
    //
    //  Deliberately NOT `forrobox::midiFile::kPPQ` and friends. The writer's
    //  constants are the SUBJECT of these checks; reading them here would make
    //  every assertion tautological — mutate the writer and both sides move
    //  together, which is the shape of a check that cannot fail.
    constexpr int kSpecPPQ         = 96;    // PLANNING.md:806 — "type 0, PPQ 96"
    constexpr int kSpecStepTicks   = 24;    // PLANNING.md:806 — "sixteenth = 24 ticks"
    constexpr int kSpecGateTicks   = 19;    // PLANNING.md:808 — 80% of 24, floored
    constexpr int kSpecChannel     = 10;    // PLANNING.md:808 — "0x99 (channel 10)"
    constexpr int kSpecNumTracks   = 1;     // PLANNING.md:806 — "one track"
    constexpr int kSpecFileType    = 0;     // PLANNING.md:806 — "type 0"

    struct GmRow
    {
        const char* lane;
        int         note;
        int         planningLine;   ///< the row in PLANNING.md that specifies it
    };

    /** `PLANNING.md:815-822`, one row per lane, in `ids::lanes` order.

        The line number is not decoration: a wrong note number here is traceable
        to the row that specifies it, which is the difference between "the GM map
        is wrong" and "line 818 says 82 and we wrote 81". */
    constexpr std::array<GmRow, 8> gmSpec {{
        { "zabumba",   36, 815 },
        { "triangulo", 81, 816 },
        { "pandeiro",  54, 817 },
        { "ganza",     82, 818 },
        { "bb",        36, 819 },
        { "cx",        38, 820 },
        { "hh",        42, 821 },
        { "tom",       45, 822 },
    }};

    // ── scaffolding ─────────────────────────────────────────────────────────

    const forrobox::ChannelGate nothingMuted {};

    /** Reads the produced bytes back through JUCE's own parser.

        A second hand-written reader here would share this plan's assumptions
        with the writer; `juce::MidiFile` shares none of them. */
    struct ReadBack
    {
        juce::MidiFile file;
        int            fileType { -1 };
        bool           parsed { false };

        explicit ReadBack (const std::vector<std::uint8_t>& bytes)
        {
            juce::MemoryInputStream stream (bytes.data(), bytes.size(), false);

            // createMatchingNoteOffs = false: the point is to see the note-offs
            // the WRITER produced. Letting JUCE synthesise them would mean the
            // gate check measured JUCE's defaults.
            parsed = file.readFrom (stream, false, &fileType);
        }

        const juce::MidiMessageSequence* track() const
        {
            return file.getNumTracks() > 0 ? file.getTrack (0) : nullptr;
        }

        // The three accessors below replace eleven copies of the same walk. The
        // copies were not identical — some took the first match and some the
        // last — with nothing marking which difference was deliberate, and each
        // repeated the null-track guard that makes a dropped one pass with no
        // evidence at all.
        std::vector<int> noteOnTicks() const
        {
            std::vector<int> ticks;

            forEachNoteOn ([&ticks] (const juce::MidiMessage& m)
                           { ticks.push_back (static_cast<int> (m.getTimeStamp())); });

            return ticks;
        }

        std::vector<int> noteOnNotes() const
        {
            std::vector<int> notes;

            forEachNoteOn ([&notes] (const juce::MidiMessage& m)
                           { notes.push_back (m.getNoteNumber()); });

            return notes;
        }

        int noteOnCount() const { return static_cast<int> (noteOnTicks().size()); }

        /** The exact microseconds-per-quarter-note the tempo meta carries, or -1.

            Read out of the raw meta bytes rather than through
            `getTempoSecondsPerQuarterNote`, which divides by a million and hands
            back a double — the one digit this check is about would round away. */
        int tempoMicroseconds() const
        {
            auto microseconds = -1;

            if (const auto* sequence = track())
                for (const auto* event : *sequence)
                    if (event->message.isTempoMetaEvent() && event->message.getRawDataSize() >= 6)
                    {
                        const auto* raw = event->message.getRawData();
                        microseconds = (raw[3] << 16) | (raw[4] << 8) | raw[5];
                    }

            return microseconds;
        }

        template <typename Visit>
        void forEachNoteOn (Visit&& visit) const
        {
            if (const auto* sequence = track())
                for (const auto* event : *sequence)
                    if (event->message.isNoteOn())
                        visit (event->message);
        }
    };

    /** Sets an APVTS parameter by id to a plain (not normalised) value.

        At file scope because it was a lambda inside ONE test, and the other
        test that needed it fifty lines below open-coded the same three calls
        rather than reach a lambda it could not see. The same shape exists in
        VoiceTest.cpp's AudioRig and in two other suites; promoting it to
        `fbtest` in TestHarness.h is the real fix and is recorded in PROJECT.md,
        but that is four files outside this plan. */
    void setParameter (ForroBoxAudioProcessor& processor, juce::StringRef id, float value)
    {
        if (auto* parameter = processor.getAPVTS().getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    /** A state with a SINGLE hit, on `lane`, at step 0, velocity 100.

        The step and the velocity were parameters until /simplify: every caller
        passed 0 and 100, so the signature advertised a generality no test used
        and a reader had to visit four call sites to learn that. */
    forrobox::State stateWithHit (int lane)
    {
        forrobox::State state;
        state.lanes[static_cast<size_t> (lane)][0] = 100;
        return state;
    }

    // ── the cases ───────────────────────────────────────────────────────────

    void testHeaderIsWhatPlanningSpecifies()
    {
        section ("the written file reads back as type 0, PPQ 96, one track");

        auto state = stateWithHit (0);
        const auto bytes = forrobox::renderStandardMidiFile (state, 120, 16, nothingMuted);

        const ReadBack read (bytes);

        check (read.parsed, "juce::MidiFile parses the bytes the writer produced");
        checkEqual (read.fileType, kSpecFileType, "the file type is 0 — PLANNING.md:806");
        checkEqual (read.file.getNumTracks(), kSpecNumTracks, "one track — PLANNING.md:806");
        checkEqual (static_cast<int> (read.file.getTimeFormat()), kSpecPPQ,
                    "the time format is PPQ 96 — PLANNING.md:806");
    }

    void testTempoAndTimeSignatureMetas()
    {
        section ("the tempo meta carries the BPM and the time signature is 4/4");

        // 139, not 120: 60000000/139 = 431654.676, so truncating instead of
        // rounding gives a different integer. At 120 the two agree and the check
        // would have no teeth — the same hole the cross-check's matrix had.
        constexpr int bpm = 139;
        constexpr int expectedMicroseconds = 431655;   // round(60000000 / 139)

        auto state = stateWithHit (0);
        const auto bytes = forrobox::renderStandardMidiFile (state, bpm, 16, nothingMuted);
        const ReadBack read (bytes);

        auto numerator = 0, denominator = 0;

        if (const auto* sequence = read.track())
            for (const auto* event : *sequence)
                if (event->message.isTimeSignatureMetaEvent())
                    event->message.getTimeSignatureInfo (numerator, denominator);

        checkEqual (read.tempoMicroseconds(), expectedMicroseconds,
                    "the tempo meta is round(60000000 / bpm) — PLANNING.md:809");
        checkEqual (numerator, 4, "the time signature numerator is 4 — PLANNING.md:809");
        checkEqual (denominator, 4, "the time signature denominator is 4 — PLANNING.md:809");

        // The guard, which nothing else reaches. The BPM parameter cannot be 0,
        // but this function takes a plain int, and 60000000/0 would hand
        // std::llround an infinity. The cross-check cannot cover it: the
        // prototype's answer to a zero BPM is a tempo meta of 00 00 00, which is
        // not a behaviour to agree with.
        const ReadBack guardedRead (forrobox::renderStandardMidiFile (state, 0, 16, nothingMuted));

        checkEqual (guardedRead.tempoMicroseconds(), 60000000 / forrobox::ids::kMinBpm,
                    "a BPM of 0 is clamped to kMinBpm rather than dividing by zero");
    }

    void testEveryNoteIsChannelTenWithAGateOfNineteenTicks()
    {
        section ("every note is on channel 10 and lasts 19 of its 24 ticks");

        // Every lane, several steps, so this is not one note's worth of evidence.
        forrobox::State state;

        for (size_t lane = 0; lane < forrobox::ids::lanes.size(); ++lane)
            for (int step : { 0, 3, 7, 12 })
                state.lanes[lane][static_cast<size_t> (step)]
                    = static_cast<std::uint8_t> (40 + 7 * static_cast<int> (lane));

        const auto bytes = forrobox::renderStandardMidiFile (state, 120, 16, nothingMuted);
        const ReadBack read (bytes);

        auto wrongChannel = 0, wrongGate = 0, unmatched = 0;

        // The one case that keeps its own body, because it PAIRS each note-on
        // with its note-off rather than merely reading it. Not
        // MidiMessageSequence::getTimeOfMatchingKeyUp: that needs
        // updateMatchedPairs(), which SYNTHESISES a note-off for a note-on that
        // has none — masking exactly what `unmatched` is counting.
        const auto* sequence = read.track();

        read.forEachNoteOn ([&] (const juce::MidiMessage& noteOn)
        {
            if (noteOn.getChannel() != kSpecChannel)
                ++wrongChannel;

            const auto onTick = noteOn.getTimeStamp();
            const auto note   = noteOn.getNoteNumber();

            const auto* off = std::find_if (sequence->begin(), sequence->end(),
                [note, onTick] (const juce::MidiMessageSequence::MidiEventHolder* candidate)
                {
                    return candidate->message.isNoteOff()
                        && candidate->message.getNoteNumber() == note
                        && candidate->message.getTimeStamp() > onTick;
                });

            if (off == sequence->end())
                ++unmatched;
            else if (static_cast<int> ((*off)->message.getTimeStamp() - onTick) != kSpecGateTicks)
                ++wrongGate;
        });

        checkEqual (read.noteOnCount(), 32, "8 lanes x 4 steps produced 32 note-ons");
        checkEqual (wrongChannel, 0, "every note-on is on channel 10 — PLANNING.md:808");
        checkEqual (unmatched, 0, "every note-on has a note-off after it");
        checkEqual (wrongGate, 0,
                    "every note lasts 19 ticks — 80% of a 24-tick step, PLANNING.md:806 and :808");
    }

    void testGeneralMidiNoteNumbers()
    {
        section ("the GM note numbers are the ones PLANNING.md:815-822 specifies");

        static_assert (gmSpec.size() == forrobox::ids::lanes.size(),
                       "the GM spec table must have one row per lane");

        for (size_t lane = 0; lane < gmSpec.size(); ++lane)
        {
            // The row is keyed by NAME as well as position, so a reordering of
            // ids::lanes cannot silently move a note number onto another lane.
            checkEqual (juce::String (forrobox::ids::lanes[lane]),
                        juce::String (gmSpec[lane].lane),
                        "the GM spec's row " + juce::String (static_cast<int> (lane))
                          + " names the lane ids::lanes does");

            auto state = stateWithHit (static_cast<int> (lane));
            const auto bytes = forrobox::renderStandardMidiFile (state, 120, 16, nothingMuted);
            const ReadBack read (bytes);

            const auto notes = read.noteOnNotes();

            checkEqual (notes.size() == 1 ? notes.front() : -1, gmSpec[lane].note,
                        juce::String (gmSpec[lane].lane) + " is GM note "
                          + juce::String (gmSpec[lane].note)
                          + utf8 (" — PLANNING.md:") + juce::String (gmSpec[lane].planningLine));
        }
    }

    void testADeltaOverAHundredAndTwentySevenTicks()
    {
        section ("a delta of more than 127 ticks survives the round trip");

        // THE POINT OF THE WHOLE FORMAT. A delta is a variable-length quantity:
        // below 128 it is one byte and any grouping bug is invisible, and above
        // it the groups carry a continuation bit that must come most-significant
        // first. Every other case here — and, until the review, every state in
        // the cross-check's matrix — kept every delta under 128, so a writer
        // that emitted the groups the wrong way round passed all of them.
        //
        // Hits at steps 0 and 8: the note-off at tick 19 to the note-on at tick
        // 192 is a delta of 173, which is 0x81 0x2D.
        auto state = stateWithHit (0);
        state.lanes[0][8] = 90;

        const auto bytes = forrobox::renderStandardMidiFile (state, 120, 16, nothingMuted);
        const ReadBack read (bytes);

        const auto ticks = read.noteOnTicks();

        const std::vector<int> expected { 0, 8 * kSpecStepTicks };

        check (ticks == expected,
               "two hits 8 steps apart read back at ticks 0 and 192 — a delta of 173, "
               "which needs two VLQ groups");
    }

    void testStoredStepsAboveTheWindowAreNotExported()
    {
        section ("velocities stored above the step window are not written");

        // `State` keeps all 32 slots when the user narrows to 16 — see
        // `State::lanes` in src/ForroBoxState.h, "narrowing merely stops reading
        // the upper half". So a writer that ignored `steps` would hand the user
        // a file with a second bar they have never heard, and every other case
        // here leaves the upper slots at zero, where the two are the same file.
        forrobox::State state;

        state.lanes[0][0]  = 100;
        state.lanes[0][6]  = 70;
        state.lanes[0][16] = 90;    // beyond a 16-step window
        state.lanes[0][24] = 90;

        const auto bytes = forrobox::renderStandardMidiFile (state, 120, 16, nothingMuted);
        const ReadBack read (bytes);

        const auto ticks = read.noteOnTicks();

        const std::vector<int> expected { 0, 6 * kSpecStepTicks };

        check (ticks == expected,
               "a 16-step export writes only steps 0-15, although the state still holds 32");

        // And the same state at 32 steps DOES carry them, so the check above
        // fails for the right reason: the slots are populated, not empty.
        const auto wide = forrobox::renderStandardMidiFile (state, 120, 32, nothingMuted);
        const ReadBack wideRead (wide);

        checkEqual (wideRead.noteOnCount(), 4, "the same state at 32 steps writes all four hits");
    }

    void testMutedChannelsAreExcluded()
    {
        section ("a muted channel writes no note, and BATERIA takes four lanes with it");

        forrobox::State state;

        for (size_t lane = 0; lane < forrobox::ids::lanes.size(); ++lane)
            state.lanes[lane][0] = 100;

        for (size_t channel = 0; channel < forrobox::ids::channelInfos.size(); ++channel)
        {
            forrobox::ChannelGate muted {};
            muted[channel] = true;

            const auto bytes = forrobox::renderStandardMidiFile (state, 120, 16, muted);
            const ReadBack read (bytes);

            // Derived, not written down: the composite channel covers four
            // lanes and the others one each, so the expected count follows from
            // the mapping rather than from a hand-typed 4.
            const auto silenced = static_cast<int> (
                forrobox::detail::channelToLanes[channel].size());

            checkEqual (read.noteOnCount(),
                        static_cast<int> (forrobox::ids::lanes.size()) - silenced,
                        juce::String ("muting ") + forrobox::ids::channelInfos[channel].id
                          + " removes its " + juce::String (silenced)
                          + " lane(s) — PLANNING.md:810");
        }
    }

    void testTheExportIsTheStoredGridNotThePerformance()
    {
        section ("with swing and CACHAÇA at maximum the ticks are still step x 24");

        // Through a real processor, because that is the path 07-02 will use:
        // the parameters are raised on the APVTS and the pattern is read under
        // lockPatternState, exactly as an export button would do it.
        ForroBoxAudioProcessor processor;

        setParameter (processor, forrobox::ids::swing, 100.0f);
        setParameter (processor, forrobox::ids::cachaca, 100.0f);

        std::vector<std::uint8_t> bytes;

        {
            auto state = processor.lockPatternState();

            for (int step : { 0, 4, 9, 15 })
                state->lanes[0][static_cast<size_t> (step)] = 100;

            bytes = forrobox::renderStandardMidiFile (*state, 120, 16, nothingMuted);
        }

        const ReadBack read (bytes);

        const auto ticks = read.noteOnTicks();

        const std::vector<int> expected { 0, 4 * kSpecStepTicks, 9 * kSpecStepTicks,
                                          15 * kSpecStepTicks };

        checkEqual (static_cast<int> (ticks.size()), 4, "four hits produced four note-ons");
        check (ticks == expected,
               "the ticks are exactly step x 24 with swing and CACHAÇA at maximum — "
               "the file export is the stored grid, and swing is not one of the writer's arguments");
    }

    void testGhostNotesAreAbsentBecauseTheyAreNeverInThePattern()
    {
        section ("no note is written at a step whose stored velocity is zero");

        // WHY THIS PASSES MATTERS. `PLANNING.md:810` says ghost notes are not
        // exported, and `PLANNING.md:584` is the reason: ghosts are never
        // written into the pattern at all, so the writer excludes them by never
        // seeing them. It does not filter them, and a future reader who assumes
        // it does would be wrong — hence this message rather than a bare
        // "ghosts are not exported".
        ForroBoxAudioProcessor processor;

        setParameter (processor,
                      forrobox::ids::channelParam (forrobox::ids::channelInfos[0].id,
                                                   forrobox::ids::ghost),
                      100.0f);

        std::vector<std::uint8_t> bytes;
        std::array<std::uint8_t, forrobox::State::kMaxSteps> stored {};

        {
            auto state = processor.lockPatternState();

            state->lanes[0][0] = 100;
            state->lanes[0][8] = 90;
            stored = state->lanes[0];

            bytes = forrobox::renderStandardMidiFile (*state, 120, 16, nothingMuted);
        }

        const ReadBack read (bytes);

        auto notesAtSilentSteps = 0;

        for (const auto tick : read.noteOnTicks())
        {
            const auto step = tick / kSpecStepTicks;

            if (step >= 0 && step < forrobox::State::kMaxSteps
                && stored[static_cast<size_t> (step)] == 0)
                ++notesAtSilentSteps;
        }

        checkEqual (notesAtSilentSteps, 0,
                    "no note at a step whose stored velocity is 0, with GHOST at maximum — "
                    "which holds because PLANNING.md:584 keeps ghosts out of the pattern, "
                    "not because the writer filters them");
    }

    // ── the export source: what we would write RIGHT NOW ────────────────────

    void testSoloDoesNotReachTheExport()
    {
        section ("a soloed channel does not strip the others from the file");

        // THE BUG THIS EXISTS FOR. `VoiceEngine::resolveChannelSettings` returns
        // `audible`, the resolved mute/SOLO gate, and it is the obvious thing to
        // reach for here. Using it would make soloing ONE channel silently
        // remove every other channel from the exported file while the plugin
        // went on playing them — and nothing about the resulting file looks
        // wrong. `exportMIDI` reads `mute` and never looks at solo.
        ForroBoxAudioProcessor processor;

        setParameter (processor,
                      forrobox::ids::channelParam (forrobox::ids::channelInfos[0].id,
                                                   forrobox::ids::mute),
                      1.0f);
        setParameter (processor,
                      forrobox::ids::channelParam (forrobox::ids::channelInfos[1].id,
                                                   forrobox::ids::solo),
                      1.0f);

        {
            auto state = processor.lockPatternState();

            for (size_t lane = 0; lane < forrobox::ids::lanes.size(); ++lane)
                state->lanes[lane][0] = 100;
        }

        const ReadBack read (forrobox::renderCurrentGroove (processor).bytes);

        // Channel 0 muted takes its one lane. Everything else stays — including
        // the four the composite kit covers, which no channel soloed.
        const auto silenced = static_cast<int> (
            forrobox::detail::channelToLanes[0].size());

        checkEqual (read.noteOnCount(),
                    static_cast<int> (forrobox::ids::lanes.size()) - silenced,
                    "muting one channel and soloing another leaves every unsoloed "
                    "channel in the file — the export reads MUTE, not the resolved gate");
    }

    void testExportFilename()
    {
        section ("the filename is forrobox_<profile>_<bpm>bpm.mid");

        struct Case { const char* profile; float bpm; const char* expected; };

        for (const auto& c : { Case { "campina",   132.0f, "forrobox_campina_132bpm.mid" },
                               Case { "caruaru",   138.0f, "forrobox_caruaru_138bpm.mid" },
                               Case { "petrolina",  96.0f, "forrobox_petrolina_96bpm.mid" } })
        {
            ForroBoxAudioProcessor processor;
            setParameter (processor, forrobox::ids::bpm, c.bpm);

            {
                auto state = processor.lockPatternState();
                state->activeProfile = c.profile;
            }

            checkEqual (forrobox::renderCurrentGroove (processor).filename,
                        juce::String (c.expected),
                        juce::String ("the filename follows the profile and the BPM: ")
                          + c.expected);
        }

        // THE DIRTY FLAG DOES NOT CHANGE IT. `markCustom()` sets `dirty` and
        // never clears `activeProfile`, so an edited CAMPINA exports under
        // campina's name in the prototype too. A "custom" here would be invented
        // behaviour, and this is the check that says so.
        ForroBoxAudioProcessor dirty;
        setParameter (dirty, forrobox::ids::bpm, 132.0f);

        {
            auto state = dirty.lockPatternState();
            state->activeProfile = "campina";
            state->dirty = true;
        }

        checkEqual (forrobox::renderCurrentGroove (dirty).filename,
                    juce::String ("forrobox_campina_132bpm.mid"),
                    "a DIRTY pattern keeps its profile's name — app.js:454 reads "
                    "activeProfile, which markCustom never clears");
    }

    void testAHostileProfileIdCannotEscapeTheFilename()
    {
        section ("a profile id that is not a plain name falls back to custom");

        // NOT hypothetical, and my own plan said it was. `State::readFrom`
        // (src/ForroBoxState.cpp:106-111) preserves an unrecognised profile
        // string VERBATIM and deliberately — "a project saved by a newer build
        // must not lose its profile" — so a host project or preset blob can put
        // anything here. The filename becomes a PATH: `File::getChildFile`
        // resolves `../../x`, which would write outside the temp folder.
        //
        // A jassert does not cover this. Asserts compile out of the Release
        // build the user actually runs, which is the only build that ever opens
        // someone else's project file.
        for (const auto* hostile : { "../../x", "a/b", "CAMPINA", "campina 2", "" })
        {
            ForroBoxAudioProcessor processor;
            setParameter (processor, forrobox::ids::bpm, 132.0f);

            {
                auto state = processor.lockPatternState();
                state->activeProfile = juce::String (juce::CharPointer_UTF8 (hostile));
            }

            checkEqual (forrobox::renderCurrentGroove (processor).filename,
                        juce::String ("forrobox_custom_132bpm.mid"),
                        juce::String ("an unusable profile id falls back to custom rather "
                                      "than reaching the filesystem: ")
                          + juce::String (juce::CharPointer_UTF8 (hostile)));
        }
    }

    void testOlderExportsAreSwept()
    {
        section ("a new export deletes the previous one and never itself");

        // A SCRATCH folder, never the real one. `sweepOldExports` deletes every
        // `.mid` it finds, so pointing this at the shared temp directory would
        // destroy a file a real drag was still handing to a DAW — on a
        // developer's machine, or between two overlapping CI runs.
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("forrobox-sweep-test")
                              .getChildFile (juce::Uuid().toDashedString());
        folder.createDirectory();

        // Three stand-ins for earlier drags, plus the one a drag is about to
        // hand the OS.
        const auto stale = folder.getChildFile ("forrobox_stale_100bpm.mid");
        const auto older = folder.getChildFile ("forrobox_older_110bpm.mid");
        const auto keep  = folder.getChildFile ("forrobox_campina_132bpm.mid");

        for (const auto& f : { stale, older, keep })
            f.replaceWithText ("x");

        forrobox::DragMidiButton::sweepOldExports (folder, keep);

        check (keep.existsAsFile(),
               "the file the sweep was told to keep survives — it is the one the OS "
               "is about to read, and deleting it is the failure the completion "
               "callback was rejected for");
        check (! stale.existsAsFile() && ! older.existsAsFile(),
               "every earlier export is gone, so the folder does not grow with every drag");

        folder.deleteRecursively();
    }

} // namespace

void runMidiExportTests()
{
    testHeaderIsWhatPlanningSpecifies();
    testTempoAndTimeSignatureMetas();
    testEveryNoteIsChannelTenWithAGateOfNineteenTicks();
    testGeneralMidiNoteNumbers();
    testADeltaOverAHundredAndTwentySevenTicks();
    testStoredStepsAboveTheWindowAreNotExported();
    testSoloDoesNotReachTheExport();
    testExportFilename();
    testAHostileProfileIdCannotEscapeTheFilename();
    testOlderExportsAreSwept();
    testMutedChannelsAreExcluded();
    testTheExportIsTheStoredGridNotThePerformance();
    testGhostNotesAreAbsentBecauseTheyAreNeverInThePattern();
}
