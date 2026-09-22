#include "Settings.h"

namespace forrobox
{

namespace
{

/** The file's identity.

    ASCII, deliberately. Phase 1 decided the plugin's DISPLAY name is ASCII
    because accented characters in a host's own UI were unreliable; the same
    argument applies harder to a PATH, which crosses a filesystem encoding as
    well. A user's config directory is not the place to discover that. */
constexpr const char* kApplicationName = "Forro Box";
constexpr const char* kFilenameSuffix  = "settings";

/** The directory, PER PLATFORM, because JUCE's own default is wrong on one of
    them and silently so.

    `PropertiesFile::Options::getDefaultFile` resolves Linux as
    `~/<folderName>` VERBATIM (juce_PropertiesFile.cpp:100-103), and only falls
    back to a dot-prefixed `~/.<applicationName>` when `folderName` is empty. So
    the obvious `folderName = "Forro Box"` creates a VISIBLE `~/Forro Box/`
    directory in the user's home — which is what this shipped as until the path
    was printed during 08-02's own test run. A plugin does not put a visible
    folder in somebody's home directory.

    `.config` is where this belongs on Linux and BSD; the XDG base-directory
    spec has been the convention for over a decade. macOS and Windows already
    resolve correctly — `~/Library/Application Support/<folderName>` and
    `%APPDATA%\<folderName>` — so only the one platform is special-cased, and it
    is special-cased because JUCE's rule there differs, not because we prefer a
    different layout.

    This is why the plan required the path to be PRINTED rather than assumed. */
#if JUCE_LINUX || JUCE_BSD
constexpr const char* kFolderName = ".config/Forro Box";
#else
constexpr const char* kFolderName = "Forro Box";
#endif

juce::PropertiesFile::Options defaultOptions()
{
    juce::PropertiesFile::Options options;

    options.applicationName     = kApplicationName;
    options.filenameSuffix      = kFilenameSuffix;
    options.folderName          = kFolderName;
    options.osxLibrarySubFolder = "Application Support";

    // The OS decides where this goes, and it must be the USER's directory: a
    // shared or system location would need privileges the plugin does not have,
    // and PROJECT.md forbids writing anywhere under Program Files.
    options.commonToAllUsers = false;

    return options;
}

/** An optional sign followed by at least one digit, and nothing else. */
bool isStrictInteger (juce::StringRef text) noexcept
{
    auto p = text.text;

    if (*p == '+' || *p == '-')
        ++p;

    if (! p.isDigit())
        return false;

    while (! p.isEmpty())
    {
        if (! p.isDigit())
            return false;

        ++p;
    }

    return true;
}

} // namespace

Settings::Settings() = default;

Settings& Settings::shared()
{
    static Settings instance;
    return instance;
}

std::unique_ptr<juce::PropertiesFile> Settings::open() const
{
    auto options = defaultOptions();

    // Zero, so `setValue` writes through instead of arming a 3-second timer it
    // then immediately cancels. The handle is destroyed at the end of the
    // operation anyway, but a timer started and stopped inside that window is
    // work done for nothing.
    options.millisecondsBeforeSaving = 0;

    if (targetOverride != juce::File())
    {
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        return std::make_unique<juce::PropertiesFile> (targetOverride, options);
    }

    return std::make_unique<juce::PropertiesFile> (options);
}

int Settings::get (Setting setting) const
{
    const auto& info = settings::info (setting);
    const auto  store = open();

    // No null check: `open()` returns `make_unique` on both paths, which throws
    // rather than returning null.
    if (! store->containsKey (info.key))
        return info.defaultValue;

    // READ AS A STRING FIRST, because `getIntValue`'s fallback covers only a
    // MISSING key: a key present as "bright" parses to 0, and clamping 0 into
    // 35..100 gives 35 — the dimmest accent, not the default. A damaged file
    // would therefore have produced a plugin that looked deliberately wrong
    // rather than one that looked untouched. /code-review.
    const auto raw = store->getValue (info.key).trim();

    // A STRICT PARSE, not a character filter. `containsOnly ("+-0123456789")`
    // was the first attempt and it admits "-", "+", "--" and "1-2" — each of
    // which `getIntValue` turns into 0 or a wrong number, and clamping 0 into
    // 35..100 yields 35: the DIMMEST accent, which is the exact failure the
    // comment above says this prevents. A damaged file would have produced a
    // plugin that looked deliberately wrong rather than untouched.
    // /code-review.
    if (! isStrictInteger (raw))
        return info.defaultValue;

    // Parsed as 64-bit so a value wider than an int does not wrap into range —
    // "99999999999999999999" would otherwise come back as something plausible.
    const auto parsed = raw.getLargeIntValue();

    return static_cast<int> (juce::jlimit<juce::int64> (info.minValue, info.maxValue, parsed));
}

void Settings::set (Setting setting, int value)
{
    const auto& info = settings::info (setting);
    const auto  store = open();

    // Clamped here too. A caller that computes an index from a menu is exactly
    // where an off-by-one lands, and writing it would persist the mistake past
    // the session that made it.
    store->setValue (info.key, juce::jlimit (info.minValue, info.maxValue, value));
    store->saveIfNeeded();
}

theme::Mode Settings::themeMode() const
{
    return get (Setting::theme) == 0 ? theme::Mode::dark : theme::Mode::light;
}

type::MonoFamily Settings::monoFamily() const
{
    return static_cast<type::MonoFamily> (get (Setting::displayFont));
}

float Settings::cornerRadiusPx() const
{
    return settings::cornerRadiiPx[static_cast<size_t> (get (Setting::cornerRadius))];
}

float Settings::accentIntensity() const
{
    // The table stores PERCENT because that is what PLANNING.md's column says
    // and what a menu shows; the LookAndFeel wants 0..1. One conversion, here,
    // rather than at each call site.
    return static_cast<float> (get (Setting::accentIntensity)) / 100.0f;
}

int Settings::defaultStepChoiceIndex() const
{
    return get (Setting::defaultSteps);
}

int Settings::defaultStepCount() const
{
    return ids::stepWindows[static_cast<size_t> (defaultStepChoiceIndex())];
}

juce::File Settings::realFileLocation()
{
    // The location the PLUGIN uses, whatever a test has redirected the store to.
    // Printed once per run: the visible `~/Forro Box/` bug was caught ONLY
    // because this path was reported, and once the suite began redirecting
    // itself that safety net would have been reported away. /code-review.
    return defaultOptions().getDefaultFile();
}

Settings::ScopedTestFile::ScopedTestFile (const juce::File& target)
    : previous (Settings::shared().targetOverride)
{
    Settings::shared().targetOverride = target;
}

Settings::ScopedTestFile::~ScopedTestFile()
{
    // BACK TO WHAT WAS THERE, not back to the default. These nest: the whole
    // suite runs inside one, and the settings tests open more inside that. An
    // inner scope that reset to the default would hand every later test the real
    // user's preferences file, which is the isolation this type exists to
    // provide, removed by its own destructor.
    Settings::shared().targetOverride = previous;
}

} // namespace forrobox
