/* ============================================================================
   FORRÓ BOX — the user groove library (18-01)

   The file format, the library's save/rename/overwrite/delete, and the
   processor's USER bank. Every library here lives in its own temp folder; the
   whole suite also runs inside a `ScopedTestFolder` (TestMain.cpp), so nothing
   reaches the developer's own grooves.
============================================================================ */
#include <JuceHeader.h>

#include "ForroBoxState.h"
#include "ParameterIDs.h"
#include "PluginProcessor.h"
#include "UserGrooves.h"

#include "RigStart.h"
#include "TestHarness.h"
#include "TestSuites.h"

#include <memory>

using namespace fbtest;

namespace
{
using forrobox::UserGroove;
using forrobox::UserGrooveLibrary;

/** A fresh, empty folder that is deleted with the scope. */
struct TempFolder
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("forrobox-grooves-" + juce::Uuid().toDashedString());

    ~TempFolder() { dir.deleteRecursively(); }

    int numFiles() const { return dir.getNumberOfChildFiles (juce::File::findFiles); }
};

/** Every slot distinct-ish and the full 0-127 range reached, so a codec that
    dropped a slot, a lane or the top bit cannot round-trip it. */
UserGroove sampleGroove (int seed = 0)
{
    UserGroove groove;
    groove.bpm     = 97 + seed;
    groove.swing   = 37.0f;
    groove.cachaca = 81.5f;

    for (size_t lane = 0; lane < groove.lanes.size(); ++lane)
        for (size_t step = 0; step < groove.lanes[lane].size(); ++step)
            groove.lanes[lane][step] = static_cast<std::uint8_t> ((lane * 32 + step + static_cast<size_t> (seed) * 7) % 128);

    return groove;
}

bool sameContent (const UserGroove& a, const UserGroove& b)
{
    return a.bpm == b.bpm && std::abs (a.swing - b.swing) < 1.0e-4f
        && std::abs (a.cachaca - b.cachaca) < 1.0e-4f && a.lanes == b.lanes;
}

float parameterValue (ForroBoxAudioProcessor& processor, juce::StringRef id)
{
    return processor.getAPVTS().getRawParameterValue (id)->load();
}

