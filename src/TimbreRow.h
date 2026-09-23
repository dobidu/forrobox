/* ============================================================================
   FORRÓ BOX — one timbre row

   `.timbre` — css:416-429: a bordered box on `--panel` carrying the character's
   name, its sub-label, and a 7 px LED that lights `--c-ganza` when the row is
   the selected one.

   Its own control, as `StepPad` is for `.pad` and `BpmField` for `.bpm`. Every
   distinct rule in this stylesheet that a user can click became a component of
   its own, and this is one: `Button` models `.btn` and paints a single label,
   which is neither the ground nor the two stacked labels this rule asks for.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "LookAndFeel.h"
#include "SelectableTile.h"
#include "Effects.h"
#include "MixBus.h"
#include "Surface.h"


namespace forrobox
{

class TimbreRow final : public SelectableTile
{
public:
    /** `index` is the parameter's own CHOICE index, which is also the row's
        index in `timbreSpecs` — the table `MixBus` reads its cutoff and drive
        from, so the row cannot name one character and sound like another. */
    TimbreRow (ForroBoxLookAndFeel&, int index);

    /** The height this row needs, without building one.

        `Button::heightOf`, `Knob::preferredHeight`, `Segmented::heightOf` and
        `ValueScreen::heightOf` are the same pattern, and the last one's docstring
        names it: the owner reserves the box by ASKING the control. The box model
        was computed in `SidePanelLayout` instead — its padding, its border, its
        two type rows and its LED, all from outside the class that paints them. */
    static int heightOf() noexcept;

    /** The LED's box, for the tests — `Knob::dialBounds`'s precedent, which is
        where a control's interior belongs rather than in the owner's layout. */
    juce::Rectangle<int> ledBounds() const noexcept;

    void paint (juce::Graphics&) override;

    /** True for the row `app.js:571` toggles the whole chassis on — asked of the
        TABLE by its `cssId`, never by a literal index. */
    bool isCiclotron() const noexcept;

    /** One offset copy of the name — css:425's `text-shadow`. */
    struct Fringe
    {
        juce::Colour colour;
        float        offsetPx;
    };

    /** css:425's two fringes, in PAINT order: back to front, which is the
        reverse of the order CSS lists text shadows in.

        A seam, because a render cannot answer either half of it.

        The COLOUR: the fringes are composited at 70% over the row's own
        background, so the reddest pixel in the result sits BETWEEN `--danger`
        (#ff4136) and `--c-bateria` (#e84646) — and a check asking which it is
        nearer answered bateria on a correct build. Only one of them is named by
        the stylesheet; that is the `accentSpecs`/`channelInfos` hazard one table
        over.

        The OFFSET: swapping which colour goes on which side moves each fringe's
        centre of mass, but not symmetrically — the glyph run is not symmetric
        and the black text covers the two differently, so the cyan centroid
        stayed left of the red one under a build that had them backwards. The
        render still proves both fringes are THERE; this is what says where. */
    static std::array<Fringe, 2> aberrationFringes (ForroBoxLookAndFeel&);

    /** css:426-427 — while this row is the SELECTED Ciclotron one, its
        sub-label turns `--danger` and blinks on a 1.4 s `steps(1)` loop.

        Told its elapsed time, like every other animation here. Does nothing on
        any other row, or on this one unselected: css:426 is
        `.timbre.ciclo.active`, a conjunction, and a check proves both halves. */
    void advanceBlink (double seconds);

    float blinkOpacityForTest() const noexcept;

    /** `SelectableTile`'s hook: the blink starts and stops with the selection.

        An OVERRIDE rather than a `refreshBlink()` the owner must remember to
        call. It shipped as the latter for a revision and cost nine paired
        `setSelected(...)` / `refreshBlink()` call sites, two of them in
        production — so one forgotten call gave a row with fringes and a frozen
        sub-label, and no check could see it because the tests carried the same
        pairing by hand. /simplify. */
    void selectionChanged() override;

private:
    /** css:427 — `0%,88%,100% { 1 } 90% { 0.25 } 92% { 1 } 96% { 0.4 }`, held
        between stops. A `steps(1)` track, so it CUTS. */
    KeyframeLoop blink { ciclo::kBlinkSeconds,
                         { { 0.00, ciclo::kBlinkOn },
                           { 0.88, ciclo::kBlinkOn },
                           { 0.90, ciclo::kBlinkDim },
                           { 0.92, ciclo::kBlinkOn },
                           { 0.96, ciclo::kBlinkHalf },
                           { 1.00, ciclo::kBlinkOn } },
                         [this] { repaint(); },
                         KeyframeTiming::steps1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimbreRow)
};

} // namespace forrobox
