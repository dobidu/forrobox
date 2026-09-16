/* ============================================================================
   FORRÓ BOX — a component's slice of the pattern

   The sequencer grid and the bateria kit overlay are two views of one stored
   pattern: a rectangle of StepPads, row-major and dense, that shows what the
   lanes hold and writes back when one is clicked. Everything about that was
   spelled TWICE — the pad vector, the step window, the publication generation,
   the rebuild, the lookup, the toggle, the refresh and the follower — differing
   only in which lanes a row covers.

   It was not a theoretical duplication. BOTH of 05-04's Task 2 fixes were
   re-fixes of bugs the grid had already had:

     - "reads the pattern once and never again", 05-01's bug, re-shipped in the
       overlay and found by the same /code-review question one plan later;
     - "records the publication generation OUTSIDE the lock", 05-03's bug, which
       that plan shipped twice and 05-04 then wrote out a third time.

   Two fixes, applied to two copies, one plan apart. 05-04's close hoisted
   `readStepWindow` and `snapshotPattern`; this is the rest, extracted before
   Phase 6's side panel makes a third copy of it.

   COMPOSITION, not a base class. A grid and a modal overlay have different
   painting, different layout, different lifetimes and different owners for their
   pads — what they share is this rectangle and nothing else.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "StepPad.h"
#include "VoiceEngine.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class ForroBoxAudioProcessor;

namespace forrobox
{

/** What one row of a pattern view shows and writes.

    The ONLY difference between the two views. The grid's five rows read
    `lanesForRow` and write `writeLaneForRow` — four lanes collapsed into the
    BATERIA row, one each for the rest. The overlay's four read and write one
    lane each. `displayedVelocity` takes a cover and returns the max across it,
    which degenerates correctly for a cover of one, so one function serves both
    rather than one function and a special case. */
struct PatternRow
{
    /** The lanes whose maximum velocity this row displays. */
    detail::LaneCover read {};

    /** The lane a click on this row toggles, or -1 for a row that cannot be
        edited. -1 rather than lane 0: a row with no writable lane must do
        NOTHING, and a fallback of 0 would silently edit ZABUMBA — the bug
        `writeLaneForRow` records finding at 05-01. */
    int write { -1 };

    juce::Colour colour;
};

/** The rectangle of pads, and everything that keeps it in step with the state.

    Not a Component. The pads are children of whatever their owner says — the
    grid parents them to itself, the overlay to its sliding panel — so this holds
    them and places none of them. Layout stays with the view that has one. */
class PatternPads
{
public:
    /** `host` is the component the pads become children of, and it must outlive
        this. Passed rather than inferred: the overlay's pads belong to its PANEL
        child, not to the overlay, because the panel is what fades and slides. */
    PatternPads (ForroBoxLookAndFeel&, juce::Component& host);

    /** The processor this view reads and writes through. Null until attached,
        and every method is a no-op until then — the headless UI tests build views
        with no processor at all. */
    void setProcessor (::ForroBoxAudioProcessor*);

    /** The row table. Replacing it rebuilds, because the pad colours come from
        it. */
    void setRows (std::vector<PatternRow>);

    /** Clear and recreate every pad for the current step window.

        Idempotent. Appending would stack a second set invisibly over the first
        and let a later lookup index past the rows — the shape
        `Chassis::attachParameters` records. */
    void rebuild();

    /** Pull every pad into step with the stored pattern. */
    void refreshFromState();

    /** Follow writers other than this view.

        The step WINDOW first: it changes how many pads there are, so a velocity
        refresh against the old count would leave 16 pads showing a 32-step
        window. Then the publication generation, which `publishIfChanged`
        increments when the lanes DIFFER and not otherwise — so this sees a host
        recall, a profile load and the view's own click, and does not see the
        reads the state handle is also taken for. */
    void refreshIfStateChanged();

    /** Toggle one cell, through the row's OWN write lane. */
    void toggle (int row, int step);

    StepPad* padFor (int row, int step) const;

    int getStepCount() const noexcept { return stepCount; }
    int getNumRows() const noexcept { return static_cast<int> (rows.size()); }

    /** The publication these pads are currently showing, for the tests. */
    std::uint32_t getGeneration() const noexcept { return lastPatternGeneration; }

    /** Called for each pad as it is built, so an owner can apply state a fresh
        pad does not carry — the grid's row dimming, which must survive a rebuild
        on a STEPS change landing while a channel is muted. */
    std::function<void (StepPad&, int row, int step)> onPadCreated;

    /** Called after a rebuild, so an owner can re-place what it just replaced.
        `refreshIfStateChanged` rebuilds on a step-window change, and the pads
        have no bounds until their owner gives them some. */
    std::function<void()> onRebuilt;

private:
    ForroBoxLookAndFeel& lnf;
    juce::Component& host;

    ::ForroBoxAudioProcessor* processor { nullptr };

    std::vector<PatternRow> rows;

    /** Row-major and DENSE: `row * stepCount + step`.

        `Chassis::PlacedKnob` stores its cell instead, because its pool SKIPS a
        channel with no parameter and one skipped slot would shift every later
        knob into the wrong strip. A rebuild here cannot skip: it is an
        unconditional nested loop over every row and every step. Carrying the
        coordinates anyway meant a flat list that three call sites then had to
        un-flatten. Settled at 05-01, by /simplify. */
    std::vector<std::unique_ptr<StepPad>> pads;

    int stepCount { 0 };

    /** The pattern publication these pads are showing. Recorded after every
        refresh — including the one `toggle` does for itself, so a view's own
        edit does not come back a frame later as a second refresh. */
    std::uint32_t lastPatternGeneration { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternPads)
};

} // namespace forrobox