void setParameter (ForroBoxAudioProcessor& processor, juce::StringRef id, float value)
{
    auto* parameter = processor.getAPVTS().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

// ── AC-1: the file ───────────────────────────────────────────────────────────

void testFileRoundTrip()
{
    currentSection = "user grooves: file round trip";
    TempFolder temp;
    UserGrooveLibrary library (temp.dir);

    const auto name = utf8 ("Xote da M\xc3\xa3" "e \xe2\x80\x94 v2");
    juce::String id;
    check (library.save (name, sampleGroove(), id).wasOk(), "a valid groove saves");

    UserGrooveLibrary reader (temp.dir);
    const auto* loaded = reader.find (id);

    check (loaded != nullptr, "a second library finds the saved groove");

    if (loaded == nullptr)
        return;

    check (sameContent (*loaded, sampleGroove()), "all 8 lanes x 32 slots of 0-127 and the feel round-trip exactly");
    check (loaded->name == name, "a non-ASCII name round-trips");
    check (temp.dir.getChildFile (id + UserGrooveLibrary::kExtension).existsAsFile(), "the file is <id>.forrogroove");
    check (juce::Uuid (id).toDashedString() == id && ! juce::Uuid (id).isNull(), "the id is a lower-case dashed UUID");
    check (reader.skipped().isEmpty(), "a valid file is not reported");
}

void testStrictRead()
{
    currentSection = "user grooves: strict read";

    const auto id = juce::Uuid().toDashedString();
    auto valid = sampleGroove();
    valid.id = id;
    valid.name = "ok";

    struct Variant { const char* what; std::function<void (juce::XmlElement&)> edit; const char* reason; };

    const std::vector<Variant> variants {
        { "a foreign root",      [] (juce::XmlElement& x) { x.setTagName ("Preset"); },                          "root" },
        { "version 2",           [] (juce::XmlElement& x) { x.setAttribute ("version", 2); },                    "version" },
        { "a missing lane",      [] (juce::XmlElement& x) { x.removeChildElement (x.getChildElement (3), true); }, "missing" },
        { "a duplicate lane",    [] (juce::XmlElement& x) { x.addChildElement (new juce::XmlElement (*x.getChildElement (0))); }, "twice" },
        { "an unknown lane",     [] (juce::XmlElement& x) { x.getChildElement (2)->setAttribute ("id", "cowbell"); }, "unknown lane" },
        { "31 steps",            [] (juce::XmlElement& x) {
              auto* lane = x.getChildElement (1);
              lane->setAttribute ("steps", lane->getStringAttribute ("steps").upToLastOccurrenceOf (" ", false, false)); }, "steps" },
        { "a value of 128",      [] (juce::XmlElement& x) {
              auto* lane = x.getChildElement (0);
              lane->setAttribute ("steps", "128" + lane->getStringAttribute ("steps").fromFirstOccurrenceOf (" ", true, false)); }, "steps" },
        { "a negative value",    [] (juce::XmlElement& x) {
              auto* lane = x.getChildElement (0);
              lane->setAttribute ("steps", "-1" + lane->getStringAttribute ("steps").fromFirstOccurrenceOf (" ", true, false)); }, "steps" },
        { "an id not the file name", [] (juce::XmlElement& x) { x.setAttribute ("id", juce::Uuid().toDashedString()); }, "file name" },
        { "an upper-case id",    [] (juce::XmlElement& x) { x.setAttribute ("id", x.getStringAttribute ("id").toUpperCase()); }, "UUID" },
        { "an empty name",       [] (juce::XmlElement& x) { x.setAttribute ("name", "   "); },                    "name" },
        { "no bpm",              [] (juce::XmlElement& x) { x.removeAttribute ("bpm"); },                        "bpm" },
        { "a stray element",     [] (juce::XmlElement& x) { x.createNewChildElement ("voice"); },               "unexpected" },
        { "an unknown attribute", [] (juce::XmlElement& x) { x.setAttribute ("colour", "red"); },               "unknown attribute" },
        { "an unknown lane attribute", [] (juce::XmlElement& x) { x.getChildElement (4)->setAttribute ("pan", 0); }, "unknown lane attribute" },
        { "content inside a lane", [] (juce::XmlElement& x) { x.getChildElement (5)->createNewChildElement ("evil"); }, "content inside" },
        { "a bpm that is not a number", [] (juce::XmlElement& x) { x.setAttribute ("bpm", "fast"); }, "not a number" },
        { "a swing that is not a number", [] (juce::XmlElement& x) { x.setAttribute ("swing", "abc"); }, "not a number" },
    };

    check (forrobox::fromXml (*forrobox::toXml (valid), id).groove.has_value(), "the unedited file parses");

    for (const auto& variant : variants)
    {
        auto xml = forrobox::toXml (valid);
        variant.edit (*xml);
        const auto parsed = forrobox::fromXml (*xml, id);

        check (! parsed.groove.has_value(), juce::String ("refused: ") + variant.what);
        check (parsed.error.contains (variant.reason),
               juce::String ("the reason names it: ") + variant.what + " -> '" + parsed.error + "'");
    }

    // CLAMPED, not refused: the feel out of range is the nearest playable value.
    {
        auto xml = forrobox::toXml (valid);
        xml->setAttribute ("bpm", 999);
        xml->setAttribute ("swing", -5.0);
        xml->setAttribute ("cachaca", 250.0);
        const auto parsed = forrobox::fromXml (*xml, id);

        check (parsed.groove.has_value(), "an out-of-range feel still loads");

        if (parsed.groove.has_value())
        {
            checkEqual (parsed.groove->bpm, forrobox::ids::kMaxBpm, "bpm clamps to the parameter's maximum");
            checkEqual (parsed.groove->swing, 0.0f, "swing clamps to 0");
            checkEqual (parsed.groove->cachaca, forrobox::ids::kPercentMax, "cachaca clamps to 100");
        }
    }

    // IN A FOLDER: each bad file is skipped and reported, and its valid
    // neighbour still loads.
    {
        TempFolder temp;
        temp.dir.createDirectory();
        check (forrobox::toXml (valid)->writeTo (temp.dir.getChildFile (id + UserGrooveLibrary::kExtension)), "wrote the valid file");

        for (const auto& variant : variants)
        {
            auto bad = sampleGroove();
            bad.id = juce::Uuid().toDashedString();
            bad.name = "bad";
            auto xml = forrobox::toXml (bad);
            variant.edit (*xml);
            xml->writeTo (temp.dir.getChildFile (bad.id + UserGrooveLibrary::kExtension));
        }

        temp.dir.getChildFile (juce::Uuid().toDashedString() + UserGrooveLibrary::kExtension).replaceWithText ("<not xml");
        temp.dir.getChildFile ("notes.txt").replaceWithText ("not a groove, not our extension");

        UserGrooveLibrary library (temp.dir);
        checkEqual (static_cast<int> (library.grooves().size()), 1, "only the valid file loads");
        checkEqual (library.skipped().size(), static_cast<int> (variants.size()) + 1,
                    "every malformed .forrogroove is reported; other files are ignored");
        check (library.find (id) != nullptr, "the valid neighbour is the one that loaded");
    }
}

// ── AC-2: save, rename, overwrite, delete ────────────────────────────────────

void testLibraryOperations()
{
    currentSection = "user grooves: library operations";
    TempFolder temp;
    UserGrooveLibrary library (temp.dir);

    check (library.grooves().empty(), "a folder that does not exist is an empty library");
    check (! temp.dir.exists(), "reading does not create the folder");

    juce::String idB, idA, idC;
    check (library.save ("b", sampleGroove (1), idB).wasOk(), "saved b");
    check (library.save ("A", sampleGroove (2), idA).wasOk(), "saved A");
    check (library.save ("  c  ", sampleGroove (3), idC).wasOk(), "saved c (trimmed)");

    const auto names = [&library]
    {
        juce::StringArray list;
        for (const auto& g : library.grooves())
            list.add (g.name);
        return list.joinIntoString (",");
    };

    checkEqual (names(), juce::String ("A,b,c"), "the bank is sorted by name, ignoring case");
    checkEqual (temp.numFiles(), 3, "one file per groove");

    // RENAME keeps the id and the file.
    check (library.rename (idB, "Z").wasOk(), "renamed b to Z");
    checkEqual (names(), juce::String ("A,c,Z"), "a rename re-sorts");
    check (library.find (idB) != nullptr && library.find (idB)->name == "Z", "the rename kept the id");
    check (temp.dir.getChildFile (idB + UserGrooveLibrary::kExtension).existsAsFile(), "and the file name");
    checkEqual (temp.numFiles(), 3, "a rename adds no file");

    // OVERWRITE replaces content, keeps id and name.
    check (library.overwrite (idC, sampleGroove (9)).wasOk(), "overwrote c");
    check (library.find (idC) != nullptr && sameContent (*library.find (idC), sampleGroove (9)), "the content is the new one");
    check (library.find (idC) != nullptr && library.find (idC)->name == "c", "the name is kept");

    // THE GENERATION moves when the bank does, and only then.
    {
        const auto before = library.generation();
        library.rescan();
        checkEqual (library.generation(), before, "a rescan of an unchanged folder keeps the generation");
        check (library.rename (idC, "c2").wasOk() && library.generation() != before, "a rename moves it");
        check (library.rename (idC, "c").wasOk(), "and back");
    }

    // A second library on the same folder sees every change.
    UserGrooveLibrary other (temp.dir);
    checkEqual (static_cast<int> (other.grooves().size()), 3, "a second instance sees three grooves");

    // DELETE removes the file.
    check (library.remove (idA).wasOk(), "deleted A");
    check (! temp.dir.getChildFile (idA + UserGrooveLibrary::kExtension).exists(), "the file is gone");
    other.rescan();
    check (other.find (idA) == nullptr, "a second instance sees the delete after rescan");

    // REFUSALS write nothing.
    const auto before = temp.numFiles();
    juce::String unused;
    check (library.save ("", sampleGroove(), unused).failed(), "an empty name fails");
    check (library.save ("   ", sampleGroove(), unused).failed(), "a blank name fails");
    check (library.save (juce::String::repeatedString ("x", forrobox::kMaxUserGrooveNameLength + 1), sampleGroove(), unused).failed(),
           "a name over the limit fails");
    check (library.save (juce::String::repeatedString ("x", forrobox::kMaxUserGrooveNameLength), sampleGroove(), unused).wasOk(),
           "a name at the limit saves");
    check (library.save ("two\nlines", sampleGroove(), unused).failed(), "a name with a newline fails");
    check (library.rename (idC, "").failed(), "renaming to an empty name fails");
    check (library.rename ("no-such-id", "x").failed(), "renaming an unknown id fails");
    check (library.overwrite ("no-such-id", sampleGroove()).failed(), "overwriting an unknown id fails");
    check (library.remove ("no-such-id").failed(), "deleting an unknown id fails");
    checkEqual (temp.numFiles(), before + 1, "the refusals wrote nothing (only the at-limit save did)");
    check (library.find (idC) != nullptr && library.find (idC)->name == "c", "a refused rename left the name alone");
}

// ── AC-3 / AC-4: the processor's USER bank ───────────────────────────────────

struct Identity { juce::String profile, groove; bool dirty; };

Identity identityOf (ForroBoxAudioProcessor& processor)
{
    auto handle = processor.lockPatternState();
    return { handle->activeProfile, handle->activeGroove, handle->dirty };
}

void drawLanes (ForroBoxAudioProcessor& processor, int seed)
{
    auto handle = processor.lockPatternState();
    handle->lanes = sampleGroove (seed).lanes;
    handle->dirty = true;
}

void testProcessorUserBank()
{
    currentSection = "user grooves: the processor's USER bank";
    const forrobox::test::ScopedGrooveFolder temp;

    ForroBoxAudioProcessor processor;

    // 32 steps, so every drawn slot is heard and saved as drawn.
    setParameter (processor, forrobox::ids::steps, 1.0f);

    // SAVE: the state becomes the new groove, pristine.
    drawLanes (processor, 4);
    setParameter (processor, forrobox::ids::bpm, 101.0f);
    setParameter (processor, forrobox::ids::swing, 12.0f);
    setParameter (processor, forrobox::ids::cachaca, 64.0f);
    check (processor.saveUserGroove ("Meu Xote").wasOk(), "saveUserGroove succeeds");

    const auto saved = identityOf (processor);
    checkEqual (saved.profile, juce::String (forrobox::ids::userProfile), "the state names the USER profile");
    check (UserGrooveLibrary::shared().find (saved.groove) != nullptr, "and the new groove's id");
    check (! saved.dirty, "and is pristine: every channel on PAT 01, nothing parked");
    checkEqual (processor.activeGrooveName(), juce::String ("Meu Xote"), "the screen names it");
    check (temp.dir.getChildFile (saved.groove + UserGrooveLibrary::kExtension).existsAsFile(),
           "it was written into the redirected folder");

    // A second groove, different everywhere.
    drawLanes (processor, 5);
    setParameter (processor, forrobox::ids::bpm, 140.0f);
    check (processor.saveUserGroove ("Outro").wasOk(), "saved a second groove");
    const auto second = identityOf (processor).groove;

    // LOAD the first, with the character changed in between.
    const auto timbreBefore = 1.0f - parameterValue (processor, forrobox::ids::timbre);
    setParameter (processor, forrobox::ids::timbre, timbreBefore);
    const auto muteId = forrobox::ids::channelParam ("bateria", forrobox::ids::mute);
    const auto muteBefore = 1.0f - parameterValue (processor, muteId);
    setParameter (processor, muteId, muteBefore);
    processor.selectPatternSlot (0, 3);

    check (processor.loadUserGroove (saved.groove), "loadUserGroove finds the first groove");
    {
        auto handle = processor.lockPatternState();
        check (handle->lanes == sampleGroove (4).lanes, "its lanes are loaded");
        checkEqual (handle->getPatternSlot (0), forrobox::State::kMinPatternSlot, "the slots go back to PAT 01");
        check (! handle->dirty, "a loaded groove is pristine");
    }
    checkEqual (parameterValue (processor, forrobox::ids::bpm), 101.0f, "its bpm is loaded");
    checkEqual (parameterValue (processor, forrobox::ids::swing), 12.0f, "its swing is loaded");
    checkEqual (parameterValue (processor, forrobox::ids::cachaca), 64.0f, "its cachaca is loaded");
    checkEqual (parameterValue (processor, forrobox::ids::timbre), timbreBefore, "the timbre is untouched");
    checkEqual (parameterValue (processor, muteId), muteBefore, "the mutes are untouched");
    check (! processor.loadUserGroove ("no-such-id"), "an unknown id loads nothing");

    // CYCLE, clamped, in name order: "Meu Xote", "Outro".
    processor.cycleGroove (-1);
    checkEqual (identityOf (processor).groove, saved.groove, "the left arrow stops at the first");
    processor.cycleGroove (+1);
    checkEqual (identityOf (processor).groove, second, "the right arrow moves to the next by name");
    checkEqual (processor.activeGrooveName(), juce::String ("Outro"), "and the screen follows");
    processor.cycleGroove (+1);
    checkEqual (identityOf (processor).groove, second, "the right arrow stops at the last");

    // A SAVE OVER OTHER SLOTS is that groove plus content it does not carry.
    processor.selectPatternSlot (0, 2);
    drawLanes (processor, 6);
    check (processor.saveUserGroove ("Com slots").wasOk(), "saved with zabumba on PAT 02");
    check (identityOf (processor).dirty, "a save over other slots leaves the state dirty");
    checkEqual (processor.patternSlotOf (0), 2, "and saving never moves or wipes a slot");

    // OVERWRITE the groove the state names.
    processor.selectPatternSlot (0, 1);
    check (processor.loadUserGroove (second), "back on Outro");
    drawLanes (processor, 7);
    check (processor.overwriteUserGroove().wasOk(), "overwriteUserGroove succeeds");
    check (UserGrooveLibrary::shared().find (second) != nullptr
               && UserGrooveLibrary::shared().find (second)->lanes == sampleGroove (7).lanes,
           "the file now holds the new lanes");
    check (! identityOf (processor).dirty, "and the state is pristine again");

    // RENAME through the processor reaches the screen.
    check (processor.renameUserGroove (second, "Outro 2").wasOk(), "renameUserGroove succeeds");
    checkEqual (processor.activeGrooveName(), juce::String ("Outro 2"), "the screen shows the new name");

    // Not under USER: overwrite refuses.
    processor.loadProfile (forrobox::allProfiles()[0]);
    check (processor.overwriteUserGroove().failed(), "overwrite refuses when no user groove is playing");
}

void testUserGrooveSurvivesTheHost()
{
    currentSection = "user grooves: host round trip and absence";
    const forrobox::test::ScopedGrooveFolder temp;

    ForroBoxAudioProcessor donor;
    setParameter (donor, forrobox::ids::steps, 1.0f);
    drawLanes (donor, 8);
    check (donor.saveUserGroove ("Guardado").wasOk(), "saved");
    const auto id = identityOf (donor).groove;

    juce::MemoryBlock blob;
    donor.getStateInformation (blob);

    ForroBoxAudioProcessor restored;
    restored.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));

    const auto back = identityOf (restored);
    checkEqual (back.profile, juce::String (forrobox::ids::userProfile), "activeProfile survives the host");
    checkEqual (back.groove, id, "activeGroove survives the host");
    check (! back.dirty, "and so does pristine");
    check (restored.lockPatternState()->lanes == sampleGroove (8).lanes, "and the lanes");
    checkEqual (restored.activeGrooveName(), juce::String ("Guardado"), "the name resolves through the library");

    // ABSENT: another machine, or deleted behind the state's back.
    juce::String otherId;
    check (UserGrooveLibrary::shared().save ("Aaa primeiro", sampleGroove (9), otherId).wasOk(), "a second groove exists");
    check (UserGrooveLibrary::shared().remove (id).wasOk(), "the named groove is removed from the library");

    check (restored.lockPatternState()->lanes == sampleGroove (8).lanes, "its lanes still play");
    checkEqual (restored.activeGrooveName(), juce::String(), "an id the library lacks shows no name");
    restored.cycleGroove (+1);
    checkEqual (identityOf (restored).groove, otherId, "either arrow then loads the bank's first groove");

    // DELETE THE ACTIVE groove through the processor.
    check (restored.deleteUserGroove (otherId).wasOk(), "deleteUserGroove succeeds");
    check (identityOf (restored).dirty, "deleting the active groove marks the state dirty");
    check (restored.deleteUserGroove (otherId).failed(), "deleting it again fails");

    // AN EMPTY BANK: the arrows do nothing (and, in a Debug tree, assert nothing).
    restored.cycleGroove (+1);
    restored.cycleGroove (-1);
    checkEqual (identityOf (restored).groove, otherId, "with no user grooves the arrows leave the state alone");
}

