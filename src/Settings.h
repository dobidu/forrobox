/* ============================================================================
   FORRÓ BOX — user preferences, persisted GLOBALLY

   `PLANNING.md:850-864` specifies five tweakable settings and says they "belong
   in a settings/gear menu, persisted globally (not per-project)". That last
   clause is the whole reason this file exists rather than five more entries in
   the APVTS: everything the plugin has stored until now — 46 parameters, the
   grid, the active profile — belongs to a PROJECT and travels with it. These do
   not. They are how this user likes the plugin to look, and they should be the
   same in the next project they open.

   THE PLUGIN'S FIRST GLOBAL STORE. There is no other preferences file in `src/`,
   so this is a new subsystem and not a variation on one. Two consequences are
   stated here rather than left to be discovered:

     * ONE FILE, MANY INSTANCES. A host loads every instance of a plugin into one
       process, so `Settings::shared()` returns one object for all of them. That
       is deliberate — per-instance preferences would mean the second Forró Box
       on a track looked different from the first.

     * WHO HEARS A CHANGE. Every instance in this PROCESS: `set` notifies its
       `Listener`s, and each open `Chassis` is one (13-02). Another process — a
       second host — is NOT notified; it reads the change when its next editor
       opens. Because every access reads the file fresh, a write only ever rewrites the key it touched
       plus whatever was on disk a moment earlier — so a second process cannot
       revert a setting it never edited, which an in-memory copy written back
       whole would have done. Acceptable for cosmetic preferences, and not
       acceptable for project state, which is why project state is not in here.

   EVERY READ CLAMPS, against the table below. A preferences file is plain XML in
   the user's own config directory: hand-editable, truncatable by a full disk,
   and writable by a future build with a wider range. `State`'s bounded scalars
   are the precedent — `ForroBoxState.h` keeps `presetIdx` private behind a
   clamping setter so an out-of-range value cannot exist in memory at all, rather
   than validating at the serialisation boundary and leaving a window where a bad
   value sits in memory until the next save happens to catch it.
============================================================================ */
#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "ParameterIDs.h"
#include "Theme.h"
#include "Typography.h"

#include <array>

namespace forrobox
{

/** The five settings, in `PLANNING.md:855-862`'s order. */
enum class Setting
{
    theme,            ///< 0 Dark, 1 OP-1 Light
    cornerRadius,     ///< index into `settings::cornerRadiiPx`
    accentIntensity,  ///< percent, 35..100
    displayFont,      ///< index into `settings::fontNames` — DECLARED here, wired in 08-03
    defaultSteps      ///< index into `ids::stepWindows`, so 16 or 32
};

namespace settings
{

/** One setting's identity and bounds, in ONE place.

    The same shape as `ids::channelInfos` and for the same reason: an id, a range
    and a default that live in three places diverge at the first edit, and the
    divergence here would be silent — a default that disagrees with its own range
    is a value the user never chose. */
struct SettingInfo
{
    /** The key written into the preferences file. NEVER rename one: a renamed
        key does not migrate, it silently reverts that setting to its default on
        every existing installation. Same law as the parameter IDs. */
    const char* key;

