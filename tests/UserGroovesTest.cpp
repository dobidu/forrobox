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

juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

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
    TempFolder temp;
    const UserGrooveLibrary::ScopedTestFolder redirect (temp.dir);

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
    TempFolder temp;
    const UserGrooveLibrary::ScopedTestFolder redirect (temp.dir);

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
    TempFolder temp;
    const UserGrooveLibrary::ScopedTestFolder redirect (temp.dir);

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

} // namespace

void runUserGrooveTests()
{
    testFileRoundTrip();
    testStrictRead();
    testLibraryOperations();
    testProcessorUserBank();
    testUserGrooveSurvivesTheHost();
    testCaptureIsWhatIsHeard();
}