void testCaptureIsWhatIsHeard()
{
    currentSection = "user grooves: a narrow window saves what is heard";
    const forrobox::test::ScopedGrooveFolder temp;

    ForroBoxAudioProcessor processor;

    // 16 steps, with slots 16-31 still holding something else.
    setParameter (processor, forrobox::ids::steps, 0.0f);
    checkEqual (processor.currentStepWindow(), 16, "the window is 16");
    drawLanes (processor, 3);

    check (processor.saveUserGroove ("Dezesseis").wasOk(), "saved at 16 steps");
    const auto* saved = UserGrooveLibrary::shared().find (identityOf (processor).groove);
    check (saved != nullptr, "found it");

    if (saved == nullptr)
        return;

    auto tiled = true;
    for (const auto& lane : saved->lanes)
        for (size_t i = 16; i < lane.size(); ++i)
            tiled &= lane[i] == lane[i - 16];

    check (tiled, "slots 16-31 repeat the heard bar, not the stale second bar");
    check (saved->lanes[0][0] == sampleGroove (3).lanes[0][0] && saved->lanes[0][16] != sampleGroove (3).lanes[0][16],
           "the first bar is the drawn one, and the stale second bar is gone");

    // Only groove files in the folder: no temporary left behind.
    checkEqual (temp.dir.getNumberOfChildFiles (juce::File::findFiles, "*"), 1, "a save leaves exactly one file");
}

