#include "UserGrooves.h"

#include <juce_events/juce_events.h>

#include "ParameterIDs.h"
#include "Profiles.h"
#include "Settings.h"

#include <algorithm>
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

    UserGroove groove;
    groove.id = root.getStringAttribute ("id");

    if (! isCanonicalId (groove.id))
        return refuse ("id '" + groove.id + "' is not a lower-case UUID");

    if (groove.id != juce::String (expectedId))
        return refuse ("id '" + groove.id + "' does not match the file name");

    groove.name = normaliseUserGrooveName (root.getStringAttribute ("name"));

    if (groove.name.isEmpty())
        return refuse ("missing or invalid name");

    for (const auto* attribute : { "bpm", "swing", "cachaca" })
        if (! root.hasAttribute (attribute))
            return refuse (juce::String ("missing ") + attribute);

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

void UserGrooveLibrary::rescan()
{
    bank.clear();
    skippedFiles.clear();
    scanned = true;
    ++scanGeneration;

    // A folder that does not exist is an empty library, and reading never
    // creates it: the first SAVE does.
    for (const auto& entry : juce::RangedDirectoryIterator (folder(), false,
                                                             juce::String ("*") + kExtension,
                                                             juce::File::findFiles))
    {
        const auto file = entry.getFile();
        const auto xml = juce::XmlDocument::parse (file);

        auto parsed = xml != nullptr ? fromXml (*xml, file.getFileNameWithoutExtension())
                                     : ParsedUserGroove { std::nullopt, "not readable XML" };

        if (parsed.groove.has_value())
            bank.push_back (std::move (*parsed.groove));
        else
            skippedFiles.add (file.getFileName() + ": " + parsed.error);
    }

    std::sort (bank.begin(), bank.end(), byName);
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
        if (groove.id == juce::String (id))
            return &groove;

    return nullptr;
}

juce::Result UserGrooveLibrary::write (const UserGroove& groove) const
{
    const auto target = folder();

    if (! target.createDirectory())
        return juce::Result::fail ("cannot create " + target.getFullPathName());

    // ATOMIC, through a temporary file that is swapped in: a crash or a full
    // disk mid-write leaves the old file whole. The temporary is named here
    // rather than left to `XmlElement::writeTo`, whose own is
    // `<id>_temp<hex>.forrogroove` — matching the scan's pattern, so a rescan
    // from another process mid-write, or a file left by a crash, was reported
    // as a refused groove the user never made. /code-review.
    const auto destination = fileFor (groove.id);
    juce::TemporaryFile temporary (destination,
                                   target.getChildFile (groove.id + "-" + juce::Uuid().toDashedString() + ".partial"));

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

juce::Result UserGrooveLibrary::overwrite (juce::StringRef id, const UserGroove& content)
{
    if (const auto refused = refuseIfNotifying(); refused.failed())
        return refused;

    rescan();
    const auto* existing = find (id);

    if (existing == nullptr)
        return juce::Result::fail ("no groove '" + juce::String (id) + "'");

    auto groove = content;
    groove.id   = existing->id;
    groove.name = existing->name;

    if (const auto result = write (groove); result.failed())
        return result;

    rescan();
    notify (Change::Kind::overwritten, groove.id);
    return juce::Result::ok();
}

juce::Result UserGrooveLibrary::rename (juce::StringRef id, const juce::String& name)
{
    if (const auto refused = refuseIfNotifying(); refused.failed())
        return refused;

    const auto normalised = normaliseUserGrooveName (name);

    if (normalised.isEmpty())
        return juce::Result::fail ("invalid name");

    rescan();
    const auto* existing = find (id);

    if (existing == nullptr)
        return juce::Result::fail ("no groove '" + juce::String (id) + "'");

    auto groove = *existing;
    groove.name = normalised;

    if (const auto result = write (groove); result.failed())
        return result;

    rescan();
    notify (Change::Kind::renamed, groove.id);
    return juce::Result::ok();
}

juce::Result UserGrooveLibrary::remove (juce::StringRef id)
{
    if (const auto refused = refuseIfNotifying(); refused.failed())
        return refused;

    rescan();

    if (find (id) == nullptr)
        return juce::Result::fail ("no groove '" + juce::String (id) + "'");

    if (! fileFor (id).deleteFile())
        return juce::Result::fail ("cannot delete " + fileFor (id).getFullPathName());

    rescan();
    notify (Change::Kind::removed, juce::String (id));
    return juce::Result::ok();
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

void applyUserGroove (State& state, const UserGroove& groove)
{
    state.lanes = groove.lanes;
    recordGrooveLoad (state, ids::userProfile, groove.id);
}

} // namespace forrobox
