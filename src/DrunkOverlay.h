/* ============================================================================
   FORRÓ BOX — the drunk wash

   `PLANNING.md:567-569` and css:90-98: a warm wash fades over the WHOLE chassis
   as `CACHAÇA` climbs past 65, three gradients composited and then blended onto
   everything already drawn with `mix-blend-mode: screen`.

   THE MECHANISM IS FORCED, and so is its cost. `juce::Graphics` has no blend
   modes and no filters, so the only way to screen-blend is to have the pixels.
   `Component::paintEntireComponent` is public but NOT virtual
   (juce_Component.h:1185), and a component made invisible receives no mouse
   events — so the chassis cannot be made to paint only into an image we own. It
   paints itself normally, and this layer then re-renders it into a buffer of its
   own, post-processes that, and draws it back on top. TWO chassis paints per
   frame, every frame the wash is on: measured at 4.5 ms and 11.9 ms on this
   machine, against a 33 ms budget at 30 Hz.

   `Component::createComponentSnapshot` is the obvious way to get that second
   rendering and is deliberately not used — `renderRegion` below says why, with
   the numbers, and the reason is a visible seam rather than a preference.

   That is why `paint` re-renders only `g.getClipBounds()` rather than the whole
   1200×780. A knob's repaint dirties about sixty pixels square; re-rendering the
   chassis for it would make every LED decay cost a full second paint. The wash
   is a function of position, so a partial region only needs the matching part of
   the gradient — which is what `originInWash` is for.

   SCREEN IS NOT AN ALPHA OVERLAY, and the difference is the whole reason the
   second pass is worth paying for: `out = dst + a·Cs·(1 − dst)` never darkens a
   pixel, where compositing the same colours source-over would darken the light
   theme everywhere the wash is warm. `no pixel darkens` is a check, not a note.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace forrobox
{

class DrunkOverlay final : public juce::Component
{
public:
    /** `--drunk = clamp((cachaça − 65) / 35, 0, 1)` — PLANNING.md:568, app.js:596.

        The two numbers are the design's, so they are named here and enrolled in
        `verify-theme.py` alongside the gradient stops rather than being typed
        into this file and the test that checks it. */
    static constexpr float kOnsetPercent = 65.0f;
    static constexpr float kSpanPercent  = 35.0f;

    static float amountFor (float cachacaPercent) noexcept;

    DrunkOverlay();

    void setAmount (float newAmount);
    float getAmount() const noexcept { return amount; }

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
                                        juce::Rectangle<int> area, float scale);

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

private:
    float amount { 0.0f };

    /** True while `renderRegion` is re-rendering the chassis.

        The chassis paints its children, and this is one of them — so without
        this, the re-render would paint this layer, which would re-render.
        Termination, not an optimisation. */
    bool insideRerender { false };

    int rerendersDone { 0 };
    int washBuilds { 0 };

    juce::Image unitWash;

    /** The full-size buffer the chassis is re-rendered into.

        A member rather than a local so a 60 Hz repaint does not allocate a
        megabyte per frame, and RELEASED when the wash turns off — this is the
        one place in the UI that holds device-resolution pixels, and holding
        them while the easter egg is dormant would be a cost with nothing to
        show for it. */
    juce::Image scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrunkOverlay)
};

} // namespace forrobox
