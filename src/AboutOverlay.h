/* ============================================================================
   FORRÓ BOX — the ABOUT panel

   Who made this, and where it lives. Opened from the gear menu's last item.

   INVENTED, like the gear, and for the same reason: `PLANNING.md` describes no
   About panel anywhere. The user asked for it at 08-01 UNIFY, which is the
   explicit decision PROJECT.md's design mandate requires before a control the
   design source does not specify may exist.

   NO SHARED BASE WITH `KitOverlay`, and that is a judgement rather than an
   omission. The plan asked for one to be extracted if it was worth taking;
   reading the two says it is not. `KitOverlay` is a pad grid, four kit rows,
   a parameter attachment and a reload flash — its overlay-ness is a scrim, an
   entrance curve and a close box, and of those only the curve is real code.
   That solver is ALREADY shared: 07-02 hoisted `cubicBezierEase` into
   `Surface.h` when the DRAG MIDI pulse needed the same machinery with different
   control points. So this calls the same function and there is nothing left to
   factor out. 06-01's rule for `PatternPads` was to extract when a second real
   caller exists; here the second caller already has what it needs.

   THE ENTRANCE CURVE IS THE KIT OVERLAY'S, deliberately. `css:565` is
   `.kit-overlay`'s rule and this panel has no rule of its own — so rather than
   invent a second easing, the chassis keeps ONE overlay entrance. Referencing
   `kit::kEase*` says that out loud; a private copy of the same four numbers
   would be a second set of magic constants with nothing tying them together.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "KitOverlay.h"
#include "Surface.h"

#include <array>

namespace forrobox
{

namespace about
{

/** One credited person. */
struct Author
{
    const char* name;   ///< UTF-8; goes through the charset gate
    const char* url;    ///< the FULL url, scheme and all — see below
};

/** THE ONE COPY. `ABOUT.md` ships the same names and links as documentation,
    and the test reads this array rather than a second transcription — a
    misspelt name is the kind of wrong no assertion notices when both sides were
    typed by the same hand. Same law as the groove tables one layer down.

    THE FULL URL IS STORED AND THE DISPLAY IS DERIVED, not the other way round.
    The panel used to hold `npiq.cc` because that is what it drew; once the rows
    became clickable it needed something `juce::URL` could open, and a second
    field would have been two spellings of one address free to drift. `displayUrl`
    strips the scheme and any trailing slash for drawing. */
inline constexpr std::array<Author, 2> authors {{
    { "Carlos Eduardo Batista", "https://npiq.cc/" },
    { "Esmeraldo Filho",        "https://chicocorrea.bandcamp.com/" },
}};

inline constexpr const char* repository = "https://github.com/dobidu/forrobox";
inline constexpr const char* licence    = "GPLv3";

/** `https://npiq.cc/` -> `npiq.cc`. What the panel draws. */
juce::String displayUrl (juce::StringRef fullUrl);

/** Invented geometry, every number of it. Named so a test can measure the panel
    against its own declaration rather than against a literal typed twice. */
inline constexpr int kPanelWidth   = 420;
inline constexpr int kPanelPad     = 24;
inline constexpr int kRowGap       = 6;
inline constexpr int kBlockGap     = 18;
/** The kit overlay's own scrim, ASKED FOR. `kit::kScrimOpacity` is cross-checked
    against css:555 by `verify-geometry.py:838`; a second constant holding 78 is
    enrolled in nothing, so a stylesheet change would move one overlay's scrim
    and silently leave this one — two overlays over the same chassis, and the
    gate green. Same argument as the duration below. /simplify. */
inline constexpr float kScrimOpacity = kit::kScrimOpacity;

/** Seconds — the kit overlay's own, ASKED FOR rather than copied. A chassis
    with two different overlay speeds reads as a bug, and two constants holding
    0.2 is how they drift apart. */
inline constexpr double kEntranceSeconds = kit::kEntranceSeconds;

} // namespace about

/** The panel. Draws itself; owns no state but its own entrance. */
class AboutOverlay final : public juce::Component
{
public:
    explicit AboutOverlay (ForroBoxLookAndFeel& lookAndFeelToUse);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Open or close. Opening restarts the entrance from zero. */
    void setOpen (bool shouldBeOpen);

    /** Advance the entrance by elapsed SECONDS IT IS TOLD.

        Never reads a clock. `KitOverlay`, `GainReductionMeter` and
        `HitVisualiser` all carry this law and the same reason: three 04-04
        checks failed on MSVC's clock rather than on the code. */
    void advanceEntrance (double seconds) noexcept;

    double getEntranceProgress() const noexcept { return progress; }

    /** The panel's box at rest, for the tests and for the paint. */
    juce::Rectangle<int> panelBounds() const noexcept;

    /** Where every row sits.

        ONE source for the paint AND the hit test. They were the same inline
        `removeFromTop` walk until the rows became clickable, and two walks that
        must agree is how a link ends up opening from a rectangle that is not
        the one the text was drawn in — invisible until someone clicks two
        pixels off. `ChassisLayout` and `KitOverlayLayout` reserve their boxes
        for the same reason. */
    struct Layout
    {
        juce::Rectangle<int> panel;
        juce::Rectangle<int> title;
        std::array<juce::Rectangle<int>, about::authors.size()> names;
        std::array<juce::Rectangle<int>, about::authors.size()> urls;
        juce::Rectangle<int> repository;
        juce::Rectangle<int> licence;
    };

    Layout layout() const noexcept;

    /** The full url under `point`, or an empty string. Public so a test can ask
        which link a coordinate resolves to rather than inferring it from a
        browser that must not open during a test run. */
    juce::String urlAt (juce::Point<int> point) const;

private:
    ForroBoxLookAndFeel& lnf;
    PollTimer            entrancePoll;
    double               progress { 0.0 };

    /** The row the pointer is over, so a link can show that it is one. */
    juce::String hoveredUrl;

    void poll();

    void drawLink (juce::Graphics&, juce::Rectangle<int> row, juce::StringRef text,
                   juce::StringRef fullUrl, int rise, float eased);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutOverlay)
};

} // namespace forrobox
