/* ============================================================================
   FORRÓ BOX — user preferences, persisted GLOBALLY

   `PLANNING.md:850-864` specifies five tweakable settings and says they "belong
   in a settings/gear menu, persisted globally (not per-project)". That last
   clause is the whole reason this file exists rather than five more entries in
   the APVTS: everything the plugin has stored until now — 46 parameters, the
   grid, the active profile — belongs to a PROJECT and travels with it. These do
   not. They are how this user likes the plugin to look, and they should be the
   same in the next project they open.

   THE PLUGIN'S FIRST GLOBAL STORE. There is no other `PropertiesFile` in `src/`,
   so this is a new subsystem and not a variation on one. Two consequences are
   stated here rather than left to be discovered:

     * ONE FILE, MANY INSTANCES. A host loads every instance of a plugin into one
       process, so `Settings::shared()` returns one object for all of them. That
       is deliberate — per-instance preferences would mean the second Forró Box
       on a track looked different from the first.

     * WHAT IS NOT GUARANTEED. Nobody is NOTIFIED of a change: an editor already
       on screen keeps its look until something asks it to re-read. Because every
       access opens the file fresh, a write only ever rewrites the key it touched
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
#include <memory>

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
    /** The process-wide instance.

        HOLDS NO OPEN FILE. `juce::PropertiesFile` derives from `juce::Timer`,
        and `Timer` keeps a `SharedResourcePointer<TimerThread>` as a MEMBER
        (juce_Timer.h:144) — so a static that owned one would pin the timer
        thread past `shutdownJuce_GUI()`, and `~TimerThread` asserts
        "a timer has outlived the platform event system" (juce_Timer.cpp:96-104).
        A Debug test binary traps at exit after every check has passed; a Debug
        VST3 traps at plugin unload, joining a thread from a static destructor
        under the Windows loader lock.

        So each read and write opens the file, does its work and closes it. The
        cost is a handful of XML entries parsed per access, and accesses happen
        when an editor opens and when a menu item is clicked. What it buys,
        besides the shutdown: no long-lived pointer to swap under a concurrent
        reader, and a cross-process write that cannot revert a setting it never
        touched, because it re-reads immediately before writing. /code-review. */
    static Settings& shared();

    /** Raw access, always clamped to the setting's own range. */
    int  get (Setting) const;
    void set (Setting, int value);

    // ── typed readers, so no caller repeats a conversion ────────────────────

    theme::Mode themeMode() const;
    /** The display font, as the type system's own enum. */
    type::MonoFamily monoFamily() const;
    float       cornerRadiusPx() const;
    /** 0..1, which is what `ForroBoxLookAndFeel::setAccentIntensity` takes. */
    float       accentIntensity() const;
    /** 16 or 32, straight out of `ids::stepWindows`. */
    int         defaultStepCount() const;
    /** The INDEX into `ids::stepWindows`, which is what the `steps` parameter
        stores — the parameter is a choice, not the number itself. */
    int         defaultStepChoiceIndex() const;

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

    /** Opens the target for the duration of one operation. */
    std::unique_ptr<juce::PropertiesFile> open() const;

    /** Empty means "wherever the OS says". Tests point it elsewhere. */
    juce::File targetOverride;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Settings)
};

} // namespace forrobox
