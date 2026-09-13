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

namespace forrobox
{

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

    std::function<void()> tick;
};

} // namespace forrobox

namespace forrobox::surface
{

/** `inset 0 1px 0 <highlight>` — the top edge of a raised panel. */
void raisedHighlight (juce::Graphics&, juce::Rectangle<int> area, juce::Colour highlight);

/** The inset well shadow, as a vertical gradient down from the top edge.

    A real Gaussian inner shadow is not worth a blur pass here: the design's
    `inset 0 2px 6px` reads as a short dark gradient at the top edge. */
void wellShadow (juce::Graphics&, juce::Rectangle<int> area, juce::Colour shadow, float depth);

} // namespace forrobox::surface