// ── 18-02 AC-4: every instance told ──────────────────────────────────────────

void testInstancesAreTold()
{
    currentSection = "user grooves: every instance told";
    const forrobox::test::ScopedGrooveFolder temp;
    auto& library = UserGrooveLibrary::shared();

    const auto listenersBefore = library.numListenersForTest();

    {
        ForroBoxAudioProcessor a, b;
        checkEqual (library.numListenersForTest(), listenersBefore + 2, "each processor registers");

        setParameter (a, forrobox::ids::steps, 1.0f);
        setParameter (b, forrobox::ids::steps, 1.0f);

        drawLanes (a, 1);
        check (a.saveUserGroove ("X").wasOk(), "A saves X");
        const auto x = identityOf (a).groove;
        check (b.loadUserGroove (x), "B loads X");
        check (! identityOf (b).dirty, "B is pristine on X");

        // A OVERWRITES X: B's lanes are no longer X.
        drawLanes (a, 2);
        check (a.overwriteUserGroove().wasOk(), "A overwrites X");
        check (! identityOf (a).dirty, "A, the writer, re-adopts X and is pristine");
        check (identityOf (b).dirty, "B stops claiming X once A overwrote it");
        check (b.lockPatternState()->lanes == sampleGroove (1).lanes, "and B still plays what it had");

        // A RENAMES X: B keeps it, under the new name.
        check (b.loadUserGroove (x), "B reloads X");
        check (a.renameUserGroove (x, "Y").wasOk(), "A renames X to Y");
        check (! identityOf (b).dirty, "a rename leaves B pristine");
        checkEqual (b.activeGrooveName(), juce::String ("Y"), "and B shows the new name");
        check (b.activeUserGroove().has_value() && b.activeUserGroove()->name == "Y",
               "B's active user groove is Y");
        check (! a.activeUserGroove().has_value() || a.activeUserGroove()->id == x, "A's is the same groove");

        // A DELETES it: both stop claiming it.
        check (a.deleteUserGroove (x).wasOk(), "A deletes Y");
        check (identityOf (a).dirty, "A is dirty after its own delete");
        check (identityOf (b).dirty, "and so is B");
        check (! b.activeUserGroove().has_value(), "B has no active user groove any more");

        // Under a regional profile there is none.
        a.loadProfile (forrobox::allProfiles()[1]);
        check (! a.activeUserGroove().has_value(), "a regional state has no active user groove");
    }

    checkEqual (library.numListenersForTest(), listenersBefore, "destroyed processors leave the list");

    // A WRITE FROM INSIDE A NOTIFICATION is refused.
    struct Reentrant final : UserGrooveLibrary::Listener
    {
        juce::Result nested = juce::Result::ok();
        int calls = 0;

        void userGrooveChanged (const UserGrooveLibrary::Change&) override
        {
            if (++calls > 1)
                return;

            juce::String unused;
            nested = UserGrooveLibrary::shared().save ("nested", sampleGroove(), unused);
        }
    } reentrant;

    library.addListener (&reentrant);
    {
        const ExpectAssertions expected (1, "UserGrooves.cpp", "a nested groove write is refused");
        juce::String id;
        check (library.save ("outer", sampleGroove(), id).wasOk(), "the outer save succeeds");
    }
    library.removeListener (&reentrant);

    check (reentrant.nested.failed(), "the nested save is refused");
    library.rescan();
    checkEqual (static_cast<int> (library.grooves().size()), 1, "and wrote nothing");
}