    int minValue;
    int maxValue;
    int defaultValue;
};

/** Indexed by `Setting`. The defaults are `PLANNING.md:856-862`'s column. */
inline constexpr std::array<SettingInfo, 5> infos {{
    { "theme",             0,  1,   0 },   // Dark
    { "corner_radius",     0,  1,   1 },   // 2 px
    { "accent_intensity", 35, 100, 100 },  // 100%
    { "display_font",      0,  2,   0 },   // IBM Plex Mono — read by nothing until 08-03
    { "default_steps",     0,  1,   0 },   // 16
}};

/** `PLANNING.md:858` — "0px (hard) / 2px". Two values, not a range, so they are
    a table rather than a min/max the UI would have to interpolate.

    THE 2 IS ASKED FOR, not typed. `theme::kCornerRadius` is cross-checked
    against CSS `--r` by `verify-theme.py:272`, so a stylesheet change moves it —
    and a literal here would stay at 2.0f, leaving a fresh install rendering at a
    radius the design source does not specify while the menu item still printed
    "2 px". Its own docstring already names this setting. /simplify. */
inline constexpr std::array<float, 2> cornerRadiiPx { 0.0f, theme::kCornerRadius };

/** `PLANNING.md:861`. Only the first is embedded today; 08-03 embeds the other
    two and wires the setting.

    Declared now so 08-03 is a WIRING change rather than a schema one. NOT
    because "the file format does not change" — that was the first rationale
    here and it is false: the store writes a key only when `set` is called,
    nothing calls it for this one, so no installed file contains `display_font`
    today whether this row exists or not, and an absent key reads back as its
    default either way. /simplify caught the claim. */
inline constexpr std::array<const char*, 3> fontNames {
    "IBM Plex Mono", "JetBrains Mono", "Space Mono"
};

// THE MENU'S ORDER AND THE TYPE SYSTEM'S ENUM ARE ONE MAPPING, pinned PER ROW.
//
// A size check alone does not pin it: reordering this table to
// {"IBM Plex Mono", "Space Mono", "JetBrains Mono"} keeps the size at three,
// passes, and labels Space Mono while loading JetBrains — a label that lies
// about what is drawn. The first version asserted only the size under a comment
// claiming it caught exactly that. /code-review.
static_assert (fontNames.size() == static_cast<size_t> (type::kNumMonoFamilies),
               "every display font must name a MonoFamily");

constexpr const char* nameOf (type::MonoFamily family) noexcept
{
    return fontNames[static_cast<size_t> (family)];
}

static_assert (ids::detail::sameId (nameOf (type::MonoFamily::ibmPlexMono),   "IBM Plex Mono"));
static_assert (ids::detail::sameId (nameOf (type::MonoFamily::jetBrainsMono), "JetBrains Mono"));
static_assert (ids::detail::sameId (nameOf (type::MonoFamily::spaceMono),     "Space Mono"));

constexpr const SettingInfo& info (Setting s) noexcept
{
    return infos[static_cast<size_t> (s)];
}

/** Cross-checks the table against itself. A default outside its own range is the
    one error in here that no runtime clamp could report, because the clamp would
    silently produce a value the table does not name. */
constexpr bool defaultsAreInRange() noexcept
{
    for (const auto& i : infos)
        if (i.defaultValue < i.minValue || i.defaultValue > i.maxValue)
            return false;

    return true;
}

static_assert (defaultsAreInRange(), "a setting's default falls outside its own range");

// THE ENUM AND THE TABLE ARE ONE MAPPING, pinned here. Every test derives the
// enumerator from the array index, so a row inserted at the top of `infos`
// without touching the enum would leave `themeMode()` reading `corner_radius`,
// `cornerRadiusPx()` indexing its table with an accent percent, and the whole
// suite green. /code-review found that the tests were tautological about this;
// a tautology is not closed by another test, it is closed at compile time.
static_assert (ids::detail::sameId (info (Setting::theme).key,            "theme"));
static_assert (ids::detail::sameId (info (Setting::cornerRadius).key,     "corner_radius"));
static_assert (ids::detail::sameId (info (Setting::accentIntensity).key,  "accent_intensity"));
static_assert (ids::detail::sameId (info (Setting::displayFont).key,      "display_font"));
static_assert (ids::detail::sameId (info (Setting::defaultSteps).key,     "default_steps"));
static_assert (infos.size() == 5, "PLANNING.md:855-862 specifies five settings");
static_assert (cornerRadiiPx.size()
                   == static_cast<size_t> (info (Setting::cornerRadius).maxValue + 1),
               "the corner-radius table and its range disagree");
static_assert (fontNames.size()
                   == static_cast<size_t> (info (Setting::displayFont).maxValue + 1),
               "the font table and its range disagree");
static_assert (ids::stepWindows.size()
                   == static_cast<size_t> (info (Setting::defaultSteps).maxValue + 1),
               "the default-steps range must index ids::stepWindows, not a second copy of 16/32");

} // namespace settings

/** The preferences, shared by every plugin instance in the process. */
class Settings
{
public:
    /** Every setting from ONE read of the file, with the typed readers — the
        only typed readers: `shared().snapshot().themeMode()` and so on.

        For a caller that needs several values at once — the menu's ticks,
        `settings::applyTo` — so it parses the file once rather than once per
        value. A value, not a view: it does not change when the file does. */
    class Snapshot
    {
    public:
        /** Clamped to the setting's own range, like `Settings::get`. */
        int get (Setting s) const noexcept { return values[static_cast<size_t> (s)]; }

        theme::Mode      themeMode() const noexcept;
        /** The display font, as the type system's own enum. */
        type::MonoFamily monoFamily() const noexcept;
        float            cornerRadiusPx() const noexcept;
        /** 0..1, which is what `ForroBoxLookAndFeel::setAccentIntensity` takes. */
        float            accentIntensity() const noexcept;
        /** 16 or 32, straight out of `ids::stepWindows`. */
        int              defaultStepCount() const noexcept;
        /** The INDEX into `ids::stepWindows`, which is what the `steps`
            parameter stores — the parameter is a choice, not the number. */
        int              defaultStepChoiceIndex() const noexcept;

    private:
        friend class Settings;

        /** Only `Settings::snapshot` makes one, so no snapshot can exist holding
            zeros — below `accent_intensity`'s own floor. /code-review. */
        Snapshot() = default;

        std::array<int, settings::infos.size()> values {};
    };

