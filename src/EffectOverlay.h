/* ============================================================================
   FORRÓ BOX — the chassis-wide effect layer

   ONE layer for however many treatments are active, and that is the point
   rather than a convenience. 08-04's `CACHAÇA` wash and 08-05's Ciclotron™
   degradation are both whole-chassis pixel effects, and the expensive part of
   either is the SECOND rendering of the chassis they both need. A second
   overlay beside this one would be the obvious shape and would cost a THIRD
   chassis paint whenever both are on. Measured, per frame at 1200×780:

       no treatment   4.4 ms
       wash only     12.1 ms      one re-render plus one pixel pass
       Ciclotron     11.3 ms      one re-render plus two
       both          14.2 ms      one re-render plus three

   — where two overlays would put both-on near 22 ms of a 33 ms budget, for a
   treatment that is a joke. So the chassis is re-rendered once and every active
   pass runs over those pixels. That is also why the buffer they share has ONE
   owner: see `releaseBuffersIfIdle`.

   THE PASSES STACK IN THE STYLESHEET'S ORDER, which is observable and checked:
   css:109's `filter` applies to the chassis element itself, css:102's
   scanline `::before` is z-index 58, and css:90's wash `::after` is 60. So
   degrade, then scanlines, then wash.

   `PLANNING.md:567-569` and css:90-98: a warm wash fades over the WHOLE chassis
   as `CACHAÇA` climbs past 65, three gradients composited and then blended onto
   everything already drawn with `mix-blend-mode: screen`.

   THE MECHANISM IS FORCED. `juce::Graphics` has no blend modes and no filters,
   so the only way to screen-blend — or to apply a CSS `filter` — is to have the
   pixels. `Component::paintEntireComponent` is public but NOT virtual
   (juce_Component.h:1185), and a component made invisible receives no mouse
   events — so the chassis cannot be made to paint only into an image we own. It
   paints itself normally, and this layer then re-renders it into a buffer of its
   own, post-processes that, and draws it back on top. TWO chassis paints per
   frame, every frame any treatment is on; the table above is what that costs.

   `Component::createComponentSnapshot` is the obvious way to get that second
   rendering and is deliberately not used — `renderRegion` below says why, with
   the numbers, and the reason is a visible seam rather than a preference.

   That is why `paint` re-renders only `g.getClipBounds()` rather than the whole
   1200×780. A knob's repaint dirties about sixty pixels square; re-rendering the
   chassis for it would make every LED decay cost a full second paint. Both the
   wash and the scanlines are functions of POSITION, so a partial region needs
   the matching part of each — which is what `originInWash` and
   `firstDeviceRow` are for.

   AND THE THREE PASSES STAY THREE. /simplify benchmarked fusing them into one
   loop over the buffer: 3.56 ms as three passes against 3.66 ms fused, because
   the buffer streams out of memory either way and the per-row scanline branch
   costs more inside the hot body than two extra traversals save. Stepping the
   scanline pass by its period instead of testing every row measured identical
   to three decimal places. Both recorded so the next reader does not re-derive
   them from first principles and get the sign wrong.

   SCREEN IS NOT AN ALPHA OVERLAY, and the difference is the whole reason the
   wash's pass is worth paying for: `out = dst + a·Cs·(1 − dst)` never darkens a
   pixel, where compositing the same colours source-over would darken the light
   theme everywhere the wash is warm. `no pixel darkens` is a check, not a note
   — and it is a check on THAT pass alone, because the scanlines beside it are
   source-over black and do nothing but darken.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Effects.h"
#include "Surface.h"

namespace forrobox
{

class EffectOverlay final : public juce::Component
{
public:
    static float amountFor (float cachacaPercent) noexcept;

    EffectOverlay();

    void setAmount (float newAmount);
    float getAmount() const noexcept { return amount; }

    /** css:109 and css:102-116 — the Ciclotron™ degradation, on or off.

        A BOOL where the wash takes a ramp: css gives the treatment a
        `transition: opacity 0.3s` on the overlay and nothing on the filter, and
        the trigger is a three-way choice rather than a continuous knob. */
    void setCiclotron (bool);

    bool isCiclotron() const noexcept { return ciclotron; }

    /** Advance the scanline flicker by a known number of seconds. Told its
        elapsed time like every other animation here; does nothing when the
        treatment is off. */
    void advanceFlicker (double seconds);

    float flickerOpacityForTest() const noexcept { return flicker.value(); }

    /** css:109's `filter: saturate(0.9) contrast(1.06)`, in place.

        SATURATE THEN CONTRAST, because CSS applies filter functions left to
        right, and they do not commute — saturate mixes channels toward
        luminance and contrast pushes each away from mid-grey.

        In sRGB, not linear light: the CSS Filter Effects shorthand functions
        operate on the sRGB values, unlike a bare SVG `feColorMatrix`.

        This pass needs to know which byte is RED, where `screenOnto` does not —
        the saturate matrix mixes the channels, so their order is no longer
        irrelevant. */
    static void degradeOnto (juce::Image& destination);

    /** css:102-108's scanlines, composited SOURCE-OVER at `opacity`.

        `rgba(0,0,0,0.14)` over the first 1 of every 3 DEVICE rows. Device, not
        design: the stylesheet's `1px` is drawn on the device grid, so a
        scanline that scaled with the editor would be 2 px thick at 2x and stop
        reading as a scanline.

        @param firstDeviceRow  which row of the chassis `destination`'s top row
                               is, so a partial repaint gets the same bands a
                               full one draws there. The `originInWash` problem,
                               one pass over. */
    static void scanlinesOnto (juce::Image& destination, int firstDeviceRow, float opacity);

    void paint (juce::Graphics&) override;

    /** The three gradients of css:91-95 at full strength, as a premultiplied
        ARGB image the size of the chassis IN DEVICE PIXELS.

        Built at strength 1 and cached, because `amount` changes on every frame
        of a knob drag while the gradients do not — `screenOnto` scales it. A
        wash rebuilt per frame would be three ellipse solves per pixel, which is
        the expensive half of this whole file.

        Static so the tests can measure and assert it with no component tree. */
    static juce::Image buildUnitWash (int width, int height);

    /** Re-renders `source` into `scratch`, at its OWN coordinates, clipped to
        `area` — and hands back the sub-image covering that area.

        NOT `Component::createComponentSnapshot`, and what follows is MEASURED
        rather than reasoned — two explanations of it have already been wrong.

        That method grabs a region into an image of the region's SIZE, so the
        component paints at an offset. Substituting it back in and running the
        partial-repaint check gives a maximum channel difference, against what
        an unclipped repaint puts in the same pixels, of **3/255 at 1x, 3/255 at
        1.5x and 88/255 at 1.1x**. The wash draws its result back over the
        chassis, so that is a visibly different rectangle at the boundary of
        every dirty region: a seam around each knob, LED and meter the moment
        CACHAÇA passes 65, and a bad one at an awkward scale.

        WHAT IS NOT THE CAUSE, each checked rather than assumed. Not the
        identity `AffineTransform::scale` that method also adds when the grab is
        not the whole component — the first fix removed exactly that and the
        difference stayed at 3. And not gradient dithering, which a second
        version of this comment asserted: `juce_graphics` contains no dithering
        outside its JPEG decoder, and `GradientPixelIterators::Linear` indexes a
        LUT by a position that an integer translation shifts identically at both
        ends. /simplify caught that claim.

        What the numbers do point at, for the 1.1x case, is that method's own
        scale arithmetic: it derives the image size as `roundToInt (scaleFactor *
        width)` and then adds `scale (w / width, h / height)`, a per-region
        rounded ratio that is not `scaleFactor` for most region sizes. The 3/255
        at 1x, where that ratio is exactly 1, is not explained — and is recorded
        as unexplained rather than given a third guess.

        Painting into a full-size buffer at the component's OWN coordinates
        avoids all of it, and the clip keeps the cost proportional to the region
        rather than to the chassis.

        Caught by widening the partial-repaint check to the boxes
        `pollVisualisers` actually dirties — the three arbitrary rectangles it
        started with all missed it.

        The DEVICE rectangle comes back with the image because the caller has to
        draw it at exactly the logical position it was taken from. Rounding the
        origin and the size independently — `round(s·x) + round(s·w)` rather
        than the smallest integer container of `s·(x, w)` — put the blit half a
        device pixel out at any fractional editor scale, which is every window
        width that is not a multiple of 1200. /code-review. */
    struct RenderedRegion
    {
        juce::Image          image;    ///< a view into `scratch`, not a copy
        juce::Rectangle<int> device;   ///< where that view sits, in device pixels
    };

    static RenderedRegion renderRegion (juce::Component& source, juce::Image& scratch,
                                        juce::Rectangle<int> area, float scale, int& builds);

    /** `out = dst + amount·P·(1 − dst)`, in place, where P is the wash's
        PREMULTIPLIED colour — which is why no division by its alpha appears.

        @param originInWash  where `destination`'s top-left sits in the wash, so
                             a partial repaint gets the gradient it belongs
                             under. */
    static void screenOnto (juce::Image& destination, juce::Point<int> originInWash,
                            const juce::Image& unitWash, float amount);

    /** How many times this layer has asked the chassis to re-render itself.

        A test seam, and the only honest way to check AC-5: "costs nothing when
        it is off" is a claim about work NOT done, and a timing threshold would
        pass on a fast machine with the second rendering still running. */
    int rerendersDoneForTest() const noexcept { return rerendersDone; }

    /** How many times the gradient has been built.

        The other half of the same law: `paint` keeps the wash across an off/on
        cycle precisely so a knob dragged over the 65% onset does not rebuild
        three ellipse solves per pixel on the message thread every time it
        crosses. A comment saying so is not a guarantee — this is what a check
        reads. */
    int washBuildsForTest() const noexcept { return washBuilds; }

    /** How many times the render buffer has been allocated.

        The other half of the same law, and it exists because a mutation
        survived without it. `setAmount` and `setCiclotron` each used to decide
        when to free `scratch` and they disagreed — dragging CACHAÇA down across
        65 with CICLOTRON selected threw away a buffer the flicker was still
        repainting into, at 3.7 MB freed and re-allocated per frame. Nothing
        could see it: every pixel the treatment draws is still CORRECT, so no
        render check can fail. Only the allocation count moves. */
    int scratchBuildsForTest() const noexcept { return scratchBuilds; }