// ── 19-01: groove files ──────────────────────────────────────────────────────

juce::File scratchFile (const juce::File& dir, const juce::String& name)
{
    dir.createDirectory();
    return dir.getChildFile (name);
}

void testFormatDocExample()
{
    currentSection = "groove files: the documented example parses";

    const juce::File doc { juce::String::fromUTF8 (FORROBOX_GROOVE_FORMAT_DOC) };
    check (doc.existsAsFile(), "docs/groove-format.md is where the build says");

    // Line endings normalised: a Windows checkout has CRLF (CI found it).
    const auto text = doc.loadFileAsString().replace ("\r\n", "\n");
    const auto start = text.indexOf ("```xml");
    const auto xml = text.substring (start + 6).upToFirstOccurrenceOf ("```", false, false).trim();
    check (start >= 0 && xml.startsWith ("<?xml"), "the page carries an XML example");
    check (text.contains ("</ForroBoxGroove>\n```"), "whose code block closes on its own line, so the page renders");

    TempFolder temp;
    const auto file = scratchFile (temp.dir, "example.forrogroove");
    file.replaceWithText (xml);

    const auto parsed = forrobox::readGrooveFile (file, {});
    check (parsed.groove.has_value(), "the documented example is a file the real reader accepts ("
                                          + parsed.error + ")");

    if (parsed.groove.has_value())
    {
        checkEqual (parsed.groove->name, juce::String ("Xote da Feira"), "with the example's name");
        checkEqual (parsed.groove->bpm, 96, "and its tempo");
    }
}