    /** The process-wide instance.

        HOLDS NO OPEN FILE, AND NO TIMER. Each read parses the file with
        `juce::XmlDocument` and each write rewrites it through
        `XmlElement::writeTo`, which goes via a `TemporaryFile`, so the write is
        atomic. The format is `juce::PropertiesFile`'s own XML, so every file a
        previous build wrote still loads.

        It is not a `PropertiesFile` because that class is a `juce::Timer`, and
        `Timer` keeps a `SharedResourcePointer<TimerThread>` as a MEMBER
        (juce_Timer.h:144). Two consequences:
          * a static that owned one would pin the timer thread past
            `shutdownJuce_GUI()` — the 08-02 trap at exit and at plugin unload;
          * the per-access `PropertiesFile` that replaced it SPAWNED AND JOINED
            the timer thread on every access whenever no other timer was alive:
            about 14 times per gear click, and inside the processor's
            constructor during a headless scan. 13-01.
        Plain file I/O has neither cost.

        Each access still goes to disk, deliberately. A write re-reads the file
        immediately before rewriting it and keeps every key it did not change,
        unknown ones included, so a second process — or a newer build — cannot
        have a setting reverted by one that never edited it. An in-memory copy
        written back whole would do exactly that. The read-modify-write runs
        under a `juce::InterProcessLock`, so two processes writing at once are
        serialised rather than racing; without it, each could read before the
        other wrote and revert the other's change. /code-review.

        A FILE THAT EXISTS BUT CANNOT BE READ IS NEVER OVERWRITTEN BLIND. Reads
        treat it as defaults, but before a write replaces it, it is copied aside
        as `<name>.damaged`, and if even that copy fails (a lock, a permission)
        the write is refused. Otherwise one click would replace every other
        preference with a single key. /code-review. */
    static Settings& shared();

    /** Raw access, always clamped to the setting's own range. */
    int  get (Setting) const;

    /** False when the value did not reach the disk: the lock timed out, the
        directory is not writable, or a damaged file could not be set aside.
        The menu needs no special case — it re-reads, so it shows what is in
        force — but a caller that must know, can. */
    bool set (Setting, int value);

    /** Every setting, from one read. */
    Snapshot snapshot() const;

    /** TEST-ONLY: how many times the file has been read in this process, so a
        test can hold a caller to one read. Atomic because the processor's
        constructor reads from a host's loader thread. */
    static int readCountForTest() noexcept;

    /** Told after every `set` that reached the disk — never after one that
        failed, because then nothing changed.

        MESSAGE THREAD ONLY: add, remove and the notification itself. Listeners
        are UI objects and `set` is called from a menu; `juce::ListenerList` is
        not locked, and that is right for a list only one thread touches — so
        all three assert it. A listener must remove itself before it is
        destroyed, and must not call `set` from `settingsChanged` (a nested
        round is dropped). */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void settingsChanged() = 0;
    };

    void addListener (Listener*);
    void removeListener (Listener*);

    /** Tells every listener, without a write. For the one change that does not
        come through `set`: an editor's seed moving the PROCESS-global display
        font, which every other open instance has measured under. Ignored
        while a notification is already running. */
    void notifyListeners();

    /** TEST-ONLY: so a test can see a destroyed listener left the list. */
    int numListenersForTest() const noexcept;


    /** Where the PLUGIN's file lives, whatever a test has redirected to.

        Printed once per suite run, and that is what caught the store writing a
        VISIBLE `~/Forro Box/` directory into the user's home. Static, because
        the question it answers is about the product rather than about wherever
        this run happens to be pointed — a non-static twin briefly existed, had
        no callers, and would have printed a temp path. /code-review, /simplify. */
    static juce::File realFileLocation();

    /** Redirects the store at `file` for the duration of the scope, so a test
        never touches the real user's preferences.

        RESTORES THE PREVIOUS TARGET, which is not the same as reopening the
        default — and the difference is not academic. The whole suite runs inside
        one of these (`tests/TestMain.cpp`), and an inner scope that reopened the
        DEFAULT on the way out would drop every later test back onto the real
        user's file. That is what the first version did, under a comment claiming
        it nested correctly; a stored Light theme then failed 13 checks. */
    class ScopedTestFile
    {
    public:
        explicit ScopedTestFile (const juce::File& file);
        ~ScopedTestFile();

        ScopedTestFile (const ScopedTestFile&) = delete;
        ScopedTestFile& operator= (const ScopedTestFile&) = delete;

    private:
        juce::File previous;
    };

private:
    Settings();

    struct Contents
    {
        /** Key -> raw text, in file order. Empty unless the file parsed. */
        juce::StringPairArray values { false };

        /** The file exists, is not empty, and did not parse as a settings file
            (malformed, another root tag, unreadable). Reads treat it as
            defaults; a write must not destroy it. */
        bool damaged = false;
    };

    Contents read() const;

    /** Rewrites the whole file from `values`, atomically. */
    bool writeValues (const juce::StringPairArray& values) const;

    /** The override, or where the OS puts the plugin's file. */
    juce::File target() const;

    /** Empty means "wherever the OS says". Tests point it elsewhere. */
    juce::File targetOverride;

    /** Serialises `set`'s read-modify-write across processes. Holds nothing
        until entered: no thread, no timer. */
    juce::InterProcessLock writeLock { "ForroBoxSettings" };

    juce::ListenerList<Listener> listeners;
    bool notifying = false;

    enum class Write { written, unchanged, failed };

    /** `set`'s work: lock, re-read, set one key, write — or not, when the key
        already holds the value. */
    Write writeUnderLock (Setting, int value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Settings)
};

} // namespace forrobox
