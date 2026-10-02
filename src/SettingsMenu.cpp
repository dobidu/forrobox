#include "SettingsMenu.h"

#include "LookAndFeel.h"
#include "ParameterIDs.h"
#include "Typography.h"

namespace forrobox
{

// ── 08-02: the settings menu ───────────────────────────────────────────────
//
// A `juce::PopupMenu`, drawn by JUCE against this chassis's own LookAndFeel.
// That is the whole reason it is a PopupMenu rather than a panel: `PLANNING.md`
// specifies no settings UI at all — `:902` calls the prototype's tweaks panel
// "prototype-only scaffolding — not part of the plugin design" — so a custom
// panel would mean inventing a geometry, a type scale and a hit-test for five
// controls the design source never drew. A menu invents nothing but the gear.

namespace
{
/** Result ids. Zero is reserved: `PopupMenu` sends it for a dismissal.

    EVERY BASE IS OFFSET BY AN INDEX, never by a VALUE, and that is a bug fix
    rather than a style. The accent items were first built as `kAccentBase +
    percent`, which put 100% at 300 + 100 = 400 — exactly `kStepsBase`. Two menu
    items then shared one id, the dispatch's accent branch swallowed both step
    items, and choosing "Default step count -> 16" silently set the accent to
    100% instead while the step setting became unreachable from the UI
    altogether. Found by /code-review; the tick checks in `UiTest` caught it in
    the same run. An index cannot collide with a neighbouring base as long as no
    setting offers 100 choices, which `kSpan` asserts. */
enum SettingsMenuId
{
    kSpan       = 100,
    kThemeBase  = 1 * kSpan,
    kRadiusBase = 2 * kSpan,
    kAccentBase = 3 * kSpan,
    kStepsBase  = 4 * kSpan,
    kFontBase   = 5 * kSpan,
    kAboutId    = 9 * kSpan,
};

/** The accent values the menu offers, inside `PLANNING.md:859`'s 35-100 range.
    A menu cannot present a continuum, so it presents its ends and the middle —
    and the FLOOR and CEILING come from the settings table rather than being
    typed here, so widening the range widens the menu. */
constexpr std::array<int, 4> kAccentSteps { 35, 60, 80, 100 };

/** The theme labels, indexed by `Setting::theme`, sized like every other band
    from a table so the band's count is written once. /simplify. */
constexpr std::array<const char*, 2> kThemeNames { "Dark", "OP-1 Light" };
static_assert (kThemeNames.size()
                   == static_cast<size_t> (settings::info (Setting::theme).maxValue + 1),
               "one theme label per value the theme setting allows");

// NO BAND RUNS INTO THE NEXT, asserted against the tables themselves rather
// than against a literal. The accent ids were once `kAccentBase + percent`,
// which put 100% on 400 — `kStepsBase` exactly — so two items shared one id and
// the step setting became unreachable from the menu. A check in the test file
// spelled `300 + 4 <= 400`, every term typed there, and could not have seen a
// renumbering here. /code-review found the collision; /simplify moved the guard
// to where the constants are.
static_assert (kAccentBase + static_cast<int> (kAccentSteps.size()) <= kStepsBase,
               "the accent ids must end before the step ids begin");
static_assert (kThemeBase + static_cast<int> (kThemeNames.size()) <= kRadiusBase,
               "the theme ids must end before the corner-radius ids begin");
static_assert (kRadiusBase + static_cast<int> (settings::cornerRadiiPx.size()) <= kAccentBase,
               "the corner-radius ids must end before the accent ids begin");
static_assert (kStepsBase + static_cast<int> (ids::stepWindows.size()) <= kFontBase,
               "the step ids must end before the display-font ids begin");
static_assert (kFontBase + static_cast<int> (settings::fontNames.size()) <= kAboutId,
               "the display-font ids must end before the About id");

static_assert (kAccentSteps.front()
                   == forrobox::settings::info (forrobox::Setting::accentIntensity).minValue,
               "the menu's lowest accent must be the setting's floor");
static_assert (kAccentSteps.back()
                   == forrobox::settings::info (forrobox::Setting::accentIntensity).maxValue,
               "the menu's highest accent must be the setting's ceiling");
} // namespace

int SettingsMenu::themeItem (int index) noexcept  { return kThemeBase + index; }
int SettingsMenu::radiusItem (int index) noexcept { return kRadiusBase + index; }
int SettingsMenu::accentItem (int index) noexcept { return kAccentBase + index; }
int SettingsMenu::stepsItem (int index) noexcept  { return kStepsBase + index; }
int SettingsMenu::fontItem (int index) noexcept   { return kFontBase + index; }
int SettingsMenu::aboutItem() noexcept            { return kAboutId; }
int SettingsMenu::accentStepPercent (int index) noexcept
{
    return kAccentSteps[static_cast<size_t> (juce::jlimit (0, static_cast<int> (kAccentSteps.size()) - 1, index))];
}
int SettingsMenu::numAccentSteps() noexcept { return static_cast<int> (kAccentSteps.size()); }

juce::PopupMenu SettingsMenu::build (const Settings& store)
{

    juce::PopupMenu menu;

    // EVERY ITEM SHOWS ITS CURRENT VALUE with a tick. A menu that only sets is a
    // menu that cannot tell you what the plugin is doing, and these are exactly
    // the settings a user forgets having changed.
    // THE STORE IS READ ONCE, not per item or per setting: every read parses the
    // settings file, and the menu opens on a click. /simplify, 13-01.
    const auto snap = store.snapshot();

    juce::PopupMenu themeMenu;
    const auto theme = snap.get (Setting::theme);
    for (size_t i = 0; i < kThemeNames.size(); ++i)
        themeMenu.addItem (kThemeBase + static_cast<int> (i), kThemeNames[i], true, theme == static_cast<int> (i));
    menu.addSubMenu ("Theme", themeMenu);

    juce::PopupMenu radiusMenu;
    const auto radius = snap.get (Setting::cornerRadius);
    for (size_t i = 0; i < settings::cornerRadiiPx.size(); ++i)
        radiusMenu.addItem (kRadiusBase + static_cast<int> (i),
                            juce::String (juce::roundToInt (settings::cornerRadiiPx[i])) + " px",
                            true,
                            radius == static_cast<int> (i));
    menu.addSubMenu ("Corner radius", radiusMenu);

    juce::PopupMenu accentMenu;
    const auto accent = snap.get (Setting::accentIntensity);
    for (size_t i = 0; i < kAccentSteps.size(); ++i)
        accentMenu.addItem (kAccentBase + static_cast<int> (i),
                            juce::String (kAccentSteps[i]) + "%",
                            true,
                            accent == kAccentSteps[i]);
    menu.addSubMenu ("Accent intensity", accentMenu);

    // The one setting here that is not cosmetic. It seeds a FRESH instance and
    // never touches this one — see the processor's constructor.
    juce::PopupMenu stepsMenu;
    const auto steps = snap.get (Setting::defaultSteps);
    for (size_t i = 0; i < ids::stepWindows.size(); ++i)
        stepsMenu.addItem (kStepsBase + static_cast<int> (i),
                           juce::String (ids::stepWindows[i]),
                           true,
                           steps == static_cast<int> (i));
    menu.addSubMenu ("Default step count", stepsMenu);

    // The DISPLAY FONT, labelled from the same table the loader indexes, so the
    // name in the menu and the family that gets drawn cannot disagree.
    juce::PopupMenu fontMenu;
    const auto font = snap.get (Setting::displayFont);
    for (size_t i = 0; i < settings::fontNames.size(); ++i)
        fontMenu.addItem (kFontBase + static_cast<int> (i),
                          juce::String (settings::fontNames[i]),
                          true,
                          font == static_cast<int> (i));
    menu.addSubMenu ("Display font", fontMenu);

    menu.addSeparator();
    menu.addItem (kAboutId, juce::String::fromUTF8 ("About Forr\xc3\xb3 Box\xe2\x80\xa6"));

    return menu;
}

SettingsMenu::Result SettingsMenu::apply (int resultId, Settings& store)
{
    if (resultId == 0)
        return Result::dismissed;

    if (resultId == kAboutId)
        return Result::about;

    // EVERY BRANCH BOUNDS-CHECKS ITS OWN BAND. Only the accent one did, so
    // `kThemeBase + 7` wrote theme = 1 and returned TRUE — `store.set` clamps,
    // so an id the menu never built became a silent wrong-setting write while
    // this function's docstring promised it wrote nothing. Harmless only while
    // every band is full; the moment one shrinks it is a real wrong write.
    // /code-review.
    const auto within = [resultId] (int base, size_t count)
    {
        return resultId >= base
            && resultId < base + static_cast<int> (count);
    };

    if (within (kThemeBase, kThemeNames.size()))
        store.set (Setting::theme, resultId - kThemeBase);
    else if (within (kRadiusBase, settings::cornerRadiiPx.size()))
        store.set (Setting::cornerRadius, resultId - kRadiusBase);
    else if (within (kAccentBase, kAccentSteps.size()))
        store.set (Setting::accentIntensity,
                   kAccentSteps[static_cast<size_t> (resultId - kAccentBase)]);
    else if (within (kStepsBase, ids::stepWindows.size()))
        store.set (Setting::defaultSteps, resultId - kStepsBase);
    else if (within (kFontBase, settings::fontNames.size()))
        store.set (Setting::displayFont, resultId - kFontBase);
    else
        return Result::unknown;   // an id this menu never offered: no write

    return Result::changed;
}

namespace settings
{
void applyTo (ForroBoxLookAndFeel& lnf, const Settings& store)
{
    // THE SETTERS PHASE 4 LEFT WITH NO CALLERS. `LookAndFeel.h:59` says exactly
    // why they exist: "Both tweakables are user-facing in Phase 8's settings
    // menu, so they are settable now rather than being constants that have to
    // be dug out later." This is that caller.
    // ONE READ for all four, not one per setter. 13-01.
    const auto snap = store.snapshot();

    lnf.setMode (snap.themeMode());
    lnf.setCornerRadius (snap.cornerRadiusPx());
    lnf.setAccentIntensity (snap.accentIntensity());

    // PUSHED into the type system, not pulled from it. `Typography` is a leaf
    // the whole UI depends on; having it read the store would put a file open
    // behind every glyph, and 08-02 measured `Settings::get` at 12.4 us.
    type::setMonoFamily (snap.monoFamily());
}
} // namespace settings

} // namespace forrobox