void testExportImportRoundTrip()
{
    currentSection = "groove files: export and import round trip";

    TempFolder outbox;
    juce::File userFile, regionalFile;
    juce::String userId;

    {
        const forrobox::test::ScopedGrooveFolder sender;
        ForroBoxAudioProcessor processor;
        setParameter (processor, forrobox::ids::steps, 1.0f);

        drawLanes (processor, 1);
        setParameter (processor, forrobox::ids::bpm, 111.0f);
        check (processor.saveUserGroove (juce::String::fromUTF8 ("Meu Bai\xc3\xa3o")).wasOk(), "a user groove to export");
        userId = identityOf (processor).groove;

        check (processor.suggestedExportFileName().endsWith (UserGrooveLibrary::kExtension),
               "the suggested file name carries the extension");

        userFile = scratchFile (outbox.dir, "renamed by the sender.forrogroove");
        check (processor.exportGroove (userFile).wasOk(), "a pristine user groove exports");
        checkEqual (processor.grooveForExport().id, userId, "under its library id");

        // A FEEL CHANGE never sets `dirty`, but the export is no longer that
        // groove — a fresh id. /code-review.
        setParameter (processor, forrobox::ids::bpm, 150.0f);
        check (processor.grooveForExport().id != userId, "a changed tempo exports under a fresh id");
        setParameter (processor, forrobox::ids::bpm, 111.0f);
        checkEqual (processor.grooveForExport().id, userId, "and the library id again once it matches");

        // AN EDITED REGIONAL GROOVE AT 16 STEPS: a new groove, tiled as heard.
        processor.loadProfile (forrobox::allProfiles()[0]);
        setParameter (processor, forrobox::ids::steps, 0.0f);
        drawLanes (processor, 3);
        regionalFile = scratchFile (outbox.dir, "regional.forrogroove");
        check (processor.exportGroove (regionalFile).wasOk(), "an edited regional groove exports");
    }

    const forrobox::test::ScopedGrooveFolder receiver;
    ForroBoxAudioProcessor processor;
    auto& library = UserGrooveLibrary::shared();

    const auto first = processor.importGrooves ({ userFile, regionalFile });
    check (first.size() == 2 && first[0].result == UserGrooveLibrary::ImportOutcome::Result::added
               && first[1].result == UserGrooveLibrary::ImportOutcome::Result::added,
           "both arrive in an empty library as added");

    const auto* user = library.find (userId);
    check (user != nullptr, "the user groove keeps its id across the trip, whatever its file was called");

    if (user != nullptr)
    {
        check (user->lanes == sampleGroove (1).lanes, "and its lanes");
        checkEqual (user->bpm, 111, "and its tempo");
        checkEqual (user->name, juce::String::fromUTF8 ("Meu Bai\xc3\xa3o"), "and its name");
    }

    if (first.size() == 2)
    {
        const auto* regional = library.find (first[1].id);
        check (first[1].id != userId && regional != nullptr, "the regional groove arrives under a fresh id");

        if (regional != nullptr)
        {
            auto tiled = true;
            for (const auto& lane : regional->lanes)
                for (size_t i = 16; i < lane.size(); ++i)
                    tiled &= lane[i] == lane[i - 16];

            check (tiled, "with its lanes as heard at 16 steps");
            checkEqual (regional->name,
                        juce::String (juce::CharPointer_UTF8 (forrobox::allProfiles()[0].defaultGroove().name)),
                        "named as the screen named it");
        }
    }

    const auto again = processor.importGrooves ({ userFile });
    check (again.size() == 1 && again[0].result == UserGrooveLibrary::ImportOutcome::Result::identical,
           "importing the same file again is identical, and changes nothing");
    checkEqual (static_cast<int> (library.grooves().size()), 2, "still two grooves");
}

