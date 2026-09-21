/* ============================================================================
   FORRÓ BOX — the two surface primitives every region shares

   `inset 0 1px 0 <highlight>` and the well's inset shadow are declared on the
   header, the side panel, the sequencer and the footer. They were private
   methods on Chassis while Chassis was the only thing painting a region; 04-05
   splits the header and the footer into their own components, and a mechanism
   three components need is not a private method of one of them.

   The colour is an ARGUMENT at every call, never read from a field here: the
   regions share the mechanism and NOT its value (see theme::Shadows). Reaching
   for a single field is what shipped the light header's highlight at 0.50 where
   the stylesheet says 0.05.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <utility>

namespace forrobox
{

/** The rate every region bar's state poll runs at.

    ONE name, because it was one decision written three times. `HeaderBar`,
    `FooterBar` and `SidePanel` each declared their own 30, and the newest one's
    comment said the other two "already settled on this rate for the same
    reason" — which is this file's own hoisting trigger, stated by the third
    copy rather than acted on.

    30 Hz is what a lit button, a tempo readout, a stored-profile highlight and
    a 200 ms tag fade need. Deliberately NOT the only rate in the codebase:
    `seq::kPlayheadPollHz` is 60 because a sweeping line is the one thing here
    the eye tracks continuously, and `kStepTilingPollHz` is 15 because it
    services host automation rather than a viewer. Three rates, three reasons —
    folding those in would be the opposite error, one name for three decisions. */
inline constexpr int kUiPollHz = 30;

/** True when this event is a plain click released inside `box`.

    TWO conditions, one law. Right-click belongs to the HOST — `Button::mouseDown`
    has said so since 04-03 — and a press that dragged out is not a click.

    Here because 06-05 collapsed five duplicated shapes and then wrote this one
    twice in the same plan: `HitZone::mouseUp` and `SelectableTile::mouseUp`
    shipped byte-identical bodies, in two files whose own headers argue that a
    law three classes restate is a law two of them will ship without.
    /simplify.

    `Button` and `Segmented` still state it themselves. Both gate on a `pressed`
    flag first and `Button` tests `contentBox()` rather than the local bounds,
    so folding them in is a change to their files rather than to these —
    recorded in PROJECT.md rather than done here. */
inline bool isPlainClickInside (const juce::MouseEvent& event, juce::Rectangle<int> box) noexcept
{
    return ! event.mods.isPopupMenu() && box.contains (event.getPosition());
}

/** A CSS `cubic-bezier(x1, y1, x2, y2)` easing, SOLVED rather than approximated.

    The curve is parametric: x and y are both cubics in a parameter s, and the
    easing is y at the s where x == t. A `smoothstep` looks like it and is a
    different function — which is exactly the plausible substitute this project's
    tests exist to catch, so callers pass the control points the stylesheet
    names and nothing is eyeballed.

    Here rather than in `KitOverlay`, where it was written for css:565's
    `(.2,.7,.3,1)`. 07-02 needs css:527's `ease-in-out`, which is
    `(.42,0,.58,1)` — a different curve through the same solver. Chassis.h's own
    rule: "a law that needs a comment naming its other home is a law that wants
    hoisting." */
double cubicBezierEase (double t, double x1, double y1, double x2, double y2) noexcept;

/** CSS `ease-in-out` — `cubic-bezier(0.42, 0, 0.58, 1)`, the curve css:527 and
    css:539 name by keyword.

    These four are BARE LITERALS on purpose, where `kit::kEaseX1..Y2` are named
    constants that `verify-geometry` enrols. The stylesheet writes the keyword
    `ease-in-out`, not the numbers, so there is nothing in the design source to
    compare them against — they are the CSS specification's definition of that
    keyword, not a design decision this project gets to make. Naming them would
    only move them into NOT_COMPARED for that same reason.

    JUCE ships the identical curve as `juce::Easings::createEaseInOut()`
    (juce_animation). Not adopted: that module is not linked, its result is a
    `std::function<float(float)>` where this is an inlinable `double`, and one
    curve does not justify a module. Recorded so the next reader does not have
    to rediscover it. */
