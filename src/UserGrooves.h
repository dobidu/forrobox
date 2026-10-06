#pragma once

/*  The user's own grooves (18-01) — "save the current groove under a name".

    WHAT ONE IS. A built-in groove's shape: a name, the feel (bpm, swing,
    cachaça) and the eight lanes. Not the timbre, mutes, mixer, voices, IR or
    the other seven pattern slots — the user's decision at Phase 18 planning,
    and the same line 09-03 drew for a built-in groove: those are the profile's
    character, and a groove inside it does not change them.

    THE LANES AT THE STATE'S RESOLUTION. A built-in pattern is 16 characters of
    '.' and '1'-'9'; a grid the user has drawn is 32 slots of 0-127, and
    squeezing it into the built-in notation would round every velocity it
    touched. So the file stores exactly what `State::lanes` holds.

    ONE FILE PER GROOVE, `<id>.forrogroove`, in a `grooves` folder beside the
    settings file — the directory `Settings` already resolves per platform, so
    the Linux `~/Forro Box/` trap it documents is not repeated here. The id is a
    UUID and the file name, so a rename rewrites one file and two processes
    saving at once can never pick the same name. Phase 19 imports and exports
    this same format.

    READ STRICTLY, `decodePattern`'s rule: a file that is wrong in any way is
    skipped and reported, never half-loaded. A groove that loads with one lane
    silently missing is the failure hardest to notice. The feel is clamped to
    the parameters' ranges, as a project restore clamps it.

    MESSAGE THREAD ONLY. Nothing here is reachable from `processBlock`: a load
    goes through the processor's pattern-state handle like every other.
*/

#include <juce_core/juce_core.h>

#include "ForroBoxState.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace forrobox
{

struct UserGroove
{
    juce::String id;     ///< a lower-case dashed UUID; also the file name
    juce::String name;   ///< UTF-8, trimmed, 1..kMaxUserGrooveNameLength characters
    int   bpm     { 132 };
    float swing   { 0.0f };
    float cachaca { 0.0f };
    std::array<State::Lane, static_cast<size_t> (State::kNumLanes)> lanes {};
};

/** What the header's screen can show. A name is for reading at a glance, and
    the design's preset labels are short. */
inline constexpr int kMaxUserGrooveNameLength = 24;

/** `name` trimmed, or empty when it cannot be a groove name: empty, too long,
    or holding a control character (a newline would break the one-line screen). */
juce::String normaliseUserGrooveName (const juce::String& name);

/** The file's root element. */
std::unique_ptr<juce::XmlElement> toXml (const UserGroove&);

struct ParsedUserGroove
{
    std::optional<UserGroove> groove;
    juce::String error;   ///< why not, when `groove` is empty
};

/** Strict: see the header comment. `expectedId` is the file's stem — a file
    whose id disagrees with its name is refused, so the file name can be trusted
    as the id when renaming or deleting. */
ParsedUserGroove fromXml (const juce::XmlElement&, juce::StringRef expectedId);

/** The folder of groove files, as a sorted bank.

    One per process (`shared()`), so every instance in a host sees the same
    bank. Another PROCESS's writes are seen on the next `rescan()`, which the
    processor calls before it walks the bank — a directory listing of small
    files, and the cycler is not a hot path. */
class UserGrooveLibrary
{
public:
    static constexpr const char* kExtension = ".forrogroove";

    /** An empty folder means "beside the plugin's settings file". */
    explicit UserGrooveLibrary (juce::File folder = {});

    static UserGrooveLibrary& shared();

    juce::File folder() const;

    /** Re-reads the folder. Files that fail to parse are listed in `skipped`. */
    void rescan();

    /** The bank, sorted by name ignoring case, then by id — so every instance
        and process walks it in the same order. Scans once if never scanned. */
    const std::vector<UserGroove>& grooves();

    const UserGroove* find (juce::StringRef id);

    /** Bumped by every scan, so a view polling the bank can tell "the same
        bank" with one compare instead of re-reading every groove. */
    int generation() const noexcept { return scanGeneration; }

    /** "file name: reason" per file the last scan refused. */
    const juce::StringArray& skipped() const noexcept { return skippedFiles; }

    /** Writes `content` under `name` with a new id, returned in `newId`. */
    juce::Result save (const juce::String& name, const UserGroove& content, juce::String& newId);

    /** Replaces an existing groove's feel and lanes; keeps its id and name. */
    juce::Result overwrite (juce::StringRef id, const UserGroove& content);

    juce::Result rename (juce::StringRef id, const juce::String& name);

    juce::Result remove (juce::StringRef id);

    /** What a write did, told to every listener after it succeeded (18-02). */
    struct Change
    {
        enum class Kind { saved, overwritten, renamed, removed };

        Kind kind;
        juce::String id;
    };

    /** SYNCHRONOUS, on the message thread — `Settings::Listener`'s model
        (13-01). Every processor is one, so an instance playing a groove another
        instance overwrites or deletes stops claiming it. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void userGrooveChanged (const Change&) = 0;
    };

    /** Any thread: see `listeners`. */
    void addListener (Listener*);
    void removeListener (Listener*);

    /** TEST-ONLY: so a test can see a destroyed listener left the list. */
    int numListenersForTest() const noexcept { return listeners.size(); }

    /** Points `shared()` at `folder` for the scope, and restores the previous
        folder after — nesting the way `Settings::ScopedTestFile` does, and for
        the reason it gives. The suite runs inside one, so no test ever writes
        into the developer's own library. */
    class ScopedTestFolder
    {
    public:
        explicit ScopedTestFolder (const juce::File& folder);
        ~ScopedTestFolder();

        ScopedTestFolder (const ScopedTestFolder&) = delete;
        ScopedTestFolder& operator= (const ScopedTestFolder&) = delete;

    private:
        juce::File previous;
    };

private:
    juce::File fileFor (juce::StringRef id) const;
    juce::Result write (const UserGroove&) const;

    /** A write from inside a notification, refused: the listeners still being
        told would hear about a library that has already moved on. Settings'
        rule (14-01). */
    juce::Result refuseIfNotifying() const;

    /** Overwrite, rename and remove: the guard, the rescans either side, the
        lookup and the notification, around the one step that differs. */
    juce::Result changeExisting (juce::StringRef id, Change::Kind,
                                 const std::function<juce::Result (const UserGroove&)>& act);
    void notify (Change::Kind, const juce::String& id);

    /** LOCKED, unlike Settings' list: a processor registers in its constructor
        and leaves in its destructor, and a host may run either on a loader
        thread (`loadProfileUnchecked`'s note, 11-06). Notification itself is
        message-thread only. */
    juce::ListenerList<Listener, juce::Array<Listener*, juce::CriticalSection>> listeners;
    bool notifying = false;

    juce::File folderOverride;
    std::vector<UserGroove> bank;
    juce::StringArray skippedFiles;
    bool scanned = false;
    int scanGeneration = 0;
};

/** Loads `groove` into the state: its lanes, `activeProfile = ids::userProfile`,
    `activeGroove = groove.id`, and everything `recordGrooveLoad` does. */
void applyUserGroove (State&, const UserGroove&);

} // namespace forrobox