void testImportDuplicates()
{
    currentSection = "groove files: the duplicate rules";

    const forrobox::test::ScopedGrooveFolder folder;
    TempFolder inbox;
    auto& library = UserGrooveLibrary::shared();
    using Result = UserGrooveLibrary::ImportOutcome::Result;

    juce::String idX;
    check (library.save ("X", sampleGroove (1), idX).wasOk(), "X in the library");

    const auto fileWith = [&inbox] (const juce::String& id, const juce::String& name, int seed, const char* fileName)
    {
        auto groove = sampleGroove (seed);
        groove.id = id;
        groove.name = name;
        const auto file = scratchFile (inbox.dir, fileName);
        forrobox::writeGrooveFile (groove, file);
        return file;
    };

    const auto same = fileWith (idX, "X", 1, "same.forrogroove");
    const auto differs = fileWith (idX, "X", 2, "differs.forrogroove");
    const auto differsAgain = fileWith (idX, "X", 3, "differs-again.forrogroove");

    struct Counter final : UserGrooveLibrary::Listener
    {
        int calls = 0;
        void userGrooveChanged (const UserGrooveLibrary::Change&) override { ++calls; }
    } counter;

    library.addListener (&counter);
    const auto outcomes = library.importFiles ({ same, differs, differsAgain, inbox.dir.getChildFile ("missing.forrogroove") });
    library.removeListener (&counter);

    check (outcomes.size() == 4, "one outcome per file");

    if (outcomes.size() == 4)
    {
        check (outcomes[0].result == Result::identical, "the same groove is identical");
        check (outcomes[1].result == Result::copied && outcomes[1].id != idX, "a different one with X's id is a copy, new id");
        checkEqual (outcomes[1].name, juce::String ("X (2)"), "named X (2)");
        check (outcomes[2].result == Result::copied, "and another");
        checkEqual (outcomes[2].name, juce::String ("X (3)"), "named X (3)");
        check (outcomes[3].result == Result::refused && outcomes[3].reason.isNotEmpty(), "a missing file is refused with a reason");
    }

    check (library.find (idX) != nullptr && library.find (idX)->lanes == sampleGroove (1).lanes,
           "X itself is untouched");
    checkEqual (static_cast<int> (library.grooves().size()), 3, "X, X (2), X (3)");
    checkEqual (counter.calls, 1, "one notification for the whole batch");

    // A 24-CHARACTER NAME is trimmed so " (2)" fits.
    const auto longName = juce::String::repeatedString ("W", forrobox::kMaxUserGrooveNameLength);
    juce::String idLong;
    check (library.save (longName, sampleGroove (4), idLong).wasOk(), "a 24-character groove");
    const auto longCopy = library.importFiles ({ fileWith (idLong, longName, 5, "long.forrogroove") });
    check (longCopy.size() == 1 && longCopy[0].result == Result::copied, "its different twin is a copy");

    if (longCopy.size() == 1)
    {
        checkEqual (longCopy[0].name.length(), forrobox::kMaxUserGrooveNameLength, "still within the name limit");
        check (longCopy[0].name.endsWith (" (2)"), "and ends in (2): " + longCopy[0].name);
    }
}

