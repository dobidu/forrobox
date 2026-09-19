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
/* ── the laws two pattern views both obey ───────────────────────────────────

   Declared HERE, not in SequencerGrid.h where they started. `PatternPads` was
   hoisted OUT of the grid, and it then included the grid's header to get its
   own vocabulary back — the dependency arrow pointing at the thing it was
   extracted from. That cost every consumer `SequencerLayout`, `Chassis.h`,
   `Button.h`, `ChoiceButtonsAttachment.h`, `Typography.h` and `Surface.h` for
   six declarations, and told the next reader the grid owns a law two non-grid
   components obey. `SequencerGrid.h` includes this file, so every existing call
   site is unchanged. /simplify.
   ------------------------------------------------------------------------- */

namespace seq
{
/** The velocity a click writes, and the one it clears to — app.js:390-395.

    100, not 127: the prototype's `togglePad` writes 100, and a pad toggled on
    should look like the profiles' own mid-strong hits rather than the loudest
    value the format allows. */
/** 60 fps — the sweep is the only thing here that must be SMOOTH rather than
    merely current, so it polls faster than the header's and footer's 30 Hz. */
inline constexpr int kPlayheadPollHz = 60;

inline constexpr int kToggleOnVelocity = 100;
inline constexpr int kToggleOffVelocity = 0;
} // namespace seq

/** Which lanes one grid row covers.

    Four of the eight lanes share the BATERIA channel, so its row covers four and
    every other row covers one. DERIVED rather than written down, for the reason
    `VoiceEngine::channelForLane` and `ghostingKitLane` are: a table saying
    "bateria is lanes 4-7" would be a second copy that a reordered lane list could
    silently invalidate.

    A lookup into `detail::channelToLanes`, which is built once at compile time
    from the same two id lists the forward map uses. Returns a reference: there
    is nothing to construct. */
using LaneSet = detail::LaneCover;

const LaneSet& lanesForRow (int channelIndex);

/** The lane a click on one row WRITES.

    A row covering one lane writes that lane. The composite BATERIA row writes
    CAIXA alone — `app.js:389` records why in its own comment: "collapsed row
    edits caixa (cx) — the backbeat; deep edits live in the kit view". Writing all
    four would make one click destroy a pattern.

    Caixa is found by NAME, never by index, the way `ghostingKitLane` finds the
    hi-hat.

    Returns -1 for a row covering no lanes, which every caller already rejects
    through its bounds guard. A row like that cannot exist while
    `compositeChannel()` returns the FIRST lane-less channel, but returning lane
    0 for it — the old fallback — would have made a second lane-less channel edit
    ZABUMBA on every click instead of doing nothing. Found by /code-review. */
int writeLaneForRow (int channelIndex);

/** What one row DISPLAYS: the maximum velocity across the lanes it covers.

    `app.js:365-371` — four lanes collapse into one row, so the row lights if any
    of them does.

    Takes the row's lane cover rather than deriving it, so a refresh derives once
    per row instead of once per cell — see `LaneSet`. */
int displayedVelocity (const State& state, const LaneSet& covered, int step);

/** The active step window, from the PROCESSOR's one reader.

    A free function beside `lanesForRow` and `displayedVelocity`, which serve the
    same role: a law two pad-showing components both need. It was a member of the
    grid, `SequencerGrid::readStepCount`, and the overlay then spelled the same
    expression twice more — the divergence that member was extracted to end at
    05-03, fallback and all. 06-01 deleted the member: `PatternPads` is the only
    thing that asks the question now, from `rebuild` and `refreshIfStateChanged`,
    and both go through HERE. /simplify, then /code-review. */
int readStepWindow (const ::ForroBoxAudioProcessor*);

/** The pattern and the publication it belongs to, taken under ONE lock.

    The generation MUST come out from inside the lock: `~LockedState` publishes
    while the lock is still held, so reading it afterwards lets a writer land
    between the copy and the record and be recorded as already shown. 05-03
    shipped that bug twice, and 05-04 then wrote the corrected idiom out a third
    time in the overlay. It is a concurrency invariant, so it is a function
    rather than two identical comments. /simplify. */
State snapshotPattern (::ForroBoxAudioProcessor&, std::uint32_t& generation);

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

    /** The processor this view reads and writes through.

        Null until attached. `refreshFromState`, `refreshIfStateChanged` and
        `toggle` early-return while it is — but `rebuild` and `setRows` do NOT:
        `readStepWindow (nullptr)` answers with the first step window, so a view
        given rows but no processor builds a live 16-column rectangle of pads
        showing nothing. That is what the headless UI tests rely on, and an
        earlier version of this sentence said "every method is a no-op until
        then", which named a behaviour the code does not have. /code-review. */
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

    /** Fire the confirmation flash on every LIT pad — `PLANNING.md:615`,
        `app.js:546`'s `.pad.on`. Both views hold one of these, so a reload
        flashes the sequencer and the kit overlay alike. */
    void flashLitPads();

    /** Advance every pad's flash by elapsed SECONDS it is TOLD. */
    void advanceFlash (double seconds) noexcept;

    int getStepCount() const noexcept { return stepCount; }
    int getNumRows() const noexcept { return static_cast<int> (rows.size()); }

    /** The publication these pads are currently showing.

        Public so a test can watch two views follow ONE publication rather than
        each other — see `testTheTwoViewsFollowOnePublication`, which is the only
        check that distinguishes a shared follower from two that agree. */
    std::uint32_t getGeneration() const noexcept { return lastPatternGeneration; }

    /** Called at the end of EVERY rebuild, from whichever path reached it.

        A rebuild replaces every pad, so the owner has to re-place them and
        re-apply anything a fresh pad does not carry — bounds, and the grid's row
        dimming. There was a second hook, `onPadCreated`, firing once per pad to
        seed the dim; and this one fired from only ONE of the three rebuild
        paths, so both owners hand-wrote `resized()` after the other two. The
        contract was "after a rebuild the owner gets a word" everywhere except
        where the owner called rebuild itself — which is the class of rule this
        whole class exists to delete. /simplify. */
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
