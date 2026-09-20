/* ============================================================================
   FORRÓ BOX — an invisible clickable rectangle

   A container that wants one region of itself to be clickable has two choices:
   override `mouseUp` and test a layout rectangle by hand, or put a child there.
   Before 05-04 every `mouseUp`, `mouseMove` and `mouseExit` override in `src/`
   was on a CONTROL; that plan added three to `SequencerGrid` alone, which then
   reimplemented hover tracking, cursor switching and targeted repaint — all
   three things a Component already does.

   The cost was not only repetition. `/simplify` found the right-click guard
   missing from every container that had hand-rolled its own hit test, because
   each one restated the dispatch and each one forgot the same clause. A child
   inherits it from `Component`'s own routing instead.

   Deliberately NOT used for `KitOverlay::mouseUp`. That is the INVERSE test —
   a click anywhere EXCEPT the panel dismisses — which is a scrim, not a zone,
   and the overlay's z-order is already delicate enough that `PatternPads`
   carries a comment about a rebuild burying its close button.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Surface.h"

#include <functional>

namespace forrobox
{

class HitZone final : public juce::Component
{
public:
    HitZone();
    ~HitZone() override = default;

    /** Fired on a left-click released inside. Right-click falls through to the
        host, as `Button::mouseDown` has required since 04-03. */
    std::function<void()> onClick;

    /** Told when the pointer enters or leaves, so the owner can repaint the
        thing this zone sits over. The zone paints NOTHING itself. */
    std::function<void (bool isHovered)> onHoverChanged;

    /** Whether the pointer is currently inside. */
    bool isHovered() const noexcept { return hovered; }

private:
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void setHovered (bool);

    bool hovered { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HitZone)
};

// ── inline, and the file has no .cpp ────────────────────────────────────────
//  Four bodies totalling fifteen lines, against 1.6 s of clean-build time for a
//  translation unit whose whole cost is including juce_gui_basics.h — measured
//  by /simplify. `SelectableTile.h`, added in the same plan with the same kind
//  of content, was header-only from the start; this was the inconsistency.
//  Included by exactly two headers, both of which already pull in
//  juce_gui_basics, so nothing pays for it twice.

inline HitZone::HitZone()
{
    // The cursor is the zone's own — SequencerGrid.cpp says why it used to not be.
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

inline void HitZone::mouseUp (const juce::MouseEvent& event)
{
    // The rule is `isPlainClickInside` in Surface.h, shared with SelectableTile
    // — these two shipped it byte-identical before /simplify.
    if (isPlainClickInside (event, getLocalBounds()) && onClick != nullptr)
        onClick();
}

inline void HitZone::mouseEnter (const juce::MouseEvent&) { setHovered (true); }
inline void HitZone::mouseExit  (const juce::MouseEvent&) { setHovered (false); }

inline void HitZone::setHovered (bool shouldBeHovered)
{
    if (hovered == shouldBeHovered)
        return;

    hovered = shouldBeHovered;

    if (onHoverChanged != nullptr)
        onHoverChanged (hovered);
}


} // namespace forrobox