void testHostileFiles()
{
    currentSection = "groove files: hostile, damaged and foreign files";

    const forrobox::test::ScopedGrooveFolder folder;
    TempFolder inbox;
    auto& library = UserGrooveLibrary::shared();

    juce::String keepId;
    check (library.save ("Keep", sampleGroove (1), keepId).wasOk(), "one groove to keep");

    auto valid = sampleGroove (2);
    valid.id = juce::Uuid().toDashedString();
    valid.name = "Valid";
    const auto validXml = forrobox::toXml (valid)->toString();

    std::vector<std::pair<juce::String, juce::File>> cases;
    const auto add = [&] (const juce::String& what, const juce::String& name, std::function<void (const juce::File&)> make)
    {
        const auto file = scratchFile (inbox.dir, name);
        make (file);
        cases.push_back ({ what, file });
    };

    add ("over 64 KB", "big.forrogroove", [&] (const juce::File& f)
         { f.replaceWithText (validXml + "<!--" + juce::String::repeatedString ("x", 70000) + "-->"); });
    add ("empty", "empty.forrogroove", [] (const juce::File& f) { f.create(); });
    add ("random bytes", "random.forrogroove", [] (const juce::File& f)
    {
        juce::MemoryBlock block (4096);
        juce::Random random (42);
        random.fillBitsRandomly (block.getData(), block.getSize());
        f.replaceWithData (block.getData(), block.getSize());
    });
    add ("a PNG", "picture.forrogroove", [] (const juce::File& f)
    {
        const unsigned char png[] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 13, 'I', 'H', 'D', 'R' };
        f.replaceWithData (png, sizeof (png));
    });
    add ("a directory", "folder.forrogroove", [] (const juce::File& f) { f.createDirectory(); });
    add ("a missing path", "missing.forrogroove", [] (const juce::File&) {});
    add ("UTF-16 text", "utf16.forrogroove", [&] (const juce::File& f) { f.replaceWithText (validXml, true, true); });
    add ("another XML root", "other.forrogroove", [] (const juce::File& f) { f.replaceWithText ("<Preset version=\"1\"/>"); });
    add ("version 2", "v2.forrogroove", [&] (const juce::File& f)
         { f.replaceWithText (validXml.replace ("version=\"1\"", "version=\"2\"")); });
    add ("a DOCTYPE entity bomb", "bomb.forrogroove", [] (const juce::File& f)
    {
        juce::String bomb = "<?xml version=\"1.0\"?>\n<!DOCTYPE g [\n <!ENTITY a \"aaaaaaaaaa\">\n";
        for (int i = 1; i < 10; ++i)
            bomb << " <!ENTITY " << juce::String::charToString ((juce::juce_wchar) ('a' + i)) << " \""
                 << juce::String::repeatedString ("&" + juce::String::charToString ((juce::juce_wchar) ('a' + i - 1)) + ";", 10)
                 << "\">\n";
        bomb << "]>\n<ForroBoxGroove version=\"1\" name=\"&j;\"/>";
        f.replaceWithText (bomb);
    });
    add ("not XML", "text.forrogroove", [] (const juce::File& f) { f.replaceWithText ("xote, baiao, arrasta-pe"); });
    add ("deep nesting under 64 KB", "deep.forrogroove", [&] (const juce::File& f)
    {
        // ~20,000 levels: what JUCE's recursive parser would have to descend.
        f.replaceWithText (validXml.upToFirstOccurrenceOf ("<lane", false, false)
                           + juce::String::repeatedString ("<a>", 20000));
    });

    const auto before = library.grooves().size();
    const auto started = juce::Time::getMillisecondCounterHiRes();

    for (const auto& [what, file] : cases)
    {
        const auto outcome = library.importFiles ({ file });
        check (outcome.size() == 1 && outcome[0].result == UserGrooveLibrary::ImportOutcome::Result::refused
                   && outcome[0].reason.isNotEmpty(),
               "refused with a reason: " + what + (outcome.empty() ? juce::String() : " (" + outcome[0].reason + ")"));
    }

    check (juce::Time::getMillisecondCounterHiRes() - started < 1000.0, "and all of them in under a second");

    // A UTF-8 BOM is NOT hostile: editors write one, and the reader before
    // 19-01 accepted it. /code-review.
    {
        const auto file = scratchFile (inbox.dir, "bom.forrogroove");
        juce::MemoryOutputStream bytes;
        bytes.write ("\xef\xbb\xbf", 3);   // the UTF-8 byte-order mark
        bytes.writeString (validXml);
        file.replaceWithData (bytes.getData(), bytes.getDataSize() - 1);   // without writeString's NUL
        juce::MemoryBlock raw;
        check (file.loadFileAsData (raw) && raw.getSize() > 3 && static_cast<unsigned char> (raw[0]) == 0xef,
               "a file that starts with a BOM written");
        const auto parsed = forrobox::readGrooveFile (file, {});
        check (parsed.groove.has_value(), "a UTF-8 file with a byte-order mark still reads (" + parsed.error + ")");
    }

    // The bomb and the nesting are stopped BEFORE the parser, by their own
    // rules — not by luck of whatever the parser makes of them.
    for (const auto& [what, file] : cases)
    {
        const auto outcome = library.importFiles ({ file });

        if (what.contains ("DOCTYPE"))
            check (! outcome.empty() && outcome[0].reason.contains ("DOCTYPE"),
                   "the entity bomb is refused for its DOCTYPE, before parsing");

        if (what.contains ("nesting"))
            check (! outcome.empty() && outcome[0].reason.contains ("too many elements"),
                   "deep nesting is refused by the element count, before parsing");
    }
    checkEqual (library.grooves().size(), before, "nothing was written");
    check (library.find (keepId) != nullptr, "the library is unchanged");

    // IN THE LIBRARY FOLDER, the scan refuses them the same way and keeps the rest.
    auto planted = 0;
    for (const auto& [what, file] : cases)
        if (file.existsAsFile() && file.copyFileTo (library.folder().getChildFile (juce::Uuid().toDashedString()
                                                                                    + UserGrooveLibrary::kExtension)))
            ++planted;

    library.rescan();
    checkEqual (library.grooves().size(), before, "a scan over planted files still holds exactly the valid grooves");
    checkEqual (library.skipped().size(), planted, "and reports each planted file");
}
} // namespace

void runUserGrooveTests()
{
    testFileRoundTrip();
    testStrictRead();
    testLibraryOperations();
    testProcessorUserBank();
    testUserGrooveSurvivesTheHost();
    testCaptureIsWhatIsHeard();
    testInstancesAreTold();
    testFormatDocExample();
    testExportImportRoundTrip();
    testImportDuplicates();
    testHostileFiles();
}
