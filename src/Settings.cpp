#include "Settings.h"

#include <atomic>

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

/** `juce::PropertiesFile`'s XML vocabulary (juce_PropertiesFile.cpp:43-46).
    COPIED, because JUCE keeps `PropertyFileConstants` private — and the
    format-compatibility checks in the suite, which write with a real
    `PropertiesFile` and read with this store and back again, are what hold the
    copy to the original. */
constexpr const char* kFileTag   = "PROPERTIES";
constexpr const char* kValueTag  = "VALUE";
constexpr const char* kNameAttr  = "name";
constexpr const char* kValueAttr = "val";

std::atomic<int> readCount { 0 };

/** One setting out of the file's raw text: its default when absent or damaged,
    otherwise clamped to its range. */
int parseValue (const juce::StringPairArray& values, Setting setting)
{
    const auto& info = settings::info (setting);

    if (! values.containsKey (info.key))
        return info.defaultValue;

    // READ AS A STRING FIRST, because `getIntValue`'s fallback covers only a
    // MISSING key: a key present as "bright" parses to 0, and clamping 0 into
    // 35..100 gives 35 — the dimmest accent, not the default. A damaged file
    // would therefore have produced a plugin that looked deliberately wrong
    // rather than one that looked untouched. /code-review.
    const auto raw = values[info.key].trim();

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

} // namespace

Settings::Settings() = default;

Settings& Settings::shared()
{
    static Settings instance;
    return instance;
}

juce::File Settings::target() const
{
    return targetOverride != juce::File() ? targetOverride : realFileLocation();
}

Settings::Contents Settings::read() const
{
    readCount.fetch_add (1, std::memory_order_relaxed);

    // Case-SENSITIVE keys, as `PropertySet`'s are by default.
    Contents contents;
    auto& values = contents.values;

    const auto file = target();

    // Missing or empty: nothing to lose, so not damaged — just defaults.
    if (! file.existsAsFile() || file.getSize() == 0)
        return contents;

    const auto root = juce::XmlDocument::parse (file);

    // THE ROOT TAG IS CHECKED, as `PropertiesFile::loadAsXml` checks it: a file
    // that parses but is something else is not a settings file, and its
    // `VALUE` children are not settings.
    if (root == nullptr || ! root->hasTagName (kFileTag))
    {
        contents.damaged = true;
        return contents;
    }

    for (auto* e : root->getChildWithTagNameIterator (kValueTag))
    {
        const auto name = e->getStringAttribute (kNameAttr);

        if (name.isEmpty())
            continue;

        // A value that was itself XML is stored as a child element, and JUCE
        // reads it back as single-line text. Mirrored, so a key this build does
        // not know survives the round trip in the form it was written.
        values.set (name, e->getFirstChildElement() != nullptr
                              ? e->getFirstChildElement()->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader())
                              : e->getStringAttribute (kValueAttr));
    }

    return contents;
}

bool Settings::writeValues (const juce::StringPairArray& values) const
{
    juce::XmlElement root (kFileTag);

    for (int i = 0; i < values.size(); ++i)
    {
        auto* e = root.createNewChildElement (kValueTag);
        e->setAttribute (kNameAttr, values.getAllKeys()[i]);

        // As `PropertiesFile::saveAsXml` does: text that parses as XML is
        // stored as an element, anything else as the attribute.
        if (auto child = juce::parseXML (values.getAllValues()[i]))
            e->addChildElement (child.release());
        else
            e->setAttribute (kValueAttr, values.getAllValues()[i]);
    }

    const auto file = target();

    // `PropertiesFile::save`'s own guards: no file, or a directory where the
    // file should be, is a failure before anything is created. /code-review.
    if (file == juce::File() || file.isDirectory()
         || ! file.getParentDirectory().createDirectory())
        return false;

    return root.writeTo (file, {});
}

int Settings::get (Setting setting) const
{
    return parseValue (read().values, setting);
}

Settings::Snapshot Settings::snapshot() const
{
    const auto values = read().values;

    Snapshot snap;

    for (size_t i = 0; i < snap.values.size(); ++i)
        snap.values[i] = parseValue (values, static_cast<Setting> (i));

    return snap;
}

bool Settings::set (Setting setting, int value)
{
    JUCE_ASSERT_MESSAGE_THREAD

    switch (writeUnderLock (setting, value))
    {
        case Write::failed:    return false;
        case Write::unchanged: return true;    // nothing changed, nobody to tell
        case Write::written:   break;
    }

    // EVERY INSTANCE IN THIS PROCESS HEARS IT, the one whose menu was clicked
    // included — after the write landed, so a listener reading the store sees
    // the new value, and after the inter-process lock is released, so that
    // read never waits on a lock this process holds. Synchronous, on the
    // caller's thread (the message thread). 13-02.
    notifyListeners();
    return true;
}

