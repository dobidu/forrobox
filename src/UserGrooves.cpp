#include "UserGrooves.h"

#include <juce_events/juce_events.h>

#include "ParameterIDs.h"
#include "Profiles.h"
#include "Settings.h"

#include <algorithm>
#include <cstring>
#include <cmath>

namespace forrobox
{

namespace
{
constexpr const char* kRootTag   = "ForroBoxGroove";
constexpr const char* kLaneTag   = "lane";
constexpr int         kVersion   = 1;

bool isCanonicalId (const juce::String& id)
{
    // Lower-case and dashed, exactly as `juce::Uuid::toDashedString` writes it,
    // so one groove has one possible file name. A non-hex character parses as
    // zero rather than failing, which the round trip catches.
    const juce::Uuid parsed (id);
    return ! parsed.isNull() && parsed.toDashedString() == id;
}

/** Thirty-two decimal steps, 0-127, separated by spaces. Anything else is not
    ours — refused, not repaired. */
bool parseSteps (const juce::String& text, State::Lane& out)
{
    auto tokens = juce::StringArray::fromTokens (text, " ", "");
    tokens.removeEmptyStrings();

    if (tokens.size() != static_cast<int> (out.size()))
        return false;

    for (size_t i = 0; i < out.size(); ++i)
    {
        const auto& token = tokens[static_cast<int> (i)];

        if (token.length() > 3 || ! token.containsOnly ("0123456789"))
            return false;

        const auto value = token.getIntValue();

        if (value > State::kMaxVelocity)
            return false;

        out[i] = static_cast<std::uint8_t> (value);
    }

    return true;
}

juce::String formatSteps (const State::Lane& lane)
{
    juce::StringArray tokens;

    for (const auto step : lane)
        tokens.add (juce::String (static_cast<int> (step)));

    return tokens.joinIntoString (" ");
}

float clampPercent (double value)
{
    return std::isfinite (value) ? juce::jlimit (0.0f, ids::kPercentMax, static_cast<float> (value)) : 0.0f;
}

juce::String unknownAttribute (const juce::XmlElement& element, std::initializer_list<const char*> known)
{
    for (int i = 0; i < element.getNumAttributes(); ++i)
    {
        const auto& name = element.getAttributeName (i);

        if (std::none_of (known.begin(), known.end(), [&name] (const char* k) { return name == k; }))
            return name;
    }

    return {};
}

bool byName (const UserGroove& a, const UserGroove& b)
{
    const auto order = a.name.compareIgnoreCase (b.name);
    return order != 0 ? order < 0 : a.id < b.id;
}
} // namespace

juce::String normaliseUserGrooveName (const juce::String& name)
{
    const auto trimmed = name.trim();

    if (trimmed.isEmpty() || trimmed.length() > kMaxUserGrooveNameLength)
        return {};

    for (auto p = trimmed.getCharPointer(); ! p.isEmpty(); ++p)
        if (*p < 0x20 || *p == 0x7f)
            return {};

    return trimmed;
}

std::unique_ptr<juce::XmlElement> toXml (const UserGroove& groove)
{
    auto root = std::make_unique<juce::XmlElement> (kRootTag);

    root->setAttribute ("version", kVersion);
    root->setAttribute ("id",      groove.id);
    root->setAttribute ("name",    groove.name);
    root->setAttribute ("bpm",     groove.bpm);
    root->setAttribute ("swing",   static_cast<double> (groove.swing));
    root->setAttribute ("cachaca", static_cast<double> (groove.cachaca));

    for (size_t lane = 0; lane < groove.lanes.size(); ++lane)
    {
        auto* child = root->createNewChildElement (kLaneTag);
        child->setAttribute ("id",    ids::lanes[lane]);
        child->setAttribute ("steps", formatSteps (groove.lanes[lane]));
    }

    return root;
}

ParsedUserGroove fromXml (const juce::XmlElement& root, juce::StringRef expectedId)
{
    const auto refuse = [] (const juce::String& why) { return ParsedUserGroove { std::nullopt, why }; };

    if (! root.hasTagName (kRootTag))
        return refuse ("not a groove file (root <" + root.getTagName() + ">)");

    if (! root.hasAttribute ("version") || root.getIntAttribute ("version") != kVersion)
        return refuse ("unsupported version '" + root.getStringAttribute ("version") + "'");

    // STRICT IN v1: an attribute this reader does not know is a refusal, not
    // something to skip. Loosening that later is the safe direction; a reader
    // that ignored fields could not be tightened without breaking files.
    if (const auto unknown = unknownAttribute (root, { "version", "id", "name", "bpm", "swing", "cachaca" });
        unknown.isNotEmpty())
        return refuse ("unknown attribute '" + unknown + "'");

    UserGroove groove;
    groove.id = root.getStringAttribute ("id");

    if (! isCanonicalId (groove.id))
        return refuse ("id '" + groove.id + "' is not a lower-case UUID");

    if (expectedId.isNotEmpty() && groove.id != juce::String (expectedId))
        return refuse ("id '" + groove.id + "' does not match the file name");

    groove.name = normaliseUserGrooveName (root.getStringAttribute ("name"));

    if (groove.name.isEmpty())
        return refuse ("missing or invalid name");

    for (const auto* attribute : { "bpm", "swing", "cachaca" })
    {
        if (! root.hasAttribute (attribute))
            return refuse (juce::String ("missing ") + attribute);

        // A NUMBER, or a refusal: `getIntAttribute ("fast")` is 0, which the
        // clamp would quietly turn into 40 BPM. Only RANGE is forgiven.
        // /code-review.
        const auto text = root.getStringAttribute (attribute).trim();
        const auto integerOnly = juce::StringRef (attribute) == juce::StringRef ("bpm");

        if (text.isEmpty() || ! text.containsAnyOf ("0123456789")
            || ! text.containsOnly (integerOnly ? "-0123456789" : "-.0123456789"))
            return refuse (juce::String (attribute) + " '" + text + "' is not a number");
    }

    // CLAMPED, as a restore clamps: a value out of range is a hand edit or a
    // newer build's wider range, and the nearest playable value is the honest
    // reading of either.
    groove.bpm     = juce::jlimit (ids::kMinBpm, ids::kMaxBpm, root.getIntAttribute ("bpm"));
    groove.swing   = clampPercent (root.getDoubleAttribute ("swing"));
    groove.cachaca = clampPercent (root.getDoubleAttribute ("cachaca"));

    std::array<bool, ids::lanes.size()> seen {};

    for (const auto* child : root.getChildIterator())
    {
        if (! child->hasTagName (kLaneTag))
            return refuse ("unexpected element <" + child->getTagName() + ">");

        if (const auto unknown = unknownAttribute (*child, { "id", "steps" }); unknown.isNotEmpty())
            return refuse ("unknown lane attribute '" + unknown + "'");

        // A lane is EMPTY: everything it says is in its two attributes, and the
        // format page promises that anything unlisted refuses the file.
        // /code-review.
        if (child->getNumChildElements() != 0)
            return refuse ("a lane with content inside it");

        const auto laneId = child->getStringAttribute ("id");
        const auto found = std::find_if (ids::lanes.begin(), ids::lanes.end(),
                                         [&laneId] (const char* id) { return laneId == id; });

        if (found == ids::lanes.end())
            return refuse ("unknown lane '" + laneId + "'");

        const auto lane = static_cast<size_t> (std::distance (ids::lanes.begin(), found));

        if (seen[lane])
            return refuse ("lane '" + laneId + "' appears twice");

        seen[lane] = true;

        if (! parseSteps (child->getStringAttribute ("steps"), groove.lanes[lane]))
            return refuse ("lane '" + laneId + "' is not "
                           + juce::String (State::kMaxSteps) + " steps of 0-127");
    }

    for (size_t lane = 0; lane < seen.size(); ++lane)
        if (! seen[lane])
            return refuse (juce::String ("lane '") + ids::lanes[lane] + "' is missing");

    return { std::move (groove), {} };
}

// ── the library ──────────────────────────────────────────────────────────────

UserGrooveLibrary::UserGrooveLibrary (juce::File folderToUse)
    : folderOverride (std::move (folderToUse))
{
}

UserGrooveLibrary& UserGrooveLibrary::shared()
{
    static UserGrooveLibrary instance;
    return instance;
}

juce::File UserGrooveLibrary::folder() const
{
    return folderOverride != juce::File() ? folderOverride
                                          : Settings::realFileLocation().getSiblingFile ("grooves");
}

juce::File UserGrooveLibrary::fileFor (juce::StringRef id) const
{
    return folder().getChildFile (juce::String (id) + kExtension);
}

bool sameGroove (const UserGroove& a, const UserGroove& b)
{
    return a.id == b.id && a.name == b.name && a.bpm == b.bpm
        && juce::exactlyEqual (a.swing, b.swing) && juce::exactlyEqual (a.cachaca, b.cachaca)
        && a.lanes == b.lanes;
}

void UserGrooveLibrary::rescan()
{
    std::vector<UserGroove> next;
    juce::StringArray skipped;

    // A folder that does not exist is an empty library, and reading never
    // creates it: the first SAVE does.
    for (const auto& entry : juce::RangedDirectoryIterator (folder(), false,
                                                             juce::String ("*") + kExtension,
                                                             juce::File::findFiles))
    {
        const auto file = entry.getFile();
        auto parsed = readGrooveFile (file, file.getFileNameWithoutExtension());

        if (parsed.groove.has_value())
            next.push_back (std::move (*parsed.groove));
        else
            skipped.add (file.getFileName() + ": " + parsed.error);
    }

    std::sort (next.begin(), next.end(), byName);

    skippedFiles = skipped;

    // THE GENERATION MOVES ONLY WHEN THE BANK DID. Every arrow press under MEUS
    // rescans (another process may have written), and a bump on an unchanged
    // bank made every open editor rebuild its whole list for nothing.
    // /simplify.
    const auto changed = ! scanned
                      || ! std::equal (next.begin(), next.end(), bank.begin(), bank.end(), sameGroove);
    scanned = true;

    if (changed)
    {
        bank = std::move (next);
        ++scanGeneration;
    }
}

const std::vector<UserGroove>& UserGrooveLibrary::grooves()
{
    if (! scanned)
        rescan();

    return bank;
}

const UserGroove* UserGrooveLibrary::find (juce::StringRef id)
{
    for (const auto& groove : grooves())
        if (groove.id == id)   // no String built per comparison: this runs from a 30 Hz poll
            return &groove;

    return nullptr;
}

juce::Result UserGrooveLibrary::write (const UserGroove& groove) const
{
    const auto target = folder();

    if (! target.createDirectory())
        return juce::Result::fail ("cannot create " + target.getFullPathName());

    return writeGrooveFile (groove, fileFor (groove.id));
}

juce::Result writeGrooveFile (const UserGroove& groove, const juce::File& destination)
{
    // ATOMIC, through a temporary file that is swapped in: a crash or a full
    // disk mid-write leaves the old file whole. The temporary is named here
    // rather than left to `XmlElement::writeTo`, whose own is
    // `<id>_temp<hex>.forrogroove` — matching the scan's pattern, so a rescan
    // from another process mid-write, or a file left by a crash, was reported
    // as a refused groove the user never made. /code-review.
    juce::TemporaryFile temporary (destination,
                                   destination.getSiblingFile (groove.id + "-" + juce::Uuid().toDashedString() + ".partial"));

    if (! toXml (groove)->writeTo (temporary.getFile()) || ! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("cannot write " + destination.getFullPathName());

    return juce::Result::ok();
}

juce::Result UserGrooveLibrary::save (const juce::String& name, const UserGroove& content, juce::String& newId)
{
    if (const auto refused = refuseIfNotifying(); refused.failed())
        return refused;

    auto groove = content;
    groove.name = normaliseUserGrooveName (name);

    if (groove.name.isEmpty())
        return juce::Result::fail ("invalid name");

    groove.id = juce::Uuid().toDashedString();

    if (const auto result = write (groove); result.failed())
        return result;

    newId = groove.id;
    rescan();
    notify (Change::Kind::saved, groove.id);
    return juce::Result::ok();
}

juce::Result UserGrooveLibrary::changeExisting (juce::StringRef id, Change::Kind kind,
                                                const std::function<juce::Result (const UserGroove&)>& act)
{
    if (const auto refused = refuseIfNotifying(); refused.failed())
        return refused;

    // Rescanned FIRST, so the groove acted on is the one on disk now — another
    // process may have changed it — and AFTER, so the bank is what was written.
    rescan();
    const auto* existing = find (id);

    if (existing == nullptr)
        return juce::Result::fail ("no groove '" + juce::String (id) + "'");

    const auto groove = *existing;   // a copy: the rescan below replaces the bank

    if (const auto result = act (groove); result.failed())
        return result;

    rescan();
    notify (kind, groove.id);
    return juce::Result::ok();
}

juce::Result UserGrooveLibrary::overwrite (juce::StringRef id, const UserGroove& content)
{
    return changeExisting (id, Change::Kind::overwritten, [this, &content] (const UserGroove& existing)
    {
        auto groove = content;
        groove.id   = existing.id;
        groove.name = existing.name;
        return write (groove);
    });
}

juce::Result UserGrooveLibrary::rename (juce::StringRef id, const juce::String& name)
{
    const auto normalised = normaliseUserGrooveName (name);

    if (normalised.isEmpty())
        return juce::Result::fail ("invalid name");

    return changeExisting (id, Change::Kind::renamed, [this, &normalised] (const UserGroove& existing)
    {
        auto groove = existing;
        groove.name = normalised;
        return write (groove);
    });
}

juce::Result UserGrooveLibrary::remove (juce::StringRef id)
{
    return changeExisting (id, Change::Kind::removed, [this] (const UserGroove& existing)
    {
        const auto file = fileFor (existing.id);
        return file.deleteFile() ? juce::Result::ok()
                                 : juce::Result::fail ("cannot delete " + file.getFullPathName());
    });
}

juce::Result UserGrooveLibrary::refuseIfNotifying() const
{
    if (! notifying)
        return juce::Result::ok();

    jassertfalse;
    return juce::Result::fail ("a groove write from inside a library notification");
}

void UserGrooveLibrary::notify (Change::Kind kind, const juce::String& id)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const juce::ScopedValueSetter<bool> guard (notifying, true);
    const Change change { kind, id };
    listeners.call ([&change] (Listener& l) { l.userGrooveChanged (change); });
}

void UserGrooveLibrary::addListener (Listener* listener)    { listeners.add (listener); }
void UserGrooveLibrary::removeListener (Listener* listener) { listeners.remove (listener); }

UserGrooveLibrary::ScopedTestFolder::ScopedTestFolder (const juce::File& testFolder)
    : previous (shared().folderOverride)
{
    shared().folderOverride = testFolder;
    shared().scanned = false;
}

UserGrooveLibrary::ScopedTestFolder::~ScopedTestFolder()
{
    // BACK TO WHAT WAS THERE — see `Settings::ScopedTestFile`.
    shared().folderOverride = previous;
    shared().scanned = false;
}

ParsedUserGroove readGrooveFile (const juce::File& file, juce::StringRef expectedId)
{
    const auto refuse = [] (const juce::String& why) { return ParsedUserGroove { std::nullopt, why }; };

    // EVERY READ GOES THROUGH HERE — a scan of the library folder and an
    // import alike — because both read files the plugin did not write: one a
    // stranger sent, the other whatever was copied into the folder.
    if (! file.existsAsFile())
        return refuse ("not a file");

    // THE SIZE FIRST, before a byte is read: a groove is ~2 KB, so a large
    // file is not one, and reading it to find out would let it choose how much
    // memory this costs.
    if (file.getSize() > kMaxGrooveFileBytes)
        return refuse ("too large (" + juce::String (file.getSize()) + " bytes)");

    juce::MemoryBlock data;

    if (! file.loadFileAsData (data))
        return refuse ("unreadable");

    if (data.getSize() == 0)
        return refuse ("empty");

    if (data.getSize() > static_cast<size_t> (kMaxGrooveFileBytes))
        return refuse ("too large");

    const auto* bytes = static_cast<const char*> (data.getData());
    const auto size = static_cast<int> (data.getSize());

    // UTF-8 TEXT, nothing else: a NUL rules out binary and UTF-16 (whose
    // ASCII is half zero bytes), and the validity check rules out the rest.
    if (std::memchr (bytes, 0, data.getSize()) != nullptr)
        return refuse ("binary, or not UTF-8 text");

    if (! juce::CharPointer_UTF8::isValidString (bytes, size))
        return refuse ("not UTF-8 text");

    // A UTF-8 BYTE-ORDER MARK is skipped, as `XmlDocument::parse (File)` did
    // before 19-01 — editors that write one ("UTF-8 with BOM") are common.
    // /code-review.
    const auto bomLength = size >= 3 && std::memcmp (bytes, "\xef\xbb\xbf", 3) == 0 ? 3 : 0;
    const auto text = juce::String (juce::CharPointer_UTF8 (bytes + bomLength), juce::CharPointer_UTF8 (bytes + size));

    // AT MOST A FEW DOZEN ELEMENTS, counted before the parser sees them. JUCE
    // parses — and frees — XML recursively, so 64 KB of `<a><a><a>…` is ~21,000
    // levels: enough to overflow the message thread's stack and take the host
    // down, at import and at every later scan. A groove has nine elements.
    // /code-review.
    auto elements = 0;

    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
        if (*p == '<')
        {
            const auto next = *(p + 1);

            if (next != '/' && next != '?' && next != '!' && ++elements > kMaxGrooveFileElements)
                return refuse ("too many elements (more than " + juce::String (kMaxGrooveFileElements) + ")");
        }

    // NO DTD AT ALL. JUCE's XmlDocument expands entities declared in an
    // internal subset, which is the "billion laughs" door; a groove file has
    // no use for one, so its presence is a refusal before the parser sees it.
    if (text.contains ("<!DOCTYPE") || text.contains ("<!ENTITY"))
        return refuse ("a DOCTYPE or entity declaration (not allowed)");

    juce::XmlDocument document (text);
    const auto root = document.getDocumentElement();

    if (root == nullptr)
        return refuse ("not XML (" + document.getLastParseError() + ")");

    return fromXml (*root, expectedId);
}

namespace
{
/** `name (n)` for the smallest n from 2 that no groove in `taken` is called,
    the base trimmed so the whole still fits the name limit. */
juce::String copyName (const juce::String& name, const std::vector<UserGroove>& taken)
{
    for (int n = 2;; ++n)
    {
        const auto suffix = " (" + juce::String (n) + ")";
        const auto base = name.substring (0, kMaxUserGrooveNameLength - suffix.length()).trimEnd();
        const auto candidate = base + suffix;

        if (std::none_of (taken.begin(), taken.end(),
                          [&candidate] (const UserGroove& g) { return g.name.equalsIgnoreCase (candidate); }))
            return candidate;
    }
}
} // namespace

std::vector<UserGrooveLibrary::ImportOutcome> UserGrooveLibrary::importFiles (const juce::Array<juce::File>& files)
{
    std::vector<ImportOutcome> outcomes;

    if (const auto refused = refuseIfNotifying(); refused.failed())
    {
        for (const auto& file : files)
            outcomes.push_back ({ ImportOutcome::Result::refused, file, {}, {}, refused.getErrorMessage() });

        return outcomes;
    }

    // What the library holds now, plus what this batch adds — so two copies
    // of one file in a batch are "added" then "identical", and two different
    // files with one id become "X" and "X (2)".
    rescan();
    auto known = bank;
    auto wroteAny = false;

    for (const auto& file : files)
    {
        // The id is the FILE'S OWN, not its name: a shared file may well
        // arrive renamed.
        auto parsed = readGrooveFile (file, {});

        if (! parsed.groove.has_value())
        {
            outcomes.push_back ({ ImportOutcome::Result::refused, file, {}, {}, parsed.error });
            continue;
        }

        auto groove = std::move (*parsed.groove);
        auto result = ImportOutcome::Result::added;

        const auto existing = std::find_if (known.begin(), known.end(),
                                            [&groove] (const UserGroove& g) { return g.id == groove.id; });

        if (existing != known.end())
        {
            if (sameGroove (*existing, groove))
            {
                outcomes.push_back ({ ImportOutcome::Result::identical, file, groove.id, groove.name, {} });
                continue;
            }

            // SAME ID, DIFFERENT GROOVE: a copy beside it, never over it.
            groove.id = juce::Uuid().toDashedString();
            groove.name = copyName (groove.name, known);
            result = ImportOutcome::Result::copied;
        }

        if (const auto written = write (groove); written.failed())
        {
            outcomes.push_back ({ ImportOutcome::Result::refused, file, {}, {}, written.getErrorMessage() });
            continue;
        }

        outcomes.push_back ({ result, file, groove.id, groove.name, {} });
        known.push_back (std::move (groove));
        wroteAny = true;
    }

    // ONE rescan and ONE notification for the batch, not one per file.
    if (wroteAny)
    {
        rescan();
        notify (Change::Kind::saved, {});
    }

    return outcomes;
}

void applyUserGroove (State& state, const UserGroove& groove)
{
    state.lanes = groove.lanes;
    recordGrooveLoad (state, ids::userProfile, groove.id);
}

} // namespace forrobox