inline double easeInOut (double t) noexcept
{
    return cubicBezierEase (t, 0.42, 0.0, 0.58, 1.0);
}

/** A juce::Timer that calls a std::function.

    Both region bars poll for the handful of things that have no parameter to
    attach to — the transport's atomic, the host's tempo, the persisted profile,
    the limiter's gain reduction — and each had written this same five-line
    adapter. Phase 5's sequencer and Phase 6's side panel are the third and
    fourth. Hoisted at 04-05 by /simplify, which judged that this is the ONE part
    of the two bars that is genuinely identical: their layouts, controls, paints
    and refresh signatures all differ, so a shared base class would hoist this
    and little else.

    JUCE runs every Timer off one shared thread, so a second instance is a list
    entry rather than a thread — measured at 04-05, which is why the two bars
    keep their own rather than sharing one tick. */
struct PollTimer final : juce::Timer
{
    void timerCallback() override { if (tick != nullptr) tick(); }

    /** Seconds since the previous call, or 0 on the first.

        An animation in this codebase is always TOLD its elapsed time and never
        reads a clock — 04-04, where three checks failed on MSVC's clock rather
        than on the code. The DRIVER has to read one, and the kit overlay and the
        side panel had written the identical eight lines to do it: the same
        `getMillisecondCounterHiRes() * 0.001`, the same `lastSeconds` member,
        the same `if (previous <= 0.0) return 0` with the same comment. Chassis.h
        states the rule this trips: "a law that needs a comment naming its other
        home is a law that wants hoisting". /simplify.

        `restart()` re-bases it, so a panel that has been shut for ten minutes
        reports one frame rather than ten minutes on its first tick. */
    double secondsSinceLastTick() noexcept
    {
        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const auto previous = std::exchange (lastSeconds, now);

        return previous <= 0.0 ? 0.0 : now - previous;
    }

    /** The same, clamped — a stalled message thread finishes an animation
        rather than skipping past it, and a clock that steps backwards never runs
        one in reverse.

        The BOUND is the caller's, not shared: the entrance, the flash and the
        CUSTOM tag's fade each have their own duration, which is the law
        `surface::glowDot` states for its radius. Four sites wrote out the same
        `jlimit (0.0, k…Seconds, secondsSinceLastTick())` with the same comment
        before this. /simplify. */
    double secondsSinceLastTick (double maxSeconds) noexcept
    {
        return juce::jlimit (0.0, maxSeconds, secondsSinceLastTick());
    }

    void restart() noexcept { lastSeconds = 0.0; }

    std::function<void()> tick;

private:
    double lastSeconds { 0.0 };
};

} // namespace forrobox

namespace forrobox::surface
{

/** `inset 0 1px 0 <highlight>` — the top edge of a raised panel. */
void raisedHighlight (juce::Graphics&, juce::Rectangle<int> area, juce::Colour highlight);

/** A glowing dot: `box-shadow: 0 0 <radius>px <colour>` around a filled circle.

    Written out three times before this — the strip's trigger LED, the side
    panel's timbre LED and its bundle dot — each `juce::DropShadow (c, r, {})`
    then `fillEllipse`, and only `HitVisualiser` carried the comment explaining
    why a zero-offset DropShadow is the right reproduction of a CSS glow.

    The PAINTER is shared; the RADIUS is not. Each rule declares its own blur and
    each caller passes its own constant, because a shared mechanism does not
    imply a shared value — the law `StepPad.h` and `Knob.h` both record.

    `HitVisualiser::paintLed` deliberately does NOT go through this: it shadows
    one rectangle and fills a different, smaller one, and its glow is skipped
    entirely at radius 0. Folding it in would change what it paints. */
void glowDot (juce::Graphics&, juce::Rectangle<int> box, juce::Colour, int glowRadius);

/** The inset well shadow, as a vertical gradient down from the top edge.

    A real Gaussian inner shadow is not worth a blur pass here: the design's
    `inset 0 2px 6px` reads as a short dark gradient at the top edge. */
void wellShadow (juce::Graphics&, juce::Rectangle<int> area, juce::Colour shadow, float depth);

} // namespace forrobox::surface