private:
    float amount { 0.0f };
    bool  ciclotron { false };

    /** css:110-116 — the 4 s `steps(1)` flicker. A step track, not an eased
        one: the dips are cuts. */
    KeyframeLoop flicker { ciclo::kFlickerSeconds,
                           { { 0.00, ciclo::kFlickerBase },
                             { 0.47, ciclo::kFlickerDip1 },
                             { 0.48, ciclo::kFlickerPeak1 },
                             { 0.49, ciclo::kFlickerBase },
                             { 0.92, ciclo::kFlickerDip2 },
                             { 0.93, ciclo::kFlickerPeak2 },
                             { 0.94, ciclo::kFlickerBase },
                             { 1.00, ciclo::kFlickerBase } },
                           [this] { repaint(); },
                           KeyframeTiming::steps1 };

    /** True while `renderRegion` is re-rendering the chassis.

        The chassis paints its children, and this is one of them — so without
        this, the re-render would paint this layer, which would re-render.
        Termination, not an optimisation. */
    bool insideRerender { false };

    int rerendersDone { 0 };
    int washBuilds { 0 };
    int scratchBuilds { 0 };

    juce::Image unitWash;

    /** The full-size buffer the chassis is re-rendered into.

        A member rather than a local so a 60 Hz repaint does not allocate a
        megabyte per frame, and RELEASED when the wash turns off — this is the
        one place in the UI that holds device-resolution pixels, and holding
        them while the easter egg is dormant would be a cost with nothing to
        show for it. */
    juce::Image scratch;

    /** Frees the render buffer when NO treatment is left to use it.

        ONE predicate, in one place. `setAmount` and `setCiclotron` each used to
        decide this for themselves and they disagreed: the wash's setter freed
        `scratch` on `amount <= 0` alone, so dragging CACHAÇA down across 65
        with CICLOTRON selected threw away a buffer the flicker was still
        repainting into at 30 Hz — a 3.7 MB free and re-allocation per frame at
        1x, 15 MB at 2x, on the message thread. Exactly the thrash the wash
        cache two members up exists to avoid.

        This is the cost of one class carrying two features, and the reason it
        is worth paying once here: a third pass must widen THIS, not every
        setter's `&&` chain. /simplify. */
    void releaseBuffersIfIdle();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectOverlay)
};

} // namespace forrobox