void Settings::notifyListeners()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // NOT RE-ENTRANT. A listener that calls `set` from `settingsChanged` would
    // otherwise notify again from inside the notification, and again — a
    // recursion bounded only by the stack. The nested write still lands; the
    // nested round is dropped, because every listener is already being told.
    // `Listener` says not to do it; this makes doing it harmless. /code-review.
    if (notifying)
        return;

    const juce::ScopedValueSetter<bool> guard (notifying, true);
    listeners.call (&Listener::settingsChanged);
}

void Settings::addListener (Listener* listener)
{
    JUCE_ASSERT_MESSAGE_THREAD
    listeners.add (listener);
}

void Settings::removeListener (Listener* listener)
{
    JUCE_ASSERT_MESSAGE_THREAD
    listeners.remove (listener);
}
int  Settings::numListenersForTest() const noexcept { return listeners.size(); }

Settings::Write Settings::writeUnderLock (Setting setting, int value)
{
    const auto& info = settings::info (setting);
    const auto clamped = juce::jlimit (info.minValue, info.maxValue, value);

    // ONE WRITER AT A TIME, across processes. Bounded, because this runs on the
    // message thread: a lock held that long is a stuck process, and a dropped
    // cosmetic preference is the better failure than a frozen UI.
    if (! writeLock.enter (1000))
        return Write::failed;

    // `ScopedLockType` only waits forever, so the bounded `enter` above is paired
    // with its exit by hand.
    const juce::ScopeGuard exitOnReturn { [this] { writeLock.exit(); } };

    // RE-READ, CHANGE ONE KEY, REWRITE. Everything else in the file — another
    // process's newer value, a key a future build added — goes back exactly as
    // it was read.
    auto contents = read();

    // ALREADY THE VALUE IN FORCE: nothing to write, nobody to tell. Re-picking
    // the ticked item would otherwise re-lay out and repaint every open
    // instance. Decided on the read this write needs anyway. /code-review,
    // /simplify. A damaged file is not "in force" — it reads as defaults, and
    // re-picking the default must still repair it.
    if (! contents.damaged && parseValue (contents.values, setting) == clamped)
        return Write::unchanged;

    // NOT OVER A FILE THAT COULD NOT BE READ. Its contents are unknown — every
    // other preference, keys from a newer build — so it is copied aside first,
    // and if the copy fails too, nothing is written. /code-review.
    if (contents.damaged)
    {
        const auto file = target();

        if (! file.copyFileTo (file.getSiblingFile (file.getFileName() + ".damaged")))
            return Write::failed;
    }

    auto& values = contents.values;

    // Clamped here too. A caller that computes an index from a menu is exactly
    // where an off-by-one lands, and writing it would persist the mistake past
    // the session that made it.
    values.set (info.key, juce::String (clamped));
    return writeValues (values) ? Write::written : Write::failed;
}

int Settings::readCountForTest() noexcept
{
    return readCount.load (std::memory_order_relaxed);
}

// ── typed readers ───────────────────────────────────────────────────────────

theme::Mode Settings::Snapshot::themeMode() const noexcept
{
    return get (Setting::theme) == 0 ? theme::Mode::dark : theme::Mode::light;
}

type::MonoFamily Settings::Snapshot::monoFamily() const noexcept
{
    return static_cast<type::MonoFamily> (get (Setting::displayFont));
}

float Settings::Snapshot::cornerRadiusPx() const noexcept
{
    return settings::cornerRadiiPx[static_cast<size_t> (get (Setting::cornerRadius))];
}

float Settings::Snapshot::accentIntensity() const noexcept
{
    // The table stores PERCENT because that is what PLANNING.md's column says
    // and what a menu shows; the LookAndFeel wants 0..1. One conversion, here,
    // rather than at each call site.
    return static_cast<float> (get (Setting::accentIntensity)) / 100.0f;
}

int Settings::Snapshot::defaultStepChoiceIndex() const noexcept
{
    return get (Setting::defaultSteps);
}

int Settings::Snapshot::defaultStepCount() const noexcept
{
    return ids::stepWindows[static_cast<size_t> (defaultStepChoiceIndex())];
}

juce::File Settings::realFileLocation()
{
    // The location the PLUGIN uses, whatever a test has redirected the store to.
    // Printed once per run: the visible `~/Forro Box/` bug was caught ONLY
    // because this path was reported, and once the suite began redirecting
    // itself that safety net would have been reported away. /code-review.
    //
    // Resolved once: on Windows it is a shell query, and the answer does not
    // change while the process lives. /code-review.
    static const auto defaultFile = defaultOptions().getDefaultFile();
    return defaultFile;
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
