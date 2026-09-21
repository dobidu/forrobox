/* ============================================================================
   FORRÓ BOX — UI suite

   Phase 3 established that audio claims are proved by offline render plus
   measurement, with listening as a separate human checkpoint. This is the same
   split for the UI, and it works for the same structural reason: a
   juce::Component that is never added to a desktop needs no window peer, so it
   can be painted into a juce::Image with no display, no X11 and no host.

   The measurement helpers live HERE and not in TestHarness.h. ~250 of that
   header's lines are already DSP used by one suite, and STATE has carried
   "move the measurement layer out of TestHarness.h" since 03-02 because the
   other suites recompile on every edit to it. Adding a second measurement layer
   to the same header would make that worse in the same plan that could avoid it.

   Every helper below is self-tested against a synthetic component with a known
   answer, INCLUDING a case it must reject. That is not ceremony: Phase 3 found
   twelve wrong measurements before it found a wrong line of code, and the one
   instrument that already had a self-test was the pattern the other nine were
   missing.
============================================================================ */
#include <JuceHeader.h>

#include <cstring>

#include "TestHarness.h"
#include "TestSuites.h"
#include "FakePlayHead.h"

#include <FontData.h>

#include "Chassis.h"
#include "ChoiceAttachment.h"
#include "SequencerGrid.h"
#include "Playhead.h"
#include "HitVisualiser.h"
#include "KitOverlay.h"
#include "ProfileButton.h"
#include "TimbreRow.h"
#include "Profiles.h"
#include "DragMidiButton.h"
#include "FooterBar.h"
#include "HeaderBar.h"
#include "GainReductionMeter.h"
#include "Button.h"
#include "Knob.h"
#include "StepPad.h"
#include "Fader.h"
#include "ToggleAttachment.h"
#include "Segmented.h"
#include "ValueScreen.h"
#include "LogoMark.h"
#include "BpmField.h"
#include "BpmAttachment.h"
#include "KnobAttachment.h"
#include "ValueTooltip.h"
#include "LookAndFeel.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Theme.h"
#include "Typography.h"

using namespace fbtest;

namespace
{
using forrobox::Chassis;
using forrobox::ChassisLayout;
using forrobox::FooterBar;
using forrobox::FooterLayout;
using forrobox::GainReductionMeter;
using forrobox::SequencerGrid;
using forrobox::SequencerLayout;
using forrobox::DragMidiButton;
using forrobox::ForroBoxLookAndFeel;
using forrobox::Button;
using forrobox::Knob;
using forrobox::StepPad;
using forrobox::Fader;
using forrobox::Segmented;
using forrobox::ValueScreen;
using forrobox::LogoMark;
using forrobox::BpmField;
using forrobox::BpmAttachment;
using forrobox::KnobAttachment;
using forrobox::ValueTooltip;
namespace theme   = forrobox::theme;
namespace footer  = forrobox::footer;
namespace grmeter  = forrobox::grmeter;
namespace seq      = forrobox::seq;
namespace dragmidi = forrobox::dragmidi;
namespace type  = forrobox::type;
namespace pad   = forrobox::pad;
namespace fader = forrobox::fader;
namespace segmented = forrobox::segmented;
namespace logo  = forrobox::logo;
namespace bpmfield = forrobox::bpmfield;

// ── the measurement instruments ─────────────────────────────────────────────

/** Paints a component into an image at the given size. No peer, no display.

    `paintEntireComponent (g, false)` is the call that makes this work: it is
    the same entry point the real repaint path uses, so what is measured is
    what a host would show, not a second drawing path written for tests. */
juce::Image renderComponent (juce::Component& component, int width, int height)
{
    component.setBounds (0, 0, width, height);

    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics g (image);
    component.paintEntireComponent (g, false);

    return image;
}

/** One component's bounds in ANOTHER component's coordinate space.

    `Component::getBounds` is in the PARENT's space, and 04-05 made that matter:
    the header's controls moved into a HeaderBar whose own bounds start at the
    chassis origin, so their local coordinates happen to equal the chassis's —
    but the footer's bar starts at y=724, so its children's local bounds are
    small-y rectangles that alias straight into the HEADER's boxes. Two STYLE
    tests picked up the footer's OUTPUT toggle that way the moment it existed
    and reported that STYLE had two segments.

    Every comparison of a control's position against a layout rectangle goes
    through this now, the header's included — those were correct only by that
    coincidence. Pass the OWNER of the layout as the root: a header box against
    `headerBarOf(...)`, a footer box against `chassis.getFooterBar()`. */
juce::Rectangle<int> boundsIn (juce::Component& root, juce::Component& c)
{
    auto* parent = c.getParentComponent();

    return parent == nullptr ? c.getBounds() : root.getLocalArea (parent, c.getBounds());
}

juce::Colour pixelAt (const juce::Image& image, int x, int y)
{
    jassert (juce::isPositiveAndBelow (x, image.getWidth()));
    jassert (juce::isPositiveAndBelow (y, image.getHeight()));

    return image.getPixelAt (x, y);
}

/** ARGB as hex, because a colour mismatch printed as two decimal triples tells
    the reader nothing about which channel moved. */
juce::String hex (juce::Colour colour)
{
    return "0x" + juce::String::toHexString (static_cast<int> (colour.getARGB())).paddedLeft ('0', 8);
}

/** The per-channel slop a composited pixel needs, measured across the three
    compilers rather than guessed.

    The 1 px strip dividers are `--line` blended over `--bg`. Predicting that
    with `Colour::overlaidWith` lands one LSB off under GCC and Clang and two
    off under MSVC, whose rasteriser rounds the premultiplied blend differently.
    Reimplementing three rasterisers in a test would be three instruments each
    needing its own proof, for a difference of 2/255. */
inline constexpr int kCompositeSlop = 2;

/** Like checkPixel, but tolerant by `slop` per channel.

    For a pixel whose expected value comes from COMPOSITING rather than a flat
    fill: JUCE's rasteriser blends translucent colours in premultiplied 8-bit
    and `Colour::overlaidWith` does not agree with it to the last bit. */
void checkPixelNear (const juce::Image& image, int x, int y, juce::Colour expected,
                     int slop, const juce::String& description)
{
    const auto actual = pixelAt (image, x, y);

    const auto within = [slop] (juce::uint8 a, juce::uint8 b)
    {
        return std::abs ((int) a - (int) b) <= slop;
    };

    const auto ok = within (actual.getRed(), expected.getRed())
                 && within (actual.getGreen(), expected.getGreen())
                 && within (actual.getBlue(), expected.getBlue())
                 && within (actual.getAlpha(), expected.getAlpha());

    check (ok, description + " at (" + juce::String (x) + "," + juce::String (y) + ")"
                           + (ok ? juce::String()
                                 : fbtest::utf8 (" — expected ") + hex (expected) + fbtest::utf8 (" ±") + juce::String (slop)
                                       + ", got " + hex (actual)));
}

/** An exact pixel: checkPixelNear with no tolerance. */
void checkPixel (const juce::Image& image, int x, int y, juce::Colour expected,
                 const juce::String& description)
{
    checkPixelNear (image, x, y, expected, 0, description);
}

/** Total brightness over a region.

    The weight discriminator. Advance width is NOT usable for that: measured at
    24 px, the four Space Grotesk widths for "FORRO BOX" are 100.326 / 100.589 /
    100.646 / 100.777 — a 0.45% total spread that no honest tolerance separates
    from rounding, so a width assertion would pass with four copies of one
    weight. Ink mass over the same string spans 417.3 / 523.2 / 572.3 / 617.9. */
double contrastMass (const juce::Image& image, juce::Rectangle<int> area, juce::Colour background);

double inkMass (const juce::Image& image, juce::Rectangle<int> area)
{
    // Literally contrastMass against black: |b - 0| == b. One pixel loop, not
    // two copies of it — and the distinction that matters is the ARGUMENT, not
    // the arithmetic, which is what the two self-tests below exercise.
    return contrastMass (image, area, juce::Colours::black);
}

double inkMass (const juce::Image& image) { return inkMass (image, image.getBounds()); }

/** Ink measured as departure from a known background.

    `inkMass` is only meaningful over BLACK, which is why the font swatches
    paint black. Used on the chassis it is nearly useless: `--panel` has
    brightness 0.118, so a 100x20 empty strip region scores ~236 and any
    threshold low enough to detect text is one an empty region also clears. The
    first version of the head-row check below was exactly that — an assertion
    that could not fail.

    This sums |brightness - background brightness| instead, so an untouched
    region measures ~0 whatever colour it is. */
double contrastMass (const juce::Image& image, juce::Rectangle<int> area, juce::Colour background)
{
    const auto reference = background.getBrightness();
    auto total = 0.0;

    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            total += std::abs (pixelAt (image, x, y).getBrightness() - reference);

    return total;
}

/** The horizontal extent of contrasting pixels within a region.

    Lets a DRAWN string be measured, not just a computed width. Needed because
    a negative control that zeroed the tracking inside `drawTracked` passed
    every check: `trackedWidth` was tested, the drawing path was not, and the
    two each computed the tracking themselves. */
int inkWidth (const juce::Image& image, juce::Rectangle<int> area, juce::Colour background)
{
    const auto reference = background.getBrightness();
    auto left = -1, right = -1;

    for (int x = area.getX(); x < area.getRight(); ++x)
    {
        auto lit = false;

        for (int y = area.getY(); y < area.getBottom() && ! lit; ++y)
            lit = std::abs (pixelAt (image, x, y).getBrightness() - reference) > 0.05f;

        if (lit)
        {
            if (left < 0)
                left = x;

            right = x;
        }
    }

    return left < 0 ? 0 : right - left + 1;
}

/** How far a colour leans to the red-yellow side of neutral.

    Lets "the anchor strip is measurably warmer" be a number rather than a
    judgement. Hue is the wrong instrument for it: a neutral grey's hue is
    arbitrary, so comparing hues of two near-greys compares noise. */
float warmth (juce::Colour colour)
{
    return (colour.getFloatRed() - colour.getFloatBlue()) * colour.getSaturation();
}

/** A component painting one flat colour with an optional inner rectangle —
    the known-answer subject the instruments are proved against. */
struct Swatch final : juce::Component
{
    void paint (juce::Graphics& g) override
    {
        g.fillAll (background);

        if (! inner.isEmpty())
        {
            g.setColour (foreground);
            g.fillRect (inner);
        }
    }

    juce::Colour background { juce::Colours::black };
    juce::Colour foreground { juce::Colours::white };
    juce::Rectangle<int> inner;
};

/** How far one pixel departs from a known background, 0..1, as the LARGEST
    channel difference.

    Not a brightness difference, and that is not a preference — the light theme
    breaks brightness. `--line-strong` there is rgba(0,0,0,0.28), dark on cream,
    so the track arc has a big brightness contrast; but the zabumba orange value
    arc sits at almost the same BRIGHTNESS as `--panel` #f1ede4, so a brightness
    instrument scored the value arc near zero and reported a bipolar knob's arc
    sweeping the wrong way. The dark theme hid it completely, and so did the
    white-on-black self-tests.

    This is 04-01's rule one level out: a brightness instrument is only valid
    over the background it was proved on, and the fix is to measure the quantity
    that actually distinguishes the two colours. */
double colourDistance (juce::Colour pixel, juce::Colour background)
{
    return juce::jmax (std::abs (pixel.getFloatRed()   - background.getFloatRed()),
                       std::abs (pixel.getFloatGreen() - background.getFloatGreen()),
                       std::abs (pixel.getFloatBlue()  - background.getFloatBlue()));
}

/** Ink measured per angular SECTOR about a centre, and per radius band.

    The instrument for an arc, and it exists in this shape because the obvious
    one cannot work. An arc is thin, curved and anti-aliased, so "some coloured
    pixels appeared in the upper-left quadrant" is a claim that a wrong radius,
    a wrong sweep direction, a wrong start angle and a completely different arc
    all satisfy. Four wrong arcs would pass it.

    What discriminates is the PROFILE: how much ink sits in each angular sector,
    and at what radius. A unipolar arc at half travel fills sectors from -135
    deg to 0; a bipolar one at the same value fills nothing (it starts AT 0);
    and an arc drawn at the hub's radius puts its ink in a different band
    entirely. `testArcInstrument` below proves the instrument separates exactly
    those cases before any arc claim rests on it.

    Angles are degrees, 0 = up, clockwise positive — the spec's convention. */
struct ArcProfile
{
    static constexpr int kNumSectors = 36;      ///< 10 degrees each

    std::array<double, kNumSectors> sector {};  ///< ink per 10-degree sector
    double total { 0.0 };

    /** The sector index an angle falls in, or -1 when outside the sweep. */
    static int sectorFor (float degrees) noexcept
    {
        const auto wrapped = degrees < 0.0f ? degrees + 360.0f : degrees;
        const auto index = static_cast<int> (wrapped / (360.0 / kNumSectors));

        return juce::isPositiveAndBelow (index, kNumSectors) ? index : -1;
    }

    /** Ink summed over the sectors spanning [fromDeg, toDeg]. */
    double over (float fromDeg, float toDeg) const noexcept
    {
        auto sum = 0.0;

        for (int i = 0; i < kNumSectors; ++i)
        {
            // The sector's own centre angle, mapped back into -180..180.
            auto centreDeg = (i + 0.5) * (360.0 / kNumSectors);
            if (centreDeg > 180.0)
                centreDeg -= 360.0;

            if (centreDeg >= juce::jmin (fromDeg, toDeg)
                && centreDeg <= juce::jmax (fromDeg, toDeg))
                sum += sector[static_cast<size_t> (i)];
        }

        return sum;
    }
};

/** Profiles the ink in one radius band around `centre`, by angular sector.

    `innerRadius`/`outerRadius` in pixels, so a caller can look at the arc band
    (around radius 38 scaled) without the hub (radius 30 scaled) contaminating
    it — which is the difference the self-test proves it can see. */
ArcProfile arcProfile (const juce::Image& image, juce::Point<float> centre,
                       float innerRadius, float outerRadius, juce::Colour background)
{
    ArcProfile out;

    for (int y = 0; y < image.getHeight(); ++y)
    {
        for (int x = 0; x < image.getWidth(); ++x)
        {
            const auto dx = static_cast<float> (x) + 0.5f - centre.x;
            const auto dy = static_cast<float> (y) + 0.5f - centre.y;
            const auto radius = std::sqrt (dx * dx + dy * dy);

            if (radius < innerRadius || radius > outerRadius)
                continue;

            const auto ink = colourDistance (image.getPixelAt (x, y), background);

            if (ink < 0.02)
                continue;   // untouched background, within rasteriser noise

            // atan2 with dx first and -dy second puts 0 at twelve o'clock and
            // increases clockwise, matching the spec's own convention.
            const auto degrees = juce::radiansToDegrees (std::atan2 (dx, -dy));
            const auto index = ArcProfile::sectorFor (static_cast<float> (degrees));

            if (index >= 0)
            {
                out.sector[static_cast<size_t> (index)] += ink;
                out.total += ink;
            }
        }
    }

    return out;
}

/** The ink-weighted mean radius of what is drawn in a band about `centre`.

    Turns "the arc is at radius 38" into a number that can be compared ACROSS
    SIZES, which is what AC-1's relative-geometry claim needs: a 54 px knob's
    arc must sit at 54/32 times the radius of a 32 px knob's, and no amount of
    per-size pixel probing establishes that. Proved below against arcs drawn at
    known radii. */
double inkRadiusCentroid (const juce::Image& image, juce::Point<float> centre,
                          float minRadius, float maxRadius, juce::Colour background)
{
    auto weighted = 0.0, weight = 0.0;

    for (int y = 0; y < image.getHeight(); ++y)
    {
        for (int x = 0; x < image.getWidth(); ++x)
        {
            const auto dx = static_cast<float> (x) + 0.5f - centre.x;
            const auto dy = static_cast<float> (y) + 0.5f - centre.y;
            const auto radius = std::sqrt (dx * dx + dy * dy);

            if (radius < minRadius || radius > maxRadius)
                continue;

            const auto ink = colourDistance (image.getPixelAt (x, y), background);

            if (ink < 0.02)
                continue;

            weighted += ink * radius;
            weight += ink;
        }
    }

    return weight > 0.0 ? weighted / weight : 0.0;
}

/** The largest per-pixel colour difference between two renders.

    AC-1's load-bearing word is "distinguishable", and this is the instrument
    that word rests on: six states drawn by one `paint` is six chances to draw
    the same thing twice, and a per-state threshold would not notice — two
    states can each satisfy "carries ink in the instrument colour" while being
    the same image. The maximum, not the mean: the beat ring is one pixel wide
    on a 26 px pad, so a mean over the whole pad dilutes it to nothing. */
double maxPixelDifference (const juce::Image& a, const juce::Image& b)
{
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
        return 1.0;

    auto worst = 0.0;

    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            worst = juce::jmax (worst, colourDistance (a.getPixelAt (x, y), b.getPixelAt (x, y)));

    return worst;
}

/** A flat ground to render a control against.

    Declared SEVEN times across the rigs before this — five as `Ground`, one as
    `BlackHolder`, one as `Sheet` — each a two-line component with the same
    body. The rigs themselves stay separate: each asks its own component for its
    geometry, which is the point of each. It is only the holder that was copied.
    Found by /simplify. */
struct Ground final : juce::Component
{
    void paint (juce::Graphics& g) override { g.fillAll (ground); }

    juce::Colour ground { juce::Colours::black };
};

/** One MouseEvent on one component.

    Eleven sites spelled out the 14-argument constructor, five of whose
    arguments are floats nobody reads — and `AttachedKnobRig::eventAt`'s own
    comment already recorded that regression at SEVEN. Only `mods` and
    `numClicks` ever vary. A transposed pressure/orientation pair compiles and
    silently changes which branch runs, and no assertion here could see it. */
juce::MouseEvent mouseEventOn (juce::Component& c, juce::Point<float> localPos,
                               juce::ModifierKeys mods = {}, int numClicks = 1,
                               std::optional<juce::Point<float>> mouseDownPos = {})
{
    // `mouseDownPos` defaults to `localPos`, which is what every existing caller
    // wants and what this helper always did. It became a PARAMETER at 07-02:
    // passing the same point for both makes `getDistanceFromDragStart()` zero,
    // so no event this produced could ever cross a drag threshold. The moment
    // DRAG MIDI grew one, the existing "dragging it changes no state" check
    // started passing because no drag ever STARTED — a check that cannot fail,
    // created by a change three files away.
    const auto downPos = mouseDownPos.value_or (localPos);

    return { juce::Desktop::getInstance().getMainMouseSource(), localPos, mods,
             1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
             juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(),
             numClicks, false };
}

/** Click a point in a CONTAINER's coordinate space, the way a real click
    arrives: JUCE routes it to the deepest visible child that hit-tests there,
    and that child receives it in its OWN coordinates.

    Added at 06-05, when `SequencerGrid`'s row labels stopped being a rectangle
    the container tested by hand and became `HitZone` children. Calling
    `grid.mouseUp(...)` no longer reaches them — and repointing the tests
    straight at the zone would have been the weaker fix, because it asserts the
    zone REACTS without asserting it is where the layout says and on top of what
    is under it. `getComponentAt` is the same recursive hit-test the real mouse
    uses, so a zone placed at the wrong bounds, left invisible, or buried by a
    sibling fails here exactly as it would on screen.

    Returns the component that actually received the click, so a caller can
    assert WHICH one it was. `getComponentAt` returns the CONTAINER when no
    child is under the point, so "nothing is here" is asserted by checking the
    TYPE. It returns null only when the container is INVISIBLE or the point is
    outside it — both of which are rig mistakes, so they fail loudly here rather
    than reading as a click that landed on nothing. /simplify.

    DOWN THEN UP, because a release alone is not a click to most of this
    codebase: `Button::mouseUp` early-returns unless `mouseDown` set `pressed`
    (Button.cpp:188), and `StepPad` and `Segmented` gate the same way. Sending
    only `mouseUp` made this helper a silent no-op on every Button while still
    returning non-null — a check that cannot fail, in the helper written to stop
    exactly that. `HitZone` and `SelectableTile` ignore `mouseDown`, so nothing
    regresses. /simplify. */
juce::Component* clickInside (juce::Component& container, juce::Point<int> pointInContainer,
                              juce::ModifierKeys mods = {})
{
    check (container.isVisible(),
           "clickInside needs a VISIBLE container: getComponentAt routes through the visible "
           "flag, so an invisible rig turns every routed click into a silent no-op");

    auto* target = container.getComponentAt (pointInContainer);

    check (target != nullptr, "clickInside found nothing at the point — the container does not "
                              "contain it, or the rig never made the container visible");

    if (target == nullptr)
        return nullptr;

    const auto local = target->getLocalPoint (&container, pointInContainer).toFloat();

    target->mouseDown (mouseEventOn (*target, local, mods));
    target->mouseUp   (mouseEventOn (*target, local, mods));

    return target;
}

/** WHAT sits at a point, without clicking it.

    `clickInside` sends a real `mouseDown` then `mouseUp` (06-06, so it is not a
    no-op on a Button), which means asking it "is there a zone here?" also
    DELIVERS a click: over the pad strip it toggles a lane, sets `dirty` and
    bumps the publication count. Two checks did exactly that and were harmless
    only by accident — the state they dirtied happened to be overwritten later.
    A check that mutates what a neighbouring check reads is a trap with a
    timer on it. /code-review. */
juce::Component* componentAt (juce::Component& container, juce::Point<int> pointInContainer)
{
    check (container.isVisible(),
           "componentAt needs a VISIBLE container, for clickInside's reason");

    return container.getComponentAt (pointInContainer);
}

/** Move the pointer onto, or off, whatever sits at a point in a container.

    The hover counterpart of `clickInside`, for the same reason: hovering used
    to be a `mouseMove` the container handled by hand, and is now `mouseEnter` /
    `mouseExit` on a child. Returns the component under the point, so a caller
    can assert there is NO zone somewhere — which is a different claim from a
    handler that declined. Null means the same rig mistake it means for
    `clickInside`, and fails the same way. */
juce::Component* hoverInside (juce::Component& container, juce::Point<int> pointInContainer,
                              bool entering)
{
    check (container.isVisible(),
           "hoverInside needs a VISIBLE container, for clickInside's reason");

    auto* target = container.getComponentAt (pointInContainer);

    check (target != nullptr, "hoverInside found nothing at the point");

    if (target == nullptr)
        return nullptr;

    const auto local = target->getLocalPoint (&container, pointInContainer).toFloat();

    if (entering)
        target->mouseEnter (mouseEventOn (*target, local, {}, 0));
    else
        target->mouseExit (mouseEventOn (*target, local, {}, 0));

    return target;
}

/** Drains the message queue so a parameter -> UI update has arrived.

    juce::ParameterAttachment posts through an AsyncUpdater, so every test that
    changes a parameter needs this. 1 ms, not 8: runDispatchLoopUntil is a
    FIXED-duration loop — it never returns early when the queue empties — so
    every call sleeps its full budget. Measured at 8 ms: 92 calls x 8.015 ms =
    737 ms of a 2.80 s suite, spent asleep. 1 ms still delivers every pending
    update (verified 40/40 across repeated runs).

    ONE definition, because the reasoning above used to live on only one of
    three copies and the other two were bare one-liners — an invitation to
    raise the number back to 8 at the first flaky async test. */
void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil (1); }

/** Counts begin/end gesture pairs as a host would see them. */

/** Counts begin/end gesture pairs as a host would see them.

    Declared four times before this, identically. */
struct GestureCounter final : juce::AudioProcessorParameter::Listener
{
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool starting) override { starting ? ++begins : ++ends; }

    int begins { 0 }, ends { 0 };
};

/** The step pad's six static states — AC-1's subject.

    ONE table. It was declared twice, in `testStepPadStates` and again in
    `writeReferenceRenders`, differing only in the caption strings — so a
    seventh state would have been asserted and then quietly missing from the
    checkpoint sheet a human reads. */
struct PadState
{
    const char* caption;
    int         velocity;
    bool        beat;
    bool        hover;
};

inline constexpr std::array<PadState, 6> padStates {{
    { "OFF",      0,   false, false },
    { "ON 127",   127, false, false },
    { "ON 60",    60,  false, false },
    { "GHOST 20", 20,  false, false },
    { "BEAT",     0,   true,  false },
    { "HOVER",    0,   false, true  },
}};

/** Draws one string in one face, for the weight measurements. */
struct TextSwatch final : juce::Component
{
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
        g.setColour (juce::Colours::white);
        g.setFont (type::fontFor (face, heightPx));
        g.drawText (text, getLocalBounds(), juce::Justification::centredLeft);
    }

    type::Face face { type::Face::sansRegular };
    float heightPx { 24.0f };
    juce::String text { "FORRO BOX" };
};

// ── the instruments' own proofs ──────────────────────────────────────────────

void testMeasurementInstruments()
{
    section ("the UI measurement instruments prove themselves");

    // renderComponent + pixelAt: a known flat fill, and a known inner rect.
    {
        Swatch swatch;
        swatch.background = juce::Colour (0xff123456);
        swatch.foreground = juce::Colour (0xffabcdef);
        swatch.inner = { 20, 20, 10, 10 };

        const auto image = renderComponent (swatch, 60, 60);

        checkEqual (image.getWidth(), 60, "renderComponent honours the width it is given");
        checkEqual (image.getHeight(), 60, "and the height");
        checkPixel (image, 5, 5, juce::Colour (0xff123456), "the background reads back exactly");
        checkPixel (image, 25, 25, juce::Colour (0xffabcdef), "and the inner rect");

        // The rejection case: a pixel OUTSIDE the inner rect must not read as
        // the foreground. Without this, an instrument that returned the
        // foreground everywhere would pass every check above.
        check (pixelAt (image, 5, 5) != swatch.foreground,
               "and a pixel outside the inner rect is NOT the foreground — so the probe "
               "is reading the coordinate it was given, not a constant");
    }

    // No display. Asserted here rather than assumed from the suite passing:
    // an X11 connection opened by something else in the process would make
    // "it worked" no evidence at all.
    {
        check (juce::JUCEApplicationBase::getInstance() == nullptr,
               "the suite runs with no Application instance, so nothing has opened a desktop");

        Swatch swatch;
        swatch.background = juce::Colours::red;
        const auto image = renderComponent (swatch, 8, 8);

        check (swatch.getPeer() == nullptr,
               "and the rendered component has no window peer — the render path needs no display");
        checkPixel (image, 4, 4, juce::Colours::red, "yet it painted");
    }

    // inkMass: monotone in coverage, and zero on an empty field.
    {
        Swatch swatch;
        swatch.background = juce::Colours::black;
        swatch.foreground = juce::Colours::white;

        swatch.inner = {};
        checkEqual (inkMass (renderComponent (swatch, 40, 40)), 0.0,
                    "inkMass is 0 on an all-black field");

        swatch.inner = { 0, 0, 10, 10 };
        const auto small = inkMass (renderComponent (swatch, 40, 40));
        swatch.inner = { 0, 0, 20, 20 };
        const auto large = inkMass (renderComponent (swatch, 40, 40));

        checkEqual (small, 100.0, "and equals the lit-pixel count for a white-on-black rect");
        // `large < large + 1.0` stood here and is trivially true, so this was a
        // one-sided `> 3.9` that an over-counting inkMass would have passed.
        check (large > small * 3.9 && large < small * 4.1,
               "and quadruples when the side doubles (" + juce::String (small, 1)
                   + " -> " + juce::String (large, 1) + ")");

        // The rejection case: restricted to a region that contains no ink, it
        // must report zero rather than the whole image's total.
        swatch.inner = { 0, 0, 10, 10 };
        checkEqual (inkMass (renderComponent (swatch, 40, 40), { 20, 20, 10, 10 }), 0.0,
                    "and reports 0 for a region the ink does not reach");
    }

    // contrastMass: ~0 on an untouched non-black field, real on marked pixels.
    // The instrument inkMass could not provide, and the reason it exists.
    {
        Swatch swatch;
        swatch.background = juce::Colour (0xff1e1e1e);   // --panel, the real case
        swatch.foreground = juce::Colours::white;

        swatch.inner = {};
        const auto emptyPanel = renderComponent (swatch, 40, 40);

        checkEqual (contrastMass (emptyPanel, { 0, 0, 40, 40 }, swatch.background), 0.0,
                    "contrastMass is 0 over an untouched --panel field");
        check (inkMass (emptyPanel) > 100.0,
               "while inkMass reports " + juce::String (inkMass (emptyPanel), 0)
                   + " for the same empty field — which is why the two are different instruments");

        swatch.inner = { 0, 0, 10, 10 };
        const auto marked = renderComponent (swatch, 40, 40);
        const auto mass = contrastMass (marked, { 0, 0, 40, 40 }, swatch.background);

        checkEqual (mass, 100.0 * (1.0 - swatch.background.getBrightness()),
                    "and equals the per-pixel contrast times the marked area");

        // The rejection case: a region the mark does not reach reports 0, so it
        // is reading the coordinates and not the whole image.
        checkEqual (contrastMass (marked, { 20, 20, 10, 10 }, swatch.background), 0.0,
                    "and 0 for a region the mark does not reach");
    }

    // inkWidth: the extent of what is drawn, and 0 on a blank field.
    {
        Swatch swatch;
        swatch.background = juce::Colour (0xff1e1e1e);
        swatch.foreground = juce::Colours::white;

        swatch.inner = {};
        checkEqual (inkWidth (renderComponent (swatch, 40, 40), { 0, 0, 40, 40 }, swatch.background),
                    0, "inkWidth is 0 on a blank field");

        swatch.inner = { 5, 5, 12, 12 };
        checkEqual (inkWidth (renderComponent (swatch, 40, 40), { 0, 0, 40, 40 }, swatch.background),
                    12, "and equals the marked width, wherever the mark sits");

        // The rejection case: measured over a window the mark only partly
        // overlaps, it must report the overlap and not the mark's full width.
        swatch.inner = { 5, 5, 12, 12 };
        checkEqual (inkWidth (renderComponent (swatch, 40, 40), { 10, 0, 30, 40 }, swatch.background),
                    7, "and clips to the window it is given");
    }

    // warmth: signed, and zero on a neutral.
    {
        check (warmth (juce::Colour (0xffe8650a)) > 0.5f,
               "warmth is strongly positive for the zabumba orange");
        checkEqual (warmth (juce::Colour (0xff808080)), 0.0f, "and 0 for a neutral grey");
        check (warmth (juce::Colour (0xff0a65e8)) < -0.5f,
               "and negative for that orange's blue mirror — so it measures direction, "
               "not just saturation");
    }

    // ── colourDistance: the measure both arc instruments depend on ──────────
    //
    // The case that matters is the one that shipped a wrong result: the zabumba
    // orange value arc over the LIGHT theme's --panel. Brightness cannot see
    // it; colour distance can. Asserted as a comparison between the two
    // instruments, so the reason this one exists is itself checked.
    {
        const auto cream  = theme::colour (theme::Token::panel, theme::Mode::light);
        const auto orange = theme::accent (theme::Accent::zabumba);

        const auto brightnessGap = std::abs (orange.getBrightness() - cream.getBrightness());
        const auto colourGap = colourDistance (orange, cream);

        check (brightnessGap < 0.05,
               "the zabumba orange and the light theme's --panel are within "
                   + juce::String (brightnessGap, 3)
                   + " of each other in BRIGHTNESS — which is why a brightness instrument scored "
                     "the value arc near zero and misread a bipolar sweep");
        check (colourGap > 0.4,
               "while their colour distance is " + juce::String (colourGap, 3)
                   + " — so this is the measure that can see the arc");
        check (colourGap > brightnessGap * 5.0,
               "and it is the better instrument by a wide margin, not a marginal preference");

        // The rejection case: identical colours must measure exactly 0, so a
        // non-zero reading is always a real difference.
        checkEqual (colourDistance (cream, cream), 0.0,
                    "and a colour against itself measures exactly 0");
    }

    // ── arcProfile: the instrument every AC-2 claim rests on ────────────────
    //
    // Proved against arcs drawn BY THE TEST at known angles and radii, so the
    // instrument is validated independently of Knob's own painting. Each case
    // is one the plan says a naive "coloured pixels in the right quadrant"
    // check would pass — that is the point.
    {
        /** Strokes one arc at a known radius and sweep. */
        struct ArcSwatch final : juce::Component
        {
            void paint (juce::Graphics& g) override
            {
                g.fillAll (juce::Colours::black);

                juce::Path path;
                const auto c = getLocalBounds().toFloat().getCentre();
                path.addCentredArc (c.x, c.y, radius, radius, 0.0f,
                                    juce::degreesToRadians (juce::jmin (fromDeg, toDeg)),
                                    juce::degreesToRadians (juce::jmax (fromDeg, toDeg)), true);

                g.setColour (juce::Colours::white);
                g.strokePath (path, juce::PathStrokeType (stroke));
            }

            float radius { 38.0f }, stroke { 5.0f }, fromDeg { -135.0f }, toDeg { 135.0f };
        };

        const juce::Point<float> centre { 50.0f, 50.0f };
        const auto band = [&] (ArcSwatch& s) {
            return arcProfile (renderComponent (s, 100, 100), centre, 33.0f, 43.0f,
                               juce::Colours::black);
        };

        // 1. A full sweep puts ink across the whole -135..135 span and NONE in
        //    the gap below the knob, which is what proves it reads angle at all.
        ArcSwatch full;
        const auto fullProfile = band (full);

        check (fullProfile.over (-135.0f, 135.0f) > 0.0,
               "arcProfile finds ink across a full -135..135 sweep");
        checkEqual (fullProfile.over (150.0f, 180.0f), 0.0,
                    "and none in the 90-degree gap below the knob, so it is reading ANGLE");

        // 2. Half travel: a unipolar arc (-135 -> 0) and a bipolar one at the
        //    same value (0 -> 0, empty) must be TOLD APART. This is the case
        //    the plan names as the one a quadrant check cannot see.
        ArcSwatch unipolarHalf;
        unipolarHalf.fromDeg = -135.0f;
        unipolarHalf.toDeg = 0.0f;
        const auto uni = band (unipolarHalf);

        check (uni.over (-135.0f, -15.0f) > 0.0, "a unipolar half-travel arc fills the left sweep");

        // A RATIO, and measured outside the sectors the arc's endpoint touches.
        // `checkEqual (over (5, 135), 0)` stood here and passed under GCC and
        // Clang while MSVC measured 1.69: the arc ends exactly at 0 degrees, its
        // 5 px stroke is anti-aliased across that boundary, and the three
        // rasterisers round the edge pixels differently — the same disagreement
        // kCompositeSlop exists for. The claim worth making is that the arc does
        // not EXTEND right, which a ratio states without depending on which
        // rasteriser drew it.
        check (uni.over (15.0f, 135.0f) < uni.over (-135.0f, -15.0f) * 0.05,
               "and essentially nothing to the right of centre ("
                   + juce::String (uni.over (15.0f, 135.0f), 2) + " against "
                   + juce::String (uni.over (-135.0f, -15.0f), 2) + ")");

        // The bipolar equivalent at the same value draws nothing at all.
        ArcSwatch bipolarHalf;
        bipolarHalf.fromDeg = 0.0f;
        bipolarHalf.toDeg = 0.0f;

        check (band (bipolarHalf).total < uni.total * 0.05,
               "while a BIPOLAR knob at the same half-travel value draws essentially nothing — "
               "the two are distinguishable, which a quadrant check could not do");

        // 3. Bipolar below centre sweeps LEFT, above centre sweeps RIGHT. A
        //    sweep-direction error is invisible without this pair.
        ArcSwatch bipolarLow, bipolarHigh;
        bipolarLow.fromDeg = -70.0f;  bipolarLow.toDeg = 0.0f;
        bipolarHigh.fromDeg = 0.0f;   bipolarHigh.toDeg = 70.0f;

        const auto low = band (bipolarLow);
        const auto high = band (bipolarHigh);

        check (low.over (-135.0f, -10.0f) > low.over (10.0f, 135.0f) * 10.0,
               "a bipolar arc below centre puts its ink LEFT of up");
        check (high.over (10.0f, 135.0f) > high.over (-135.0f, -10.0f) * 10.0,
               "and above centre it puts it RIGHT — so sweep direction is measurable");

        // 4. RADIUS is discriminated: the same sweep drawn at the hub's radius
        //    must be invisible in the arc band. Without this, an arc painted at
        //    the wrong radius passes every angular claim above.
        ArcSwatch atHubRadius;
        atHubRadius.radius = 30.0f;
        const auto hubBand = band (atHubRadius);

        check (hubBand.total < fullProfile.total * 0.25,
               "an arc drawn at the hub's radius 30 barely registers in the 33..43 arc band ("
                   + juce::String (hubBand.total, 1) + " against " + juce::String (fullProfile.total, 1)
                   + ") — so the instrument reads RADIUS, not just angle");

        // 5. The rejection case: a blank field reports exactly zero, so the
        //    numbers above are ink and not an artefact of the scan.
        ArcSwatch blank;
        blank.stroke = 0.0f;
        checkEqual (band (blank).total, 0.0, "and a blank field profiles to exactly 0");

        // ── inkRadiusCentroid, the instrument AC-1's ratio claim rests on ───
        //
        // Proved against arcs drawn at three KNOWN radii, including a pair
        // whose ratio is the one the 54 px / 32 px comparison will make.
        for (const auto known : { 20.0f, 30.0f, 38.0f })
        {
            ArcSwatch at;
            at.radius = known;

            const auto measured = inkRadiusCentroid (renderComponent (at, 100, 100), centre,
                                                     5.0f, 49.0f, juce::Colours::black);

            check (std::abs (measured - known) <= 0.5,
                   "inkRadiusCentroid recovers a known arc radius of " + juce::String (known, 0)
                       + " (measured " + juce::String (measured, 2) + ")");
        }

        // And the rejection case: it must NOT return a plausible-looking radius
        // for a blank field, which would make every ratio below meaningless.
        ArcSwatch nothing;
        nothing.stroke = 0.0f;
        checkEqual (inkRadiusCentroid (renderComponent (nothing, 100, 100), centre,
                                       5.0f, 49.0f, juce::Colours::black),
                    0.0, "and reports 0 — not a mid-band radius — for a blank field");
    }
}

// ── AC-1: the fonts ─────────────────────────────────────────────────────────

void testEmbeddedFonts()
{
    section ("seven embedded weights, distinct and correctly named");

    struct Expectation { type::Face face; const char* family; const char* style; };

    const std::array<Expectation, type::kNumFaces> expected {{
        { type::Face::sansRegular,  "Space Grotesk", "Regular"  },
        { type::Face::sansMedium,   "Space Grotesk", "Medium"   },
        { type::Face::sansSemiBold, "Space Grotesk", "SemiBold" },
        { type::Face::sansBold,     "Space Grotesk", "Bold"     },
        { type::Face::monoRegular,  "IBM Plex Mono", "Regular"  },
        { type::Face::monoMedium,   "IBM Plex Mono", "Medium"   },
        { type::Face::monoSemiBold, "IBM Plex Mono", "SemiBold" },
    }};

    for (const auto& row : expected)
    {
        const auto face = type::typefaceFor (row.face);

        if (face == nullptr)
        {
            check (false, juce::String ("the ") + row.family + " " + row.style + " face loads");
            continue;
        }

        checkEqual (face->getName().toStdString(), std::string (row.family),
                    juce::String ("family for the ") + row.style + " face");
        checkEqual (face->getStyle().toStdString(), std::string (row.style),
                    juce::String ("style for the ") + row.family + " " + row.style + " face");
    }

    // No two faces in a family may report the same style. This is the assertion
    // that would have caught the instancer's un-patched output, where all four
    // Space Grotesk weights reported family "Space Grotesk Light", style
    // "Regular" — while still rendering, and while usWeightClass was correct.
    {
        auto distinct = true;

        for (size_t i = 0; i < expected.size(); ++i)
            for (size_t j = i + 1; j < expected.size(); ++j)
                if (juce::String (expected[i].family) == juce::String (expected[j].family))
                {
                    const auto a = type::typefaceFor (expected[i].face);
                    const auto b = type::typefaceFor (expected[j].face);

                    if (a != nullptr && b != nullptr && a->getStyle() == b->getStyle())
                        distinct = false;
                }

        check (distinct, "no two faces within a family report the same style name");
    }

    // Ink mass, strictly increasing with weight, per family.
    const std::array<std::pair<const char*, std::array<type::Face, 4>>, 1> sansFamily {{
        { "Space Grotesk", { type::Face::sansRegular, type::Face::sansMedium,
                             type::Face::sansSemiBold, type::Face::sansBold } },
    }};

    for (const auto& [familyName, faces] : sansFamily)
    {
        TextSwatch swatch;
        std::array<double, 4> mass {};

        for (size_t i = 0; i < faces.size(); ++i)
        {
            swatch.face = faces[i];
            mass[i] = inkMass (renderComponent (swatch, 240, 40));
        }

        for (size_t i = 1; i < mass.size(); ++i)
        {
            // 5% of the lighter weight. Measured smallest real step is ~8%
            // (572.3 -> 617.9), so this has margin without being so loose that
            // two copies of one weight would pass.
            const auto step = (mass[i] - mass[i - 1]) / mass[i - 1];

            check (step > 0.05,
                   juce::String (familyName) + " weight " + juce::String ((int) i)
                       + " is at least 5% more ink than weight " + juce::String ((int) i - 1)
                       + " (" + juce::String (mass[i - 1], 1) + " -> " + juce::String (mass[i], 1)
                       + ", +" + juce::String (step * 100.0, 1) + "%)");
        }
    }

    // The three mono weights, same argument.
    {
        TextSwatch swatch;
        swatch.text = "120 BPM";
        const std::array<type::Face, 3> monoFaces {
            type::Face::monoRegular, type::Face::monoMedium, type::Face::monoSemiBold
        };

        std::array<double, 3> mass {};
        for (size_t i = 0; i < monoFaces.size(); ++i)
        {
            swatch.face = monoFaces[i];
            mass[i] = inkMass (renderComponent (swatch, 240, 40));
        }

        for (size_t i = 1; i < mass.size(); ++i)
            check ((mass[i] - mass[i - 1]) / mass[i - 1] > 0.05,
                   juce::String ("IBM Plex Mono weight ") + juce::String ((int) i)
                       + " is at least 5% more ink than weight " + juce::String ((int) i - 1)
                       + " (" + juce::String (mass[i - 1], 1) + " -> " + juce::String (mass[i], 1) + ")");
    }

    // Both OFL licences ship — checked in the BINARY, not on disk.
    //
    // The compliance constraint is that the fonts "must remain OFL-licensed and
    // be attributed accordingly", and the licence travelling inside the plugin
    // is the stronger form of that claim: a file in the repository is not
    // attribution for a VST3 someone installs. The first version located
    // assets/fonts/ from __FILE__ and failed under MSVC, which reports a
    // different path there than GCC and Clang do — a test that depended on the
    // build's source layout rather than on the product.
    {
        const auto licence = [] (const char* data, int size)
        {
            return juce::String::fromUTF8 (data, size);
        };

        const auto sansLicence = licence (FontData::SpaceGroteskOFL_txt,
                                          FontData::SpaceGroteskOFL_txtSize);
        const auto monoLicence = licence (FontData::IBMPlexMonoOFL_txt,
                                          FontData::IBMPlexMonoOFL_txtSize);

        check (sansLicence.contains ("SIL OPEN FONT LICENSE"),
               "Space Grotesk's OFL licence is embedded in the binary ("
                   + juce::String (FontData::SpaceGroteskOFL_txtSize) + " bytes)");
        check (monoLicence.contains ("SIL OPEN FONT LICENSE"),
               "and IBM Plex Mono's (" + juce::String (FontData::IBMPlexMonoOFL_txtSize) + " bytes)");

        // The rejection case: a licence blob that had been truncated to nothing
        // would still "contain" an empty needle, so assert the needle is real.
        check (! sansLicence.contains ("SIL OPEN FONT LICENCE"),
               "and the licence match is a real substring test, not a vacuous one");
    }
}

void testTypeScale()
{
    section ("the type scale is the spec's table");

    // Every enum value indexes its own row. The jassert in styleFor catches a
    // reordering in a debug build; this catches it in Release too, where the
    // whole suite runs.
    auto aligned = true;
    for (size_t i = 0; i < type::textStyles.size(); ++i)
        aligned = aligned && (static_cast<size_t> (type::textStyles[i].style) == i);

    check (aligned, "every textStyles row sits at its own enum's index");
    checkEqual ((int) type::textStyles.size(), type::kNumStyles,
                "and the table has one row per style");

    // Spot-checks against PLANNING.md's table, chosen as the rows whose values
    // are load-bearing elsewhere: the wordmark is the only 700 in the header,
    // the section label carries the widest tracking in the design, and the BPM
    // readout is the largest type anywhere.
    const auto& wordmark = type::styleFor (type::Style::wordmark);
    checkEqual (wordmark.heightPx, 17.0f, "wordmark is 17 px");
    check (wordmark.face == type::Face::sansBold, "at Space Grotesk 700");
    checkEqual (wordmark.letterSpacingEm, 0.13f, "with 0.13em tracking");

    const auto& sectionLabel = type::styleFor (type::Style::sectionLabel);
    checkEqual (sectionLabel.letterSpacingEm, 0.20f, "section labels track at 0.20em");
    check (sectionLabel.uppercase, "and are uppercased");

    const auto& bpm = type::styleFor (type::Style::bpmReadout);
    checkEqual (bpm.heightPx, 22.0f, "the BPM readout is 22 px");
    check (bpm.face == type::Face::monoMedium, "in IBM Plex Mono 500");

    checkEqual (type::styleFor (type::Style::bpmSuffix).opacity, 0.55f,
                "and its BPM suffix is dimmed to 55%");

    // Tracking widens the string, and by the amount the spec asks for: n glyphs
    // get n-1 gaps, not n. Counting n would leave every centred label half a
    // tracking step left of centre.
    {
        const juce::String text { "SEQUENCER" };
        const auto tracked = type::trackedWidth (type::Style::sectionLabel, text);
        const auto plain = juce::GlyphArrangement::getStringWidth (
            type::fontFor (sectionLabel.face, sectionLabel.heightPx), text);

        const auto expectedExtra = sectionLabel.letterSpacingEm * sectionLabel.heightPx
                                 * static_cast<float> (text.length() - 1);

        checkEqual (tracked - plain, expectedExtra,
                    "trackedWidth adds tracking between glyphs only (n-1 gaps)");
        check (tracked > plain, "so a tracked label is wider than an untracked one");
    }

    // A single glyph gets no tracking at all — the n-1 rule at its boundary.
    checkEqual (type::trackedWidth (type::Style::sectionLabel, "S")
                    - juce::GlyphArrangement::getStringWidth (
                          type::fontFor (sectionLabel.face, sectionLabel.heightPx), "S"),
                0.0f, "and a one-glyph string gets no tracking");

    checkEqual (type::trackedWidth (type::Style::sectionLabel, ""), 0.0f,
                "and an empty string measures 0");

    // And drawTracked ACTUALLY APPLIES it. Everything above tests the width
    // CALCULATION; a control that set the drawing path's tracking to zero
    // passed all 1275 checks, because the expression lived in both functions
    // and only one was covered. This measures the drawn pixels.
    {
        struct TrackedSwatch final : juce::Component
        {
            void paint (juce::Graphics& g) override
            {
                g.fillAll (background);
                g.setColour (juce::Colours::white);
                type::drawTracked (g, style, "SEQUENCER", getLocalBounds().toFloat(),
                                   juce::Justification::centredLeft);
            }

            juce::Colour background { juce::Colour (0xff1e1e1e) };
            type::Style style { type::Style::sectionLabel };
        };

        TrackedSwatch wide;                                // 0.20em
        TrackedSwatch narrow;
        narrow.style = type::Style::profileDescription;    // 0.00em, and 9.5px vs 9px

        const juce::Rectangle<int> band { 0, 0, 300, 20 };

        const auto drawnWide = inkWidth (renderComponent (wide, band.getWidth(), band.getHeight()),
                                         band, wide.background);
        const auto computedWide = type::trackedWidth (type::Style::sectionLabel, "SEQUENCER");

        // Within 2 px: the drawn extent is glyph ink, while the computed width
        // is advances, so the two differ by the first glyph's left side bearing
        // and the last one's right.
        check (std::abs (drawnWide - juce::roundToInt (computedWide)) <= 3,
               "a drawn tracked label occupies the width trackedWidth predicts ("
                   + juce::String (drawnWide) + " drawn against "
                   + juce::String (computedWide, 1) + " computed)");

        // And the tracking is what makes it wide: the same string in a style
        // with no tracking must be measurably narrower, by about the 8 gaps.
        const auto drawnNarrow = inkWidth (renderComponent (narrow, band.getWidth(), band.getHeight()),
                                           band, narrow.background);

        const auto expectedGap = juce::roundToInt (type::trackingFor (type::Style::sectionLabel)
                                                   * 8.0f);   // "SEQUENCER" is 9 glyphs

        check (drawnWide - drawnNarrow > expectedGap / 2,
               "and drops by roughly the 8 inter-glyph gaps when drawn in an untracked style ("
                   + juce::String (drawnWide) + " vs " + juce::String (drawnNarrow)
                   + ", tracking accounts for " + juce::String (expectedGap) + " px)");
    }

    // trackingFor is the single source both paths read.
    checkEqual (type::trackingFor (type::Style::sectionLabel),
                type::styleFor (type::Style::sectionLabel).letterSpacingEm
                    * type::styleFor (type::Style::sectionLabel).heightPx,
                "trackingFor is letterSpacingEm x heightPx");
    checkEqual (type::trackingFor (type::Style::profileDescription), 0.0f,
                "and 0 for a style the spec does not track");
}

// ── AC-2: the tokens ────────────────────────────────────────────────────────

void testThemeTokens()
{
    section ("both palettes, and what the cross-check cannot see");

    // The cross-check proves the VALUES against forrobox.css on every build, so
    // this suite deliberately does not restate them — that would be the
    // hand-copied duplicate the cross-check exists to replace. What it asserts
    // instead is the structure the cross-check cannot: that the table is
    // indexed by its enum, and that the two themes really differ.
    auto aligned = true;
    for (size_t i = 0; i < theme::tokenSpecs.size(); ++i)
        aligned = aligned && (static_cast<size_t> (theme::tokenSpecs[i].token) == i);

    check (aligned, "every tokenSpecs row sits at its own enum's index");

    aligned = true;
    for (size_t i = 0; i < theme::accentSpecs.size(); ++i)
        aligned = aligned && (static_cast<size_t> (theme::accentSpecs[i].accent) == i);

    check (aligned, "and every accentSpecs row does too");

    // The light theme is a real second palette, not the dark one re-tinted.
    check (theme::colour (theme::Token::bg, theme::Mode::light).getBrightness()
             > theme::colour (theme::Token::bg, theme::Mode::dark).getBrightness() + 0.5f,
           "the light background is much brighter than the dark one");
    check (theme::colour (theme::Token::fg, theme::Mode::light).getBrightness()
             < theme::colour (theme::Token::fg, theme::Mode::dark).getBrightness() - 0.5f,
           "and its foreground much darker — the palette inverts, it does not shift");

    // --danger is theme-independent, per PLANNING.md ("unchanged across themes").
    checkEqual (theme::colour (theme::Token::danger, theme::Mode::dark).getARGB(),
                theme::colour (theme::Token::danger, theme::Mode::light).getARGB(),
                "--danger is the same in both themes");

    // Accents carry instrument identity, so they must not vary by theme. There
    // is no Mode parameter to get wrong — this asserts the five are distinct,
    // which is what makes them identity rather than decoration.
    {
        auto distinct = true;
        for (size_t i = 0; i < theme::kNumAccents; ++i)
            for (size_t j = i + 1; j < theme::kNumAccents; ++j)
                distinct = distinct && (theme::accentSpecs[i].argb != theme::accentSpecs[j].argb);

        check (distinct, "the five instrument accents are five different colours");
    }

    // The alpha tokens are alpha, not pre-blended opaque values. If they had
    // been flattened against the dark panel, the light theme's secondary text
    // would be a dark grey on cream instead of translucent black.
    // `isOpaque()`, not `isTransparent()`: the latter is alpha == 0, so it is
    // false for every partially-transparent colour and the check it looked like
    // it was making was the opposite of the one it made.
    check (! theme::colour (theme::Token::fgDim, theme::Mode::dark).isOpaque(),
           "--fg-dim carries alpha rather than being pre-blended (alpha "
               + juce::String ((int) theme::colour (theme::Token::fgDim, theme::Mode::dark).getAlpha()) + ")");
    check (! theme::colour (theme::Token::line, theme::Mode::light).isOpaque(),
           "and so does --line in the light theme");
    check (theme::colour (theme::Token::panel, theme::Mode::dark).isOpaque(),
           "while the surface tokens are opaque — so the check above distinguishes them");

    // mix() is sRGB interpolation, as color-mix(in srgb, ...) is.
    // (int), not the uint8 the getter returns: checkEqual streams its operands,
    // and a uint8 streams as a character — the first version of this line
    // reported "expected ?, got " on a genuine mismatch.
    checkEqual ((int) theme::mix (juce::Colours::black, juce::Colours::white, 0.5f).getRed(),
                128, "mix() at 0.5 is the sRGB midpoint");
    checkEqual ((int) theme::mix (juce::Colours::black, juce::Colours::white, 0.12f).getRed(),
                31, "and at 0.12 moves 12% of the way (round(0.12 x 255) = 31)");
    checkEqual (theme::mix (juce::Colours::black, juce::Colours::white, 0.0f).getARGB(),
                juce::Colours::black.getARGB(), "and at 0 returns the base unchanged");
    checkEqual (theme::mix (juce::Colours::black, juce::Colours::white, 1.0f).getARGB(),
                juce::Colours::white.getARGB(), "and at 1 the other colour");
}

// ── AC-3: geometry under scaling ────────────────────────────────────────────

void testChassisGeometry()
{
    section ("the chassis is laid out in design px");

    const auto layout = ChassisLayout::forBounds ({ 0, 0, ChassisLayout::kWidth, ChassisLayout::kHeight });

    checkEqual (layout.header.getHeight(), 72, "header is 72 px tall");
    checkEqual (layout.sequencer.getHeight(), 196, "the sequencer row is 196");
    checkEqual (layout.footer.getHeight(), 56, "the footer is 56");
    checkEqual (layout.main.getHeight(), 780 - 72 - 196 - 56, "and main takes the rest");
    checkEqual (layout.sidePanel.getWidth(), 280, "the side panel is 280 px wide");

    // The four rows tile the height exactly: no gap, no overlap. Asserted as a
    // partition rather than as four heights, because four correct heights that
    // do not sum to 780 is a representable state.
    checkEqual (layout.header.getY(), 0, "the header starts at the top");
    checkEqual (layout.header.getBottom(), layout.main.getY(), "main follows the header");
    checkEqual (layout.main.getBottom(), layout.sequencer.getY(), "the sequencer follows main");
    checkEqual (layout.sequencer.getBottom(), layout.footer.getY(), "the footer follows the sequencer");
    checkEqual (layout.footer.getBottom(), ChassisLayout::kHeight, "and reaches the bottom");

    checkEqual (layout.matrix.getRight(), layout.sidePanel.getX(),
                "the matrix meets the side panel with no gap");
    checkEqual (layout.sidePanel.getRight(), ChassisLayout::kWidth,
                "and the side panel reaches the right edge");

    // Five strips, 1 px gaps, and no accumulated rounding error: the matrix is
    // 609 px at the design size, which does not divide by five.
    checkEqual ((int) layout.strips.size(), 5, "there are five strips");
    checkEqual (layout.strips.front().getX(), layout.matrix.getX(),
                "the first starts at the matrix' left edge");
    checkEqual (layout.strips.back().getRight(), layout.matrix.getRight(),
                "and the last ends at its right edge, so rounding is distributed not accumulated");

    for (size_t i = 1; i < layout.strips.size(); ++i)
        checkEqual (layout.strips[i].getX() - layout.strips[i - 1].getRight(),
                    ChassisLayout::kStripGap,
                    "gap " + juce::String ((int) i) + " between strips is exactly 1 px");

    auto widthSpread = 0;
    for (const auto& strip : layout.strips)
        widthSpread = juce::jmax (widthSpread,
                                  std::abs (strip.getWidth() - layout.strips.front().getWidth()));

    check (widthSpread <= 1,
           "and no two strips differ in width by more than 1 px (spread "
               + juce::String (widthSpread) + ")");

    for (const auto& strip : layout.strips)
    {
        checkEqual (strip.getY(), layout.matrix.getY(), "every strip is full-height (top)");
        checkEqual (strip.getBottom(), layout.matrix.getBottom(), "and (bottom)");
    }

    // The strips plus the gaps tile the matrix exactly. This is what makes
    // paintMatrix's gap-only fill equivalent to filling the whole matrix: any
    // column the strips leave uncovered must be a gap the fill walks, or --bg
    // would show through where --line belongs.
    checkEqual (layout.strips.front().getX(), layout.matrix.getX(),
                "the first strip starts at the matrix's left edge");
    checkEqual (layout.strips.back().getRight(), layout.matrix.getRight(),
                "and the last one ends at its right edge, so only the gaps are uncovered");

    // ── each strip's interior, which only paint could see before ────────────
    //
    // The head row, the accent bar and the reserved controls box were
    // removeFromTop locals inside paintStrip. Nothing could asssert them, and
    // the controls box was computed and then discarded — so the plan's
    // "reserve their boxes" deliverable was unreachable by 04-02/03/04.
    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
    {
        const auto& strip    = layout.strips[static_cast<size_t> (i)];
        const auto& interior = layout.stripLayouts[static_cast<size_t> (i)];
        const auto  label    = juce::String ("strip ") + juce::String (i + 1);

        checkEqual (interior.headRow.getY(), strip.getY() + ChassisLayout::kStripPadTop,
                    label + "'s head row sits below the top pad");
        checkEqual (interior.headRow.getHeight(), ChassisLayout::kHeadRowHeight,
                    label + "'s head row is the spec's height");
        checkEqual (interior.headRow.getX(), strip.getX() + ChassisLayout::kStripPadSide,
                    label + "'s interior is inset by the side pad");
        checkEqual (interior.headRow.getWidth(),
                    strip.getWidth() - 2 * ChassisLayout::kStripPadSide,
                    label + "'s interior is inset on BOTH sides");

        checkEqual (interior.accentBar.getY(),
                    interior.headRow.getBottom() + ChassisLayout::kAccentBarMarginTop,
                    label + "'s accent bar follows the head row by its top margin");
        checkEqual (interior.accentBar.getHeight(), ChassisLayout::kAccentBarHeight,
                    label + "'s accent bar is 4 px");

        checkEqual (interior.controls.getY(),
                    interior.accentBar.getBottom() + ChassisLayout::kAccentBarMarginBottom,
                    label + "'s reserved controls box follows the bar by its bottom margin");
        checkEqual (interior.controls.getBottom(),
                    strip.getBottom() - ChassisLayout::kStripPadBottom,
                    label + "'s reserved box runs to the bottom pad");
        check (interior.controls.getHeight() > 0,
               label + "'s reserved box is non-empty, so a later plan has somewhere to put a knob ("
                   + juce::String (interior.controls.getHeight()) + " px)");

        check (strip.contains (interior.controls),
               label + "'s reserved box is inside the strip");

        // ── the full stack, PLANNING.md:276-294 in order ────────────────────
        //
        // Asserted as a SEQUENCE, not as a set of independent y values: each
        // box must follow the previous one by exactly its declared margin, so
        // a reordered pair is a failure rather than two numbers that still add
        // up. 04-02's plan said this stack "exactly tiles" controls — it does
        // NOT, and that wording was wrong: `.strip` is a flex column with
        // justify-content: flex-start and no growing child (css:264-268), so
        // the column packs from the top and the leftover is real empty space
        // at the bottom. What is asserted instead is order, non-overlap,
        // containment, and that nothing overflows.
        /** One row of the stack: the box as BUILT, and the height and margin it
            DECLARES.

            Both, because the two assertions below need different things. Order
            and non-overlap read the built box; the fits-the-reserved-space sum
            must read the constants, since juce::Rectangle::removeFromTop CLAMPS
            — so a margin grown by 100 px silently squashes the last box to zero
            and leaves the slack at exactly 0. Summing the built heights would
            make that check unable to fail, which it was when /simplify's
            suggestion to fold the two was taken literally. */
        struct Row
        {
            const char*          name;
            juce::Rectangle<int> box;
            int                  marginAbove;
            int                  declaredHeight;
        };

        const std::array<Row, 9> stack {{
            { "sampleSlot",    interior.sampleSlot,    ChassisLayout::kSampleSlotMarginTop,
                                                       ChassisLayout::kSampleSlotHeight },
            { "hitVisualiser", interior.hitVisualiser, ChassisLayout::kHitVisualiserMarginTop,
                                                       ChassisLayout::kHitVisualiserHeight },
            { "dividerTop",    interior.dividerTop,    ChassisLayout::kStripDividerMargin,
                                                       ChassisLayout::kStripDividerHeight },
            { "knobGrid",      interior.knobGrid,      ChassisLayout::kStripDividerMargin,
                                                       ChassisLayout::kKnobGridHeight },
            { "dividerBottom", interior.dividerBottom, ChassisLayout::kStripDividerMargin,
                                                       ChassisLayout::kStripDividerHeight },
            { "patternCycler", interior.patternCycler, ChassisLayout::kStripDividerMargin
                                                         + ChassisLayout::kPatternRowMarginTop,
                                                       ChassisLayout::kPatternRowHeight },
            { "muteSolo",      interior.muteSolo,      ChassisLayout::kMuteSoloMarginTop,
                                                       ChassisLayout::kMuteSoloHeight },
            { "ghostLabel",    interior.ghostLabel,    ChassisLayout::kGhostRowMarginTop,
                                                       ChassisLayout::kGhostLabelHeight },
            { "ghostFader",    interior.ghostFader,    ChassisLayout::kGhostLabelGap,
                                                       ChassisLayout::kFaderHeight },
            // subDots is strip-5-only, so it is asserted separately below.
        }};

        // The bateria sub-dots row exists on exactly one strip.
        const auto isBateriaStrip = (static_cast<theme::Accent> (i) == theme::Accent::bateria);

        auto previousBottom = interior.controls.getY();

        for (const auto& row : stack)
        {
            const auto  what = label + "'s " + row.name;

            check (! row.box.isEmpty(), what + " is a non-empty reserved box");
            checkEqual (row.box.getY(), previousBottom + row.marginAbove,
                        what + " follows the box above it by its declared margin");
            check (interior.controls.contains (row.box),
                   what + " is inside the reserved controls box");
            checkEqual (row.box.getX(), interior.controls.getX(),
                        what + " spans the interior's full width (left)");
            checkEqual (row.box.getWidth(), interior.controls.getWidth(),
                        what + " spans the interior's full width (right)");

            previousBottom = row.box.getBottom();
        }

        // The DECLARED total against the box, not the clamped output.
        //
        // `slack >= 0` stood here under a comment claiming it caught a stack
        // grown past the bottom pad. It could not: juce::Rectangle::removeFromTop
        // CLAMPS to the remaining height, so growing a margin by 100 px silently
        // squashes the last box to zero and leaves slack at exactly 0. The sum
        // of the constants is independent of that clamping, so it fails on the
        // mis-typed margin the old assertion was written for.
        // Summed from the SAME table the order and non-overlap checks walk, and
        // from its DECLARED heights rather than its built ones — see struct Row.
        // The sum used to be transcribed again below the table, a third copy of
        // the stack that could disagree with the first about the order while
        // still adding up.
        auto declaredTotal = isBateriaStrip ? ChassisLayout::kSubDotsMarginTop
                                                  + ChassisLayout::kSubDotsRowHeight
                                            : 0;

        for (const auto& row : stack)
            declaredTotal += row.marginAbove + row.declaredHeight;

        check (declaredTotal <= interior.controls.getHeight(),
               label + "'s declared stack (" + juce::String (declaredTotal)
                   + " px) fits the reserved box (" + juce::String (interior.controls.getHeight())
                   + " px) — asserted on the constants, which clamping cannot hide");

        // And no box was clamped: every one still has the height it declares.
        // This is what turns the sum above into a claim about what was BUILT.
        checkEqual (interior.ghostFader.getHeight(), ChassisLayout::kFaderHeight,
                    label + "'s last stacked box kept its full height, so nothing was clamped");

        if (isBateriaStrip)
        {
            check (! interior.subDots.isEmpty(), label + " is bateria, so its sub-dots row exists");
            checkEqual (interior.subDots.getY(),
                        interior.ghostFader.getBottom() + ChassisLayout::kSubDotsMarginTop,
                        label + "'s sub-dots follow the ghost fader by their margin");
            checkEqual (interior.subDots.getHeight(), ChassisLayout::kSubDotsRowHeight,
                        label + "'s sub-dots row is as tall as its TALLEST child");
            check (ChassisLayout::kSubDotsRowHeight > ChassisLayout::kSubDotSize,
                   "and that is the 9 px label, not the 8 px circle — the flex law kPatternRowHeight "
                   "records, applied to the one box below it that still restated a child's size");
        }
        else
        {
            check (interior.subDots.isEmpty(),
                   label + " is not bateria, so its sub-dots row is EMPTY rather than "
                           "present-but-wrong");
        }

        // ── the four knob cells ─────────────────────────────────────────────
        //
        // Row-major across two columns, VOL / PITCH / DECAY / PAN.
        for (size_t c = 0; c < interior.knobCells.size(); ++c)
        {
            const auto& cell = interior.knobCells[c];
            const auto  what = label + " knob cell " + juce::String ((int) c);

            check (! cell.isEmpty(), what + " is non-empty");
            check (interior.knobGrid.contains (cell), what + " is inside the knob grid");
            // The RELATION, not the constant. `cell.getHeight() ==
            // kKnobCellHeight` was a tautology: both sides are the same
            // constant, so a control that shrank the cell to
            // `kStripKnobSize + kLabelGap` — dropping the label row — left all
            // 1900 checks green. Making kKnobCellHeight ask
            // Knob::preferredHeight removed the duplicate EXPRESSION without
            // adding the assertion it stood for.
            check (cell.getHeight() >= Knob::preferredHeight (ChassisLayout::kStripKnobSize, true),
                   what + " is tall enough for the knob placed in it (cell "
                       + juce::String (cell.getHeight()) + " px, knob needs "
                       + juce::String (Knob::preferredHeight (ChassisLayout::kStripKnobSize, true))
                       + " px)");
            check (cell.getWidth() >= ChassisLayout::kStripKnobSize,
                   what + " is wide enough for a 32 px dial (" + juce::String (cell.getWidth()) + " px)");
        }

        // Two columns, two rows, with the declared gaps between them.
        checkEqual (interior.knobCells[1].getX() - interior.knobCells[0].getRight(),
                    ChassisLayout::kKnobGridColGap, label + "'s knob columns are 6 px apart");
        checkEqual (interior.knobCells[2].getY() - interior.knobCells[0].getBottom(),
                    ChassisLayout::kKnobGridRowGap, label + "'s knob rows are 9 px apart");
        checkEqual (interior.knobCells[0].getY(), interior.knobCells[1].getY(),
                    label + "'s first knob row shares one top");
        checkEqual (interior.knobCells[2].getY(), interior.knobCells[3].getY(),
                    label + "'s second knob row shares one top");

        // And the grid is exactly the two rows it claims to be — no slack
        // hiding inside it, which is what would let a third row appear.
        checkEqual (interior.knobGrid.getHeight(), ChassisLayout::kKnobGridHeight,
                    label + "'s knob grid is exactly two cell rows plus one gap");
        checkEqual (interior.knobCells[3].getBottom(), interior.knobGrid.getBottom(),
                    label + "'s bottom knob row ends flush with the grid");
    }
}

void testChassisScalesAsOneTransform()
{
    section ("scaling is one transform on a fixed-size child");

    ForroBoxAudioProcessor processor;
    ForroBoxAudioProcessorEditor editor { processor };

    const std::array<std::pair<const char*, int>, 3> sizes {{
        { "1x",   1200 },
        { "1.5x", 1800 },
        { "2x",   2400 },
    }};

    for (const auto& [label, width] : sizes)
    {
        const auto height = juce::roundToInt (width * 780.0 / 1200.0);
        editor.setSize (width, height);

        checkEqual (editor.getChassisScale(), (float) width / 1200.0f,
                    juce::String ("the scale at ") + label);

        // The child keeps the DESIGN size in its own coordinates at every host
        // size. That is the property that lets every later layout number be a
        // design pixel — if the child were resized instead of transformed, each
        // component would have to scale its own geometry.
        auto* child = editor.getChildComponent (0);

        if (child == nullptr)
        {
            check (false, juce::String ("the editor has a chassis child at ") + label);
            continue;
        }

        checkEqual (child->getWidth(), 1200,
                    juce::String ("the chassis is still 1200 wide in its own coords at ") + label);
        checkEqual (child->getHeight(), 780, juce::String ("and 780 tall at ") + label);

        // And the transform actually maps it onto the editor.
        const auto mapped = child->getLocalBounds().toFloat()
                                 .transformedBy (child->getTransform());

        checkEqual (juce::roundToInt (mapped.getWidth()), width,
                    juce::String ("and the transform maps it to the editor width at ") + label);
    }

    // The aspect lock. 20:13 exactly at the design size, and the constrainer
    // holds it for anything the host asks for.
    check (editor.getConstrainer() != nullptr, "the editor installs a constrainer");

    if (auto* constrainer = editor.getConstrainer())
        checkEqual (constrainer->getFixedAspectRatio(), 1200.0 / 780.0,
                    "which holds the 20:13 design ratio");

    // A deliberately wrong-aspect request: the constrainer is what the host
    // goes through, so drive it the way a host would rather than setSize.
    {
        juce::Rectangle<int> bounds { 0, 0, 1200, 780 };
        const juce::Rectangle<int> previous { 0, 0, 1200, 780 };
        const juce::Rectangle<int> limits { 0, 0, 4000, 4000 };

        bounds.setSize (1600, 780);   // 2.05:1, far from 20:13
        editor.getConstrainer()->checkBounds (bounds, previous, limits,
                                              false, false, false, true);

        const auto ratio = (double) bounds.getWidth() / (double) bounds.getHeight();
        check (std::abs (ratio - 1200.0 / 780.0) < 0.01,
               "and corrects a wrong-aspect request back to 20:13 (got "
                   + juce::String (ratio, 4) + ")");
    }
}

// ── AC-4: the surfaces ──────────────────────────────────────────────────────

/** A probe point well inside a region.

    Inset from the edges on purpose: the regions carry 1 px borders and a top
    highlight line, so a probe on a boundary reads the border and reports a
    failure about the fill. */
juce::Point<int> insideOf (juce::Rectangle<int> area)
{
    return area.getCentre();
}

void testChassisSurfaces (theme::Mode mode, const juce::String& modeName)
{
    section (fbtest::utf8 ("the chassis surfaces read as their tokens — ") + modeName);

    ForroBoxLookAndFeel lnf { mode };
    Chassis chassis { lnf };

    const auto image = renderComponent (chassis, ChassisLayout::kWidth, ChassisLayout::kHeight);
    const auto& layout = chassis.getLayout();

    const auto probe = [&] (juce::Rectangle<int> area, theme::Token expected, const juce::String& what)
    {
        const auto point = insideOf (area);
        checkPixel (image, point.x, point.y, theme::colour (expected, mode), what);
    };

    // The header carries a vertical gradient, so its centre is between the two
    // stops rather than equal to either. Bounded instead of pinned.
    {
        const auto point = insideOf (layout.header);
        const auto raised = theme::colour (theme::Token::raised, mode);
        const auto top = theme::mix (raised, juce::Colours::white, ChassisLayout::kHeaderGradientWeight);
        const auto actual = pixelAt (image, point.x, point.y);

        const auto between = actual.getBrightness() >= juce::jmin (raised.getBrightness(), top.getBrightness()) - 0.002f
                          && actual.getBrightness() <= juce::jmax (raised.getBrightness(), top.getBrightness()) + 0.002f;

        check (between, "the header's gradient lies between --raised and --raised+3% white "
                        "(got " + hex (actual) + ")");
    }

    // The panel's own PADDING, not its centre: 06-02 filled the region, and its
    // centre now lands on a profile button's `--panel` ground. The ground token
    // is still the thing being checked, so the probe moves to a place the
    // content does not reach rather than the check being dropped.
    {
        const auto ground = layout.sidePanel.withTrimmedLeft (2)
                                            .withWidth (forrobox::side::kPadX - 3)
                                            .withTrimmedTop (2)
                                            .withHeight (forrobox::side::kPadY - 3);

        probe (ground, theme::Token::raised, "the side panel's ground is --raised");
    }
    probe (layout.footer, theme::Token::raised, "the footer is --raised");

    // The sequencer well's shadow gradient covers the top ~18 px, so probe
    // below it for the flat fill.
    probe (layout.sequencer.withTrimmedTop (40), theme::Token::sunken,
           "the sequencer well is --sunken below its shadow");

    // Strips: --panel, except the anchor.
    for (int i = 1; i < ChassisLayout::kNumStrips; ++i)
    {
        const auto strip = layout.strips[static_cast<size_t> (i)];
        const auto point = insideOf (strip);
        checkPixel (image, point.x, point.y, theme::colour (theme::Token::panel, mode),
                    juce::String ("strip ") + juce::String (i + 1) + " is --panel");
    }

    // The anchor tint: warmer than --panel, and warmer than every other strip.
    {
        const auto anchorPoint = insideOf (layout.strips[0]);
        const auto anchor = pixelAt (image, anchorPoint.x, anchorPoint.y);
        const auto panel = theme::colour (theme::Token::panel, mode);

        check (warmth (anchor) > warmth (panel),
               "the zabumba strip is measurably warmer than --panel ("
                   + juce::String (warmth (anchor), 4) + " vs " + juce::String (warmth (panel), 4) + ")");

        // And subtle — measured as the spec's own mix weight rather than by a
        // proxy. `saturation < 0.35` was the first attempt and it is the wrong
        // instrument: the tint sits at brightness 0.21, where saturation
        // exaggerates a small absolute shift (it reports 0.50 for a channel
        // move of 24/255). The spec says
        // `color-mix(in srgb, var(--panel) 88%, var(--c-zabumba))`, so the
        // testable claim is that each channel moved 12% of the way from --panel
        // to the accent.
        const auto expectedTint = theme::mix (panel, theme::accent (theme::Accent::zabumba),
                                              ChassisLayout::kAnchorAccentWeight);

        checkPixel (image, anchorPoint.x, anchorPoint.y, expectedTint,
                    "and it is exactly color-mix(--panel 88%, --c-zabumba)");

        // Which is a small absolute shift: the largest channel move is under
        // 10% of full scale, so it cannot read as a coloured panel.
        const auto largestMove = juce::jmax (std::abs ((int) anchor.getRed()   - (int) panel.getRed()),
                                             std::abs ((int) anchor.getGreen() - (int) panel.getGreen()),
                                             std::abs ((int) anchor.getBlue()  - (int) panel.getBlue()));

        // The bound comes from the spec's own arithmetic, not from a guessed
        // number: 12% of the largest channel distance between --panel and the
        // accent. Allowing 1 LSB for the single quantisation mix() performs.
        const auto channelSpan = juce::jmax (std::abs ((int) theme::accent (theme::Accent::zabumba).getRed()   - (int) panel.getRed()),
                                             std::abs ((int) theme::accent (theme::Accent::zabumba).getGreen() - (int) panel.getGreen()),
                                             std::abs ((int) theme::accent (theme::Accent::zabumba).getBlue()  - (int) panel.getBlue()));
        const auto expectedMove = juce::roundToInt (ChassisLayout::kAnchorAccentWeight
                                                    * static_cast<float> (channelSpan));

        check (std::abs (largestMove - expectedMove) <= 1,
               "and moves its largest channel by 12% of the --panel-to-accent distance, so it "
               "stays a tint and not a colour (" + juce::String (largestMove) + " against "
                   + juce::String (expectedMove) + " of " + juce::String (channelSpan) + ")");

        auto warmest = true;
        for (int i = 1; i < ChassisLayout::kNumStrips; ++i)
        {
            const auto point = insideOf (layout.strips[static_cast<size_t> (i)]);
            warmest = warmest && (warmth (anchor) > warmth (pixelAt (image, point.x, point.y)));
        }

        check (warmest, "and it is the ONLY warm strip — no other carries the tint");
    }

    // The two raised-edge highlights, each on the row it actually occupies.
    //
    // These exist because changing the light header's highlight from 0.50 to
    // 0.05 white — a 10x error — passed all 1288 checks: verify-theme.py pins
    // the VALUE and nothing proved the value reaches a pixel. Writing them
    // also found that the footer's highlight was painted and then overpainted
    // by its own border, so it never rendered at all.
    //
    // The probe row follows the CSS box model: an inset box-shadow is drawn
    // inside the border box, so a surface with a border-top has its highlight
    // one row down, and one with a border-left is inset one column.
    {
        const auto shadows = theme::shadowsFor (mode);
        const auto raised  = theme::colour (theme::Token::raised, mode);

        // The header paints a gradient, so the ground beneath its top row is
        // the gradient's first stop and not the flat token.
        const auto headerGround = theme::mix (raised, juce::Colours::white, ChassisLayout::kHeaderGradientWeight);

        struct Edge
        {
            const char*          what;
            juce::Point<int>     probe;
            juce::Colour         ground;
            juce::Colour         highlight;
        };

        const std::array<Edge, 3> edges {{
            { "the header", { layout.header.getCentreX(), layout.header.getY() },
              headerGround, shadows.headerHighlight },
            // border-left, so the highlight is inset one column.
            { "the side panel", { layout.sidePanel.getCentreX(), layout.sidePanel.getY() },
              raised, shadows.raisedHighlight },
            // border-top, so the highlight is one row BELOW the border.
            { "the footer", { layout.footer.getCentreX(), layout.footer.getY() + 1 },
              raised, shadows.raisedHighlight },
        }};

        for (const auto& edge : edges)
            checkPixelNear (image, edge.probe.x, edge.probe.y,
                            edge.ground.overlaidWith (edge.highlight), kCompositeSlop,
                            juce::String (edge.what) + "'s `inset 0 1px 0` highlight is on its own row");

        // And the probe can tell the two recipes apart where they differ. In
        // dark they are 0.04 and 0.05 white and genuinely indistinguishable at
        // this tolerance, so the claim is made only where it is true — the
        // light theme, where the regression was 0.05 against 0.50.
        const auto asHeader = raised.overlaidWith (shadows.headerHighlight);
        const auto asRaised = raised.overlaidWith (shadows.raisedHighlight);

        // Across ALL channels, not red alone: --raised is #faf7f0 in the light
        // theme, so white at 0.05 against 0.50 moves red by 3 and blue by 7.
        // Measuring one channel reported 2 and made a real 7/255 separation
        // look like none.
        const auto apart = juce::jmax (std::abs ((int) asHeader.getRed()   - (int) asRaised.getRed()),
                                       std::abs ((int) asHeader.getGreen() - (int) asRaised.getGreen()),
                                       std::abs ((int) asHeader.getBlue()  - (int) asRaised.getBlue()));

        if (mode == theme::Mode::light)
            check (apart > kCompositeSlop,
                   "and in the light theme the two recipes are far enough apart that this probe "
                   "would catch them being swapped (" + juce::String (apart)
                       + " against a tolerance of " + juce::String (kCompositeSlop) + ")");
        else
            check (apart <= kCompositeSlop,
                   "while in the dark theme 0.04 and 0.05 white land " + juce::String (apart)
                       + "/255 apart — inside the tolerance, so the probe cannot separate them "
                         "there and does not claim to");
    }

    // The accent bars, one per strip, each exactly its instrument's colour.
    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
    {
        // The bar comes from the layout the paint code used, not from the pad
        // constants re-added here: those agreed by coincidence, and reordering
        // the stacking would have left this probing panel fill and passing.
        const auto strip = layout.strips[static_cast<size_t> (i)];
        const auto barY  = layout.stripLayouts[static_cast<size_t> (i)].accentBar.getCentreY();

        checkPixel (image, strip.getCentreX(), barY,
                    theme::accent (static_cast<theme::Accent> (i)),
                    juce::String ("strip ") + juce::String (i + 1) + "'s accent bar is its accent colour");
    }

    // The 1 px inter-strip gaps are the dividers, and they show --line
    // composited over --bg, NOT over --panel: the strips do not paint beneath
    // the gaps, so what is behind a gap is the chassis ground. That matches the
    // stylesheet, where `.matrix { background: var(--line) }` sits on the
    // chassis. This test first expected --line over --panel and failed against
    // correct code.
    for (size_t i = 1; i < layout.strips.size(); ++i)
    {
        const auto gapX = layout.strips[i - 1].getRight();
        const auto expected = theme::colour (theme::Token::bg, mode)
                                  .overlaidWith (theme::colour (theme::Token::line, mode));

        checkPixelNear (image, gapX, insideOf (layout.matrix).y, expected, kCompositeSlop,
                        "gap " + juce::String ((int) i) + " shows --line over --bg, so the gap IS the divider");

        // The slop must not be wide enough to swallow the failure that matters:
        // a gap painted as --panel instead of the divider.
        check (std::abs ((int) expected.getRed() - (int) theme::colour (theme::Token::panel, mode).getRed())
                 > kCompositeSlop,
               "and the tolerance is far narrower than the difference from --panel, so it "
               "cannot hide an undrawn divider");

        // And it is distinguishable from a strip: a 1 px gap that happened to
        // match --panel would be invisible, which is the failure that matters.
        check (expected != theme::colour (theme::Token::panel, mode),
               "and reads differently from the strips it separates");
    }

    // Nothing is left unpainted: --bg must not survive anywhere inside the
    // chassis, because every region covers it. A region that failed to paint
    // would show as --bg and this is what would catch it.
    {
        auto bgPixels = 0;
        const auto bg = theme::colour (theme::Token::bg, mode);

        for (int y = 0; y < image.getHeight(); y += 7)
            for (int x = 0; x < image.getWidth(); x += 7)
                if (pixelAt (image, x, y) == bg)
                    ++bgPixels;

        check (bgPixels == 0,
               "no sampled pixel is still --bg, so every region painted ("
                   + juce::String (bgPixels) + " left)");
    }
}

void testStripNamesAreDrawn()
{
    section ("the strips carry their names, from ids::channelInfos");

    ForroBoxLookAndFeel lnf { theme::Mode::dark };
    Chassis chassis { lnf };

    const auto image = renderComponent (chassis, ChassisLayout::kWidth, ChassisLayout::kHeight);
    const auto& layout = chassis.getLayout();

    // Ink in the head row of every strip, measured as CONTRAST against that
    // strip's own background. Not the string itself: reading text back out of
    // pixels would be an OCR instrument needing its own proof, and what is at
    // risk here is a strip drawing nothing.
    //
    // `inkMass` was used first and made the check unfailable: --panel has
    // brightness 0.118, so an empty 163x16 region already scores ~300 and every
    // threshold that detects text is one an empty region also clears.
    const auto strippedBackground = [&] (int index)
    {
        const auto panel = theme::colour (theme::Token::panel, theme::Mode::dark);

        return index == 0 ? theme::mix (panel, theme::accent (theme::Accent::zabumba),
                                        ChassisLayout::kAnchorAccentWeight)
                          : panel;
    };

    const auto headRowOf = [&] (int index)
    {
        return layout.stripLayouts[static_cast<size_t> (index)].headRow;
    };

    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
    {
        const auto mass = contrastMass (image, headRowOf (i), strippedBackground (i));

        check (mass > 50.0,
               juce::String ("strip ") + juce::String (i + 1) + " ("
                   + forrobox::ids::channelInfos[static_cast<size_t> (i)].displayName
                   + ") draws text in its head row (contrast " + juce::String (mass, 1) + ")");
    }

    // The bound that makes the threshold above mean something: an equally sized
    // region of the SAME strip, in the reserved area, must measure far lower.
    // Without this the checks above are satisfied by any non-empty rendering.
    {
        // The reserved controls box IS the empty region, so this reads the
        // layout's own rectangle instead of guessing 30 px up from the bottom.
        // The BOTTOM of the reserved box: its top rows sit inside the accent
        // bar's 10 px glow, which measured 23.8 rather than 0 and is real ink.
        const auto controls = layout.stripLayouts[2].controls;
        const auto blank = controls.withTrimmedTop (controls.getHeight()
                                                    - ChassisLayout::kHeadRowHeight);

        const auto blankMass = contrastMass (image, blank, strippedBackground (2));
        const auto textMass  = contrastMass (image, headRowOf (2), strippedBackground (2));

        checkEqual (blankMass, 0.0,
                    "while an equally sized reserved region of the same strip measures 0");
        check (textMass > 10.0 * juce::jmax (1.0, blankMass),
               "so the head-row measurement is reading the text, not the strip ("
                   + juce::String (textMass, 1) + " against " + juce::String (blankMass, 1) + ")");
    }
}

// ── AC-1 / AC-2: the knob's geometry and both polarities ────────────────────

/** Renders one knob on a black ground, so the instruments have a known
    background and the arc band is uncontaminated by chassis surfaces. */
struct KnobRig
{
    KnobRig (theme::Mode mode, int dialSize, Knob::Polarity polarity, float proportion,
             juce::Colour arcColour = juce::Colour (0xffe8650a), juce::String label = {})
        : lnf (mode),
          knobComponent (lnf, dialSize, polarity, arcColour, label)
    {
        knobComponent.setProportion (proportion);

        // A black holder: `renderComponent` paints into a transparent image, and
        // the knob itself is not opaque, so without a ground the brightness
        // instruments would be measuring against alpha rather than a colour.
        // Sized for whether this knob HAS a label. It was always sized
        // `preferredHeight (dialSize, false)`, so a labelled knob got no room
        // for its label row and labelBounds() came back empty — the rig could
        // not render one even when asked.
        holder.addAndMakeVisible (knobComponent);
        holder.setSize (dialSize, Knob::preferredHeight (dialSize, label.isNotEmpty()));
        knobComponent.setBounds (holder.getLocalBounds());
    }

    juce::Image render() { return renderComponent (holder, holder.getWidth(), holder.getHeight()); }

    juce::Point<float> centre() const
    {
        return knobComponent.dialBounds().getCentre();
    }

    ForroBoxLookAndFeel lnf;
    Knob                knobComponent;
    Ground              holder;
};

void testKnobGeometryIsRelative()
{
    section ("the knob's geometry is the spec's, at every size");

    // PLANNING.md:347 — "rendered at 28/32/54px" from ONE 100x100 viewBox.
    const std::array<int, 3> sizes { 28, 32, 54 };
    std::array<double, 3> measuredArcRadius {};

    for (size_t i = 0; i < sizes.size(); ++i)
    {
        const auto size = sizes[i];
        const auto label = juce::String (size) + " px";

        // Full travel, unipolar: the arc spans the whole sweep, which gives the
        // radius measurement the most ink to work with.
        KnobRig rig { theme::Mode::dark, size, Knob::Polarity::unipolar, 1.0f };
        const auto image = rig.render();
        const auto c = rig.centre();
        const auto scale = static_cast<double> (size) / forrobox::knob::kViewBox;

        // The arc's radius, measured — not probed at one predicted pixel.
        const auto expectedArc = forrobox::knob::kArcRadius * scale;
        const auto arcR = inkRadiusCentroid (image, c,
                                             static_cast<float> (expectedArc - 4.0 * scale * 2.0),
                                             static_cast<float> (expectedArc + 4.0 * scale * 2.0),
                                             juce::Colours::black);
        measuredArcRadius[i] = arcR;

        check (std::abs (arcR - expectedArc) <= juce::jmax (0.6, 0.06 * expectedArc),
               "at " + label + " the value arc sits at radius " + juce::String (expectedArc, 2)
                   + " viewBox-scaled (measured " + juce::String (arcR, 2) + ")");

        // The hub is a DIFFERENT radius, and must be found there and not at the
        // arc's radius — the pair is what proves the scaling is not collapsing.
        const auto expectedHub = forrobox::knob::kHubRadius * scale;

        check (expectedArc > expectedHub,
               "at " + label + " the arc radius exceeds the hub radius, as 38 > 30");

        // Ink in the sweep's 90-degree gap (below the knob) can only be hub or
        // background — the arc never reaches there. So a hub-radius band probed
        // in the gap isolates the hub.
        const auto gapProfile = arcProfile (image, c,
                                            static_cast<float> (expectedHub * 0.4),
                                            static_cast<float> (expectedHub),
                                            juce::Colours::black);

        check (gapProfile.over (160.0f, 180.0f) > 0.0,
               "at " + label + " the hub fills the sweep's gap below the knob, so it is painted "
                               "at its own radius and not the arc's");
    }

    // ── the relative claim itself ───────────────────────────────────────────
    //
    // This is AC-1's load-bearing assertion: if 38/30/5/16 had been treated as
    // PIXELS, every knob would share one radius and this ratio would be 1.0.
    const auto ratio = measuredArcRadius[2] / measuredArcRadius[1];
    const auto expectedRatio = 54.0 / 32.0;

    check (std::abs (ratio - expectedRatio) <= 0.04,
           "a 54 px knob's arc radius is 54/32 times a 32 px knob's — the geometry is RELATIVE, "
           "not pixels (" + juce::String (ratio, 4) + " against "
               + juce::String (expectedRatio, 4) + ")");

    check (std::abs (ratio - 1.0) > 0.5,
           "and the ratio is emphatically not 1.0, which is what treating the viewBox units as "
           "pixels would have produced");

    // ── the micro-label reaches a pixel ─────────────────────────────────────
    //
    // Its HEIGHT is cross-checked three ways (kLabelHeight from the type scale,
    // kKnobCellHeight, preferredHeight) and its INK was checked nowhere: every
    // KnobRig defaulted to an empty label, so deleting the label block from
    // Knob::paint left the suite green. 04-01's rule, in its purest form — a
    // value cross-check does not prove the value reaches a pixel.
    {
        KnobRig labelled { theme::Mode::dark, 54, Knob::Polarity::unipolar, 0.5f,
                           juce::Colour (0xffe8650a), "VOL" };
        KnobRig bare     { theme::Mode::dark, 54, Knob::Polarity::unipolar, 0.5f };

        // Measured in the LABEL ROW only, below the dial, where no arc reaches.
        const auto labelRow = labelled.knobComponent.labelBounds();

        check (! labelRow.isEmpty(), "a labelled knob reserves a label row");

        const auto inked = contrastMass (labelled.render(), labelRow, juce::Colours::black);
        const auto blank = contrastMass (bare.render(),
                                         { labelRow.getX(), labelRow.getY(),
                                           labelRow.getWidth(), labelRow.getHeight() },
                                         juce::Colours::black);

        // The floor is 1.0, not a guessed 5.0: "VOL" at 9 px over a 54 px knob
        // measures 4.7, so a 5.0 threshold was above the real value. This
        // check's job is only "there is ink"; the RATIO below is what proves it
        // is the label rather than the dial.
        check (inked > 1.0,
               "and draws its text there (contrast " + juce::String (inked, 1)
                   + ", measured 4.7 for VOL at 54 px)");
        check (inked > blank * 10.0,
               "while an unlabelled knob's same region is empty (" + juce::String (blank, 1)
                   + ") — so this is reading the LABEL, not the dial above it");
    }

    // The label row is the knob's own, not the chassis's.
    checkEqual (Knob::preferredHeight (32, false), 32,
                "an unlabelled knob needs only its dial");
    checkEqual (Knob::preferredHeight (32, true),
                32 + forrobox::knob::kLabelGap + forrobox::knob::kLabelHeight,
                "and a labelled one adds the gap and the micro-label row");
}

void testKnobPolarities()
{
    section ("unipolar grows from the sweep start, bipolar from centre");

    const auto measure = [] (Knob::Polarity polarity, float proportion)
    {
        KnobRig rig { theme::Mode::dark, 54, polarity, proportion };
        const auto scale = 54.0 / forrobox::knob::kViewBox;
        const auto expected = forrobox::knob::kArcRadius * scale;

        return arcProfile (rig.render(), rig.centre(),
                           static_cast<float> (expected - 5.0),
                           static_cast<float> (expected + 5.0),
                           juce::Colours::black);
    };

    // ── unipolar ────────────────────────────────────────────────────────────
    {
        const auto atMin = measure (Knob::Polarity::unipolar, 0.0f);
        const auto atHalf = measure (Knob::Polarity::unipolar, 0.5f);
        const auto atMax = measure (Knob::Polarity::unipolar, 1.0f);

        // At the minimum the VALUE arc has zero extent, so the only ink in the
        // band is the track. At half it covers the left sweep. The track is
        // present in all three, so the claims are about the LEFT/RIGHT split.
        check (atHalf.over (-135.0f, -10.0f) > 0.0,
               "a unipolar knob at half travel has ink left of centre");

        // Half travel must fill the left sweep MORE than the right, because the
        // value arc doubles the ink there on top of the track.
        check (atHalf.over (-135.0f, -10.0f) > atHalf.over (10.0f, 135.0f) * 1.3,
               "and measurably more there than to the right, where only the track sits ("
                   + juce::String (atHalf.over (-135.0f, -10.0f), 1) + " against "
                   + juce::String (atHalf.over (10.0f, 135.0f), 1) + ")");

        // At full travel both halves carry the value arc, so the asymmetry goes.
        const auto fullSplit = atMax.over (-135.0f, -10.0f) / juce::jmax (1.0, atMax.over (10.0f, 135.0f));
        check (fullSplit < 1.3,
               "at full travel the arc covers both halves, so the asymmetry disappears ("
                   + juce::String (fullSplit, 2) + ")");

        check (atMin.total < atMax.total,
               "and a knob at its minimum carries less ink than one at its maximum, because the "
               "value arc has no extent there");
    }

    // ── bipolar ─────────────────────────────────────────────────────────────
    {
        const auto atCentre = measure (Knob::Polarity::bipolar, 0.5f);
        const auto below = measure (Knob::Polarity::bipolar, 0.15f);
        const auto above = measure (Knob::Polarity::bipolar, 0.85f);

        // The defining property: at centre the value arc has ZERO extent, so
        // the band holds only the track — symmetric left and right.
        const auto centreSplit = atCentre.over (-135.0f, -10.0f)
                               / juce::jmax (1.0, atCentre.over (10.0f, 135.0f));
        check (std::abs (centreSplit - 1.0) < 0.3,
               fbtest::utf8 ("a bipolar knob at centre is left/right symmetric — its value arc has no extent (")
                   + juce::String (centreSplit, 2) + ")");

        check (below.over (-135.0f, -10.0f) > below.over (10.0f, 135.0f) * 1.3,
               "below centre it sweeps LEFT");
        check (above.over (10.0f, 135.0f) > above.over (-135.0f, -10.0f) * 1.3,
               "above centre it sweeps RIGHT — which is the direction a unipolar knob never does");
    }

    // ── the two polarities differ at the SAME value ─────────────────────────
    //
    // The claim that actually separates the implementations: at 0.5 a unipolar
    // knob has filled half its sweep and a bipolar one has drawn nothing.
    {
        const auto uni = measure (Knob::Polarity::unipolar, 0.5f);
        const auto bip = measure (Knob::Polarity::bipolar, 0.5f);

        check (uni.over (-135.0f, -10.0f) > bip.over (-135.0f, -10.0f) * 1.3,
               "at the SAME half-travel value the unipolar knob has filled its left sweep and the "
               "bipolar one has not — so polarity is observable, not just declared ("
                   + juce::String (uni.over (-135.0f, -10.0f), 1) + " against "
                   + juce::String (bip.over (-135.0f, -10.0f), 1) + ")");
    }

    // ── and the indicator line ignores polarity ─────────────────────────────
    //
    // PLANNING.md:352-357: polarity changes the ARC; the line points to the
    // value angle in both. Measured inside the hub, where no arc reaches.
    {
        const auto lineProfile = [] (Knob::Polarity polarity)
        {
            KnobRig rig { theme::Mode::dark, 54, polarity, 0.5f };
            const auto scale = 54.0 / forrobox::knob::kViewBox;

            return arcProfile (rig.render(), rig.centre(), 2.0f,
                               static_cast<float> (forrobox::knob::kHubRadius * scale * 0.8),
                               juce::Colours::black);
        };

        const auto uniLine = lineProfile (Knob::Polarity::unipolar);
        const auto bipLine = lineProfile (Knob::Polarity::bipolar);

        // At proportion 0.5 the value angle is 0 — straight up. Both must put
        // the line's ink in the same place.
        check (uniLine.over (-15.0f, 15.0f) > 0.0,
               "at half travel the indicator line points straight up");
        check (std::abs (uniLine.over (-15.0f, 15.0f) - bipLine.over (-15.0f, 15.0f))
                   < uniLine.over (-15.0f, 15.0f) * 0.1,
               "and both polarities place it identically — polarity changes the arc, not the line");
    }

    // ── the line MOVES, which is the knob's identity ────────────────────────
    //
    // Added because a control froze the rotation at 0 and every check above
    // still passed: they all measure at proportion 0.5, where the value angle
    // IS 0. A knob whose indicator never moves would have shipped, and
    // PLANNING.md:357 calls that line the knob's identity.
    {
        const auto lineSector = [] (float proportion)
        {
            KnobRig rig { theme::Mode::dark, 54, Knob::Polarity::unipolar, proportion };
            const auto scale = 54.0 / forrobox::knob::kViewBox;

            return arcProfile (rig.render(), rig.centre(), 2.0f,
                               static_cast<float> (forrobox::knob::kHubRadius * scale * 0.8),
                               juce::Colours::black);
        };

        const auto atMin = lineSector (0.0f);
        const auto atMid = lineSector (0.5f);
        const auto atMax = lineSector (1.0f);

        // -135, 0 and +135 degrees: the line's ink must be in three different
        // places, and each must dominate its own sector.
        check (atMin.over (-150.0f, -120.0f) > atMin.over (-15.0f, 15.0f) * 2.0,
               "at its minimum the indicator points down-left, toward -135 degrees");
        check (atMid.over (-15.0f, 15.0f) > atMid.over (-150.0f, -120.0f) * 2.0,
               "at half travel it points up");
        check (atMax.over (120.0f, 150.0f) > atMax.over (-15.0f, 15.0f) * 2.0,
               "and at its maximum down-right, toward +135 — so the line TRACKS the value");
    }

    // ── the focus ring ──────────────────────────────────────────────────────
    //
    // Also added from a control: nothing exercised it, so a knob that never
    // showed focus passed. css:359 makes the ring the HUB STROKE turning
    // --active, so it is measured on the hub's edge and not around the dial.
    {
        const auto hubEdge = [] (bool focused)
        {
            KnobRig rig { theme::Mode::dark, 54, Knob::Polarity::unipolar, 0.5f };
            rig.knobComponent.setShowingFocusRing (focused);

            const auto scale = 54.0 / forrobox::knob::kViewBox;
            const auto hubR = forrobox::knob::kHubRadius * scale;

            // A thin band ON the hub's edge, and only in the sweep's bottom gap
            // so no arc or indicator ink can reach it.
            const auto profile = arcProfile (rig.render(), rig.centre(),
                                             static_cast<float> (hubR - 1.5),
                                             static_cast<float> (hubR + 1.5),
                                             theme::colour (theme::Token::hub, theme::Mode::dark));
            return profile.over (160.0f, 180.0f);
        };

        const auto unfocused = hubEdge (false);
        const auto focused = hubEdge (true);

        // --active is rgba(255,255,255,0.7) and --line is 0x17ffffff (alpha 23)
        // in the dark theme, so the focused stroke departs from --hub far more.
        check (focused > unfocused * 2.0,
               "a focused knob's hub stroke departs from --hub far more than an unfocused one's, "
               "because the ring IS that stroke turning --active (" + juce::String (focused, 2)
                   + " against " + juce::String (unfocused, 2) + ")");
        check (unfocused > 0.0,
               "and the unfocused stroke is still drawn — --line, not nothing");
    }
}

// ── 04-03 AC-3: the button family ───────────────────────────────────────────

/** One button on a known ground, sized as it asks to be sized. */
struct ButtonRig
{
    ButtonRig (theme::Mode mode, Button::Variant variant, juce::String label,
               Button::OnStyle onStyle = Button::OnStyle::active)
        : lnf (mode), button (lnf, variant, std::move (label), onStyle)
    {
        holder.ground = theme::colour (theme::Token::panel, mode);
        holder.addAndMakeVisible (button);

        // Sized by the BUTTON's own preferred box, then asked for the BOUNDS
        // that box needs — the transport variant's lit glow falls outside it,
        // and a Component's paint is clipped to its bounds. The box is offset
        // by the glow margin so the bounds start at kMargin rather than running
        // negative.
        const auto glow = Button::glowMargin (variant);

        const auto bounds = Button::boundsForBox (
            variant, { kMargin + glow, kMargin + glow, button.preferredWidth(),
                       button.preferredHeight() });

        holder.setSize (bounds.getRight() + kMargin, bounds.getBottom() + kMargin);
        button.setBounds (bounds);
    }

    static constexpr int kMargin = 6;

    juce::Image render() { return renderComponent (holder, holder.getWidth(), holder.getHeight()); }

    /** The button's own area in the holder's coordinates. */
    juce::Rectangle<int> area() const
    {
        return button.getBounds().reduced (Button::glowMargin (button.getVariant()));
    }

    /** A pixel well inside the button, away from its border. */
    juce::Point<int> inside() const { return area().reduced (4).getCentre(); }

    ForroBoxLookAndFeel lnf;
    Button              button;
    Ground              holder;
};

void testButtonFamily (theme::Mode mode, const juce::String& modeName)
{
    section (fbtest::utf8 ("the button family is one component with three variants — ") + modeName);

    const auto panel = theme::colour (theme::Token::panel, mode);

    // ── the base button ─────────────────────────────────────────────────────
    {
        ButtonRig rig { mode, Button::Variant::base, "LOAD" };

        checkEqual (rig.button.preferredHeight(),
                    juce::roundToInt (type::styleFor (type::Style::buttonLabel).heightPx)
                        + Button::kBasePadY * 2 + Button::kBorderWidth * 2,
                    modeName + ": the base button is its label row plus padding and borders");

        check (rig.button.preferredWidth()
                   > juce::roundToInt (type::trackedWidth (type::Style::buttonLabel, "LOAD")),
               modeName + ": and wider than its label, because it has padding");

        // Unlit: transparent ground, so the holder's --panel shows through.
        const auto unlit = rig.render();
        const auto centre = rig.inside();

        checkPixelNear (unlit, centre.x, centre.y + 6, panel, kCompositeSlop,
                        modeName + ": an unlit base button's ground is transparent — the surface "
                                   "beneath shows through");

        // Lit: --active ground.
        rig.button.setOn (true);
        const auto lit = rig.render();

        checkPixelNear (lit, centre.x, centre.y + 6,
                        panel.overlaidWith (theme::colour (theme::Token::active, mode)),
                        kCompositeSlop,
                        modeName + ": a lit one paints --active (css:144)");

        // And the label inverts to --bg. Measured as contrast against the lit
        // ground, so it is the TEXT being read and not the fill.
        const auto litText = contrastMass (lit, rig.area(),
                                           panel.overlaidWith (theme::colour (theme::Token::active, mode)));
        check (litText > 1.0,
               modeName + ": and its label is drawn in --bg over that ground (contrast "
                   + juce::String (litText, 1) + ")");
    }

    // ── mute and solo, whose on-colours are three different CSS rules ───────
    {
        ButtonRig muteRig { mode, Button::Variant::muteSolo, "M", Button::OnStyle::mute };
        ButtonRig soloRig { mode, Button::Variant::muteSolo, "S", Button::OnStyle::solo };

        muteRig.button.setOn (true);
        soloRig.button.setOn (true);

        const auto m = muteRig.inside();
        const auto s = soloRig.inside();

        checkPixelNear (muteRig.render(), m.x, m.y + 5,
                        panel.overlaidWith (theme::colour (theme::Token::danger, mode)),
                        kCompositeSlop,
                        modeName + ": M lit paints --danger (css:342)");
        checkPixelNear (soloRig.render(), s.x, s.y + 5,
                        panel.overlaidWith (theme::accent (theme::Accent::pandeiro)),
                        kCompositeSlop,
                        modeName + ": S lit paints --c-pandeiro (css:343)");

        // The two are NOT the same colour — which is the whole point of the
        // pair, and what a single shared on-colour would have broken.
        const auto mLit = theme::colour (theme::Token::danger, mode);
        const auto sLit = theme::accent (theme::Accent::pandeiro);

        check (colourDistance (mLit, sLit) > 0.3,
               modeName + ": and the two lit colours are far apart, so the probes above can tell "
                          "them apart (" + juce::String (colourDistance (mLit, sLit), 2) + ")");
    }

    // ── the arrow, the only variant with a fixed box and its own ground ─────
    {
        ButtonRig rig { mode, Button::Variant::arrow, juce::String::fromUTF8 ("\u2039") };

        checkEqual (rig.button.preferredWidth(), Button::kArrowWidth,
                    modeName + ": the arrow is a fixed 22 px wide");
        checkEqual (rig.button.preferredHeight(), Button::kArrowHeight,
                    modeName + ": and a fixed 26 px tall — it does not hug its glyph");

        // Unlike the other two, it has a ground when UNLIT (css:232).
        const auto c = rig.inside();
        checkPixelNear (rig.render(), c.x, c.y + 6, theme::colour (theme::Token::panel, mode),
                        kCompositeSlop,
                        modeName + ": and it sits on --panel even unlit, which the other two do not");
    }

    // ── what proves ONE painting path ───────────────────────────────────────
    //
    // Not assertable directly, so the shared BEHAVIOUR is asserted instead:
    // every variant lifts its label on hover, and every variant presses. Three
    // paint methods would let one of them silently lose either.
    {
        // `pressesPerCss` is read from the STYLESHEET, not from
        // Button::specFor: the first version branched on `spec.pressScale`, so
        // a control that gave mute/solo a press scale moved the expectation
        // with it and the check stayed green. A test that consults the table
        // under test proves the table agrees with itself.
        //
        // `.btn:active` (css:142) and `.arrow-btn:active` (css:236) are the only
        // two `:active` rules in forrobox.css.
        struct VariantCase { Button::Variant variant; const char* name; bool pressesPerCss; };

        const std::array<VariantCase, 4> all {{
            { Button::Variant::base,     "base",     true  },
            { Button::Variant::muteSolo, "muteSolo", false },
            { Button::Variant::arrow,    "arrow",    true  },
            { Button::Variant::load,     "load",     false },
        }};

        for (const auto& [variant, name, pressesPerCss] : all)
        {
            ButtonRig rig { mode, variant, "M" };

            const auto resting = contrastMass (rig.render(), rig.area(), panel);

            rig.button.mouseEnter (mouseEventOn (rig.button, rig.inside().toFloat(), {}, 0));

            const auto hovered = contrastMass (rig.render(), rig.area(), panel);

            check (hovered > resting,
                   modeName + ": the " + name + " variant lifts on hover (" + juce::String (resting, 1)
                       + " -> " + juce::String (hovered, 1) + ")");
        // ── press: only the variants whose rule declares one ────────────────
        //
        // `.btn:active` (css:142) and `.arrow-btn:active` (css:236) are the ONLY
        // `:active` rules in the stylesheet. `.ms-btn` and `.load-btn` have
        // none, and Task 1 gave mute/solo the base button's 0.96 anyway — an
        // invented behaviour that every rendering check happily accepted,
        // because none of them pressed a button at all.
        //
        // Compared pixel for pixel against the resting render: a scale of 1 is
        // the identity, so "no press" is an assertion that the two are the SAME
        // image, which no tolerance can fudge.
        {
            ButtonRig pressRig { mode, variant, "M" };

            const auto beforePress = pressRig.render();

            pressRig.button.mouseDown (mouseEventOn (pressRig.button, pressRig.inside().toFloat()));

            const auto difference = maxPixelDifference (beforePress, pressRig.render());

            if (pressesPerCss)
                check (difference > 0.0,
                       modeName + ": the " + name + " variant scales on press, as its :active rule "
                                                    "says (" + juce::String (difference, 4) + ")");
            else
                checkEqual (difference, 0.0,
                            modeName + ": the " + name + " variant does NOT scale on press — "
                                                         "forrobox.css gives it no :active rule");
        }
        }
    }
}

// ── AC-7: text-overflow: ellipsis ───────────────────────────────────────────

void testEllipsis()
{
    section ("type::ellipsised shortens only what does not fit");

    constexpr auto style = type::Style::sampleName;
    const juce::String ellipsis = juce::String::fromUTF8 ("\xe2\x80\xa6");
    const juce::String name = "Pandeiro Medio";

    const auto full = type::trackedWidth (style, name);

    checkEqual (type::ellipsised (style, name, full + 10.0f), name,
                "a name that fits is returned untouched");
    checkEqual (type::ellipsised (style, name, full), name,
                "and so is one that fits EXACTLY — the comparison is <=, not <");

    // The case the strip will hit the day a sample name grows. Half the width,
    // so the answer is not "drop one character".
    {
        const auto budget = full * 0.5f;
        const auto cut = type::ellipsised (style, name, budget);

        check (cut != name, "a name that does not fit is shortened");
        check (cut.endsWith (ellipsis), "and ends with an ellipsis (" + cut + ")");
        check (type::trackedWidth (style, cut) <= budget,
               "and the result actually FITS the budget it was given — measured with the same "
               "trackedWidth that will draw it, not with Font::getStringWidth, which ignores the "
               "tracking and would cut at the wrong character");
        check (cut.length() < name.length(),
               "and is strictly shorter than what it replaced");
    }

    // The rejections.
    checkEqual (type::ellipsised (style, name, 0.0f), juce::String(),
                "a zero budget returns nothing rather than a stub that overflows it");
    checkEqual (type::ellipsised (style, name, -5.0f), juce::String(),
                "and so does a negative one");
    checkEqual (type::ellipsised (style, name, 0.5f), ellipsis,
                "a budget too small even for the ellipsis returns the ellipsis alone, which is what "
                "the loop bottoms out at — stated so it is a decision rather than an accident");
}

// ── AC-3 / AC-4: the gestures, and the parameter as the single source ───────

/** A knob attached to a REAL parameter on a real processor.

    No fake parameter and no unattached mode: the plan's rule is one path, so
    what the tests drive is what the plugin runs. juce::ParameterAttachment
    posts its parameter->UI updates through an AsyncUpdater, so `settle()`
    pumps the message loop wherever a test needs the knob to have caught up. */
struct AttachedKnobRig
{
    explicit AttachedKnobRig (const juce::String& parameterId, Knob::Polarity polarity)
        : parameter (*dynamic_cast<juce::RangedAudioParameter*> (
                         processor.getAPVTS().getParameter (parameterId))),
          knobComponent (lnf, 54, polarity, juce::Colour (0xffe8650a), "VOL"),
          attachment (parameter, knobComponent)
    {
        holder.addAndMakeVisible (knobComponent);
        holder.addAndMakeVisible (tooltip);
        holder.setSize (200, 200);
        knobComponent.setBounds (60, 60, 54, Knob::preferredHeight (54, true));
        knobComponent.setTooltip (&tooltip);
    }

    static void settle()
    {
        // ParameterAttachment posts its parameter -> UI update through an
        // AsyncUpdater, so the message queue has to be drained before the knob
        // has caught up. There is no dispatch loop in a console test, so the
        // pending updates are delivered directly.
        // 1 ms, not 8. runDispatchLoopUntil is a FIXED-duration loop — it never
        // returns early when the queue empties — so every call slept the full
        // budget. Measured: 92 calls x 8.015 ms = 737 ms of a 2.80 s suite,
        // spent asleep. 1 ms still delivers every pending update (verified
        // 40/40 across repeated runs).
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }

    float value() const { return parameter.convertFrom0to1 (parameter.getValue()); }
    float proportion() const { return parameter.getValue(); }

    void setValue (float denormalised)
    {
        parameter.setValueNotifyingHost (parameter.convertTo0to1 (denormalised));
        settle();
    }

    /** A press at the knob's centre, then a drag `dy` pixels UP (positive dy
        raises the value, as controls.js measures `startY - e.clientY`). */
    void drag (int dy, juce::ModifierKeys mods = {})
    {
        const auto centre = knobComponent.getLocalBounds().getCentre();
        const auto down = eventAt (centre, mods);
        knobComponent.mouseDown (down);

        const auto moved = centre.translated (0, -dy).toFloat();
        knobComponent.mouseDrag (down.withNewPosition (moved));
        knobComponent.mouseUp (down.withNewPosition (moved));
        settle();
    }

    /** One MouseEvent on this knob. Seven sites used to spell out the same
        14-argument constructor, five of whose arguments are floats nobody
        reads — a transposed pair would have been invisible. */
    juce::MouseEvent eventAt (juce::Point<int> localPos, juce::ModifierKeys mods = {},
                              int numClicks = 1) const
    {
        return mouseEventOn (const_cast<Knob&> (knobComponent), localPos.toFloat(), mods, numClicks);
    }

    void wheel (float deltaY, bool shift, bool reversed = false)
    {
        juce::MouseWheelDetails w {};
        w.deltaY = deltaY;
        w.isReversed = reversed;

        // No juce::ModifierKeys::currentModifiers fiddling: the knob reads the
        // EVENT's modifiers now, so the rig no longer has to mutate and restore
        // process-global state to drive a handler.
        const auto mods = shift ? juce::ModifierKeys (juce::ModifierKeys::shiftModifier)
                                : juce::ModifierKeys();

        knobComponent.mouseWheelMove (
            eventAt (knobComponent.getLocalBounds().getCentre(), mods, 0), w);
        settle();
    }

    void key (int keyCode)
    {
        knobComponent.keyPressed (juce::KeyPress (keyCode));
        settle();
    }

    /** A full press AND release. The first version sent only mouseDown, which
        is exactly why it could not see that mouseUp ended a host gesture the
        Alt branch had never begun. */
    void clickWith (juce::ModifierKeys mods)
    {
        const auto e = eventAt (knobComponent.getLocalBounds().getCentre(), mods);
        knobComponent.mouseDown (e);
        knobComponent.mouseUp (e);
        settle();
    }

    void altClick()   { clickWith (juce::ModifierKeys (juce::ModifierKeys::altModifier)); }
    void rightClick() { clickWith (juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)); }

    ForroBoxAudioProcessor      processor;
    ForroBoxLookAndFeel         lnf { theme::Mode::dark };
    juce::RangedAudioParameter& parameter;
    Knob                        knobComponent;
    ValueTooltip                tooltip { lnf };
    KnobAttachment              attachment;
    Ground                      holder;
};

/** 05-01 /code-review: the guard clears THIS class's callbacks and no others.

    `KnobAttachment`'s reset list was copied verbatim from the hand-written
    destructor it replaced, and that list included `onProportionChanged` — which
    this class never installs. `HeaderBar.cpp:192` does, for the SWING and
    CACHAÇA readouts, and its lambda captures the parameter and the screen rather
    than the attachment, so it has nothing to dangle on.

    Latent today only because `buildGlobalKnob` rebuilds the Knob BEFORE the
    attachment, so the SafePointer is already null when the guard runs — which is
    exactly the declaration-order argument `ScopedControlCallbacks` exists to
    stop anyone from having to make. A detach-and-re-attach would have frozen
    both readouts at their last text, with no error.

    Driven through a real destruction rather than by reading the list, because a
    test that reads the reset lambda is a test that reads the constant it is
    checking. */
void testKnobAttachmentClearsOnlyItsOwnCallbacks()
{
    section ("the lifetime guard clears the callbacks its class installed, and no others");

    ForroBoxAudioProcessor processor;
    ForroBoxLookAndFeel    lnf { theme::Mode::dark };

    auto& parameter = *dynamic_cast<juce::RangedAudioParameter*> (
                          processor.getAPVTS().getParameter (
                              forrobox::ids::channelParam ("zabumba", forrobox::ids::vol)));

    Knob knob { lnf, 54, Knob::Polarity::unipolar, juce::Colour (0xffe8650a), "VOL" };

    auto attachment = std::make_unique<KnobAttachment> (parameter, knob);

    // Installed from OUTSIDE, the way HeaderBar installs the readout seam.
    auto readoutCalls = 0;
    knob.onProportionChanged = [&readoutCalls] (float) { ++readoutCalls; };

    check (knob.onNudge != nullptr && knob.onReset != nullptr
               && knob.getDisplayText != nullptr && knob.onTextEntered != nullptr,
           "the attachment installed its own four callbacks");

    attachment.reset();

    check (knob.onNudge == nullptr && knob.onReset == nullptr
               && knob.getDisplayText == nullptr && knob.onTextEntered == nullptr,
           "and destroying it cleared all four — every one of them captures `this`");

    check (knob.onProportionChanged != nullptr,
           "but NOT onProportionChanged, which it never installed — the readout seam survives an "
           "attachment it does not belong to");

    // And still WORKS, not merely non-null: a cleared-then-restored function
    // object would pass the check above and fire nothing. setProportion fires
    // only on a CHANGE, so the target is picked away from wherever it sits.
    const auto before = readoutCalls;
    knob.setProportion (knob.getProportion() > 0.5f ? 0.1f : 0.9f);

    check (readoutCalls > before,
           "and it still fires, so the readout would keep tracking (" + juce::String (before)
               + " -> " + juce::String (readoutCalls) + " calls)");
}

void testKnobGestures()
{
    section ("the gesture set is controls.js's, law by law");

    const auto volId = forrobox::ids::channelParam ("zabumba", forrobox::ids::vol);

    // ── drag: (dy / 160) * range ────────────────────────────────────────────
    {
        AttachedKnobRig rig { volId, Knob::Polarity::unipolar };
        rig.setValue (50.0f);

        rig.drag (40);

        // VOL is 0..100, so 40 px of 160 is a quarter of the range: +25.
        checkEqual (rig.value(), 75.0f,
                    "a 40 px drag on a 0..100 parameter moves it 40/160 of its range");

        rig.setValue (50.0f);
        rig.drag (-40);
        checkEqual (rig.value(), 25.0f, "and downward by the same amount");
    }

    // ── Shift + drag: x 0.18 ────────────────────────────────────────────────
    {
        AttachedKnobRig rig { volId, Knob::Polarity::unipolar };
        rig.setValue (50.0f);

        rig.drag (40, juce::ModifierKeys (juce::ModifierKeys::shiftModifier));

        // 25 * 0.18 = 4.5, and VOL's interval is 1, so it snaps to 4 or 5.
        const auto moved = rig.value() - 50.0f;

        check (std::abs (moved - 4.5f) <= 0.5f,
               "Shift+drag applies the 0.18 fine factor (moved " + juce::String (moved, 2)
                   + " where coarse would be 25)");
        check (moved < 25.0f * 0.25f,
               "and is emphatically finer than a coarse drag, not a rounding difference");
    }

    // ── the drag is ANCHORED, not incremental ───────────────────────────────
    //
    // Dragging out and back must land exactly where it started. An incremental
    // implementation accumulates rounding per mouse-move and drifts.
    {
        AttachedKnobRig rig { volId, Knob::Polarity::unipolar };
        rig.setValue (50.0f);

        const auto centre = rig.knobComponent.getLocalBounds().getCentre();
        const auto down = mouseEventOn (rig.knobComponent, centre.toFloat());
        rig.knobComponent.mouseDown (down);

        for (int dy : { 3, 9, 17, 31, 17, 9, 3, 0 })
            rig.knobComponent.mouseDrag (down.withNewPosition (centre.translated (0, -dy).toFloat()));

        rig.knobComponent.mouseUp (down);
        AttachedKnobRig::settle();

        checkEqual (rig.value(), 50.0f,
                    "a drag out and back lands exactly where it started — the gesture is anchored "
                    "at mouse-down, not accumulated per move");
    }

    // ── wheel: step * max(1, range/50) coarse, one interval fine ────────────
    {
        AttachedKnobRig rig { volId, Knob::Polarity::unipolar };
        rig.setValue (50.0f);

        rig.wheel (1.0f, false);

        // VOL: range 100, interval 1 -> max(1, 100/50) = 2 intervals.
        checkEqual (rig.value(), 52.0f, "one wheel notch moves max(1, range/50) intervals");

        rig.setValue (50.0f);
        rig.wheel (-1.0f, false);
        checkEqual (rig.value(), 48.0f, "and the other way");

        rig.setValue (50.0f);
        rig.wheel (1.0f, true);
        checkEqual (rig.value(), 51.0f,
                    "Shift+wheel moves ONE interval — the finest step the parameter has. The "
                    "prototype's step*0.2 quantises back to zero on an integer parameter, so "
                    "shift-wheel is a no-op there for every control");
    }

    // ── arrow keys: one step, and only when focused ─────────────────────────
    {
        AttachedKnobRig rig { volId, Knob::Polarity::unipolar };
        rig.setValue (50.0f);

        rig.key (juce::KeyPress::upKey);
        checkEqual (rig.value(), 51.0f, "Up adds one step");

        rig.key (juce::KeyPress::rightKey);
        checkEqual (rig.value(), 52.0f, "and so does Right");

        rig.key (juce::KeyPress::downKey);
        rig.key (juce::KeyPress::leftKey);
        checkEqual (rig.value(), 50.0f, "while Down and Left subtract one each");

        // A key the knob does not handle must be passed on, not swallowed.
        check (! rig.knobComponent.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)),
               "and Space is NOT consumed — it is the transport's, per PLANNING.md:873");
    }

    // ── Alt+click resets; right-click is left to the host ───────────────────
    {
        AttachedKnobRig rig { volId, Knob::Polarity::unipolar };

        const auto defaultValue = rig.parameter.convertFrom0to1 (rig.parameter.getDefaultValue());

        rig.setValue (12.0f);
        checkEqual (rig.value(), 12.0f, "a knob moved away from its default");

        rig.altClick();
        checkEqual (rig.value(), defaultValue,
                    "Alt+click resets to the PARAMETER's default (PLANNING.md:876-878)");

        rig.setValue (12.0f);
        rig.rightClick();
        checkEqual (rig.value(), 12.0f,
                    "while right-click changes nothing — it falls through to the host's own "
                    "parameter menu, which is the whole point of the 876-878 decision");
    }

    // ── the tooltip reports the parameter's own text ────────────────────────
    {
        AttachedKnobRig rig { forrobox::ids::channelParam ("zabumba", forrobox::ids::pan),
                              Knob::Polarity::bipolar };
        rig.setValue (-20.0f);
        rig.drag (0);

        check (rig.tooltip.getText().isNotEmpty(), "a gesture shows the value tooltip");
        checkEqual (rig.tooltip.getText(), rig.parameter.getCurrentValueAsText(),
                    "and it reports the PARAMETER's formatting, not a second formatter the knob "
                    "invented — PAN reads as L/C/R");
    }
}

void testKnobIsAViewOfItsParameter()
{
    section ("a knob is a view of a parameter, not a second copy of its state");

    const auto volId = forrobox::ids::channelParam ("triangulo", forrobox::ids::vol);
    const auto pitchId = forrobox::ids::channelParam ("triangulo", forrobox::ids::pitch);

    // ── parameter -> knob, without a write back ─────────────────────────────
    {
        AttachedKnobRig rig { volId, Knob::Polarity::unipolar };

        rig.setValue (30.0f);
        checkEqual (rig.knobComponent.getProportion(), 0.30f,
                    "a parameter changed from outside repaints the knob to match");

        // A repaint must not become a parameter change. Painting the knob many
        // times must leave the parameter exactly where it was — this is what
        // would break under host automation, where the two would fight.
        const auto before = rig.value();

        for (int i = 0; i < 5; ++i)
            renderComponent (rig.knobComponent, 54, Knob::preferredHeight (54, true));

        AttachedKnobRig::settle();
        checkEqual (rig.value(), before, "and repainting it writes nothing back");
    }

    // ── the range and interval come from the parameter ──────────────────────
    {
        AttachedKnobRig pitch { pitchId, Knob::Polarity::bipolar };

        // PITCH is an AudioParameterInt over -12..+12 — a different range AND a
        // different type from VOL, with no per-type code in the knob.
        pitch.setValue (0.0f);
        pitch.key (juce::KeyPress::upKey);
        checkEqual (pitch.value(), 1.0f, "PITCH steps by ONE semitone, its own interval");

        pitch.setValue (0.0f);
        pitch.wheel (1.0f, false);

        // range 24 -> max(1, 24/50) = 1 interval, so coarse and fine agree here.
        checkEqual (pitch.value(), 1.0f,
                    "and its coarse wheel is also one semitone, because max(1, 24/50) is 1 — the "
                    "multiplier comes from the parameter's range, not a constant in the knob");

        // The clamp is the parameter's too.
        pitch.setValue (12.0f);
        pitch.wheel (1.0f, false);
        checkEqual (pitch.value(), 12.0f, "and it clamps at the parameter's own maximum");
    }

    // ── reset goes to the parameter's default, not a construction value ─────
    //
    // The prototype's bug, asserted as absent: controls.js freezes `def` at
    // construction, so after a profile load its reset target is stale.
    {
        AttachedKnobRig rig { volId, Knob::Polarity::unipolar };
        const auto realDefault = rig.parameter.convertFrom0to1 (rig.parameter.getDefaultValue());

        // Simulate what a profile reload does: push a value in from outside.
        rig.setValue (7.0f);
        AttachedKnobRig::settle();

        rig.altClick();

        checkEqual (rig.value(), realDefault,
                    "reset after an external value push still lands on the parameter's default, "
                    "not on whatever the knob last saw — the prototype's frozen `def` bug, absent");
        check (std::abs (realDefault - 7.0f) > 0.5f,
               "and those two values genuinely differ, so the assertion above can fail");
    }

    // ── a drag is ONE host gesture ──────────────────────────────────────────
    {

        AttachedKnobRig rig { volId, Knob::Polarity::unipolar };
        GestureCounter counter;
        rig.parameter.addListener (&counter);

        rig.drag (40);
        AttachedKnobRig::settle();

        checkEqual (counter.begins, 1, "a drag opens exactly one host gesture");
        checkEqual (counter.ends, 1, "and closes exactly one — not a stream of them");

        rig.parameter.removeListener (&counter);
    }
}

void testTwentyStripKnobsAreLive()
{
    section ("twenty strip knobs, placed and attached");

    ForroBoxAudioProcessor processor;
    ForroBoxAudioProcessorEditor editor { processor };
    editor.setSize (ChassisLayout::kWidth, ChassisLayout::kHeight);

    // Every Knob anywhere under the editor, found by type rather than by a
    // count the chassis reports about itself.
    std::vector<Knob*> knobs;

    std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* k = dynamic_cast<Knob*> (child))
                knobs.push_back (k);

            collect (*child);
        }
    };
    collect (editor);

    const auto& layout = ChassisLayout::forBounds ({ 0, 0, ChassisLayout::kWidth,
                                                     ChassisLayout::kHeight });

    // Only the knobs in a STRIP. The header carries two more — the 54 px SWING
    // and CACHAÇA pair — so a bare count of every Knob under the editor stopped
    // meaning "the strip knobs" the moment 04-04 placed them.
    // In the EDITOR's space — the THIRD site 06-02 found reading parent-relative
    // bounds against a chassis-space rectangle. The side panel's MIX knob is a
    // child of the panel, and its local bounds land inside a strip's rectangle,
    // so it counted as a twenty-first strip knob.
    const auto inAnyStrip = [&layout, &editor] (juce::Component& c)
    {
        const auto centre = boundsIn (editor, c).getCentre();

        for (const auto& strip : layout.strips)
            if (strip.contains (centre))
                return true;

        return false;
    };

    knobs.erase (std::remove_if (knobs.begin(), knobs.end(),
                                 [&inAnyStrip] (Knob* k) { return ! inAnyStrip (*k); }),
                 knobs.end());

    checkEqual (static_cast<int> (knobs.size()), 20,
                "the editor carries twenty strip knobs (5 channels x VOL/PITCH/DECAY/PAN)");

    if (knobs.size() != 20)
        return;

    // ── each knob sits in the cell reserved for it ──────────────────────────
    for (int channel = 0; channel < ChassisLayout::kNumStrips; ++channel)
    {
        for (int slot = 0; slot < 4; ++slot)
        {
            auto* k = knobs[static_cast<size_t> (channel * 4 + slot)];
            const auto cell = layout.stripLayouts[static_cast<size_t> (channel)]
                                  .knobCells[static_cast<size_t> (slot)];
            const auto what = juce::String ("strip ") + juce::String (channel + 1)
                            + " knob " + juce::String (slot);

            check (cell.contains (k->getBounds().getCentre()),
                   what + " sits inside its reserved cell");
            checkEqual (k->getBounds().getCentreX(), cell.getCentreX(),
                        what + " is centred horizontally in its cell");
        }
    }

    // ── they are attached to the RIGHT parameters ───────────────────────────
    //
    // Driven through the parameter and observed on the knob, which proves the
    // binding rather than assuming it from construction order.
    for (int channel = 0; channel < ChassisLayout::kNumStrips; ++channel)
    {
        const auto& info = forrobox::ids::channelInfos[static_cast<size_t> (channel)];

        // ChassisLayout::knobSlots is the PRODUCTION table. A local copy here
        // would be keyed off the same order the knobs are built from, so
        // reordering it would move the knobs and this expectation together and
        // the test would agree with itself.
        for (int slot = 0; slot < 4; ++slot)
        {
            const auto* param = ChassisLayout::knobSlots[static_cast<size_t> (slot)].param;

            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
                processor.getAPVTS().getParameter (
                    forrobox::ids::channelParam (info.id, param)));

            auto* k = knobs[static_cast<size_t> (channel * 4 + slot)];
            const auto what = juce::String (info.id) + " " + juce::String (param);

            // Two distinct positions, so a knob stuck at one value fails.
            for (const auto target : { 0.25f, 0.8f })
            {
                parameter->setValueNotifyingHost (target);
                AttachedKnobRig::settle();

                check (std::abs (k->getProportion() - parameter->getValue()) < 0.02f,
                       what + "'s knob follows its own parameter to "
                           + juce::String (target, 2));
            }
        }
    }

    // ── PITCH and PAN are the bipolar pair ──────────────────────────────────
    //
    // Measured from the RENDER, not from a getter: a polarity flag stored and
    // never used would pass a getter check.
    {
        for (int channel = 0; channel < ChassisLayout::kNumStrips; ++channel)
        {
            const auto& info = forrobox::ids::channelInfos[static_cast<size_t> (channel)];

            for (const auto& knobSlot : ChassisLayout::knobSlots)
            {
                auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
                    processor.getAPVTS().getParameter (
                        forrobox::ids::channelParam (info.id, knobSlot.param)));

                // Park every knob at its centre. A BIPOLAR knob there draws no
                // value arc at all; a unipolar one has filled half its sweep.
                parameter->setValueNotifyingHost (0.5f);
            }
        }

        AttachedKnobRig::settle();
        const auto centred = renderComponent (editor, ChassisLayout::kWidth, ChassisLayout::kHeight);

        const auto scale = static_cast<double> (ChassisLayout::kStripKnobSize)
                         / forrobox::knob::kViewBox;
        const auto arcR = forrobox::knob::kArcRadius * scale;
        const auto panel = theme::colour (theme::Token::panel, theme::Mode::dark);

        for (int channel = 0; channel < ChassisLayout::kNumStrips; ++channel)
        {
            const auto bandFor = [&] (int slot)
            {
                auto* k = knobs[static_cast<size_t> (channel * 4 + slot)];
                const auto centre = editor.getLocalArea (k, k->dialBounds()).getCentre();

                return arcProfile (centred, centre,
                                   static_cast<float> (arcR - 2.5),
                                   static_cast<float> (arcR + 2.5), panel);
            };

            const auto label = juce::String ("strip ") + juce::String (channel + 1);

            // Slot 0 is VOL (unipolar), slot 1 is PITCH (bipolar). At 0.5 the
            // unipolar one has an arc on its left half and the bipolar one has
            // none, so the LEFT sweep is where they differ.
            const auto vol = bandFor (0).over (-135.0f, -20.0f);
            const auto pitch = bandFor (1).over (-135.0f, -20.0f);
            const auto pan = bandFor (3).over (-135.0f, -20.0f);

            check (vol > pitch * 1.15,
                   label + "'s VOL knob is unipolar and its PITCH knob is not — at the same "
                           "centred value only VOL has filled its left sweep ("
                       + juce::String (vol, 1) + " against " + juce::String (pitch, 1) + ")");
            check (vol > pan * 1.15,
                   label + "'s PAN knob is bipolar too");
        }
    }
}

void testKnobGestureLifecycle()
{
    section ("gestures are balanced, and the review's findings stay fixed");

    const auto volId = forrobox::ids::channelParam ("ganza", forrobox::ids::vol);

    /** Counts begin/end gesture pairs as a host would see them. */

    // ── Alt+click and right-click must not emit an UNBALANCED gesture end ────
    //
    // mouseDown's Alt branch returns before onGestureStart, but mouseUp used to
    // end a gesture regardless: JUCE asserts on an unbalanced
    // endChangeGesture, and a host sees a gesture-end with no begin.
    {
        AttachedKnobRig rig { volId, Knob::Polarity::unipolar };
        GestureCounter counter;
        rig.parameter.addListener (&counter);

        rig.altClick();
        AttachedKnobRig::settle();

        checkEqual (counter.begins, counter.ends,
                    "an Alt+click press AND release leaves begins and ends balanced ("
                        + juce::String (counter.begins) + " / " + juce::String (counter.ends) + ")");

        const auto afterAlt = counter.ends;

        rig.rightClick();
        AttachedKnobRig::settle();

        checkEqual (counter.ends, afterAlt,
                    "and a right-click emits no gesture at all — it never reaches the parameter");

        rig.parameter.removeListener (&counter);
    }

    // ── releasing Alt mid-press must not resume a drag from a stale anchor ───
    {
        AttachedKnobRig rig { volId, Knob::Polarity::unipolar };

        // A normal drag first, so an anchor exists to go stale.
        rig.setValue (50.0f);
        rig.drag (30);
        const auto afterFirstDrag = rig.value();

        // Now Alt+press (no gesture opens), then drag with Alt RELEASED.
        const auto centre = rig.knobComponent.getLocalBounds().getCentre();
        const auto altDown = mouseEventOn (rig.knobComponent, centre.toFloat(),
                                           juce::ModifierKeys (juce::ModifierKeys::altModifier));
        rig.knobComponent.mouseDown (altDown);
        AttachedKnobRig::settle();

        const auto afterReset = rig.value();

        // Same event without the modifier — as if Alt were released mid-press.
        // withNewPosition keeps the mouse-DOWN position, which is what makes
        // this a drag from the original anchor rather than a fresh press.
        const auto plain = mouseEventOn (rig.knobComponent, centre.toFloat())
                               .withNewPosition (centre.translated (0, -60).toFloat());
        rig.knobComponent.mouseDrag (plain);
        AttachedKnobRig::settle();

        checkEqual (rig.value(), afterReset,
                    "releasing Alt mid-press does not resume a drag — no gesture was open, so the "
                    "stale anchor from the earlier drag cannot jump the knob");
        check (std::abs (afterFirstDrag - afterReset) > 1.0f,
               "and those two values differ, so the assertion above can fail");
    }

    // ── double-click to type, driven as a GESTURE ───────────────────────────
    //
    // This called rig.knobComponent.onTextEntered(...) — the callback, not the
    // gesture. AC-6 states the rule it broke word for word: "driven through
    // juce::MouseEvent / KeyPress rather than by calling the value setter, so a
    // gesture wired to nothing fails". Knob::mouseDoubleClick, the TextEditor
    // it creates and the reject-keeps-it-open contract were exercised by
    // nothing — deleting mouseDoubleClick entirely left the suite green.
    {
        AttachedKnobRig rig { volId, Knob::Polarity::unipolar };
        rig.setValue (64.0f);

        /** The inline editor, found as a child — no new production API. */
        const auto editorOf = [&] () -> juce::TextEditor*
        {
            for (auto* child : rig.knobComponent.getChildren())
                if (auto* t = dynamic_cast<juce::TextEditor*> (child))
                    return t;

            return nullptr;
        };

        check (editorOf() == nullptr, "a knob has no inline editor until it is double-clicked");

        rig.knobComponent.mouseDoubleClick (
            rig.eventAt (rig.knobComponent.getLocalBounds().getCentre(), {}, 2));

        auto* editor = editorOf();

        check (editor != nullptr, "double-clicking one opens an inline editor");

        if (editor == nullptr)
            return;

        checkEqual (editor->getText(), rig.parameter.getCurrentValueAsText(),
                    "pre-filled with the parameter's own text");

        // Junk: rejected, and the editor STAYS OPEN to be corrected.
        editor->setText ("hello");
        editor->onReturnKey();
        AttachedKnobRig::settle();

        checkEqual (rig.value(), 64.0f, "typing junk changes nothing");
        check (editorOf() != nullptr,
               "and leaves the editor open to be corrected — the reject contract onTextEntered's "
               "bool return exists for");

        // A real number: applied, and the editor closes.
        editor->setText ("30");
        editor->onReturnKey();
        AttachedKnobRig::settle();

        checkEqual (rig.value(), 30.0f, "while a real number is applied");

        // closeInlineEditor defers through callAsync, so the child goes on the
        // next message drain rather than synchronously.
        AttachedKnobRig::settle();
        check (editorOf() == nullptr, "and the editor closes");
    }

    // ── the wheel honours a reversed (natural-scrolling) wheel ──────────────
    {
        AttachedKnobRig rig { volId, Knob::Polarity::unipolar };

        const auto wheelWith = [&] (float deltaY, bool reversed)
        {
            rig.setValue (50.0f);
            rig.wheel (deltaY, false, reversed);
            return rig.value();
        };

        check (wheelWith (1.0f, false) > 50.0f, "a normal wheel raises the value");
        check (wheelWith (1.0f, true) < 50.0f,
               "and the SAME delta lowers it when the wheel reports itself reversed — natural "
               "scrolling, which juce::Slider honours and this used to ignore");
    }

    // ── the interval is one definition, not two that disagree ───────────────
    {
        AttachedKnobRig rig { volId, Knob::Polarity::unipolar };

        // VOL: span 100, interval 1 -> max(1, 100/50/1) = 2.
        checkEqual (rig.attachment.coarseIntervals(), 2.0,
                    "VOL's coarse wheel is 2 of its intervals");
        checkEqual (rig.attachment.intervalSize(), 1.0, "and its interval is 1");

        // The product is the VALUE delta, and it must be the span/50 the
        // prototype specifies — not span^2/5000, which is what two disagreeing
        // definitions produced for a continuous parameter.
        checkEqual (rig.attachment.coarseIntervals() * rig.attachment.intervalSize(),
                    100.0 / 50.0,
                    "so one notch moves span/50 in value, which is controls.js's law");
    }
}

// ── 04-03 AC-1 / AC-2: the step pad ─────────────────────────────────────────

/** The row of `area` carrying the most ink against `reference`, in the area's
    own coordinates.

    An ARGMAX and deliberately not a centroid. AC-2 says the brightest point
    sits at 22% of the pad's height, and the pad's gradient is a symmetric
    falloff about a point only 5.7 px from the top of a 26 px box — so it is
    clipped hard above and runs to the edge below, and the centroid of what
    survives lands near the middle no matter where the origin is. The centroid
    would have reported the ellipse as centred and passed a circular one. */
int brightestRow (const juce::Image& image, juce::Rectangle<int> area, juce::Colour reference)
{
    auto best = -1.0;
    auto bestRow = 0;

    for (int y = 0; y < area.getHeight(); ++y)
    {
        auto row = 0.0;

        for (int x = 0; x < area.getWidth(); ++x)
            row += colourDistance (image.getPixelAt (area.getX() + x, area.getY() + y), reference);

        if (row > best)
        {
            best = row;
            bestRow = y;
        }
    }

    return bestRow;
}

/** Where a pixel sits on the line from `ground` toward some tint, normalised so
    that only the DIRECTION survives.

    "A low-velocity pad is dimmer than a full-velocity one in the SAME colour"
    is two claims, and the colour half is the one a naive check misses: fading
    toward a different hue is also dimmer. Normalising the delta by its largest
    channel drops the amount and keeps the hue, so the two velocities can be
    compared for colour alone. Not `Colour::getHue`, which is meaningless for a
    pixel this close to a neutral ground. */
std::array<float, 3> tintDirection (juce::Colour pixel, juce::Colour ground)
{
    const std::array<float, 3> delta { pixel.getFloatRed()   - ground.getFloatRed(),
                                       pixel.getFloatGreen() - ground.getFloatGreen(),
                                       pixel.getFloatBlue()  - ground.getFloatBlue() };

    const auto scale = juce::jmax (std::abs (delta[0]), std::abs (delta[1]), std::abs (delta[2]));

    if (scale < 1.0e-6f)
        return { 0.0f, 0.0f, 0.0f };

    return { delta[0] / scale, delta[1] / scale, delta[2] / scale };
}

/** The leftmost and rightmost columns of one row whose pixel is the lit FILL
    — within `tolerance` of the instrument colour itself.

    Not `inkWidth`, which is a brightness instrument. 04-02 recorded why that
    matters and this is the same trap: in the light theme the zabumba orange
    sits 0.036 from `--panel` in brightness, so inkWidth measured a fully lit
    pad as 0 px wide. It also cannot separate the fill from the 45% glow, and
    both of the claims below turn on exactly that difference.

    Returns an empty range when nothing on the row is the fill. */
juce::Range<int> litSpan (const juce::Image& image, int row, juce::Colour colour, double tolerance)
{
    auto left = -1, right = -1;

    for (int x = 0; x < image.getWidth(); ++x)
    {
        if (colourDistance (image.getPixelAt (x, row), colour) >= tolerance)
            continue;

        if (left < 0)
            left = x;

        right = x;
    }

    return left < 0 ? juce::Range<int>() : juce::Range<int> (left, right + 1);
}

float tintDistance (std::array<float, 3> a, std::array<float, 3> b)
{
    return juce::jmax (std::abs (a[0] - b[0]), std::abs (a[1] - b[1]), std::abs (a[2] - b[2]));
}

/** One step pad on a known ground, sized as the pad asks to be sized. */
struct StepPadRig
{
    StepPadRig (theme::Mode mode, juce::Colour instrumentColour, int padWidth = 40)
        : lnf (mode), stepPad (lnf, instrumentColour), colour (instrumentColour)
    {
        holder.ground = theme::colour (theme::Token::panel, mode);
        holder.addAndMakeVisible (stepPad);

        // The pad rect on the css 26/5 pitch, and the COMPONENT bounds asked of
        // StepPad — the glow needs room outside the rect, and a component's
        // paint is clipped to its own bounds.
        const auto bounds = StepPad::boundsForPadRect ({ kMargin, kMargin, padWidth, pad::kHeight });

        holder.setSize (bounds.getRight() + kMargin, bounds.getBottom() + kMargin);
        stepPad.setBounds (bounds);
    }

    static constexpr int kMargin = 6;

    juce::Image render() { return renderComponent (holder, holder.getWidth(), holder.getHeight()); }

    /** The PAD's rect in the holder's coordinates — not the component bounds,
        which carry the glow margin. */
    juce::Rectangle<int> area() const { return stepPad.getBounds().reduced (pad::kLitGlowRadius); }

    juce::MouseEvent eventAt (juce::Point<int> localPos) const
    {
        return mouseEventOn (const_cast<StepPad&> (stepPad), localPos.toFloat(), {}, 0);
    }

    ForroBoxLookAndFeel lnf;
    StepPad             stepPad;
    juce::Colour        colour;
    Ground              holder;
};

void testStepPadInstruments()
{
    section ("the step pad's instruments separate the cases they are asked to separate");

    // maxPixelDifference: identical renders are zero, and one changed pixel at
    // one part in 255 is not.
    {
        Swatch a, b;
        a.setSize (8, 8);
        b.setSize (8, 8);
        a.background = juce::Colour (0xff404040);
        b.background = juce::Colour (0xff404040);

        checkEqual (maxPixelDifference (renderComponent (a, 8, 8), renderComponent (b, 8, 8)), 0.0,
                    "maxPixelDifference is exactly zero for two identical renders");

        b.background = juce::Colour (0xff414040);

        const auto oneStep = maxPixelDifference (renderComponent (a, 8, 8), renderComponent (b, 8, 8));

        check (oneStep > 0.003 && oneStep < 0.005,
               "and resolves a single 1/255 step on one channel (" + juce::String (oneStep, 5) + ")");
    }

    // brightestRow: a band drawn at a known row is the row it reports, and it
    // reports it wherever in the box that row sits.
    {
        struct Band final : juce::Component
        {
            void paint (juce::Graphics& g) override
            {
                g.fillAll (juce::Colours::black);
                g.setColour (juce::Colours::white);
                g.fillRect (0, row, getWidth(), 1);
            }
            int row { 0 };
        };

        Band band;
        band.setSize (20, 26);

        for (const auto row : { 2, 6, 13, 23 })
        {
            band.row = row;
            checkEqual (brightestRow (renderComponent (band, 20, 26), { 0, 0, 20, 26 },
                                      juce::Colours::black),
                        row,
                        "brightestRow finds a band at row " + juce::String (row));
        }
    }

    // tintDirection: the same colour at two strengths has the same direction,
    // and a different colour at the same strength does not.
    {
        const auto ground = juce::Colour (0xff1a1a1a);
        const auto orange = juce::Colour (0xffff8c2a);
        const auto blue   = juce::Colour (0xff2a8cff);

        const auto weak   = tintDirection (ground.overlaidWith (orange.withAlpha (0.3f)), ground);
        const auto strong = tintDirection (ground.overlaidWith (orange.withAlpha (0.9f)), ground);
        const auto other  = tintDirection (ground.overlaidWith (blue.withAlpha (0.3f)), ground);

        check (tintDistance (weak, strong) < 0.02f,
               "tintDirection is unchanged by strength (" + juce::String (tintDistance (weak, strong), 4)
                   + ")");
        check (tintDistance (weak, other) > 0.5f,
               "and separates two different colours at the same strength ("
                   + juce::String (tintDistance (weak, other), 3) + ")");
    }
}

/** The thresholds every pad claim below is measured against.

    Each is set from the value the render actually produces — the measurement
    is in the comment beside it — rather than guessed. 04-02 shipped a guessed
    5.0 where the real number was 4.7, which is a threshold that happens to pass
    rather than one that means anything.

    The measurements were taken by forcing each constant to a value that cannot
    pass and reading what the failure reported, in both themes. */

/// Closest of the 15 state pairs: 0.0314 dark (beat vs hover, an 8/255 step),
/// 0.1843 light. maxPixelDifference resolves 1/255 = 0.0039, so this floor sits
/// an order of magnitude above the instrument's noise and well under the pair.
constexpr double kPairwiseFloor = 0.02;

/// The ellipse, 9 px from the origin: 0.1216 of the white survives across and
/// 0.0784 down — a ratio of 1.55. A CIRCULAR gradient would make it 1.00.
constexpr double kEllipseMargin = 1.3;

/// How close a pixel must be to the instrument colour to be the FILL rather
/// than the 45% glow. The glow over either panel is never nearer than 0.4.
constexpr double kFillTolerance = 0.25;

/// Velocity 60 against velocity 127, same pixel: the tint direction moves by
/// 0.0090 dark and 0.0041 light, which is quantisation, not a hue shift.
constexpr float kTintTolerance = 0.03f;

/// The ghost dot at the pad's centre, velocity 42 against 43: 0.0510 dark,
/// 0.0471 light.
constexpr double kDotFloor = 0.03;

/// A ghost pad's ground against an off pad's, away from the dot: 0.427 dark,
/// 0.412 light — the whole distance from the recessed grey to the lit colour.
constexpr double kGhostGroundFloor = 0.25;

void testStepPadStates (theme::Mode mode, const juce::String& modeName)
{
    section (fbtest::utf8 ("the step pad's six states are pairwise distinct — ") + modeName);

    const auto colour = theme::accent (theme::Accent::zabumba);
    const auto panel = theme::colour (theme::Token::panel, mode);

    // ── AC-1: six states, and every pair must differ ────────────────────────
    std::array<juce::Image, padStates.size()> renders;

    for (size_t i = 0; i < padStates.size(); ++i)
    {
        StepPadRig rig { mode, colour };
        rig.stepPad.setVelocity (padStates[i].velocity);
        rig.stepPad.setBeat (padStates[i].beat);

        if (padStates[i].hover)
            rig.stepPad.mouseEnter (rig.eventAt (rig.area().getCentre()));

        renders[i] = rig.render();
    }

    auto worstPair = 1.0;
    juce::String worstNames;

    for (size_t i = 0; i < padStates.size(); ++i)
    {
        for (size_t j = i + 1; j < padStates.size(); ++j)
        {
            const auto difference = maxPixelDifference (renders[i], renders[j]);

            if (difference < worstPair)
            {
                worstPair = difference;
                worstNames = juce::String (padStates[i].caption) + " vs " + padStates[j].caption;
            }
        }
    }

    check (worstPair > kPairwiseFloor,
           modeName + fbtest::utf8 (": every one of the 15 state pairs renders differently — closest is ")
               + worstNames + " at " + juce::String (worstPair, 4));

    // ── the recessed ground: two ROWS, not one row at another alpha ────────
    //
    // The four gradient alphas are cross-checked against the stylesheet by
    // verify-geometry.py, but 04-01's rule runs the other way too: a value
    // cross-check does not prove the value reaches a pixel. Nothing else here
    // would notice the light theme painting the DARK gradient — every pairwise
    // comparison is within one theme.
    //
    // What separates them is the direction of the ramp. Dark runs white 0.03 to
    // black 0.18, so its top is LIGHTER than its bottom; light runs black 0.10
    // to black 0.05, so its top is DARKER. One is the reverse of the other,
    // which no single gradient can satisfy in both themes.
    //
    // Brightness is the right instrument for exactly this one claim and the
    // wrong one almost everywhere else in this file: both overlays are neutral
    // greys over the panel, so there is no hue for it to miss, and the question
    // asked is signed — which end is lighter — which colourDistance cannot
    // answer at all.
    {
        StepPadRig rig { mode, colour };

        const auto image = rig.render();
        const auto area = rig.area();

        const auto top = pixelAt (image, area.getCentreX(), area.getY() + 3).getBrightness();
        const auto bottom = pixelAt (image, area.getCentreX(), area.getBottom() - 3).getBrightness();

        const auto topIsLighter = top > bottom;

        checkEqual (topIsLighter, mode == theme::Mode::dark,
                    modeName + ": the recessed gradient runs "
                        + juce::String (mode == theme::Mode::dark ? "light to dark" : "dark to light")
                        + ", which is this theme's OWN gradient and not the other's ("
                        + juce::String (top, 4) + " -> " + juce::String (bottom, 4) + ")");
    }

    // ── AC-2: the ellipse, its origin and the undistorted rectangle ─────────
    {
        StepPadRig rig { mode, colour };
        rig.stepPad.setVelocity (127);

        const auto image = rig.render();
        const auto area = rig.area();

        // Measured against the pure instrument colour, so what is left is the
        // WHITE the gradient adds — brightest at the origin and gone by 70%.
        // The sheen is one declared row of its own (`inset 0 1px 0`), so it is
        // excluded rather than allowed to win the argmax it does not describe.
        const auto litRows = area.withTrimmedTop (1);
        const auto brightest = 1 + brightestRow (image, litRows, colour);
        const auto expected = juce::roundToInt (pad::kLitOriginY * (float) area.getHeight());

        check (std::abs (brightest - expected) <= 2,
               modeName + ": the lit pad is brightest at row " + juce::String (brightest) + " of "
                   + juce::String (area.getHeight()) + ", where 22% of the height is "
                   + juce::String (expected));

        check (brightest < area.getHeight() / 2 - 2,
               modeName + ": and that is well ABOVE the pad's centre, which is what an offset "
                          "origin means (" + juce::String (brightest) + " < "
                   + juce::String (area.getHeight() / 2 - 2) + ")");

        // The ellipse itself: rx is 1.2 x width and ry is 1.0 x height, so at
        // the same pixel distance from the origin the horizontal falloff is
        // slower. A CIRCULAR gradient — the shape juce::ColourGradient gives
        // without the FillType transform — makes these two equal.
        const auto origin = area.getTopLeft()
                          + juce::Point<int> (juce::roundToInt (pad::kLitOriginX * (float) area.getWidth()),
                                              juce::roundToInt (pad::kLitOriginY * (float) area.getHeight()));

        constexpr int kProbe = 9;

        const auto across = colourDistance (image.getPixelAt (origin.x + kProbe, origin.y), colour);
        const auto down   = colourDistance (image.getPixelAt (origin.x, origin.y + kProbe), colour);

        check (across > down * kEllipseMargin,
               modeName + fbtest::utf8 (": the gradient is WIDER than it is tall — ") + juce::String (kProbe)
                   + " px across keeps " + juce::String (across, 4) + " of the white where "
                   + juce::String (kProbe) + " px down keeps " + juce::String (down, 4));

        // And the pad's own rounded rectangle is untouched by that stretch:
        // Graphics::addTransform would have scaled the SHAPE by 1.85 too.
        // The fill, not the glow: the glow is the instrument colour at 45%
        // over the panel and never comes near the fill's own colour.
        const auto span = litSpan (image, area.getCentreY(), colour, kFillTolerance);

        checkEqual (span.getStart(), area.getX(),
                    modeName + ": the lit rectangle still begins at the pad's own left edge");
        checkEqual (span.getEnd(), area.getRight(),
                    modeName + ": and still ends at its right edge, undistorted by the ellipse");
    }

    // ── AC-1: velocity is dimmer in the SAME colour ─────────────────────────
    {
        StepPadRig low { mode, colour };
        StepPadRig full { mode, colour };

        low.stepPad.setVelocity (60);
        full.stepPad.setVelocity (127);

        const auto probe = low.area().getCentre();
        const auto lowPixel = low.render().getPixelAt (probe.x, probe.y);
        const auto fullPixel = full.render().getPixelAt (probe.x, probe.y);

        check (colourDistance (fullPixel, panel) > colourDistance (lowPixel, panel),
               modeName + ": a velocity-60 pad is measurably dimmer than a velocity-127 one ("
                   + juce::String (colourDistance (lowPixel, panel), 3) + " vs "
                   + juce::String (colourDistance (fullPixel, panel), 3) + ")");

        const auto tint = tintDistance (tintDirection (lowPixel, panel),
                                        tintDirection (fullPixel, panel));

        check (tint < kTintTolerance,
               modeName + ": and it is dimmer in the SAME colour, not faded toward another ("
                   + juce::String (tint, 4) + ")");
    }

    // ── the ghost threshold, at the pixel ───────────────────────────────────
    //
    // The prototype's boundary: app.js adds `ghost` at v <= 42 and nothing at
    // 43, so one velocity apart must differ by a 3 px dot and by nothing else.
    // Asserted at the boundary rather than at 20-vs-100, which any wrong
    // threshold would also pass.
    {
        StepPadRig ghost { mode, colour };
        StepPadRig plain { mode, colour };

        ghost.stepPad.setVelocity (pad::kGhostVelocityMax);
        plain.stepPad.setVelocity (pad::kGhostVelocityMax + 1);

        const auto ghostImage = ghost.render();
        const auto plainImage = plain.render();
        const auto area = ghost.area();

        const auto centre = area.getCentre();

        const auto dotStep = colourDistance (ghostImage.getPixelAt (centre.x, centre.y),
                                            plainImage.getPixelAt (centre.x, centre.y));

        check (dotStep > kDotFloor,
               modeName + ": velocity " + juce::String (pad::kGhostVelocityMax)
                   + " carries the ghost dot and velocity "
                   + juce::String (pad::kGhostVelocityMax + 1) + " does not ("
                   + juce::String (dotStep, 4) + ")");

        // A ghost is a LIT pad plus a dot, not an unlit one — the prototype
        // adds `ghost` on top of `on` (app.js:374-379) and `.pad.ghost` only
        // appends the `::after` circle. PLANNING.md:451 says "renders as off";
        // the design reference wins, the standing rule for this project.
        StepPadRig off { mode, colour };
        const auto offImage = off.render();

        const auto ground = area.getTopLeft() + juce::Point<int> (2, area.getHeight() - 3);

        check (colourDistance (ghostImage.getPixelAt (ground.x, ground.y),
                               offImage.getPixelAt (ground.x, ground.y)) > kGhostGroundFloor,
               modeName + ": a ghost pad's GROUND is lit, not the recessed one ("
                   + juce::String (colourDistance (ghostImage.getPixelAt (ground.x, ground.y),
                                                   offImage.getPixelAt (ground.x, ground.y)), 3) + ")");
    }

    // ── the beat ring: --line-strong, and absent on a lit pad ───────────────
    {
        StepPadRig beat { mode, colour };
        StepPadRig plain { mode, colour };

        beat.stepPad.setBeat (true);

        const auto beatImage = beat.render();
        const auto plainImage = plain.render();
        const auto area = beat.area();

        // Sampled on the ring itself, mid-height, away from the corners.
        const auto probe = juce::Point<int> (area.getX(), area.getCentreY());
        const auto ringPixel = beatImage.getPixelAt (probe.x, probe.y);
        const auto plainPixel = plainImage.getPixelAt (probe.x, probe.y);

        const auto toStrong = colourDistance (ringPixel,
                                              plainPixel.overlaidWith (theme::colour (theme::Token::lineStrong, mode)));
        const auto toLine = colourDistance (ringPixel,
                                            plainPixel.overlaidWith (theme::colour (theme::Token::line, mode)));

        check (toStrong < toLine,
               modeName + ": the beat ring is --line-strong (css:632), not the --line of the "
                          "earlier css:481 rule that loses the cascade (" + juce::String (toStrong, 4)
                   + " vs " + juce::String (toLine, 4) + ")");

        // css:633 gives `.pad.on.beat` the lit shadow with NO ring at all, so a
        // lit beat pad is pixel-for-pixel a lit pad. Asserted so that a later
        // "fix" that draws the ring on lit pads fails rather than passing as an
        // improvement.
        StepPadRig litBeat { mode, colour };
        StepPadRig lit { mode, colour };

        litBeat.stepPad.setVelocity (127);
        litBeat.stepPad.setBeat (true);
        lit.stepPad.setVelocity (127);

        checkEqual (maxPixelDifference (litBeat.render(), lit.render()), 0.0,
                    modeName + ": and it is INVISIBLE on a lit pad, which is what css:633 says");
    }

    // ── press: the content scales, the bounds do not ────────────────────────
    {
        StepPadRig rig { mode, colour };
        rig.stepPad.setVelocity (127);

        const auto boundsWidth = rig.stepPad.getBounds().getWidth();
        const auto row = rig.area().getCentreY();

        const auto restingWidth = litSpan (rig.render(), row, colour, kFillTolerance).getLength();

        rig.stepPad.mouseDown (rig.eventAt (rig.area().getCentre()));

        const auto pressedWidth = litSpan (rig.render(), row, colour, kFillTolerance).getLength();

        check (pressedWidth < restingWidth,
               modeName + ": a pressed pad draws NARROWER than a resting one ("
                   + juce::String (pressedWidth) + " vs " + juce::String (restingWidth) + ")");

        checkEqual (rig.stepPad.getBounds().getWidth(), boundsWidth,
                    modeName + ": and its bounds are unchanged, so the row it sits in cannot reflow");
    }
}

// ── 04-03 AC-4: the fader, absolutely positioned ────────────────────────────

/** A fader bound to a REAL parameter on a real processor, in a holder, so what
    the tests drive is what the plugin runs.

    120 px wide, which PLANNING.md:335 gives the master fader — and, with the
    thumb's overhang added on each side, a track exactly 120 px across, so 25%
    and 75% land on whole pixels and the expected values are exact rather than
    a tolerance hiding a rounding law. */
struct AttachedFaderRig
{
    explicit AttachedFaderRig (const juce::String& parameterId)
        : parameter (*dynamic_cast<juce::RangedAudioParameter*> (
                         processor.getAPVTS().getParameter (parameterId))),
          faderComponent (lnf, juce::Colour (0xffe8650a)),
          attachment (parameter, faderComponent)
    {
        attachment.sendInitialUpdate();

        holder.ground = theme::colour (theme::Token::panel, theme::Mode::dark);
        holder.addAndMakeVisible (faderComponent);

        const auto bounds = Fader::boundsForBox ({ kMargin, kMargin, kBoxWidth, fader::kHeight });

        holder.setSize (bounds.getRight() + kMargin, bounds.getBottom() + kMargin);
        faderComponent.setBounds (bounds);
    }

    static constexpr int kBoxWidth = 120;
    static constexpr int kMargin = 10;

    float value() const { return parameter.convertFrom0to1 (parameter.getValue()); }

    void setValue (float denormalised)
    {
        parameter.setValueNotifyingHost (parameter.convertTo0to1 (denormalised));
        settle();
    }

    juce::MouseEvent eventAt (int localX, juce::ModifierKeys mods = {}) const
    {
        return mouseEventOn (const_cast<Fader&> (faderComponent),
                             { (float) localX, (float) faderComponent.getHeight() * 0.5f }, mods);
    }

    /** The x, in the fader's own coordinates, that is `proportion` along the
        track — derived from the track the component reports, never from a
        number retyped here. */
    int xForProportion (float proportion) const
    {
        const auto track = faderComponent.trackRect();

        return track.getX() + juce::roundToInt (proportion * (float) track.getWidth());
    }

    /** A press at one proportion, a drag to another, a release. Both ends of
        the drag go through juce::MouseEvent, which is the plan's requirement:
        calling proportionForX directly would test the arithmetic and not the
        control. */
    void pressDragRelease (float from, float to, juce::ModifierKeys mods = {})
    {
        const auto down = eventAt (xForProportion (from), mods);
        faderComponent.mouseDown (down);

        const auto moved = down.withNewPosition (
            juce::Point<float> ((float) xForProportion (to), down.position.y));

        faderComponent.mouseDrag (moved);
        faderComponent.mouseUp (moved);
        settle();
    }

    void clickAt (float proportion, juce::ModifierKeys mods = {})
    {
        const auto e = eventAt (xForProportion (proportion), mods);
        faderComponent.mouseDown (e);
        faderComponent.mouseUp (e);
        settle();
    }

    juce::Image render() { return renderComponent (holder, holder.getWidth(), holder.getHeight()); }

    ForroBoxAudioProcessor              processor;
    ForroBoxLookAndFeel                 lnf { theme::Mode::dark };
    juce::RangedAudioParameter&         parameter;
    Fader                               faderComponent;
    forrobox::ProportionAttachment<Fader> attachment;
    Ground                              holder;
};

void testFaderIsAbsolute()
{
    section ("the fader is absolute — a click jumps, and a drag is that click repeated");

    const auto volId = forrobox::ids::channelParam ("ganza", forrobox::ids::vol);

    // ── the box: padding, track, padding, plus room for the thumb ───────────
    {
        AttachedFaderRig rig { volId };

        const auto bounds = rig.faderComponent.getLocalBounds();
        const auto track = rig.faderComponent.trackRect();

        // NOT `bounds.getHeight() == fader::kHeight`, `track.getHeight() ==
        // kTrackHeight` or `track.getCentreY() == bounds.getCentreY()`. The rig
        // builds the component from `boundsForBox({..., kHeight})` and
        // boundsForBox never touches height, and trackRect IS
        // `withSizeKeepingCentre (…, kTrackHeight)` — so all three restated the
        // rig's own input and could not fail. What HAS content is that
        // boundsForBox and trackRect are inverses, which is the pair below.
        checkEqual (bounds.getWidth(), AttachedFaderRig::kBoxWidth + fader::kThumbSize,
                    "and the component reserves half a thumb at each end, because at proportion 0 "
                    "half the thumb hangs past the track and a Component's paint is clipped to its "
                    "own bounds");
        checkEqual (track.getWidth(), AttachedFaderRig::kBoxWidth,
                    "leaving the track itself exactly the css box's width");
    }

    // ── the value law: the pointer's x within the TRACK, clamped ────────────
    {
        AttachedFaderRig rig { volId };
        const auto track = rig.faderComponent.trackRect();

        checkEqual (rig.faderComponent.proportionForX (track.getX()), 0.0f,
                    "the track's left edge is proportion 0");
        checkEqual (rig.faderComponent.proportionForX (track.getRight()), 1.0f,
                    "its right edge is 1");
        checkEqual (rig.faderComponent.proportionForX (track.getCentreX()), 0.5f,
                    "and its centre is a half");

        checkEqual (rig.faderComponent.proportionForX (track.getX() - 40), 0.0f,
                    "a pointer left of the track clamps to 0 rather than running negative");
        checkEqual (rig.faderComponent.proportionForX (track.getRight() + 40), 1.0f,
                    "and right of it clamps to 1 — controls.js:237");
    }

    // ── a click JUMPS, from wherever the value happened to be ───────────────
    {
        AttachedFaderRig rig { volId };

        rig.setValue (90.0f);
        rig.clickAt (0.25f);

        checkEqual (rig.value(), 25.0f,
                    "a click at 25% of the track sets the parameter to 25% of its range, from 90 — "
                    "an absolute control, with no anchor and no accumulated delta");

        rig.clickAt (0.75f);
        checkEqual (rig.value(), 75.0f, "and a click at 75% sets 75%");

        rig.clickAt (0.0f);
        checkEqual (rig.value(), 0.0f, "the left end is the bottom of the range");

        rig.clickAt (1.0f);
        checkEqual (rig.value(), 100.0f, "and the right end is the top");
    }

    // ── a drag is the same computation, repeated ────────────────────────────
    {
        AttachedFaderRig rig { volId };

        rig.setValue (0.0f);
        rig.pressDragRelease (0.25f, 0.75f);

        checkEqual (rig.value(), 75.0f,
                    "a drag to 75% lands on 75%, wherever it began — the pointer's position IS the "
                    "value, so dragging out and back cannot drift");

        // The knob's anchored drag has to be proved to return to its start.
        // This one cannot do otherwise, and that is the point of the design.
        rig.pressDragRelease (0.75f, 0.10f);
        checkEqual (rig.value(), 10.0f, "and a drag the other way is the same law");
    }

    // ── exactly ONE host gesture per press ──────────────────────────────────
    {

        AttachedFaderRig rig { volId };
        GestureCounter counter;
        rig.parameter.addListener (&counter);

        rig.pressDragRelease (0.20f, 0.80f);

        checkEqual (counter.begins, 1,
                    "a press, a drag and a release open exactly one host gesture");
        checkEqual (counter.ends, 1, "and close exactly one");

        // Right-click belongs to the host's automation menu. It must emit
        // nothing at all — not a value, and not the unbalanced gesture end
        // 04-02's review found in the knob.
        const auto before = rig.value();

        rig.clickAt (0.5f, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier));

        checkEqual (counter.begins, 1, "a right-click opens no gesture");
        checkEqual (counter.ends, 1, "and closes none");
        checkEqual (rig.value(), before, "and changes no value — it is the host's");

        rig.parameter.removeListener (&counter);
    }

    // ── and no wheel, because controls.js gives it none ─────────────────────
    {
        AttachedFaderRig rig { volId };
        rig.setValue (50.0f);

        juce::MouseWheelDetails wheel {};
        wheel.deltaY = 1.0f;

        rig.faderComponent.mouseWheelMove (rig.eventAt (rig.xForProportion (0.5f)), wheel);
        settle();

        checkEqual (rig.value(), 50.0f,
                    "a wheel notch over the fader changes nothing — controls.js:231-247 wires no "
                    "wheel, and adding one because the knob has one would be a silent deviation");
    }
}

void testFaderPaintsItsValue()
{
    section ("the fader draws the value it is given, and holds none of its own");

    const auto volId = forrobox::ids::channelParam ("ganza", forrobox::ids::vol);

    AttachedFaderRig rig { volId };
    const auto track = rig.faderComponent.trackRect()
                     + rig.faderComponent.getBounds().getPosition();

    const auto panel = theme::colour (theme::Token::panel, theme::Mode::dark);
    const auto lineStrong = theme::colour (theme::Token::lineStrong, theme::Mode::dark);
    const auto fill = theme::saturated (juce::Colour (0xffe8650a), rig.lnf.accentIntensity(),
                                        fader::kSaturationFloor, fader::kSaturationRange);

    // The unfilled track is --line-strong over the panel; the filled part is the
    // instrument colour. Measured at the track's own centre row, one quarter and
    // three quarters along, so 25% has ink on the left and ground on the right.
    rig.setValue (25.0f);

    {
        const auto image = rig.render();
        const auto row = track.getCentreY();

        const auto inFill = image.getPixelAt (track.getX() + track.getWidth() / 8, row);
        const auto inTrack = image.getPixelAt (track.getX() + track.getWidth() * 7 / 8, row);

        check (colourDistance (inFill, fill) < 0.05,
               "at 25%, the left of the track is the instrument colour at the fader's saturation");
        check (colourDistance (inTrack, panel.overlaidWith (lineStrong)) < 0.05,
               "and the right of it is the bare --line-strong track");
    }

    // At zero there is no fill at all. A minimum fill width — the obvious
    // defence against a degenerate rounded rectangle — would make every
    // proportion below 3% render identically, and would show colour on a
    // parameter sitting at its minimum.
    {
        rig.setValue (0.0f);

        const auto image = rig.render();
        const auto row = track.getCentreY();
        check (litSpan (image, row, fill, 0.05).isEmpty(),
               "a fader at its minimum draws no fill at all, not a rounded stub");
    }

    // The thumb is centred ON the proportion point, and it MOVES with the
    // parameter — the fader holds no value of its own, so this is the whole
    // parameter -> component direction under test.
    const auto thumbCentre = [&] (float denormalised)
    {
        rig.setValue (denormalised);

        const auto image = rig.render();
        const auto row = track.getCentreY();

        // The thumb is --fg, which neither the track nor the fill is. litSpan
        // is the instrument for exactly this scan and is used three times in
        // this test's neighbours; writing it out again left the fader's two
        // AC-level claims on a private copy of its tolerance semantics.
        const auto span = litSpan (image, row, theme::colour (theme::Token::fg, theme::Mode::dark),
                                   0.05);

        return span.isEmpty() ? -1 : (span.getStart() + span.getEnd() - 1) / 2;
    };

    for (const auto proportion : { 0.0f, 0.25f, 0.5f, 1.0f })
    {
        const auto measured = thumbCentre (proportion * 100.0f);
        const auto expected = track.getX() + juce::roundToInt (proportion * (float) track.getWidth());

        check (std::abs (measured - expected) <= 1,
               "the thumb is centred on the proportion point at " + juce::String (proportion, 2)
                   + " (measured " + juce::String (measured) + ", expected "
                   + juce::String (expected) + ")");
    }
}

// ── 04-03 AC-4 / AC-5: the strip is finished ────────────────────────────────

/** Every component of one type anywhere under a component, in z-order. */
/** One stored velocity, read under the pattern lock.

    At file scope because three tests want it — it was written out as a local
    lambda twice, 800 lines apart.

    06-03's profile-load test does NOT use it, and that is deliberate: it reads
    256 slots per profile under one already-held handle, so calling this inside
    that scope would re-enter the lock and calling it outside would be 1024
    acquisitions, each release running `publishIfChanged`'s 256-byte compare.
    This docstring used to name that test as a future caller. /simplify. */
inline int storedVelocity (ForroBoxAudioProcessor& processor, int lane, int step)
{
    auto handle = processor.lockPatternState();

    return static_cast<int> (handle->lanes[static_cast<size_t> (lane)]
                                          [static_cast<size_t> (step)]);
}

/** How many of `parent`'s children of type T paint OVER `target`.

    Written out twice before this — 05-04 counted what buried the kit overlay
    (50 -> 0), 06-01 what buried the playhead across a rebuild (162 -> 0).

    TYPED, because "how many children are in front" is the wrong question: the
    sequencer's STEPS buttons sit in front of the playhead and always have, and
    they overlap nothing. What the playhead needs is that no STEP PAD is in
    front of it, which is what the bug actually was. /simplify. */
template <typename T = juce::Component>
inline int childrenInFrontOf (juce::Component& parent, juce::Component& target)
{
    const auto& children = parent.getChildren();
    const auto index = children.indexOf (&target);

    if (index < 0)
        return -1;

    auto over = 0;

    for (int i = index + 1; i < children.size(); ++i)
        if (dynamic_cast<T*> (children[i]) != nullptr)
            ++over;

    return over;
}

/** A control found under a root, WITH its bounds in that root's space.

    `Component::getBounds` is parent-relative, and 06-02 found FOUR sites
    filtering controls by it against chassis-space rectangles — correct only
    while every control was a direct child of the chassis. The side panel broke
    that, and each site was converted to `boundsIn` by hand.

    `boundsIn` is OPT-IN, and the collector already holds the root, so the
    discipline was enforced by a comment. This project's own enrolment gate
    exists because a comment is not a check: the space now travels with the
    pointer and a caller cannot reach a bound without reaching its space.

    `operator->` and `operator*` keep the call sites reading as they did. */
template <typename T>
struct Found
{
    T* control {};

    /** In the ROOT's coordinate space — the one the caller passed in. */
    juce::Rectangle<int> bounds {};

    T* operator->() const noexcept { return control; }
    T& operator*()  const noexcept { return *control; }

    // NO IMPLICIT `operator T*`. It was here so the scan-and-keep sites read as
    // they did — and that is exactly what it cost: every un-migrated site kept
    // compiling, so nothing forced a caller to notice it now had `.bounds`.
    // /simplify found four sites still calling `boundsIn` for a rectangle
    // already sitting in the struct, including one with the same space mismatch
    // this type exists to prevent. `.control` at the four scan sites buys the
    // compiler flagging the rest.
};

template <typename T>
std::vector<Found<T>> collectChildren (juce::Component& root)
{
    std::vector<Found<T>> found;

    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* typed = dynamic_cast<T*> (child))
                found.push_back ({ typed, boundsIn (root, *child) });

            walk (*child);
        }
    };

    walk (root);
    return found;
}


/** The header bar under an editor or chassis.

    Header tests compare a control's position against `headerBar.getLayout()`,
    which is in the BAR's coordinates — so they pass the bar as the root and the
    two sides are in one space by construction. Before 04-05 they compared
    chassis-space boxes against parent-relative bounds and agreed only because
    the header sits at the origin. */
forrobox::HeaderBar& headerBarOf (juce::Component& root)
{
    auto bars = collectChildren<forrobox::HeaderBar> (root);

    jassert (bars.size() == 1);
    return *bars.front();
}

/** A processor, a look-and-feel, a tooltip and an attached chassis.

    38 sites built these six lines by hand, against SEVEN existing
    single-control rigs (`KnobRig`, `ButtonRig`, `AttachedKnobRig`, `StepPadRig`,
    `AttachedFaderRig`, `ControlRig`, `AttachedBpmRig`) — the composite was the
    one with no rig and the most repetition. `/simplify` named it at 05-01,
    05-02, 05-03, 06-01 and 06-02 and it was deferred every time, because what
    it should expose kept changing. 06-05 settled the last two shapes it reaches
    through.

    DECLARATION ORDER IS THE POINT, not the line count. The chassis holds a
    `KnobAttachment` per knob and each deregisters from its parameter when
    destroyed, so the processor that OWNS those parameters must outlive the
    chassis. Declared the other way round the processor dies first and the
    attachments deregister from freed parameters — which Linux tolerates and
    MSVC crashes the whole suite on. That invariant was re-established by hand
    38 times and written down at exactly ONE of them (the reference-render
    loop). Here it is the member order, which no call site can get wrong.

    It does NOT do hierarchy-routed dispatch: `clickInside` and `hoverInside`
    are 06-05's and take any component, so they take `rig.chassis`. */
/** The processor, in a BASE so it cannot be reordered below the chassis.

    A base is destroyed AFTER every member, whatever order the members end up
    in — so the invariant is a property of the type rather than of a comment
    above a member list. An earlier version held it in member order under a
    `// ORDER IS LOAD-BEARING` note, in a plan whose own argument is that a
    comment is not a check: alphabetising the members, hoisting `lnf` so the
    ctor-init list reads in declaration order, or inserting a member above
    `processor` all compile, all pass on Linux, and all fail only on MSVC — the
    longest feedback loop here. A test cannot close that; the failure mode IS
    "Linux exit 0". /simplify. */
struct ChassisRigProcessor
{
    ForroBoxAudioProcessor processor;
};

struct ChassisRig : ChassisRigProcessor
{
    explicit ChassisRig (theme::Mode mode = theme::Mode::dark)
        : lnf (mode)
    {
        chassis.setBounds (0, 0, ChassisLayout::kWidth, ChassisLayout::kHeight);
        chassis.attachParameters (processor.getAPVTS(), &tooltip);
    }

    // NO `withState` OR `pollAll` VERB, and the reason is the one stated twenty
    // lines above about `renderChild`. 06-02 asked for a mutate-then-settle
    // verb citing "five scoped blocks" that set `dirty` or `activeProfile` and
    // refreshed by hand. Those blocks have since moved into standalone
    // `SidePanel` tests this rig cannot reach, and the ones left inside rig
    // tests refresh ONE view deliberately — so a four-call `pollAll` would make
    // them stop distinguishing. Exactly one site converted, and it REGRESSED:
    // a header-only refresh became four, five times per loop, to read one
    // `getSelectedIndex()`.
    //
    // A verb with one caller and a helper with none, in a struct that refuses
    // `renderChild` on 02-04's "a guarantee with no caller is not a guarantee",
    // is that rule stated and broken on one screen. If the rig should carry a
    // state verb, it belongs in the plan that converts the sites — not this
    // one. /simplify.

    // NO `renderChild` VERB. 06-02 asked for one — "rendering a CHILD rather
    // than the whole chassis" — but `renderComponent` already takes any
    // component and five sites already call it with a bar. A wrapper would have
    // been a second name for it, with no caller: 02-04's "a guarantee with no
    // caller is not a guarantee", and the duplication this plan exists to
    // delete. Measured at 06-06: 12 chassis renders, 5 child renders, and every
    // one of the 5 uses `renderComponent` directly.

    ForroBoxLookAndFeel lnf;
    ValueTooltip        tooltip { lnf };
    Chassis             chassis { lnf };
};

/** `collectChildren` reports bounds in the ROOT's space, and this proves it.

    SELF-TESTED, because it is a measurement instrument and 04-01's law is that
    every one of them is checked against a subject with a known answer. Without
    this the type change was unfalsifiable: every site converted at 06-06
    collects controls that are DIRECT children of the root they measure against,
    where parent-relative and root-space are the same rectangle — mutating the
    collector to return `getBounds()` left the whole suite green.

    A `ProfileButton` is the subject that can tell the difference: it lives
    inside `SidePanel`, which lives inside `Chassis`, so its parent-relative
    bounds are offset from its chassis-space bounds by the panel's own origin —
    and the panel sits at x=920, which is the offset 06-02's four sites were
    silently dropping. */
void testCollectChildrenReportsRootSpaceBounds()
{
    section ("collectChildren reports each control's bounds in the ROOT's space");

    ChassisRig rig;
    auto& chassis   = rig.chassis;

    const auto tiles = collectChildren<forrobox::ProfileButton> (chassis);

    checkEqual (static_cast<int> (tiles.size()),
                static_cast<int> (forrobox::allProfiles().size()),
                "the chassis carries one profile tile per regional groove");

    if (tiles.empty())
        return;

    const auto& first = tiles.front();

    // The SUBJECT has a known answer: the panel is not at the origin, so the
    // two spaces must differ. If they did not, this test could not fail.
    check (first.control->getBounds() != first.bounds,
           "a profile tile's parent-relative bounds differ from its chassis-space bounds — "
           "the panel is not at the chassis origin, which is what makes this measurable");

    check (first.bounds == boundsIn (chassis, *first.control),
           "and collectChildren's bounds ARE the chassis-space ones");

    check (chassis.getLayout().sidePanel.contains (first.bounds.getCentre()),
           "so a chassis-space rectangle contains it — the comparison 06-02 found four sites "
           "getting wrong by reading parent-relative bounds instead");

    check (! chassis.getLayout().sidePanel.contains (first.control->getBounds().getCentre()),
           "while the parent-relative centre falls OUTSIDE that rectangle, which is the bug "
           "this type makes unrepresentable");
}

void testStripIsFinished()
{
    section ("every reserved box is filled, and what is real drives a parameter");

    ForroBoxAudioProcessor processor;
    ForroBoxAudioProcessorEditor editor { processor };
    editor.setSize (ChassisLayout::kWidth, ChassisLayout::kHeight);

    const auto& layout = ChassisLayout::forBounds ({ 0, 0, ChassisLayout::kWidth,
                                                     ChassisLayout::kHeight });

    // Only what lives in a STRIP: the header carries seven more buttons, so an
    // unfiltered count stopped meaning "the strip's" once 04-04 placed them.
    //
    // IN THE EDITOR'S SPACE, through `boundsIn`. It used to read
    // `c->getBounds()`, which is PARENT-relative and was only ever chassis-space
    // because every Button happened to be a direct child of the chassis. 06-02's
    // side panel parents its `LOAD IR…` to itself, and that button's local
    // bounds land inside a strip's chassis-space rectangle — so it counted as a
    // sixth strip button and shifted every index after it. Exactly the bug
    // `SequencerGrid.h` records: "anything comparing a child's bounds against a
    // chassis-space rectangle repeats the bug that made two tests silently read
    // the footer's control as the header's".
    // TAKES THE CARRIED BOUNDS. The collector root and this root were both
    // `editor`, so it was recomputing, per element, the rectangle already
    // sitting in the struct — the payoff site the type was added for.
    // /simplify.
    const auto inAnyStrip = [&layout] (juce::Rectangle<int> bounds)
    {
        const auto centre = bounds.getCentre();

        for (const auto& strip : layout.strips)
            if (strip.contains (centre))
                return true;

        return false;
    };

    auto buttons = collectChildren<Button> (editor);
    auto faders = collectChildren<Fader> (editor);

    buttons.erase (std::remove_if (buttons.begin(), buttons.end(),
                                   [&] (auto b) { return ! inAnyStrip (b.bounds); }),
                   buttons.end());
    faders.erase (std::remove_if (faders.begin(), faders.end(),
                                  [&] (auto f) { return ! inAnyStrip (f.bounds); }),
                  faders.end());

    // Five strips x (LOAD + two arrows + M + S).
    checkEqual (static_cast<int> (buttons.size()), ChassisLayout::kNumStrips * 5,
                "the editor carries five buttons per strip — LOAD, two pattern arrows, M and S");
    checkEqual (static_cast<int> (faders.size()), ChassisLayout::kNumStrips,
                "and one ghost fader per strip");

    const auto image = renderComponent (editor, ChassisLayout::kWidth, ChassisLayout::kHeight);

    // ── AC-5: no box this plan owns is still empty ──────────────────────────
    //
    // Measured against the strip's own ground, so "filled" means ink appeared
    // where the layout reserved room — not that a component exists somewhere.
    // The anchor strip's ground is tinted, so each strip is measured against
    // the colour it was actually painted on.
    for (int channel = 0; channel < ChassisLayout::kNumStrips; ++channel)
    {
        const auto& interior = layout.stripLayouts[static_cast<size_t> (channel)];
        const auto label = juce::String ("strip ") + juce::String (channel + 1);

        const auto panel = theme::colour (theme::Token::panel, theme::Mode::dark);
        const auto ground = channel == 0
                          ? theme::mix (panel, theme::accent (theme::Accent::zabumba),
                                        ChassisLayout::kAnchorAccentWeight)
                          : panel;

        const std::array<std::pair<const char*, juce::Rectangle<int>>, 5> filled {{
            { "sampleSlot",    interior.sampleSlot },
            { "patternCycler", interior.patternCycler },
            { "muteSolo",      interior.muteSolo },
            { "ghostLabel",    interior.ghostLabel },
            { "ghostFader",    interior.ghostFader },
        }};

        for (const auto& [name, box] : filled)
            check (contrastMass (image, box, ground) > 0.0,
                   label + "'s " + name + " box carries content ("
                       + juce::String (contrastMass (image, box, ground), 1) + ")");

        // The bateria row, and ONLY there — an empty box is meaningful.
        if (static_cast<theme::Accent> (channel) == theme::Accent::bateria)
            check (contrastMass (image, interior.subDots, ground) > 0.0,
                   label + "'s sub-dots row carries its four circles and label");
        else
            check (interior.subDots.isEmpty(),
                   label + " reserves no sub-dots box at all, so there is nothing to fill");

        // 04-02 asserted this box was STILL EMPTY, "Phase 5's box, not this
        // plan's", so that the day it was drawn this test would be what named
        // the plan that drew it. 05-02 is that plan, and this is that day: the
        // activity meter's well is painted here even with nothing to show, the
        // way the reserved pattern cycler and mute row are.
        check (contrastMass (image, interior.hitVisualiser, ground) > 0.0,
               label + "'s hit visualiser carries its recessed well (05-02's activity meter)");

        // And the LED beside the index, which 04-02 never reserved a box for at
        // all — it sits INSIDE the head row rather than in the interior stack,
        // so the stack's tiling assertion could not have missed it.
        check (! interior.trigLed.isEmpty(),
               label + " reserves a box for its trigger LED");
        check (interior.headRow.contains (interior.trigLed),
               label + "'s LED is inside the head row it belongs to");
    }

    // ── nothing is drawn in the header's left padding gutter ───────────────
    //
    // The bare-against-populated comparison below cannot see this one: a
    // non-bateria strip's subDots rect is a default-constructed Rectangle, so
    // {0,0,0,0} at the CHASSIS origin, and paintStrip runs for both chassis —
    // the stray label lands in each render identically and the difference is
    // zero. A control removing the guard proved exactly that.
    //
    // What separates them is SHAPE. paintHeader draws a vertical gradient, a
    // highlight row and a border row, so a header row is uniform ACROSS its
    // width wherever nothing is placed. Text is not.
    //
    // Scoped to the header's `padding: 0 16px` GUTTER, which no cluster
    // reaches — it used to be the whole header, and 04-04 populating it would
    // otherwise have retired the check rather than kept it. The stray subDots
    // label lands at the chassis origin, which is inside that gutter.
    {
        auto worstRow = 0.0;
        auto worstY = 0;

        const auto gutter = layout.header.withWidth (ChassisLayout::kHeaderPadX);

        for (int y = gutter.getY(); y < gutter.getBottom(); ++y)
        {
            const auto first = image.getPixelAt (gutter.getX(), y);
            auto spread = 0.0;

            for (int x = gutter.getX(); x < gutter.getRight(); ++x)
                spread = juce::jmax (spread, colourDistance (image.getPixelAt (x, y), first));

            if (spread > worstRow)
            {
                worstRow = spread;
                worstY = y;
            }
        }

        check (worstRow < 0.01,
               "every row of the header's padding gutter is uniform across its width — the "
               "header's own gradient is vertical, so anything that varies horizontally in a "
               "region no cluster reaches was drawn by accident (row " + juce::String (worstY)
                   + " spreads " + juce::String (worstRow, 4) + ")");
    }

    // ── nothing is drawn OUTSIDE a strip ───────────────────────────────────
    //
    // A non-bateria strip's subDots rect is a default-constructed Rectangle,
    // which is {0,0,0,0} at the CHASSIS's origin — so painting it unguarded
    // does not draw nothing, it draws the sub-dot label over the header. The
    // guard existed; a control removing it changed no check, because every
    // assertion here looked inside the strips.
    //
    // The header is 04-04's and is empty today, which makes it the cleanest
    // possible detector: any ink at all in it is something that escaped.
    // The SIDE PANEL is Phase 6's. Attaching parameters must not change a single
    // pixel of it.
    //
    // Two CHASSIS, one bare and one populated — not the editor against a bare
    // chassis. The first version compared those and found a difference at
    // (1199, 762): the editor carries a ValueTooltip the chassis does not, so
    // the comparison was measuring the wrong variable. contrastMass against
    // the plain token is no good either: it scores the header's own gradient
    // and highlight as ink.
    {
        ForroBoxLookAndFeel bareLnf { theme::Mode::dark };
        Chassis bare { bareLnf };
        bare.setBounds (0, 0, ChassisLayout::kWidth, ChassisLayout::kHeight);

        ForroBoxAudioProcessor ownProcessor;
        ForroBoxLookAndFeel populatedLnf { theme::Mode::dark };
        ValueTooltip populatedTooltip { populatedLnf };
        Chassis populated { populatedLnf };

        populated.setBounds (0, 0, ChassisLayout::kWidth, ChassisLayout::kHeight);
        populated.attachParameters (ownProcessor.getAPVTS(), &populatedTooltip);

        const auto bareImage = renderComponent (bare, ChassisLayout::kWidth,
                                                ChassisLayout::kHeight);
        const auto populatedImage = renderComponent (populated, ChassisLayout::kWidth,
                                                     ChassisLayout::kHeight);

        // INVERTED at 06-02, which is the plan the row above it anticipated:
        // "Phase 6 will retire the last row". 04-04 filled the header, 04-05 the
        // footer, 05-01 the sequencer and 06-02 the side panel — so there is no
        // region left that attaching parameters must NOT change, and every one
        // of them now must.
        //
        // Kept as an assertion rather than deleted, because the claim is still
        // worth making and is the rule 04-01 learned the hard way: a region that
        // looks the same populated as bare is a region whose controls were never
        // placed, and the six reference PNGs handed to that checkpoint showed
        // exactly that.
        const std::array<std::pair<const char*, juce::Rectangle<int>>, 4> populatedRegions {{
            { "header",       layout.header },
            { "footer",       layout.footer },
            { "sequencer",    layout.sequencer },
            { "side panel",   layout.sidePanel },
        }};

        // One BitmapData per image, not a getPixelAt per pixel: each of those
        // constructs its own BitmapData, and these four regions are ~498 k
        // pixels x 2 images. Measured by /simplify at ~10 ms, most of this
        // test. The claim is `worst == 0`, so a row compare answers it exactly.
        const juce::Image::BitmapData bareBits { bareImage, juce::Image::BitmapData::readOnly };
        const juce::Image::BitmapData populatedBits { populatedImage,
                                                      juce::Image::BitmapData::readOnly };

        for (const auto& [name, region] : populatedRegions)
        {
            auto identical = true;

            for (int y = region.getY(); y < region.getBottom() && identical; ++y)
                identical = std::memcmp (bareBits.getLinePointer (y)
                                             + region.getX() * bareBits.pixelStride,
                                         populatedBits.getLinePointer (y)
                                             + region.getX() * populatedBits.pixelStride,
                                         static_cast<size_t> (region.getWidth()
                                                              * bareBits.pixelStride)) == 0;

            check (! identical,
                   juce::String ("attaching parameters CHANGES the ") + name
                       + " — every region carries controls now, and one that renders identically "
                         "bare and populated is one whose controls were never placed");
        }
    }

    // ── the labels are the CHARACTERS they were meant to be ────────────────
    //
    // The pattern arrows are U+2039 and U+203A, one character each. Through
    // juce::String's const char* constructor they arrived as three Latin-1
    // characters apiece and rendered as "a<EUR>1/2" on the reference sheet —
    // with all 2053 checks green, because every one of them measured that there
    // was ink rather than which ink. A length is the cheapest thing that can
    // tell the difference.
    for (int channel = 0; channel < ChassisLayout::kNumStrips; ++channel)
    {
        const auto label = juce::String ("strip ") + juce::String (channel + 1);

        for (const auto slot : { 1, 2 })
        {
            const auto& arrow = *buttons[static_cast<size_t> (channel * 5 + slot)];

            checkEqual (arrow.getText().length(), 1,
                        label + "'s pattern arrow is ONE character, not a mis-decoded literal ("
                            + arrow.getText() + ")");
        }

        checkEqual (buttons[static_cast<size_t> (channel * 5)]->getText(), juce::String ("LOAD"),
                    label + "'s LOAD button says LOAD");
    }

    // ── AC-5: every placed child FITS the box reserved for it ───────────────
    //
    // The assertion the pattern-row correction actually needs. Comparing
    // patternCycler.getHeight() against Button::kArrowHeight cannot fail —
    // kPatternRowHeight is DEFINED as the max of that and the screen, so it is
    // the shape verify-geometry.py's own header calls structurally incapable of
    // failing, and a jmin would have passed it. Containment compares two things
    // derived differently: the box from the stack, the child from its own
    // preferred size. A 20 px box does not contain a 26 px arrow.
    {
        const auto& firstStrip = layout.stripLayouts[0];

        struct Placement { const char* name; juce::Rectangle<int> child, box; };

        // The FADER is here too. It was missing, which left the one control
        // whose box (kFaderHeight) and size (fader::kHeight) were two separate
        // constants as the one control this check skipped. They are one
        // constant now, and it is in the list anyway.
        const std::array<Placement, 5> placed {{
            { "LOAD in sampleSlot",             buttons[0]->getBounds(), firstStrip.sampleSlot },
            { "the prev arrow in patternCycler", buttons[1]->getBounds(), firstStrip.patternCycler },
            { "M in muteSolo",                  buttons[3]->getBounds(), firstStrip.muteSolo },
            { "S in muteSolo",                  buttons[4]->getBounds(), firstStrip.muteSolo },
            // The fader's COMPONENT bounds carry the thumb overhang on purpose,
            // so what must fit the reserved box is its track's own box.
            { "the ghost fader in ghostFader",
              faders[0]->getBounds().reduced (fader::kThumbOverhang, 0), firstStrip.ghostFader },
        }};

        for (const auto& [name, child, box] : placed)
            check (box.contains (child),
                   juce::String ("strip 1: ") + name + " fits the box reserved for it ("
                       + child.toString() + " in " + box.toString() + ")");

        checkEqual (ChassisLayout::kPatternScreenHeight, 20,
                    "the pattern screen itself is still the 20 px css:329-333 declares, so the row "
                    "grew because of its OTHER child");
    }

    // ── a CLIPPED repaint draws what a full one would ───────────────────────
    //
    // Chassis::paint now skips regions and strips outside g.getClipBounds(),
    // which is what makes the ghost readout's scoped repaint worth scoping:
    // without it a 161x10 repaint laid out all 31 of the chassis's glyph
    // arrangements anyway, because text layout happens before any clipped
    // drawing rejects it. An optimisation that changes the picture is a bug, so
    // this is its control — and it fails if a region is skipped when it should
    // not be, which is the only way the skipping can be wrong.
    {
        const auto ghostLabel = layout.stripLayouts[0].ghostLabel;

        juce::Image clipped (juce::Image::ARGB, ChassisLayout::kWidth, ChassisLayout::kHeight, true);
        {
            juce::Graphics g (clipped);
            g.reduceClipRegion (ghostLabel);
            editor.paintEntireComponent (g, true);
        }

        auto worst = 0.0;

        for (int y = ghostLabel.getY(); y < ghostLabel.getBottom(); ++y)
            for (int x = ghostLabel.getX(); x < ghostLabel.getRight(); ++x)
                worst = juce::jmax (worst, colourDistance (clipped.getPixelAt (x, y),
                                                           image.getPixelAt (x, y)));

        checkEqual (worst, 0.0,
                    "a repaint clipped to one ghost readout draws that rect exactly as a full "
                    "repaint does");
    }

    // ── a second attachParameters call must not touch freed memory ──────────
    //
    // `controls = {}` assigns members in declaration order, so it frees each
    // Button and Fader while its attachment still holds a reference — and the
    // attachment's destructor then writes `onClick`/`onDragTo` into that
    // memory. Twenty writes per call, on the one path the idempotency comment
    // exists to support, and no test had ever taken it. Found by /code-review.
    //
    // Without a sanitiser this is a crash test, not a detector; with one it is
    // the whole finding. Either way the path is now exercised.
    {
        ForroBoxAudioProcessor ownProcessor;
        ForroBoxLookAndFeel ownLnf { theme::Mode::dark };
        ValueTooltip ownTooltip { ownLnf };
        Chassis chassis { ownLnf };

        chassis.setBounds (0, 0, ChassisLayout::kWidth, ChassisLayout::kHeight);

        chassis.attachParameters (ownProcessor.getAPVTS(), &ownTooltip);
        chassis.attachParameters (ownProcessor.getAPVTS(), &ownTooltip);

        // Every Button under the chassis: five per strip, the header's seven,
        // the footer's LIMITER, the sequencer's two STEPS buttons, the kit
        // overlay's close, and the side panel's LOAD IR…. What this proves is
        // that a second call REPLACES rather than appends, so the total is what
        // matters, not which region each came from — and that now covers SIX
        // owners.
        //
        // This check has caught a new control in FOUR consecutive plans: the
        // STEPS pair at 05-03, the overlay's close at 05-04 and LOAD IR… at
        // 06-02. An owner that appended instead of replacing would show here as
        // a doubled count.
        checkEqual (static_cast<int> (collectChildren<Button> (chassis).size()),
                    ChassisLayout::kNumStrips * 5 + 7 + 1
                        + static_cast<int> (forrobox::ids::stepWindows.size())
                        + 1
                        + 1,
                    "attaching twice leaves ONE set of controls, not two stacked invisibly");
        checkEqual (static_cast<int> (collectChildren<Fader> (chassis).size()),
                    ChassisLayout::kNumStrips + 1,
                    "one ghost fader per strip, plus the footer's MASTER");
    }
}

void testMuteSoloAndGhostDriveParameters()
{
    section ("MUTE, SOLO and GHOST PROB drive real parameters");


    const auto clickCentreOf = [] (juce::Component& c, juce::ModifierKeys mods = {})
    {
        const auto e = mouseEventOn (c, c.getLocalBounds().getCentre().toFloat(), mods);
        c.mouseDown (e);
        c.mouseUp (e);
    };

    ForroBoxAudioProcessor processor;
    ForroBoxAudioProcessorEditor editor { processor };
    editor.setSize (ChassisLayout::kWidth, ChassisLayout::kHeight);

    const auto chassisLayout = ChassisLayout::forBounds ({ 0, 0, ChassisLayout::kWidth,
                                                          ChassisLayout::kHeight });

    // Filtered to STRIP 1's own buttons, not indexed out of every Button under
    // the editor. That worked only because attachParameters happens to add the
    // strips before the header, which is an ordering nothing states — and the
    // header added seven more the moment 04-04 placed it.
    auto buttons = collectChildren<Button> (editor);
    auto faders = collectChildren<Fader> (editor);

    const auto firstStrip = chassisLayout.strips[0];

    // In the EDITOR's space — the second site that read parent-relative bounds
    // against a chassis-space rectangle, and the second the side panel's own
    // child broke. See the sibling comment in the strip-control test.
    // The carried bounds, for `inAnyStrip`'s reason.
    const auto inFirstStrip = [&firstStrip] (juce::Rectangle<int> bounds)
    {
        return firstStrip.contains (bounds.getCentre());
    };

    buttons.erase (std::remove_if (buttons.begin(), buttons.end(),
                                   [&] (auto b) { return ! inFirstStrip (b.bounds); }),
                   buttons.end());
    faders.erase (std::remove_if (faders.begin(), faders.end(),
                                  [&] (auto f) { return ! inFirstStrip (f.bounds); }),
                  faders.end());

    if (buttons.size() != 5 || faders.empty())
    {
        check (false, "strip 1 did not carry the five buttons and one fader these assertions need");
        return;
    }

    // Strip 1 is zabumba; its five buttons are LOAD, prev, next, M, S in the
    // order attachParameters adds them.
    auto& load = *buttons[0];
    auto& patternPrev = *buttons[1];
    auto& mute = *buttons[3];
    auto& solo = *buttons[4];
    auto& ghostFader = *faders[0];

    auto& apvts = processor.getAPVTS();
    auto* muteParameter = apvts.getParameter (forrobox::ids::channelParam ("zabumba",
                                                                          forrobox::ids::mute));
    auto* soloParameter = apvts.getParameter (forrobox::ids::channelParam ("zabumba",
                                                                          forrobox::ids::solo));
    auto* ghostParameter = dynamic_cast<juce::RangedAudioParameter*> (
                               apvts.getParameter (forrobox::ids::channelParam (
                                   "zabumba", forrobox::ids::ghost)));

    check (muteParameter != nullptr && soloParameter != nullptr && ghostParameter != nullptr,
           "the three parameters this strip drives exist");

    if (muteParameter == nullptr || soloParameter == nullptr || ghostParameter == nullptr)
        return;

    // ── MUTE ────────────────────────────────────────────────────────────────
    {
        GestureCounter counter;
        muteParameter->addListener (&counter);

        const auto before = muteParameter->getValue();

        clickCentreOf (mute);
        settle();

        check (! juce::approximatelyEqual (muteParameter->getValue(), before),
               "a click on M toggles the mute parameter");
        checkEqual (counter.begins, 1, "and the host sees exactly one gesture begin");
        checkEqual (counter.ends, 1, "and exactly one end");

        check (mute.isOn() == (muteParameter->getValue() > 0.5f),
               "the button's lit state is the PARAMETER's, not a bool it kept for itself");

        clickCentreOf (mute);
        settle();

        checkEqual (muteParameter->getValue(), before, "and a second click toggles it back");

        muteParameter->removeListener (&counter);
    }

    // ── SOLO, on its own parameter ──────────────────────────────────────────
    {
        const auto muteBefore = muteParameter->getValue();

        clickCentreOf (solo);
        settle();

        check (solo.isOn(), "a click on S lights S");
        check (! mute.isOn(), "and leaves M alone — two buttons, two parameters");
        checkEqual (muteParameter->getValue(), muteBefore, "and does not touch mute's value");

        clickCentreOf (solo);
        settle();
    }

    // ── M and S light their OWN colours, on the strip that built them ──────
    //
    // testButtonFamily proves the variant paints --danger and --c-pandeiro;
    // nothing proved the STRIP handed S the solo OnStyle. Constructing both
    // with OnStyle::mute leaves every parameter assertion above green and ships
    // two red buttons.
    {
        muteParameter->setValueNotifyingHost (1.0f);
        soloParameter->setValueNotifyingHost (1.0f);
        settle();

        const auto image = renderComponent (editor, ChassisLayout::kWidth, ChassisLayout::kHeight);

        const auto sampleOf = [&] (const Button& b)
        {
            const auto area = b.getBounds() + b.getParentComponent()->getPosition();
            return image.getPixelAt (area.getCentreX(), area.getY() + 2);
        };

        check (colourDistance (sampleOf (mute), theme::colour (theme::Token::danger,
                                                               theme::Mode::dark)) < 0.05,
               "a lit M on the strip is --danger (css:343)");
        check (colourDistance (sampleOf (solo), theme::accent (theme::Accent::pandeiro)) < 0.05,
               "and a lit S is --c-pandeiro (css:344), not a second copy of M");

        muteParameter->setValueNotifyingHost (0.0f);
        soloParameter->setValueNotifyingHost (0.0f);
        settle();
    }

    // ── a change from OUTSIDE repaints all three, without writing back ──────
    {
        GestureCounter counter;
        muteParameter->addListener (&counter);

        muteParameter->setValueNotifyingHost (1.0f);
        settle();

        check (mute.isOn(), "a mute set from outside lights the button");
        checkEqual (counter.begins, 0,
                    "and opens NO gesture — a repaint must never become a parameter change, or "
                    "host automation would fight itself");

        muteParameter->setValueNotifyingHost (0.0f);
        settle();

        check (! mute.isOn(), "and clearing it from outside unlights it");

        muteParameter->removeListener (&counter);
    }

    // ── the ghost fader, and the readout beside it ──────────────────────────
    {
        ghostParameter->setValueNotifyingHost (ghostParameter->convertTo0to1 (30.0f));
        settle();

        check (std::abs (ghostFader.getProportion() - 0.30f) < 0.01f,
               "a ghost value set from outside moves the fader (proportion "
                   + juce::String (ghostFader.getProportion(), 3) + ")");

        // The readout is painted by the chassis from what the fader's
        // onProportionChanged wrote, so finding it means the one attachment
        // reached both views.
        const auto image = renderComponent (editor, ChassisLayout::kWidth, ChassisLayout::kHeight);
        const auto& interior = chassisLayout.stripLayouts[0];

        const auto ground = theme::mix (theme::colour (theme::Token::panel, theme::Mode::dark),
                                        theme::accent (theme::Accent::zabumba),
                                        ChassisLayout::kAnchorAccentWeight);

        const auto atThirty = contrastMass (image, interior.ghostLabel, ground);

        ghostParameter->setValueNotifyingHost (ghostParameter->convertTo0to1 (100.0f));
        settle();

        const auto atHundred = contrastMass (
            renderComponent (editor, ChassisLayout::kWidth, ChassisLayout::kHeight),
            interior.ghostLabel, ground);

        check (std::abs (atHundred - atThirty) > 1.0,
               "and the NN% readout beside it CHANGES with the parameter — one attachment reaching "
               "both, not a second listener that could disagree (" + juce::String (atThirty, 1)
                   + " -> " + juce::String (atHundred, 1) + ")");
    }

    // ── a restored GHOST of 0% still shows its readout ──────────────────────
    //
    // sendInitialUpdate fires onProportionChanged only when the proportion
    // CHANGES, and a Fader starts at 0 — so a project saved with GHOST at 0%
    // reopened to one strip captioned "GHOST PROB" with no percentage beside
    // it, indistinguishable from the unwired state. Found by /code-review.
    //
    // Built fresh with the parameter already at 0, because that is the order a
    // reopened project arrives in: state first, editor second.
    {
        ForroBoxAudioProcessor zeroProcessor;
        auto* zeroGhost = zeroProcessor.getAPVTS().getParameter (
            forrobox::ids::channelParam ("zabumba", forrobox::ids::ghost));

        zeroGhost->setValueNotifyingHost (0.0f);

        ForroBoxLookAndFeel zeroLnf { theme::Mode::dark };
        ValueTooltip zeroTooltip { zeroLnf };
        Chassis zeroChassis { zeroLnf };

        zeroChassis.setBounds (0, 0, ChassisLayout::kWidth, ChassisLayout::kHeight);
        zeroChassis.attachParameters (zeroProcessor.getAPVTS(), &zeroTooltip);
        settle();

        const auto zeroImage = renderComponent (zeroChassis, ChassisLayout::kWidth,
                                                ChassisLayout::kHeight);

        const auto zeroLayout = ChassisLayout::forBounds ({ 0, 0, ChassisLayout::kWidth,
                                                            ChassisLayout::kHeight });
        const auto zeroGround = theme::mix (theme::colour (theme::Token::panel, theme::Mode::dark),
                                            theme::accent (theme::Accent::zabumba),
                                            ChassisLayout::kAnchorAccentWeight);

        // The caption alone, so the readout's own half of the row is measured
        // rather than "GHOST PROB" being mistaken for a value.
        const auto readout = zeroLayout.stripLayouts[0].ghostLabel;
        const auto rightHalf = readout.withTrimmedLeft (readout.getWidth() / 2);

        check (contrastMass (zeroImage, rightHalf, zeroGround) > 0.0,
               "a GHOST restored at 0% still paints its NN% readout — the initial update fires the "
               "callback only on a CHANGE, and a fader starts at 0");
    }

    // ── the stubs are honest stubs ──────────────────────────────────────────
    {
        const auto stateBefore = [&]
        {
            juce::MemoryBlock block;
            processor.getStateInformation (block);
            return block;
        };

        const auto before = stateBefore();

        clickCentreOf (load);
        clickCentreOf (patternPrev);
        settle();

        check (stateBefore() == before,
               "clicking LOAD and a pattern arrow changes NO parameter state — they are stubs, and "
               "PLANNING.md lists both as post-v0.1");

        // But they are visibly alive: hover lifts them, which is how a reviewer
        // can tell a stub from a dead control by looking.
        const auto restingInk = [&] (Button& b)
        {
            return contrastMass (renderComponent (editor, ChassisLayout::kWidth,
                                                  ChassisLayout::kHeight),
                                 b.getBounds() + b.getParentComponent()->getPosition(),
                                 theme::colour (theme::Token::panel, theme::Mode::dark));
        };

        const auto resting = restingInk (load);

        load.mouseEnter (mouseEventOn (load, load.getLocalBounds().getCentre().toFloat(), {}, 0));

        check (restingInk (load) > resting,
               "and LOAD still lifts on hover, so it reads as a control rather than as dead paint ("
                   + juce::String (resting, 1) + " -> " + juce::String (restingInk (load), 1) + ")");
    }
}

// ── 04-04 AC-6 / AC-7: the header's four new controls ───────────────────────

/** Any control on a known ground, sized as it asks to be sized. */
template <typename Control>
struct ControlRig
{
    template <typename... Args>
    ControlRig (theme::Mode mode, Args&&... args)
        : lnf (mode), control (lnf, std::forward<Args> (args)...)
    {
        holder.ground = theme::colour (theme::Token::panel, mode);
        holder.addAndMakeVisible (control);

        const auto w = control.preferredWidth();
        const auto h = control.preferredHeight();

        holder.setSize (w + kMargin * 2, h + kMargin * 2);
        control.setBounds (kMargin, kMargin, w, h);
    }

    static constexpr int kMargin = 8;

    juce::Image render() { return renderComponent (holder, holder.getWidth(), holder.getHeight()); }
    juce::Rectangle<int> area() const { return control.getBounds(); }

    ForroBoxLookAndFeel lnf;
    Control             control;
    Ground              holder;
};

void testSegmented (theme::Mode mode, const juce::String& modeName)
{
    section (fbtest::utf8 ("Segmented is a radio group with N-1 dividers — ") + modeName);

    const juce::StringArray codes { "CAM", "CAR", "PET", "UNI" };

    ControlRig<Segmented> rig { mode, codes, type::Style::quickSwitchCode,
                                Segmented::Variant::quickSwitch };

    checkEqual (rig.control.getNumSegments(), 4, modeName + ": four segments");

    // ── the segments tile the group, and only N-1 dividers separate them ────
    {
        auto covered = 0;

        for (int i = 0; i < 4; ++i)
            covered += rig.control.segmentBounds (i).getWidth();

        checkEqual (covered + 3 * segmented::kDividerWidth + segmented::kBorderWidth * 2,
                    rig.control.preferredWidth(),
                    modeName + ": the segments plus THREE dividers and two borders are the whole "
                               "width — `:last-child { border-right: 0 }` (css:250), which is the "
                               "one rule a loop over segments gets wrong");

        for (int i = 0; i + 1 < 4; ++i)
            checkEqual (rig.control.segmentBounds (i).getRight() + segmented::kDividerWidth,
                        rig.control.segmentBounds (i + 1).getX(),
                        modeName + ": segment " + juce::String (i) + " is one divider from the next");

        // And COUNTED in the render, not only in the arithmetic. The check
        // above is the layout's law; a paint loop that draws a divider after
        // the last segment satisfies it completely — which a control proved.
        //
        // Counted as vertical runs of --line across the group's mid-height,
        // where nothing else in an unselected group draws.
        rig.control.setSelectedIndex (-1);   // clamps to 0; the lit one is skipped below

        const auto image = rig.render();
        const auto area = rig.area();
        const auto line = theme::colour (theme::Token::sunken, mode)
                              .overlaidWith (theme::colour (theme::Token::line, mode));

        auto dividers = 0;
        auto wasDivider = false;

        for (int x = area.getX() + segmented::kBorderWidth;
             x < area.getRight() - segmented::kBorderWidth; ++x)
        {
            // Scanned just below the top border, ABOVE the glyph tops: the mid
            // height crosses the mono text, which is also not the ground and
            // counted as five more dividers.
            const auto isDivider = colourDistance (image.getPixelAt (x, area.getY() + 3), line)
                                       < 0.03;

            if (isDivider && ! wasDivider)
                ++dividers;

            wasDivider = isDivider;
        }

        checkEqual (dividers, 3,
                    modeName + ": THREE dividers are drawn between four segments, not four — "
                               "`:last-child { border-right: 0 }`, counted in the render");
    }

    // ── exactly one is lit, and it is the one the owner selected ────────────
    {
        const auto active = theme::colour (theme::Token::active, mode);

        for (const auto selected : { 0, 2, 3 })
        {
            rig.control.setSelectedIndex (selected);

            const auto image = rig.render();
            auto lit = 0;

            for (int i = 0; i < 4; ++i)
            {
                const auto probe = (rig.control.segmentBounds (i) + rig.area().getPosition())
                                       .getCentre();

                // Sampled off-centre from the glyphs, so this measures the
                // GROUND and not the label.
                const auto pixel = image.getPixelAt (probe.x, rig.area().getY() + 3);

                if (colourDistance (pixel, theme::colour (theme::Token::sunken, mode)
                                               .overlaidWith (active)) < 0.05)
                    ++lit;
            }

            checkEqual (lit, 1, modeName + ": exactly one segment is lit with " + juce::String (selected)
                                   + " selected");
        }
    }

    // ── a click reports the segment it landed on, through a real event ──────
    {
        rig.control.setSelectedIndex (0);

        auto reported = -1;
        rig.control.onSegmentClicked = [&reported] (int index) { reported = index; };

        for (const auto target : { 1, 3 })
        {
            const auto centre = rig.control.segmentBounds (target).getCentre();
            const auto e = mouseEventOn (rig.control, centre.toFloat());

            rig.control.mouseDown (e);
            rig.control.mouseUp (e);

            checkEqual (reported, target,
                        modeName + ": clicking segment " + juce::String (target) + " reports it");
        }

        // Released somewhere else: no click, the convention every other control
        // in this project follows.
        reported = -1;
        rig.control.mouseDown (mouseEventOn (rig.control,
                                             rig.control.segmentBounds (1).getCentre().toFloat()));
        rig.control.mouseUp (mouseEventOn (rig.control,
                                           rig.control.segmentBounds (3).getCentre().toFloat()));

        checkEqual (reported, -1,
                    modeName + ": pressing one segment and releasing on another reports nothing");

        // And the control does NOT move its own selection — what a selection
        // MEANS is the owner's, which is what lets STYLE and OUTPUT share it.
        checkEqual (rig.control.getSelectedIndex(), 0,
                    modeName + ": a click does not change the selection by itself");

        rig.control.onSegmentClicked = nullptr;
    }
}

void testValueScreen (theme::Mode mode, const juce::String& modeName)
{
    section (fbtest::utf8 ("ValueScreen is a --screen ground with the css:597 glow — ") + modeName);

    ControlRig<ValueScreen> rig { mode, type::Style::globalKnobReadout, 46, 10, 2 };

    rig.control.setText ("38");

    const auto image = rig.render();
    const auto area = rig.area();
    const auto screen = theme::colour (theme::Token::screen, mode);

    checkPixelNear (image, area.getCentreX(), area.getY() + 2, screen, kCompositeSlop,
                    modeName + ": the ground is --screen");

    // min-width GOVERNS for short text, and is exceeded by long text. The
    // first version asserted `preferredWidth() >= 46` on a screen built with a
    // min-width of 46 — `jmax (46, x) >= 46` is a tautology. Found by /simplify.
    {
        ControlRig<ValueScreen> narrow { mode, type::Style::globalKnobReadout, 46, 10, 2 };
        narrow.control.setText ("0");

        checkEqual (narrow.control.preferredWidth(), 46,
                    modeName + ": a short value takes the declared min-width exactly");

        narrow.control.setText ("-1000000");

        check (narrow.control.preferredWidth() > 46,
               modeName + ": and a value too wide for it exceeds it rather than being clipped ("
                   + juce::String (narrow.control.preferredWidth()) + ")");
    }

    // ── the glow reaches BEYOND the glyphs ──────────────────────────────────
    //
    // The claim css:597 actually makes. Ink inside the glyphs proves the text
    // drew; what proves the GLOW is ink in the gap between the text and the
    // border, where an unglowed screen is bare --screen.
    {
        const auto textWidth = type::trackedWidth (type::Style::globalKnobReadout, "38");
        const auto gapX = area.getCentreX() + juce::roundToInt (textWidth * 0.5f) + 3;

        const auto inGap = image.getPixelAt (gapX, area.getCentreY());

        check (colourDistance (inGap, screen) > 0.004,
               modeName + ": there is ink between the glyphs and the border, which is the glow and "
                          "nothing else (" + juce::String (colourDistance (inGap, screen), 4) + ")");
    }

    // ── the suffix is its own, smaller, dimmer row ──────────────────────────
    {
        // min-width 1, not the BPM field's 78: at 78 the clamp governs both
        // measurements and the check compares the min-width to itself. The
        // suffix's own contribution is what is under test.
        ControlRig<ValueScreen> bpm { mode, type::Style::bpmReadout, 1, 10, 3 };

        bpm.control.setText ("120");
        const auto withoutSuffix = bpm.control.preferredWidth();

        bpm.control.setSuffix (" BPM", type::Style::bpmSuffix);

        const auto added = bpm.control.preferredWidth() - withoutSuffix;

        check (added > 0,
               modeName + ": the suffix takes width of its own (" + juce::String (withoutSuffix)
                   + " -> " + juce::String (bpm.control.preferredWidth()) + ")");

        // Measured AGAINST the suffix's own row, not merely "less than the
        // value row would have taken" — a control that measured the suffix at
        // 22 px came out one rounding pixel under that bound and passed.
        const auto suffixRow = type::trackedWidth (type::Style::bpmSuffix, " BPM");
        const auto valueRow = type::trackedWidth (type::Style::bpmReadout, " BPM");

        check (std::abs (static_cast<float> (added) - suffixRow) <= 2.0f,
               modeName + ": and it is the 9 px row's width, not the 22 px one's ("
                   + juce::String (added) + " against " + juce::String (suffixRow, 1) + " and "
                   + juce::String (valueRow, 1) + ")");
    }
}

void testLogoMark (theme::Mode mode, const juce::String& modeName)
{
    section (fbtest::utf8 ("the logo mark is three sub-marks in three colours — ") + modeName);

    ForroBoxLookAndFeel lnf { mode };
    LogoMark mark { lnf };
    Ground holder;

    holder.ground = theme::colour (theme::Token::raised, mode);
    holder.addAndMakeVisible (mark);
    holder.setSize (logo::kWidth + 8, logo::kHeight + 8);
    mark.setBounds (4, 4, logo::kWidth, logo::kHeight);

    const auto image = renderComponent (holder, holder.getWidth(), holder.getHeight());

    check (contrastMass (image, mark.getBounds(), holder.ground) > 0.0,
           modeName + ": the mark draws");

    // ── THREE colours, not one ──────────────────────────────────────────────
    //
    // "There is ink" is satisfied by a single fillAll. What the lockup actually
    // claims is that the triângulo is the ACCENT while the other two are
    // neutral — so the test is that the accent appears at all, and that it is
    // far from both neutrals.
    {
        const auto accent = theme::accent (theme::Accent::zabumba);
        auto accentPixels = 0;
        auto neutralPixels = 0;

        for (int y = mark.getY(); y < mark.getBottom(); ++y)
        {
            for (int x = mark.getX(); x < mark.getRight(); ++x)
            {
                const auto pixel = image.getPixelAt (x, y);

                if (colourDistance (pixel, holder.ground) < 0.02)
                    continue;

                if (colourDistance (pixel, accent) < 0.20)
                    ++accentPixels;
                else
                    ++neutralPixels;
            }
        }

        check (accentPixels > 10,
               modeName + fbtest::utf8 (": the triângulo is drawn in --c-zabumba (") + juce::String (accentPixels)
                   + " px)");
        check (neutralPixels > 10,
               modeName + ": and the sanfona and zabumba are not (" + juce::String (neutralPixels)
                   + " px)");
    }

    // ── it SCALES, like the knob ────────────────────────────────────────────
    //
    // The one claim that separates a viewBox from a pile of pixel coordinates.
    // Drawn at double size, the mark's ink must grow with it rather than
    // staying put in the corner.
    {
        Ground bigHolder;
        LogoMark big { lnf };

        bigHolder.ground = holder.ground;
        bigHolder.addAndMakeVisible (big);
        bigHolder.setSize (logo::kWidth * 2 + 8, logo::kHeight * 2 + 8);
        big.setBounds (4, 4, logo::kWidth * 2, logo::kHeight * 2);

        const auto bigImage = renderComponent (bigHolder, bigHolder.getWidth(),
                                               bigHolder.getHeight());

        const auto smallWidth = inkWidth (image, mark.getBounds(), holder.ground);
        const auto largeWidth = inkWidth (bigImage, big.getBounds(), holder.ground);

        // Within a pixel or two: the mark is FITTED into its box, so the scale
        // is a jmin of two ratios and the anti-aliased edge rounds either way.
        check (std::abs (largeWidth - smallWidth * 2) <= 3,
               modeName + ": at twice the size the mark spans twice the width, so the paths are in "
                          "viewBox units (" + juce::String (smallWidth) + " -> "
                   + juce::String (largeWidth) + ")");

        // And the STROKES scale with it, which the width cannot see: the paths
        // would still span twice as far with a stroke width left in pixels.
        // Ink MASS is length x thickness, so a mark whose strokes scale gains
        // ~4x where one whose strokes do not gains ~2x. A control that dropped
        // the `* scale` passed the width check completely.
        const auto smallMass = contrastMass (image, mark.getBounds(), holder.ground);
        const auto largeMass = contrastMass (bigImage, big.getBounds(), holder.ground);
        const auto ratio = largeMass / juce::jmax (1.0, smallMass);

        check (ratio > 3.0,
               modeName + ": and its ink grows with the SQUARE of the scale, so the stroke widths "
                          "are viewBox units too (" + juce::String (ratio, 2) + "x, where leaving "
                          "them in pixels gives ~2x)");
    }
}

void testTransportButtonVariant (theme::Mode mode, const juce::String& modeName)
{
    section (fbtest::utf8 ("the transport button is an icon on --panel that darkens on hover — ") + modeName);

    ButtonRig rig { mode, Button::Variant::transport, "" };

    juce::Path play;
    play.startNewSubPath (7.0f, 5.0f);
    play.lineTo (7.0f, 19.0f);
    play.lineTo (19.0f, 12.0f);
    play.closeSubPath();
    rig.button.setIcon ({ play, Button::kTransportIconViewBox });

    checkEqual (rig.button.preferredWidth(), Button::kTransportSize,
                modeName + ": a fixed 34 px wide");
    checkEqual (rig.button.preferredHeight(), Button::kTransportSize, modeName + ": and 34 tall");

    const auto panel = theme::colour (theme::Token::panel, mode);
    const auto sunken = theme::colour (theme::Token::sunken, mode);

    // ── hover darkens the GROUND, where every other variant lifts its label ──
    {
        const auto resting = rig.render();
        const auto corner = rig.area().getTopLeft().translated (3, 3);

        checkPixelNear (resting, corner.x, corner.y, panel, kCompositeSlop,
                        modeName + ": it sits on --panel at rest");

        rig.button.mouseEnter (mouseEventOn (rig.button, rig.inside().toFloat(), {}, 0));

        checkPixelNear (rig.render(), corner.x, corner.y, sunken, kCompositeSlop,
                        modeName + ": and on --sunken when hovered, per css:195 — the third hover "
                                   "law, and the only one that touches the ground");
    }

    // ── lit, it is --c-ganza with its glow ──────────────────────────────────
    {
        ButtonRig lit { mode, Button::Variant::transport, "" };

        juce::Path icon;
        icon.addRectangle (6.0f, 6.0f, 12.0f, 12.0f);
        lit.button.setIcon ({ icon, Button::kTransportIconViewBox });
        lit.button.setOn (true);

        const auto image = lit.render();
        const auto centre = lit.area().getCentre();

        check (colourDistance (image.getPixelAt (centre.x, lit.area().getY() + 3),
                               theme::colour (theme::Token::active, mode)) > 0.1,
               modeName + ": a lit transport button is NOT the base button's --active ground");

        // The glow is outside the button's own rect, so it is measured there.
        const auto outside = lit.area().getX() - 3;

        check (colourDistance (image.getPixelAt (outside, centre.y),
                               theme::colour (theme::Token::panel, mode)) > 0.01,
               modeName + ": and it glows past its own edge, per css:637");
    }
}

// ── 04-04 AC-1 / AC-2: the BPM field's own law ──────────────────────────────

/** A BPM field bound to the real `bpm` parameter on a real processor. */
struct AttachedBpmRig
{
    AttachedBpmRig()
        : parameter (*dynamic_cast<juce::RangedAudioParameter*> (
                         processor.getAPVTS().getParameter (forrobox::ids::bpm))),
          field (lnf),
          attachment (parameter, field)
    {
        holder.ground = theme::colour (theme::Token::raised, theme::Mode::dark);
        holder.addAndMakeVisible (field);
        holder.setSize (bpmfield::kMinWidth + 20, field.preferredHeight() + 20);
        field.setBounds (10, 10, bpmfield::kMinWidth, field.preferredHeight());
    }

    int value() const { return juce::roundToInt (parameter.convertFrom0to1 (parameter.getValue())); }

    void setValue (int bpm)
    {
        parameter.setValueNotifyingHost (parameter.convertTo0to1 (static_cast<float> (bpm)));
        settle();
    }

    /** A press at the field's centre, a drag `pixelsUp`, a release. Positive is
        upward, which raises the tempo. */
    void drag (int pixelsUp)
    {
        const auto centre = field.getLocalBounds().getCentre();
        const auto down = mouseEventOn (field, centre.toFloat());

        field.mouseDown (down);
        field.mouseDrag (down.withNewPosition (centre.translated (0, -pixelsUp).toFloat()));
        field.mouseUp (down.withNewPosition (centre.translated (0, -pixelsUp).toFloat()));
        settle();
    }

    void wheel (float deltaY, bool reversed = false)
    {
        juce::MouseWheelDetails w {};
        w.deltaY = deltaY;
        w.isReversed = reversed;

        field.mouseWheelMove (mouseEventOn (field, field.getLocalBounds().getCentre().toFloat(),
                                            {}, 0), w);
        settle();
    }

    ForroBoxAudioProcessor      processor;
    ForroBoxLookAndFeel         lnf { theme::Mode::dark };
    juce::RangedAudioParameter& parameter;
    BpmField                    field;
    BpmAttachment               attachment;
    Ground                      holder;
};

void testBpmFieldLaw()
{
    section ("the BPM field's law is its own — 0.5 BPM per pixel, anchored");

    // ── drag: anchored, in BPM units ────────────────────────────────────────
    {
        AttachedBpmRig rig;
        rig.setValue (120);

        rig.drag (40);

        checkEqual (rig.value(), 140,
                    "40 px up from 120 gives 140 — 0.5 BPM per pixel from the press ANCHOR, which "
                    "is the knob's shape in BPM units and NOT the fader's absolute positioning");

        rig.setValue (120);
        rig.drag (-40);
        checkEqual (rig.value(), 100, "and 40 px down gives 100");
    }

    // ── pressed OFF-CENTRE, which is what separates anchored from absolute ──
    //
    // Every other drag here presses at the field's centre, and a law measured
    // from the centre is indistinguishable from one measured from the anchor
    // when they are the same point. A control that swapped the anchor for the
    // field's midpoint passed the whole suite.
    {
        AttachedBpmRig rig;
        rig.setValue (120);

        const auto press = rig.field.getLocalBounds().getCentre().translated (0, 8);
        const auto down = mouseEventOn (rig.field, press.toFloat());

        rig.field.mouseDown (down);
        rig.field.mouseDrag (down.withNewPosition (press.translated (0, -40).toFloat()));
        rig.field.mouseUp (down.withNewPosition (press.translated (0, -40).toFloat()));
        settle();

        checkEqual (rig.value(), 140,
                    "a 40 px drag from a press 8 px BELOW centre still moves 20 BPM — the law is "
                    "the distance from the ANCHOR, not from the field's middle");
    }

    // ── and it is ANCHORED, so out-and-back lands exactly where it started ──
    //
    // The claim that separates this law from an incremental one. An
    // implementation that accumulated per move would drift on the integer
    // rounding — the parameter is an AudioParameterInt.
    {
        AttachedBpmRig rig;
        rig.setValue (132);

        const auto centre = rig.field.getLocalBounds().getCentre();
        const auto down = mouseEventOn (rig.field, centre.toFloat());

        rig.field.mouseDown (down);

        for (const auto dy : { 3, 9, 17, 31, 17, 9, 3, 0 })
            rig.field.mouseDrag (down.withNewPosition (centre.translated (0, -dy).toFloat()));

        rig.field.mouseUp (down);
        settle();

        checkEqual (rig.value(), 132,
                    "a drag out and back lands exactly where it started — the gesture is anchored "
                    "at mouse-down, not accumulated per move");
    }

    // ── the wheel is +/-1, NOT the knob's max(1, range/50) ──────────────────
    {
        AttachedBpmRig rig;
        rig.setValue (120);

        rig.wheel (1.0f);
        checkEqual (rig.value(), 121,
                    "one wheel notch moves EXACTLY 1 — `Math.sign(deltaY)` (app.js:143), where the "
                    "knob's max(1, range/50) would move 5 over 40..300");

        rig.wheel (-1.0f);
        checkEqual (rig.value(), 120, "and the other way");

        // A big delta is still one notch: the prototype takes the SIGN.
        rig.wheel (9.0f);
        checkEqual (rig.value(), 121, "and a large delta is still one step, because it is a sign");

        rig.setValue (120);
        rig.wheel (1.0f, true);
        checkEqual (rig.value(), 119,
                    "a reversed wheel goes the other way — the flag 04-02's review found the knob "
                    "ignoring");
    }

    // ── clamped at both ends ────────────────────────────────────────────────
    {
        AttachedBpmRig rig;

        rig.setValue (forrobox::ids::kMinBpm);
        rig.drag (-400);
        checkEqual (rig.value(), forrobox::ids::kMinBpm, "dragging far below the range clamps to 40");

        rig.setValue (forrobox::ids::kMaxBpm);
        rig.drag (400);
        checkEqual (rig.value(), forrobox::ids::kMaxBpm, "and far above clamps to 300");
    }

    // ── one host gesture per drag ───────────────────────────────────────────
    {
        AttachedBpmRig rig;
        GestureCounter counter;

        rig.parameter.addListener (&counter);
        rig.setValue (120);
        rig.drag (20);

        checkEqual (counter.begins, 1, "a press, a drag and a release open exactly one gesture");
        checkEqual (counter.ends, 1, "and close exactly one");

        // Right-click belongs to the host.
        const auto before = rig.value();
        const auto e = mouseEventOn (rig.field, rig.field.getLocalBounds().getCentre().toFloat(),
                                     juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier));
        rig.field.mouseDown (e);
        rig.field.mouseUp (e);
        settle();

        checkEqual (counter.begins, 1, "a right-click opens no gesture");
        checkEqual (rig.value(), before, "and changes no value");

        rig.parameter.removeListener (&counter);
    }

    // ── typed text is validated as TEXT ─────────────────────────────────────
    {
        AttachedBpmRig rig;
        rig.setValue (120);

        check (rig.field.onTextEntered != nullptr, "the field offers text entry");

        check (! rig.field.onTextEntered ("hello"),
               "junk text is REJECTED — getValueForText bottoms out in getIntValue, which returns 0 "
               "for anything unparseable, so an isfinite() guard would accept it and slam the tempo "
               "to 40 while reporting success");
        settle();
        checkEqual (rig.value(), 120, "and leaves the value untouched");

        check (rig.field.onTextEntered ("150"), "a number is accepted");
        settle();
        checkEqual (rig.value(), 150, "and applied");

        // A rejection must be VISIBLE as a still-open editor, not a silent
        // discard: the seam's whole purpose is letting the typist correct it,
        // and onReturnKey used to throw the bool away. Driven through a real
        // double-click and a real return key.
        {
            const auto centre = rig.field.getLocalBounds().getCentre();
            rig.field.mouseDoubleClick (mouseEventOn (rig.field, centre.toFloat(), {}, 2));

            auto* editor = rig.field.findChildWithID ({});
            juce::ignoreUnused (editor);

            const auto editors = collectChildren<juce::TextEditor> (rig.field);

            checkEqual (static_cast<int> (editors.size()), 1,
                        "a double-click opens an editor");

            if (! editors.empty())
            {
                editors[0]->setText ("hello", false);
                editors[0]->onReturnKey();
                settle();

                checkEqual (static_cast<int> (collectChildren<juce::TextEditor> (rig.field).size()),
                            1,
                            "and junk text leaves it OPEN so the typist can correct it");
                checkEqual (rig.value(), 150, "with the value untouched");

                editors[0]->setText ("200", false);
                editors[0]->onReturnKey();
                settle();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

                checkEqual (rig.value(), 200, "a good value is applied");
                checkEqual (static_cast<int> (collectChildren<juce::TextEditor> (rig.field).size()),
                            0,
                            "and closes the editor");
            }
        }
    }
}

void testBpmFieldUnderSync()
{
    section ("SYNC makes the BPM field read-only and showing the HOST's tempo");

    AttachedBpmRig rig;
    rig.setValue (120);

    check (! rig.field.isReadOnly(), "the field is live with SYNC off");

    rig.attachment.setSyncedToHost (true, 174.0f);
    settle();

    check (rig.field.isReadOnly(),
           "SYNC makes it read-only — PLANNING.md:400 and its stub table at :838 both say so, and "
           "it is the one header behaviour the spec states twice");

    // ── every gesture refused, and NO host gesture opened ───────────────────
    {
        GestureCounter counter;
        rig.parameter.addListener (&counter);

        rig.drag (40);
        rig.wheel (1.0f);

        checkEqual (rig.value(), 120, "a drag and a wheel change nothing while synced");
        checkEqual (counter.begins, 0, "and open no host gesture at all");

        rig.parameter.removeListener (&counter);
    }

    // ── and it shows the HOST's tempo, not the parameter's ──────────────────
    //
    // Measured on the TEXT, through the field's own getter, rather than on ink.
    // A contrastMass over Token::screen could not fail here: setReadOnly
    // applies setAlpha(0.55), so every pixel in the field is a blend of screen
    // and the holder's ground and none of them is the token — "> 0.0" would
    // pass with nothing drawn at all, the exact failure contrastMass's own
    // docstring warns about. Found by /code-review.
    {
        rig.attachment.setSyncedToHost (true, 174.0f);
        settle();

        checkEqual (rig.field.displayedText(), juce::String ("174"),
                    "the field displays the HOST's 174 while the parameter holds 120");

        rig.attachment.setSyncedToHost (true, 90.0f);
        settle();

        checkEqual (rig.field.displayedText(), juce::String ("90"),
                    "and follows the host down to 90");

        checkEqual (rig.value(), 120, "the parameter itself is untouched throughout");

        // And it still DRAWS — measured as the difference between showing a
        // value and showing nothing, not as ink over an estimated ground.
        //
        // The previous version compared against a hand-composited `blended`
        // colour with `> 0.0`, which the field's own rounded corners satisfy
        // on their own: that was a code-review finding whose fix changed the
        // REFERENCE and left the threshold, so it still could not fail. Found
        // by /simplify.
        const auto withValue = contrastMass (renderComponent (rig.holder, rig.holder.getWidth(),
                                                              rig.holder.getHeight()),
                                             rig.field.getBounds(),
                                             theme::colour (theme::Token::screen,
                                                            theme::Mode::dark));

        rig.field.setValueText ({});
        const auto blank = contrastMass (renderComponent (rig.holder, rig.holder.getWidth(),
                                                          rig.holder.getHeight()),
                                         rig.field.getBounds(),
                                         theme::colour (theme::Token::screen, theme::Mode::dark));

        check (withValue > blank + 1.0,
               "and the field still DRAWS its value while read-only, rather than dimming to "
               "nothing (" + juce::String (withValue, 1) + " against a blank field's "
                   + juce::String (blank, 1) + ")");

        rig.attachment.setSyncedToHost (true, 90.0f);
        settle();
    }

    // ── a host reporting nothing falls back to the PARAMETER ────────────────
    //
    // The branch this section is named for, and nothing used to measure it:
    // isReadOnly() was already true before the call, so the one assertion here
    // could not fail whatever the fallback did.
    {
        rig.attachment.setSyncedToHost (true, 0.0f);
        settle();

        checkEqual (rig.field.displayedText(), juce::String ("120"),
                    "with SYNC on and the host reporting NO tempo, the field falls back to the "
                    "parameter's value rather than showing 0");
        check (rig.field.isReadOnly(),
               "and is still read-only, because SYNC is still on");
    }

    rig.attachment.setSyncedToHost (false, 0.0f);
    settle();
    check (! rig.field.isReadOnly(), "and SYNC off makes it live again");
}

void testHostTempoIsPublished()
{
    section ("the host's tempo is published from processBlock for the BPM field");

    ForroBoxAudioProcessor processor;
    fbtest::FakePlayHead host;

    processor.setPlayHead (&host);
    processor.prepareToPlay (48000.0, 256);

    // SYNC on, because that is the only state in which the tempo is read — and
    // the only state in which the field displays it. Published nowhere else, so
    // the atomic stays 0 while the plugin is on its own clock, which is what
    // lets the field tell "no host tempo" from a real one.
    if (auto* sync = processor.getAPVTS().getParameter (forrobox::ids::sync))
        sync->setValueNotifyingHost (1.0f);

    processor.setPlaying (true);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    const auto render = [&]
    {
        buffer.clear();
        midi.clear();
        processor.processBlock (buffer, midi);
    };

    // ── one getPosition() per block, on the paths the hoist CHANGED ─────────
    //
    // Publishing the tempo above every early return made the query
    // unconditional: a stopped plugin used to return at the transport gate and
    // a non-synced one inside planBlock, both making zero calls. The existing
    // count test covers the SYNC-ON rig only, so the two paths this changed
    // were uncovered. Found by /simplify.
    {
        const auto countOver = [&] (bool syncOn, bool pluginPlaying)
        {
            if (auto* sync = processor.getAPVTS().getParameter (forrobox::ids::sync))
                sync->setValueNotifyingHost (syncOn ? 1.0f : 0.0f);

            processor.setPlaying (pluginPlaying);

            host.resetQueryCount();

            for (int i = 0; i < 10; ++i)
                render();

            return host.queryCount();
        };

        checkEqual (countOver (false, false), 10,
                    "SYNC off and the plugin stopped: exactly one getPosition per block");
        checkEqual (countOver (false, true), 10,
                    "SYNC off and the plugin playing: exactly one");
        checkEqual (countOver (true, true), 10,
                    "SYNC on: exactly one, as it always was");
    }

    // ── a host reporting nothing publishes 0, not a guess ───────────────────
    {
        host.provideBpm = false;
        render();

        checkEqual (processor.getHostBpm(), 0.0f,
                    "a host that reports no tempo publishes 0, so the caller can tell that from "
                    "'the host says 120'");
    }

    // ── and the value it publishes is the one actually in use ───────────────
    {
        host.provideBpm = true;
        host.bpm = 174.0;
        render();

        checkEqual (processor.getHostBpm(), 174.0f, "a host at 174 publishes 174");

        host.bpm = 90.0;
        render();

        checkEqual (processor.getHostBpm(), 90.0f, "and following it down publishes 90");
    }

    // ── CLAMPED, because that is the tempo the groove is running at ─────────
    //
    // A field showing 900 while the plugin plays at 300 would be a readout of
    // something that is not happening. Nothing else in the suite touched
    // getHostBpm, so a control that published a constant went unnoticed.
    {
        host.bpm = 900.0;
        render();

        checkEqual (processor.getHostBpm(), static_cast<float> (forrobox::ids::kMaxBpm),
                    "a host above the range publishes the CLAMPED tempo, which is what the clock "
                    "is actually using");

        host.bpm = 5.0;
        render();

        checkEqual (processor.getHostBpm(), static_cast<float> (forrobox::ids::kMinBpm),
                    "and below it, the clamped minimum");
    }

    processor.setPlayHead (nullptr);
}

void testHostTransportGovernsUnderSync()
{
    section ("while SYNC is on, the HOST's transport decides whether anything plays");

    // The defect reported at 04-04's checkpoint: SYNC on, host rolling, and the
    // plugin silent until its own Play was pressed as well. `PLANNING.md:838`
    // says SYNC follows "host tempo and transport" and 02-03's summary says
    // "the host's transport decides whether anything plays" — the plugin's own
    // `playing` gated ahead of it anyway.
    //
    // It shipped because EVERY host-sync rig calls setPlaying(true) in its
    // constructor, so this combination had never been rendered once.
    struct Rig
    {
        ForroBoxAudioProcessor processor;
        fbtest::FakePlayHead   host;
        juce::AudioBuffer<float> buffer { 2, 256 };
        juce::MidiBuffer midi;

        Rig (bool syncOn, bool hostRolling)
        {
            processor.setPlayHead (&host);
            processor.prepareToPlay (48000.0, 256);

            if (auto* sync = processor.getAPVTS().getParameter (forrobox::ids::sync))
                sync->setValueNotifyingHost (syncOn ? 1.0f : 0.0f);

            host.provideBpm = true;
            host.bpm = 120.0;
            host.hostPlaying = hostRolling;
        }

        ~Rig() { processor.setPlayHead (nullptr); }

        int runBlocks (int count)
        {
            for (int i = 0; i < count; ++i)
            {
                buffer.clear();
                midi.clear();
                processor.processBlock (buffer, midi);
                host.advance (256, 48000.0);
            }

            return processor.getStepPublicationCount();
        }
    };

    // ── the reported case ───────────────────────────────────────────────────
    {
        Rig rig { true, true };

        // The plugin's own transport is deliberately NOT started.
        check (! rig.processor.isPlaying(), "the plugin's own transport is stopped");
        check (rig.runBlocks (40) > 0,
               "SYNC on and the host ROLLING plays, without the plugin's own Play being pressed — "
               "the host's transport is the transport while synced");
        check (rig.processor.isHostTransportRolling(),
               "and the processor reports the host's transport as rolling, which is what the "
               "header's Play button shows");
    }

    // ── and a stopped host still plays nothing ──────────────────────────────
    {
        Rig rig { true, false };

        rig.processor.setPlaying (true);

        checkEqual (rig.runBlocks (40), 0,
                    "SYNC on and the host STOPPED plays nothing, even with the plugin's own Play "
                    "on — which is the other half of the same rule");
        check (! rig.processor.isHostTransportRolling(),
               "and the host's transport is reported stopped");
    }

    // ── with SYNC off, the plugin's own transport governs, unchanged ────────
    {
        Rig rig { false, false };

        checkEqual (rig.runBlocks (20), 0, "SYNC off and the plugin stopped plays nothing");

        rig.processor.setPlaying (true);

        check (rig.runBlocks (40) > 0,
               "SYNC off and the plugin playing runs on its own clock, whatever the host is doing");
        check (! rig.processor.isHostTransportRolling(),
               "and the host's transport is NOT reported as rolling, because SYNC is off — the "
               "button must not claim a host is driving it when none is");
    }
}

void testTransportButtonIsHostDrivenUnderSync()
{
    section ("under SYNC the Play button shows the HOST and refuses clicks");

    ForroBoxAudioProcessor processor;
    ForroBoxAudioProcessorEditor editor { processor };
    editor.setSize (ChassisLayout::kWidth, ChassisLayout::kHeight);


    Button* play = nullptr;

    for (auto b : collectChildren<Button> (headerBarOf (editor)))
        if (headerBarOf (editor).getLayout().playButton.contains (b.bounds.getCentre()))
            play = b.control;

    check (play != nullptr, "the header carries a play button");

    if (play == nullptr)
        return;

    // Drives the header's refresh DIRECTLY rather than waiting for its 30 Hz
    // timer. The timer is scheduling; the behaviour is the refresh. Pumping a
    // real message loop and hoping the tick landed inside 60 ms passed on GCC
    // and Clang and failed three checks on MSVC — a test that depends on a wall
    // clock is flaky by construction.
    auto chassisList = collectChildren<Chassis> (editor);
    check (! chassisList.empty(), "the editor carries a chassis");

    if (chassisList.empty())
        return;

    auto chassis = chassisList.front();
    const auto pump = [chassis] { chassis->refreshHeaderFromProcessor(); };

    check (! play->isReadOnly(), "with SYNC off the button is live");

    processor.getAPVTS().getParameter (forrobox::ids::sync)->setValueNotifyingHost (1.0f);
    pump();

    check (play->isReadOnly(),
           "SYNC makes it read-only — while synced the host's transport is the only one that "
           "matters, and a Play button that still responded would be lying about what it controls");

    // ── and a click changes nothing ─────────────────────────────────────────
    {
        const auto before = processor.isPlaying();
        const auto e = mouseEventOn (*play, play->getLocalBounds().getCentre().toFloat());

        play->mouseDown (e);
        play->mouseUp (e);
        pump();

        checkEqual (processor.isPlaying(), before,
                    "clicking it while synced changes nothing at all");
    }

    // ── and it is LIT by the host, not by the plugin's own clock ────────────
    //
    // The claim the read-only check cannot make. With SYNC on and the host
    // rolling, the button must light even though the plugin's own `playing` is
    // false — showing isPlaying() there would leave it dark while the groove
    // ran, and a control that means "the host's transport" was never asserted
    // to read it.
    {
        fbtest::FakePlayHead host;
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;

        processor.setPlayHead (&host);
        processor.prepareToPlay (48000.0, 256);

        host.provideBpm = true;
        host.bpm = 120.0;
        host.hostPlaying = true;

        check (! processor.isPlaying(), "the plugin's own transport is stopped");

        for (int i = 0; i < 4; ++i)
        {
            buffer.clear();
            midi.clear();
            processor.processBlock (buffer, midi);
            host.advance (256, 48000.0);
        }

        pump();

        check (play->isOn(),
               "with SYNC on and the HOST rolling the button is lit, though the plugin's own "
               "transport is stopped — it shows the host's state, which is the one that matters");

        host.hostPlaying = false;

        for (int i = 0; i < 4; ++i)
        {
            buffer.clear();
            midi.clear();
            processor.processBlock (buffer, midi);
        }

        pump();

        check (! play->isOn(), "and unlit the moment the host stops");

        processor.setPlayHead (nullptr);
    }

    processor.getAPVTS().getParameter (forrobox::ids::sync)->setValueNotifyingHost (0.0f);
    pump();

    check (! play->isReadOnly(), "and SYNC off makes it live again");
}

void testTransportDrivesTheProcessor()
{
    section ("play and stop drive the processor's real transport");

    ForroBoxAudioProcessor processor;
    ForroBoxAudioProcessorEditor editor { processor };
    editor.setSize (ChassisLayout::kWidth, ChassisLayout::kHeight);


    // The transport buttons are the two whose centres sit in the header's own
    // playButton and stopButton boxes — found by geometry, not by add order.
    Button* play = nullptr;
    Button* stop = nullptr;

    for (auto b : collectChildren<Button> (headerBarOf (editor)))
    {
        if (headerBarOf (editor).getLayout().playButton.contains (b.bounds.getCentre()))
            play = b.control;
        else if (headerBarOf (editor).getLayout().stopButton.contains (b.bounds.getCentre()))
            stop = b.control;
    }

    check (play != nullptr && stop != nullptr, "the header carries a play and a stop button");

    if (play == nullptr || stop == nullptr)
        return;

    // The header's refresh, driven directly rather than through its 30 Hz
    // timer: the timer is scheduling, the refresh is the behaviour.
    auto chassisList = collectChildren<Chassis> (editor);

    check (! chassisList.empty(), "the editor carries a chassis");

    if (chassisList.empty())
        return;

    auto chassis = chassisList.front();
    const auto refresh = [chassis] { chassis->refreshHeaderFromProcessor(); };

    const auto click = [] (Button& b)
    {
        const auto e = mouseEventOn (b, b.getLocalBounds().getCentre().toFloat());
        b.mouseDown (e);
        b.mouseUp (e);
        settle();
    };

    check (! processor.isPlaying(), "the plugin starts stopped");

    // ── state, not a parameter ──────────────────────────────────────────────
    {
        juce::MemoryBlock before;
        processor.getStateInformation (before);

        click (*play);

        check (processor.isPlaying(), "a click on play starts the transport");

        juce::MemoryBlock after;
        processor.getStateInformation (after);

        check (after == before,
               "and writes NO parameter and no persisted state — `playing` is deliberately neither, "
               "because a play toggle on an automation lane fights the host transport and a plugin "
               "that resumes playing when a project opens is hostile");
    }

    // ── the lit state follows the PROCESSOR, polled ─────────────────────────
    {
        // The refresh is what puts the processor's state on the button, so it
        // is called directly — the proof that the button reads the atomic
        // rather than remembering its own click.
        //
        // The click above left the transport PLAYING, so the button must first
        // become lit through the refresh. Asserting it unlit straight after a
        // click passed vacuously: the button had never been lit at all.
        // Found by /code-review.
        refresh();

        check (play->isOn(),
               "the poll lights the button from the processor's own atomic, not from the click");

        processor.setPlaying (false);
        refresh();

        check (! play->isOn(),
               "stopping the transport from OUTSIDE unlights it, so its lit state is the "
               "processor's and not a bool the button kept");

        processor.setPlaying (true);
        refresh();

        check (play->isOn(), "and starting it from outside lights it again");
    }

    click (*stop);
    check (! processor.isPlaying(), "a click on stop stops the transport");
}

// ── 04-04 AC-4 / AC-5: the global knob group ────────────────────────────────

void testGlobalKnobGroup (theme::Mode mode, const juce::String& modeName)
{
    section (fbtest::utf8 ("the global knob group is lit from ABOVE its own top edge — ") + modeName);

    ChassisRig rig { mode };
    auto& chassis   = rig.chassis;


    const auto image = renderComponent (chassis, ChassisLayout::kWidth, ChassisLayout::kHeight);
    const auto& h = chassis.getHeaderBar().getLayout();

    check (! h.globalKnobs.isEmpty(), modeName + ": the group has a box");

    // ── AC-5: the group is lit from ABOVE its own top edge ──────────────────
    //
    // `radial-gradient(120% 160% at 50% -30%)` puts the origin OUTSIDE the box,
    // so there is no argmax row inside to find — the claim is that the tint
    // falls from top to bottom.
    //
    // Measured as BANDS, not row by row. The whole gradient spans about 0.07 in
    // colourDistance across ~66 rows, which is under one 8-bit step per row: a
    // per-row comparison measures quantisation, and the first version of this
    // check counted "equal" as a descent, so it could not fail at all. Three
    // controls proved that — a circular gradient, an origin at the centre, and
    // the light theme given the dark theme's shadow all passed it.
    const auto sunken = theme::colour (theme::Token::sunken, mode);
    const auto inner = h.globalKnobs.reduced (6);

    const auto tintBand = [&] (int y0, int y1, int x0, int x1)
    {
        auto total = 0.0;

        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                total += colourDistance (image.getPixelAt (x, y), sunken);

        return total;
    };

    // The group's own left margin, clear of the dial and the meta stack.
    const auto edgeX0 = h.globalKnobs.getX() + 2;
    const auto edgeX1 = h.globalKnobs.getX() + 10;

    {
        const auto top = tintBand (inner.getY(), inner.getY() + 8, edgeX0, edgeX1);
        const auto bottom = tintBand (inner.getBottom() - 8, inner.getBottom(), edgeX0, edgeX1);

        // Measured: 3.22 against 1.07 dark, 2.94 against 1.14 light — about 3x
        // and 2.6x. An origin at the group's own centre makes the two equal.
        check (top > bottom * 2.0,
               modeName + ": the tint is strongest at the TOP and falls to the bottom, which is "
                          "what an origin above the top edge means (" + juce::String (top, 2)
                   + " against " + juce::String (bottom, 2) + ")");
    }

    // ── AC-5: an ELLIPSE, so the horizontal falloff is far slower ───────────
    {
        const auto centre = tintBand (inner.getY(), inner.getY() + 8,
                                      h.globalKnobs.getCentreX() - 4,
                                      h.globalKnobs.getCentreX() + 4);
        const auto edge = tintBand (inner.getY(), inner.getY() + 8, edgeX0, edgeX1);

        // rx is 120% of the group's WIDTH — about 550 px — against ry at 160%
        // of its 66 px height. So the tint barely falls across the group's own
        // width: measured 4.89 at the centre against 3.22 at the edge, a ratio
        // of 1.5. A CIRCULAR gradient of radius ry reaches ~106 px, so the edge
        // 230 px away sits at the end colour entirely and the ratio explodes.
        check (centre < edge * 2.5,
               modeName + ": and the gradient is WIDE — its tint at the group's edge is close to "
                          "its tint at the centre (" + juce::String (edge, 2) + " against "
                   + juce::String (centre, 2) + "), which a circular gradient cannot be");
        check (centre > edge,
               modeName + ": while still being brightest at the origin's own column");
    }

    // ── each theme's OWN recessed treatment ─────────────────────────────────
    //
    // `inset 0 1px 3px rgba(0,0,0,0.4)` plus an outer `0 0 18px` glow in dark;
    // css:209 gives light `inset 0 1px 2px rgba(0,0,0,0.12)` and NO outer glow
    // at all. Two rows, not one row at a different alpha — theme::Shadows'
    // lesson from 04-01 and StepPad's again in 04-03.
    {
        // The inset row against the row below it, in the group's own margin.
        const auto insetRow = tintBand (h.globalKnobs.getY() + 1, h.globalKnobs.getY() + 2,
                                        edgeX0, edgeX1);
        const auto below = tintBand (h.globalKnobs.getY() + 4, h.globalKnobs.getY() + 5,
                                     edgeX0, edgeX1);

        check (std::abs (insetRow - below) > 1.0e-6,
               modeName + ": the group carries a recessed top edge distinct from the ground below "
                          "it (" + juce::String (insetRow, 3) + " against " + juce::String (below, 3)
                   + ")");

        // The OUTER glow: dark has 18 px of accent past the group's edge, light
        // has none at all.
        //
        // Probed to the SIDE, not above. The group is 65 px tall in a 72 px
        // header, so `getY() - 10` is off the top of the image — and
        // getPixelAt returns transparent black there, which made BOTH themes
        // measure ~1.0 against a reference pixel that was also out of bounds.
        // Two checks reading garbage, and the light one only failed because the
        // garbage happened to differ.
        const auto glowRow = h.globalKnobs.getCentreY();
        const auto headerGround = image.getPixelAt (ChassisLayout::kHeaderPadX / 2, glowRow);

        auto outside = 0.0;

        for (int x = h.globalKnobs.getX() - 12; x < h.globalKnobs.getX() - 2; ++x)
            outside = juce::jmax (outside,
                                  colourDistance (image.getPixelAt (x, glowRow), headerGround));

        if (mode == theme::Mode::dark)
            check (outside > 0.004,
                   fbtest::utf8 ("dark: the group glows past its own edge — `0 0 18px` at 12% (worst pixel ")
                       + juce::String (outside, 4) + ")");
        else
            check (outside < 0.004,
                   "light: and the light theme has NO outer glow at all, per css:209 — its own "
                   "shadow, not the dark one dimmed (worst pixel " + juce::String (outside, 4) + ")");
    }

    // ── AC-5: the SHAPE is undistorted ──────────────────────────────────────
    {
        // rx is 120% of WIDTH and ry 160% of HEIGHT. The group is far wider
        // than tall, so rx is the larger in absolute pixels and the horizontal
        // falloff is slower — a CIRCULAR gradient would make them equal.
        // The SHAPE is undistorted: the group's ground begins and ends exactly
        // at its own box. Graphics::addTransform would have scaled the rounded
        // rectangle and its border by rx/ry along with the gradient, so the
        // ground would start somewhere else entirely.
        //
        // Measured against the HEADER's own pixel at that row rather than
        // against the border's declared colour: the border is drawn over the
        // group's gradient, so its composited value is not the token, and a
        // tolerance against the token found nothing in dark and the wrong
        // column in light.
        const auto row = h.globalKnobs.getY() + 4;
        const auto headerGround = image.getPixelAt (h.globalKnobs.getX() - 24, row);

        // The threshold separates the GROUND from the GLOW, and it has to: the
        // 18 px accent glow reaches 0.027 past the group's edge in dark, while
        // stepping onto the group's own ground is 0.110 there and 0.224 in
        // light. A threshold of 0.02 sat BELOW the glow, so this probe was
        // finding the glow's edge on every platform — and MSVC's rasteriser
        // spreads it four columns further than GCC's, which is the only reason
        // it showed up. The same rasteriser-dependence 04-02 recorded.
        constexpr double kGroundStep = 0.06;

        const auto firstDifferingColumn = [&] (int from, int to, int step)
        {
            for (int x = from; x != to; x += step)
                if (colourDistance (image.getPixelAt (x, row), headerGround) > kGroundStep)
                    return x;

            return -1;
        };

        const auto leftEdge = firstDifferingColumn (h.globalKnobs.getX() - 20,
                                                    h.globalKnobs.getCentreX(), 1);
        const auto rightEdge = firstDifferingColumn (h.globalKnobs.getRight() + 20,
                                                     h.globalKnobs.getCentreX(), -1);

        check (std::abs (leftEdge - h.globalKnobs.getX()) <= 2,
               modeName + ": the group's ground begins at its own left edge, undistorted by the "
                          "ellipse (found at " + juce::String (leftEdge) + ", box at "
                   + juce::String (h.globalKnobs.getX()) + ")");
        check (std::abs (rightEdge - (h.globalKnobs.getRight() - 1)) <= 2,
               modeName + ": and ends at its own right edge (found at " + juce::String (rightEdge)
                   + ", box at " + juce::String (h.globalKnobs.getRight() - 1) + ")");
    }

    // ── the divider between the two knobs ───────────────────────────────────
    checkEqual (h.knobDivider.getWidth(), ChassisLayout::kGlobalKnobDividerWidth,
                modeName + ": the divider is 1 px wide");
    checkEqual (h.knobDivider.getHeight(), ChassisLayout::kGlobalKnobDividerHeight,
                modeName + ": and 42 px tall — its own height, not the group's");
    check (h.globalKnobs.contains (h.knobDivider),
           modeName + ": and it sits inside the group");

    // ── the two dials are 54 px and inside the group ────────────────────────
    for (const auto& [name, box] : { std::pair<const char*, juce::Rectangle<int>> { "SWING", h.swingKnob },
                                     { "CACHACA", h.cachacaKnob } })
    {
        checkEqual (box.getWidth(), ChassisLayout::kGlobalKnobSize,
                    modeName + ": the " + name + " dial is 54 px — PLANNING.md:381");
        check (h.globalKnobs.contains (box),
               modeName + ": and fits the group reserved for it");
    }

    check (h.swingKnob.getRight() < h.knobDivider.getX()
               && h.cachacaKnob.getX() > h.knobDivider.getRight(),
           modeName + ": SWING is left of the divider and CACHACA right of it");
}

void testGlobalKnobsAreLive()
{
    section ("SWING and CACHACA drive their parameters, with readouts that follow");

    ForroBoxAudioProcessor processor;
    ForroBoxAudioProcessorEditor editor { processor };
    editor.setSize (ChassisLayout::kWidth, ChassisLayout::kHeight);

    const auto& h = headerBarOf (editor).getLayout();

    // Found by GEOMETRY, not by add order — the same rule the transport test
    // follows, and the reason the header's boxes are reserved at all.
    Knob* swing = nullptr;
    Knob* cachaca = nullptr;

    for (auto k : collectChildren<Knob> (headerBarOf (editor)))
    {
        if (h.swingKnob.contains (k.bounds.getCentre()))
            swing = k.control;
        else if (h.cachacaKnob.contains (k.bounds.getCentre()))
            cachaca = k.control;
    }

    check (swing != nullptr && cachaca != nullptr, "the header carries both global knobs");

    if (swing == nullptr || cachaca == nullptr)
        return;

    auto& apvts = processor.getAPVTS();

    for (const auto& [name, id, knob] : { std::tuple<const char*, const char*, Knob*>
                                              { "SWING", forrobox::ids::swing, swing },
                                          { "CACHACA", forrobox::ids::cachaca, cachaca } })
    {
        auto* parameter = apvts.getParameter (id);
        check (parameter != nullptr, juce::String (name) + " has a parameter");

        if (parameter == nullptr)
            continue;

        GestureCounter counter;
        parameter->addListener (&counter);

        parameter->setValueNotifyingHost (0.5f);
        settle();

        const auto before = parameter->getValue();

        // Driven through a real MouseEvent, never by calling the callback.
        const auto centre = knob->getLocalBounds().getCentre();
        const auto down = mouseEventOn (*knob, centre.toFloat());

        knob->mouseDown (down);
        knob->mouseDrag (down.withNewPosition (centre.translated (0, -40).toFloat()));
        knob->mouseUp (down.withNewPosition (centre.translated (0, -40).toFloat()));
        settle();

        check (parameter->getValue() > before,
               juce::String (name) + ": a 40 px drag up raises its parameter");
        checkEqual (counter.begins, 1, juce::String (name) + ": one host gesture begin");
        checkEqual (counter.ends, 1, juce::String (name) + ": and one end");

        parameter->removeListener (&counter);
    }

    // ── the readouts follow the PARAMETER, from outside ─────────────────────
    {
        const auto readoutInk = [&] (juce::Rectangle<int> box)
        {
            // The readouts hang off the knob's onProportionChanged, which the
            // attachment fires through an AsyncUpdater — so draining the queue
            // is enough, and this no longer waits on the header's timer. It
            // used to pump 60 ms because the readouts were polled.
            settle();

            return contrastMass (renderComponent (editor, ChassisLayout::kWidth,
                                                  ChassisLayout::kHeight),
                                 box, theme::colour (theme::Token::screen, theme::Mode::dark));
        };

        auto* swingParameter = apvts.getParameter (forrobox::ids::swing);

        swingParameter->setValueNotifyingHost (0.0f);
        const auto atZero = readoutInk (h.swingRead);

        swingParameter->setValueNotifyingHost (1.0f);
        const auto atFull = readoutInk (h.swingRead);

        check (std::abs (atZero - atFull) > 1.0,
               "the SWING readout CHANGES with its parameter, set from outside — one source, no "
               "second writer that could disagree with the dial beside it (" + juce::String (atZero, 1)
                   + " -> " + juce::String (atFull, 1) + ")");
    }

    // ── SWING's arc is neutral where CACHACA's is the accent ────────────────
    //
    // PLANNING.md:390, and the one thing that distinguishes the pair visually.
    {
        for (const auto id : { forrobox::ids::swing, forrobox::ids::cachaca })
            apvts.getParameter (id)->setValueNotifyingHost (1.0f);

        settle();

        const auto image = renderComponent (editor, ChassisLayout::kWidth, ChassisLayout::kHeight);
        const auto accent = theme::accent (theme::Accent::zabumba);

        const auto accentMass = [&] (juce::Rectangle<int> box)
        {
            auto mass = 0;

            for (int y = box.getY(); y < box.getBottom(); ++y)
                for (int x = box.getX(); x < box.getRight(); ++x)
                    if (colourDistance (image.getPixelAt (x, y), accent) < 0.15)
                        ++mass;

            return mass;
        };

        const auto swingAccent = accentMass (h.swingKnob);
        const auto cachacaAccent = accentMass (h.cachacaKnob);

        check (cachacaAccent > swingAccent * 3,
               "CACHACA's value arc is --c-zabumba where SWING's is neutral (" 
                   + juce::String (cachacaAccent) + " accent px against " 
                   + juce::String (swingAccent) + ")");
    }
}

// ── 04-04 AC-6, amended at 06-03: the preset cycler is still a stub; STYLE is not ──

void testHeaderRightCluster()
{
    section ("the preset cycler still changes nothing; STYLE now loads a profile");

    // No function-scope layout: each block below builds its OWN chassis, and the
    // header's boxes now come from the bar that owns them rather than from a
    // second copy on ChassisLayout. Asking the bar is what keeps the box and the
    // control's bounds in ONE coordinate space.

    // ── STYLE lights the PERSISTED profile ──────────────────────────────────
    //
    // Built fresh per profile with the state already set, because that is the
    // order a reopened project arrives in: state first, editor second. Asserted
    // for more than one, so it cannot pass on a hard-coded 0.
    for (const auto& expected : forrobox::ids::profileInfos)
    {
        ForroBoxAudioProcessor processor;

        {
            auto state = processor.lockPatternState();
            state->activeProfile = expected.id;
        }

        ForroBoxLookAndFeel lnf { theme::Mode::dark };
        ValueTooltip tooltip { lnf };
        Chassis chassis { lnf };

        chassis.setBounds (0, 0, ChassisLayout::kWidth, ChassisLayout::kHeight);
        chassis.attachParameters (processor.getAPVTS(), &tooltip);

        auto* style = chassis.getHeaderBar().getStyleControl();

        check (style != nullptr, "the header carries the STYLE control");

        if (style == nullptr)
            return;

        checkEqual (style->getSelectedIndex(), ChassisLayout::indexOfProfile (expected.id),
                    juce::String ("with ") + expected.id + " persisted, STYLE lights its segment");
        checkEqual (style->getNumSegments(), static_cast<int> (forrobox::ids::profileInfos.size()),
                    "and carries one segment per profile, from the table verify-profiles.py "
                    "cross-checks against data.js");
    }

    // ── and it FOLLOWS activeProfile after the editor exists ────────────────
    //
    // The loop above sets activeProfile BEFORE attachParameters, so it only ever
    // exercises the build-time read — and a control that read the state once and
    // never again passed it. That is exactly the bug /code-review found on the
    // footer's OUTPUT toggle in this plan; /simplify then found the same shape
    // here, one control over, in the place Phase 6 would least expect it.
    //
    // `activeProfile` is ValueTree state rather than a parameter, so there is no
    // attachment to carry it and the header's poll is its only path. Driven
    // directly, never waited for.
    {
        ChassisRig rig;
        auto& chassis   = rig.chassis;

        auto* style = chassis.getHeaderBar().getStyleControl();

        check (style != nullptr, "the header carries the STYLE control");

        if (style == nullptr)
            return;

        // Every profile, in an order that returns to one already seen, so it
        // cannot pass by moving once and sticking.
        for (const auto* id : { "caruaru", "petrolina", "campina", "sp", "campina" })
        {
            {
                auto state = rig.processor.lockPatternState();
                state->activeProfile = id;
            }

            chassis.refreshHeaderFromProcessor();

            checkEqual (style->getSelectedIndex(), ChassisLayout::indexOfProfile (id),
                        juce::String ("a profile reload to ") + id
                            + " moves STYLE's lit segment — it is polled, because activeProfile "
                              "is state and has no parameter to attach to");
        }
    }

    // ── and clicking changes NOTHING ────────────────────────────────────────
    {
        ChassisRig rig;
        auto& processor = rig.processor;

        auto* style = rig.chassis.getHeaderBar().getStyleControl();

        if (style == nullptr)
            return;

        juce::MemoryBlock before;
        processor.getStateInformation (before);


        for (int i = 0; i < style->getNumSegments(); ++i)
        {
            const auto centre = style->segmentBounds (i).getCentre();
            const auto e = mouseEventOn (*style, centre.toFloat());

            style->mouseDown (e);
            style->mouseUp (e);
        }

        settle();

        juce::MemoryBlock after;
        processor.getStateInformation (after);

        // INVERTED at 06-03, which is the plan this check was holding the place
        // for — its own message said "a later partial wiring fails here".
        // Clicking STYLE now performs the full reload.
        check (after != before,
               "clicking a STYLE segment CHANGES the persisted state — 06-03 wired it to the same "
               "reload the side panel's list calls");

        // Against `profileInfos`, not against the control's own segment count —
        // deriving the expected value from the thing under test would also pass
        // if Segmented were lighting the clicked index directly instead of going
        // through `selectedProfileIndex()`, which is what the message claims.
        checkEqual (style->getSelectedIndex(),
                    static_cast<int> (forrobox::ids::profileInfos.size()) - 1,
                    "and the lit segment follows the profile that was loaded last, through the "
                    "processor's own predicate rather than the click");

        checkEqual (processor.selectedProfileIndex(), style->getSelectedIndex(),
                    "which is the same answer the side panel reads");

    }

    // ── the preset cycler does not even cycle its label ─────────────────────
    {
        ForroBoxAudioProcessor processor;
        ForroBoxAudioProcessorEditor editor { processor };
        editor.setSize (ChassisLayout::kWidth, ChassisLayout::kHeight);

        const auto& h = headerBarOf (editor).getLayout();

        Button* prev = nullptr;
        Button* next = nullptr;

        for (auto b : collectChildren<Button> (headerBarOf (editor)))
        {
            if (h.presetPrev.contains (b.bounds.getCentre()))
                prev = b.control;
            else if (h.presetNext.contains (b.bounds.getCentre()))
                next = b.control;
        }

        check (prev != nullptr && next != nullptr, "the header carries both preset arrows");

        if (prev == nullptr || next == nullptr)
            return;

        const auto screenInk = [&]
        {
            return contrastMass (renderComponent (editor, ChassisLayout::kWidth,
                                                  ChassisLayout::kHeight),
                                 h.presetScreen,
                                 theme::colour (theme::Token::screen, theme::Mode::dark));
        };

        const auto before = screenInk();

        for (auto* arrow : { prev, next })
        {
            const auto e = mouseEventOn (*arrow, arrow->getLocalBounds().getCentre().toFloat());
            arrow->mouseDown (e);
            arrow->mouseUp (e);
        }

        settle();

        checkEqual (screenInk(), before,
                    "the preset arrows do not even cycle the label — PLANNING.md:841 lists eight "
                    "names and says a real preset system is the intended behaviour, and a label "
                    "that changes while nothing else does is the dishonest kind of stub");

        // But they are visibly alive, which is how a reviewer tells a stub from
        // dead paint.
        const auto resting = contrastMass (renderComponent (editor, ChassisLayout::kWidth,
                                                            ChassisLayout::kHeight),
                                           prev->getBounds(),
                                           theme::colour (theme::Token::panel, theme::Mode::dark));

        prev->mouseEnter (mouseEventOn (*prev, prev->getLocalBounds().getCentre().toFloat(), {}, 0));

        const auto hovered = contrastMass (renderComponent (editor, ChassisLayout::kWidth,
                                                            ChassisLayout::kHeight),
                                           prev->getBounds(),
                                           theme::colour (theme::Token::panel, theme::Mode::dark));

        check (hovered > resting,
               "and they still lift on hover (" + juce::String (resting, 1) + " -> "
                   + juce::String (hovered, 1) + ")");
    }
}

void testEveryHeaderBoxIsFilled()
{
    section ("every box the header reserves carries content");

    ChassisRig rig;
    auto& chassis   = rig.chassis;

    const auto image = renderComponent (chassis, ChassisLayout::kWidth, ChassisLayout::kHeight);
    const auto& h = chassis.getHeaderBar().getLayout();

    // Measured against the header's own gradient at each box's top row, so
    // "filled" means ink appeared where the layout reserved room.
    const std::array<std::pair<const char*, juce::Rectangle<int>>, 14> boxes {{
        { "logoMark",      h.logoMark },      { "wordmark",      h.wordmark },
        { "bpmField",      h.bpmField },      { "syncButton",    h.syncButton },
        { "halfButton",    h.halfButton },    { "doubleButton",  h.doubleButton },
        { "playButton",    h.playButton },    { "stopButton",    h.stopButton },
        { "swingKnob",     h.swingKnob },     { "cachacaKnob",   h.cachacaKnob },
        { "swingRead",     h.swingRead },     { "cachacaRead",   h.cachacaRead },
        { "presetScreen",  h.presetScreen },  { "styleSegments", h.styleSegments },
    }};

    for (const auto& [name, box] : boxes)
    {
        check (! box.isEmpty(), juce::String ("the header reserves a ") + name + " box");

        // Compared ROW BY ROW against the header's own gutter at the same y.
        //
        // A single reference pixel could not work: the header's gradient is
        // VERTICAL, so a box 28 rows tall differs from any one pixel of it by
        // 3-4 mass with nothing drawn in it at all — and `> 0.0` then passed
        // for every one of these fourteen boxes whether it was filled or not.
        // Found by /simplify, in the newest test in the file.
        auto ink = 0.0;

        for (int y = box.getY(); y < box.getBottom(); ++y)
        {
            const auto rowGround = image.getPixelAt (ChassisLayout::kHeaderPadX / 2, y);

            for (int x = box.getX(); x < box.getRight(); ++x)
                ink += colourDistance (image.getPixelAt (x, y), rowGround);
        }

        // Scaled by area, so a large empty box cannot pass on rounding the way
        // a bare "> 0" lets it.
        const auto perPixel = ink / juce::jmax (1.0, (double) box.getWidth() * box.getHeight());

        check (perPixel > 0.01,
               juce::String ("and ") + name + " carries content (" + juce::String (perPixel, 4)
                   + " per pixel)");
    }

    // ── the content FITS the box reserved for it ────────────────────────────
    //
    // The layout reserves `min-width` for the BPM field and the preset screen,
    // and a css min-width is a floor, not a size: text wider than it overflows.
    // Nothing could see that — the filled-box check above measures ink INSIDE
    // each box, so a label running past its edge scores the same. Found by
    // /simplify.
    {
        const auto fits = [] (const char* what, type::Style style, const juce::String& text,
                              int boxWidth, int padX)
        {
            const auto needed = juce::roundToInt (type::trackedWidth (style, text))
                              + padX * 2 + ValueScreen::kBorderWidth * 2;

            check (needed <= boxWidth,
                   juce::String (what) + " fits the box reserved for it (" + juce::String (needed)
                       + " needed, " + juce::String (boxWidth) + " reserved)");
        };

        // The widest tempo the field can show, not the default it happens to
        // start at — 300 is three digits, and the suffix rides beside it.
        const auto widestBpm = juce::String (forrobox::ids::kMaxBpm);
        const auto bpmNeeded = juce::roundToInt (
                                   type::trackedWidth (type::Style::bpmReadout, widestBpm)
                                   + type::trackedWidth (type::Style::bpmSuffix, " BPM"))
                             + bpmfield::kPadX * 2 + ValueScreen::kBorderWidth * 2;

        check (bpmNeeded <= h.bpmField.getWidth(),
               "the BPM field fits its widest value plus the suffix (" + juce::String (bpmNeeded)
                   + " needed, " + juce::String (h.bpmField.getWidth()) + " reserved)");

        fits ("the preset screen", type::Style::presetScreen, ChassisLayout::presetStubLabel(),
              h.presetScreen.getWidth(), ChassisLayout::kPresetScreenPadX);

        for (size_t i = 0; i < ChassisLayout::globalKnobNames().size(); ++i)
        {
            const auto box = i == 0 ? h.swingName : h.cachacaName;

            check (juce::roundToInt (type::trackedWidth (type::Style::globalKnobName,
                                                         ChassisLayout::globalKnobNames()[i]))
                       <= box.getWidth(),
                   "the " + ChassisLayout::globalKnobNames()[i] + " label fits its column");
        }
    }

    // Every cluster inside the header, and none overlapping another.
    for (size_t i = 0; i < boxes.size(); ++i)
    {
        check (chassis.getLayout().header.contains (boxes[i].second),
               juce::String (boxes[i].first) + " is inside the header row");

        for (size_t j = i + 1; j < boxes.size(); ++j)
            check (! boxes[i].second.intersects (boxes[j].second),
                   juce::String (boxes[i].first) + " does not overlap " + boxes[j].first);
    }
}

// ── every non-ASCII character the UI draws has a glyph ──────────────────────

void testNonAsciiGlyphsExist()
{
    section ("every non-ASCII character the UI draws has a glyph in the face that draws it");

    // The embedded fonts are instanced offline and committed (04-01), so a
    // future re-instance, a subset, or a swapped weight can drop a glyph — and
    // a missing one draws as .notdef or as nothing at all. 04-04 already shipped
    // one invisible text defect: U+2039 reached juce::String through the wrong
    // constructor and rendered as "a<EUR>1/2" with every check green, because
    // each measured that there was ink rather than WHICH ink.
    //
    // Measured as ink, not as a cmap lookup: what matters is that the glyph
    // reaches a pixel, which is the same standard every other claim here meets.
    struct Glyph { const char* what; const char* utf8; type::Style style; };

    const std::array<Glyph, 13> glyphs {{
        { "U+00F7 division sign (the div-2 button)", "\xc3\xb7", type::Style::miniButtonLabel },
        { "U+00D7 multiplication sign (the x2 button)", "\xc3\x97", type::Style::miniButtonLabel },
        { "U+2039 single left angle quote (the arrows)", "\xe2\x80\xb9", type::Style::buttonLabel },
        { "U+203A single right angle quote", "\xe2\x80\xba", type::Style::buttonLabel },
        { "U+00B7 middle dot (the wordmark)", "\xc2\xb7", type::Style::wordmark },
        { "U+00D3 O-acute (FORRO)", "\xc3\x93", type::Style::wordmark },
        { "U+00C7 C-cedilla (CACHACA)", "\xc3\x87", type::Style::globalKnobName },
        { "U+00C9 E-acute (PE-DE-SERRA)", "\xc3\x89", type::Style::presetScreen },
        { "U+2197 north-east arrow (the sub-dots label)", "\xe2\x86\x97",
          type::Style::stripMicroLabel },
        // 04-05. The one reason DRAG MIDI's arrow is drawn as TEXT rather than
        // as a juce::Path — the stylesheet says `font-size: 16px`, and a path
        // would be a second way of saying that which no cross-check could
        // compare to css:539.
        { "U+2193 downwards arrow (DRAG MIDI)", "\xe2\x86\x93", type::Style::dragMidiArrow },
        // 06-04. These three are drawn text that this table had never covered,
        // found when the charset repertoire enumerated them as characters this
        // UI draws and nothing proved the face inks them. A subset or a
        // re-instance dropping one is silent — the failure 04-04 shipped.
        { "U+2014 em dash (campina's description line)", "\xe2\x80\x94",
          type::Style::profileDescription },
        { "U+2026 horizontal ellipsis (LOAD IR...)", "\xe2\x80\xa6",
          type::Style::buttonLabel },
        { "U+2122 trade mark sign (CICLOTRON)", "\xe2\x84\xa2", type::Style::timbreSubLabel },
    }};

    for (const auto& [what, utf8, style] : glyphs)
    {
        const auto text = juce::String (juce::CharPointer_UTF8 (utf8));

        checkEqual (text.length(), 1,
                    juce::String (what) + " is ONE character after decoding");

        const auto width = type::trackedWidth (style, text);

        check (width > 0.5f,
               juce::String (what) + " has a width in its own type row ("
                   + juce::String (width, 2) + " px)");

        // And it INKS. A .notdef box has a width too, so width alone would pass
        // for a font that dropped the glyph — what separates them is that a
        // space of the same nominal width leaves the swatch blank.
        TextSwatch swatch;
        swatch.face = type::styleFor (style).face;
        swatch.heightPx = 32.0f;
        swatch.text = text;
        swatch.setSize (64, 48);

        const auto inked = inkMass (renderComponent (swatch, 64, 48));

        swatch.text = " ";
        const auto blank = inkMass (renderComponent (swatch, 64, 48));

        check (inked > blank + 1.0,
               juce::String (what) + " reaches a pixel, rather than drawing as nothing ("
                   + juce::String (inked, 1) + " against a space's " + juce::String (blank, 1) + ")");
    }
}

// ── 04-05: the footer ───────────────────────────────────────────────────────

/** The meter's own law, against answers computed by hand.

    Its instrument before its use, and including a case it must REJECT — the
    shape every measurement helper in this suite has had to prove since 04-02
    shipped six checks that could not fail. */
void testGainReductionMeterInstrument()
{
    section ("the GR meter rises at once, falls linearly, and clamps at both ends");

    ForroBoxLookAndFeel lnf { theme::Mode::dark };
    GainReductionMeter meter { lnf };
    meter.setSize (grmeter::kWidth, grmeter::kHeight);

    checkEqual (meter.getDisplayedDb(), 0.0f, "it starts empty");

    // ── up at once ─────────────────────────────────────────────────────────
    //
    // The source is a peak since the last read, so a rise that was smoothed is
    // a peak that never appeared.
    meter.setReductionDb (3.0f, FooterBar::kPollSeconds);
    checkEqual (meter.getDisplayedDb(), 3.0f,
                "a rise is shown at once, whatever the elapsed time — the reading is a PEAK");

    meter.setReductionDb (3.0f, 0.0f);
    checkEqual (meter.getDisplayedDb(), 3.0f, "and a repeat of the same value does not move it");

    // ── down linearly: full scale in kDecaySeconds ─────────────────────────
    {
        // Half the decay window falls half of full scale, which from 3 dB on a
        // 6 dB scale reaches exactly 0.
        GainReductionMeter falling { lnf };
        falling.setReductionDb (grmeter::kRangeDb, 0.0f);
        checkEqual (falling.getDisplayedDb(), grmeter::kRangeDb, "primed at full scale");

        falling.setReductionDb (0.0f, grmeter::kDecaySeconds * 0.5f);
        checkEqual (falling.getDisplayedDb(), grmeter::kRangeDb * 0.5f,
                    "half the decay window falls half of FULL SCALE, not half of the distance "
                    "left — the CSS rate, not its restart-on-change semantics");

        falling.setReductionDb (0.0f, grmeter::kDecaySeconds * 0.5f);
        checkEqual (falling.getDisplayedDb(), 0.0f, "and the other half reaches empty");
    }

    // ── a fall is NOT instant, which is the thing the decay exists for ──────
    {
        GainReductionMeter falling { lnf };
        falling.setReductionDb (grmeter::kRangeDb, 0.0f);
        falling.setReductionDb (0.0f, FooterBar::kPollSeconds);

        check (falling.getDisplayedDb() > 0.0f,
               juce::String ("one poll's worth of decay leaves the meter partly lit (")
                   + juce::String (falling.getDisplayedDb(), 3) + " dB) — a meter that dropped "
                     "to the reading every poll would strobe, because every read starts from zero");
    }

    // ── the cases it must reject ───────────────────────────────────────────
    {
        GainReductionMeter clamped { lnf };

        clamped.setReductionDb (grmeter::kRangeDb * 4.0f, 0.0f);
        checkEqual (clamped.getDisplayedDb(), grmeter::kRangeDb,
                    "a reduction past full scale reads full, not past it");
        checkEqual (clamped.displayedProportion(), 1.0f, "and its proportion is exactly 1");

        // A negative reading, fed so that the clamp is the ONLY thing stopping
        // it. Handed to an EMPTY meter it is unreachable — a negative target
        // fails `target >= displayedDb`, so it falls into the decay branch,
        // where `jmax (target, 0 - 0)` is 0 whatever the target was. The first
        // version of this check asserted exactly that, and a control removing
        // the clamp altogether passed it.
        //
        // So the meter is primed full and then handed -12 with TWICE the decay
        // window: the fall overshoots empty, and only the clamp stops the
        // displayed value at zero instead of -6 dB, which would draw a fill of
        // minus one screen width.
        GainReductionMeter negative { lnf };
        negative.setReductionDb (grmeter::kRangeDb, 0.0f);
        negative.setReductionDb (-12.0f, grmeter::kDecaySeconds * 2.0f);

        checkEqual (negative.getDisplayedDb(), 0.0f,
                    "a NEGATIVE reduction bottoms out at empty rather than going past it");
        checkEqual (negative.displayedProportion(), 0.0f, "and its proportion is exactly 0");
    }

    // ── full scale is the limiter's own threshold ──────────────────────────
    //
    // NOT a tautology, though it reads like one and /simplify called it one: the
    // two sides are equal only while `kRangeDb`'s DEFINITION still reads from
    // `kLimiterThresholdDb`, and this is the only thing checking that it does.
    // Removing it on that advice turned negative control c88 — which rewrites
    // the definition to a made-up 12 dB — from detected to NOT DETECTED, which
    // is what put it back. A `static_assert` would be stronger still, but it
    // would make c88 fail to BUILD, and a build failure is not a detection.
    checkEqual (grmeter::kRangeDb, -forrobox::kLimiterThresholdDb,
                "full scale is asked of MixBus rather than picked, so the meter is full exactly "
                "when the loudest sample was pushed from 0 dBFS to the threshold");
}

/** The fill grows RIGHT to LEFT — css:518, and the one thing about this control
    a reader would assume the other way round. */
void testGainReductionMeterGrowsFromTheRight (theme::Mode mode, const juce::String& modeName)
{
    section (fbtest::utf8 ("the GR meter's fill grows right to left — ") + modeName);

    ForroBoxLookAndFeel lnf { mode };

    Ground holder;
    holder.ground = theme::colour (theme::Token::raised, mode);

    GainReductionMeter meter { lnf };
    holder.addAndMakeVisible (meter);
    holder.setSize (grmeter::kWidth + 8, grmeter::kHeight + 8);
    meter.setBounds (4, 4, grmeter::kWidth, grmeter::kHeight);

    const auto danger = theme::colour (theme::Token::danger, mode);

    /** The x range of the --danger fill, measured on the meter's middle row. */
    const auto fillSpan = [&] () -> juce::Range<int>
    {
        const auto image = renderComponent (holder, holder.getWidth(), holder.getHeight());
        const auto y = meter.getBounds().getCentreY();

        auto first = -1, last = -1;

        for (int x = meter.getBounds().getX(); x < meter.getBounds().getRight(); ++x)
            if (colourDistance (image.getPixelAt (x, y), danger) < 0.2)
            {
                if (first < 0)
                    first = x;

                last = x;
            }

        return first < 0 ? juce::Range<int>() : juce::Range<int> (first, last + 1);
    };

    // ── empty means EMPTY ──────────────────────────────────────────────────
    check (fillSpan().isEmpty(),
           modeName + ": with no reduction there is no --danger anywhere in the meter");

    // ── half a scale fills the right half ──────────────────────────────────
    meter.setReductionDb (grmeter::kRangeDb * 0.5f, 0.0f);

    const auto half = fillSpan();
    check (! half.isEmpty(), modeName + ": half a scale inks");

    if (! half.isEmpty())
    {
        const auto box = meter.getBounds();

        check (half.getEnd() >= box.getRight() - grmeter::kBorder - 1,
               modeName + ": the fill reaches the RIGHT edge, which is the end it grows from");
        check (half.getStart() > box.getCentreX() - grmeter::kHeight,
               juce::String (modeName) + fbtest::utf8 (": and not the left one — it starts at x=")
                   + juce::String (half.getStart()) + ", right of the box's centre x="
                   + juce::String (box.getCentreX()));
    }

    // ── and more reduction reaches FURTHER LEFT, not further right ─────────
    meter.setReductionDb (grmeter::kRangeDb, 0.0f);

    const auto full = fillSpan();
    check (! full.isEmpty(), modeName + ": full scale inks");

    if (! full.isEmpty() && ! half.isEmpty())
        check (full.getStart() < half.getStart(),
               juce::String (modeName) + ": a larger reduction extends the fill LEFT ("
                   + juce::String (full.getStart()) + " against " + juce::String (half.getStart())
                   + "), which is what right-to-left growth means");
}

/** MASTER and LIMITER drive real parameters, through real mouse events. */
void testFooterMasterAndLimiter()
{
    section ("MASTER and LIMITER drive their parameters, one gesture each");

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    auto& apvts = processor.getAPVTS();
    const auto& layout = chassis.getFooterBar().getLayout();

    // The footer's own controls, found by where they are rather than by an
    // index into a child list — the header's tests' rule.
    Fader* master = nullptr;
    Button* limiter = nullptr;

    // FROM THE FOOTER, and by the carried bounds. `layout.masterFader` is in
    // FOOTER space while `getBounds()` is parent-relative, and this list also
    // holds the five ghost faders whose parent IS the chassis — so the two
    // sides were in different spaces and agreed only because an exact
    // centre-point collision happens not to occur. Exactly the bug `Found`
    // was added to make unrepresentable, still live in the one site 06-06 did
    // not convert. /code-review.
    for (auto f : collectChildren<Fader> (chassis.getFooterBar()))
        if (layout.masterFader.getCentre() == f.bounds.getCentre())
            master = f.control;

    for (auto b : collectChildren<Button> (chassis))
        if (b->getText() == "LIMITER")
            limiter = b.control;

    check (master != nullptr, "the footer carries the MASTER fader");
    check (limiter != nullptr, "and the LIMITER button");

    if (master == nullptr || limiter == nullptr)
        return;

    // ── MASTER: absolute, by the fader's own law ───────────────────────────
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
                              apvts.getParameter (forrobox::ids::master));
        check (parameter != nullptr, "ids::master is a ranged parameter");

        if (parameter == nullptr)
            return;

        GestureCounter counter;
        parameter->addListener (&counter);

        const auto track = master->trackRect();
        const auto quarter = track.getX() + track.getWidth() / 4;
        const auto y = static_cast<float> (master->getLocalBounds().getCentreY());

        const auto e = mouseEventOn (*master, { static_cast<float> (quarter), y });
        master->mouseDown (e);
        master->mouseUp (e);
        settle();

        const auto expected = master->proportionForX (quarter);

        checkEqual (parameter->getValue(), expected,
                    "a click at a quarter of the TRACK writes that proportion to ids::master — "
                    "the fader is absolute, so a click is a drag of length zero");

        checkEqual (counter.begins, 1, "the host sees exactly one gesture begin");
        checkEqual (counter.ends, 1, "and exactly one end");

        parameter->removeListener (&counter);
    }

    // ── LIMITER: one complete gesture, and its lit state is the PARAMETER's ─
    {
        auto* parameter = apvts.getParameter (forrobox::ids::limiterOn);
        check (parameter != nullptr, "ids::limiter_on exists");

        if (parameter == nullptr)
            return;

        GestureCounter counter;
        parameter->addListener (&counter);

        const auto before = parameter->getValue();

        const auto e = mouseEventOn (*limiter,
                                     limiter->getLocalBounds().getCentre().toFloat());
        limiter->mouseDown (e);
        limiter->mouseUp (e);
        settle();

        check (! juce::approximatelyEqual (parameter->getValue(), before),
               "clicking LIMITER toggles ids::limiter_on");
        checkEqual (counter.begins, 1, "one gesture begin");
        checkEqual (counter.ends, 1, "and one end — a click is ONE complete gesture");

        // Driven from OUTSIDE, which is what proves the lit state is read from
        // the parameter rather than kept beside it.
        parameter->setValueNotifyingHost (1.0f);
        settle();
        check (limiter->isOn(), "a host turning the parameter on lights the button");

        parameter->setValueNotifyingHost (0.0f);
        settle();
        check (! limiter->isOn(), "and turning it off unlights it");

        parameter->removeListener (&counter);
    }
}

/** The meter reads the REAL limiter, through the processor's single-reader
    accessor. */
void testGainReductionMeterReadsTheLimiter()
{
    section ("the GR meter reads the real limiter, and is empty when it is off");

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    auto& bar = chassis.getFooterBar();

    GainReductionMeter* meter = nullptr;

    for (auto m : collectChildren<GainReductionMeter> (chassis))
        meter = m.control;

    check (meter != nullptr, "the footer carries a gain-reduction meter");

    if (meter == nullptr)
        return;

    // NOTHING calls settle() from here on. takeGainReductionDb is an exchange,
    // so the footer's own 30 Hz timer is a SECOND reader the moment a message
    // loop is pumped — and it would take the peaks this test is looking for.
    // The refresh is driven directly instead, which is also 04-04's rule about
    // never waiting on a clock.
    auto& apvts = processor.getAPVTS();

    const auto setValue = [&apvts] (juce::StringRef id, float value)
    {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    processor.prepareToPlay (44100.0, 512);

    setValue (forrobox::ids::bpm, 138.0f);
    setValue (forrobox::ids::cachaca, 100.0f);

    for (const auto& info : forrobox::ids::channelInfos)
        setValue (forrobox::ids::channelParam (info.id, forrobox::ids::vol), 100.0f);

    {
        auto state = processor.lockPatternState();

        if (const auto* caruaru = forrobox::findProfile ("caruaru"))
            forrobox::applyProfile (*state, *caruaru);
    }

    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;

    const auto render = [&] (int blocks)
    {
        for (int i = 0; i < blocks; ++i)
        {
            block.clear();
            midi.clear();
            processor.processBlock (block, midi);
        }
    };

    processor.setPlaying (true);
    render (192);

    // ── with the limiter working, the meter moves ──────────────────────────
    bar.refreshFromProcessor (FooterBar::kPollSeconds);

    check (meter->getDisplayedDb() > 0.0f,
           juce::String ("a groove hot enough to limit moves the meter (")
               + juce::String (meter->getDisplayedDb(), 2) + " dB) — the value comes from "
                 "MixBus through the processor's accessor, not from anything the UI invented");

    // ── with LIMITER off, it falls to empty ────────────────────────────────
    setValue (forrobox::ids::limiterOn, 0.0f);
    render (192);

    // One full decay window takes full scale to empty, so one call does it.
    // This said "two polls" over a loop of four, each advancing a whole window
    // rather than a poll interval — three of them were redundant and the
    // comment described neither number. Found by /simplify.
    bar.refreshFromProcessor (grmeter::kDecaySeconds);

    checkEqual (meter->getDisplayedDb(), 0.0f,
                "with LIMITER off the meter falls to empty — the bypassed limiter reduces "
                "nothing, which is the same accessor saying the opposite thing correctly");

    processor.setPlaying (false);
}

/** Every box the footer reserves is inside it, and none overlaps another. */
void testEveryFooterBoxIsReserved()
{
    section ("every box the footer reserves is inside the row and overlaps nothing");

    ChassisRig rig;
    auto& chassis   = rig.chassis;

    const auto& f = chassis.getFooterBar().getLayout();

    const std::array<std::pair<const char*, juce::Rectangle<int>>, 7> boxes {{
        { "the MASTER label", f.masterLabel },
        { "the MASTER fader", f.masterFader },
        { "the LIMITER button", f.limiterButton },
        { "the GR meter", f.grMeter },
        { "DRAG MIDI", f.dragMidi },
        { "the OUTPUT label", f.outputLabel },
        { "the OUTPUT toggle", f.outputToggle },
    }};

    const auto row = chassis.getFooterBar().getLocalBounds();

    for (size_t i = 0; i < boxes.size(); ++i)
    {
        check (! boxes[i].second.isEmpty(),
               juce::String (boxes[i].first) + " has a box");
        check (row.contains (boxes[i].second),
               juce::String (boxes[i].first) + " is inside the footer row");

        for (size_t j = i + 1; j < boxes.size(); ++j)
            check (! boxes[i].second.intersects (boxes[j].second),
                   juce::String (boxes[i].first) + " does not overlap " + boxes[j].first);
    }

    // ── the three auto margins are EQUAL, which is what flexbox does ───────
    //
    // `.drag-midi { margin: 0 auto }` and the OUTPUT group's
    // `margin-left: auto` are three auto margins in one row, and the free space
    // is split equally between them — so DRAG MIDI is NOT centred in the row,
    // which is what PLANNING.md:334 calls it. Asserted because it is exactly
    // the kind of thing that gets "fixed" to a centre later.
    {
        const auto limiterGroupRight = f.limiterGroup.getRight() + footer::kGap;
        const auto beforeDrag = f.dragMidi.getX() - limiterGroupRight;

        // The 2:1 ratio is NOT asserted against forBounds' own subtraction.
        //
        // It was, and it could not fail: one `autoMargin` is computed once and
        // spent as `autoMargin` before and `autoMargin + gap + autoMargin`
        // after, so the difference was identically zero for every input and the
        // "+/- 2 for integer division" tolerance described a rounding that
        // cannot occur. Found by /simplify; the negative control that was
        // supposed to police this (c91) had been detected by a different check
        // entirely, which is what made it look covered.
        //
        // What IS checkable is the consequence the ratio exists for: DRAG MIDI
        // sits right of the centre of the space between the two groups either
        // side of it, because a third of the free space is pushed past it.
        const auto between = juce::Range<int> (limiterGroupRight, f.outputGroup.getX());

        check (beforeDrag > 0,
               "DRAG MIDI is pushed right of the LIMITER group by an auto margin");
        check (f.dragMidi.getCentreX() < between.getStart() + between.getLength() / 2,
               juce::String ("and sits LEFT of the midpoint between the groups either side (")
                   + juce::String (f.dragMidi.getCentreX()) + " against "
                   + juce::String (between.getStart() + between.getLength() / 2)
                   + ") — three auto margins split equally, so an extra third sits after it "
                     "rather than the control being centred");
    }
}

/** The 2.6 s breath — driven to chosen points, never waited for. */
void testDragMidiIdlePulse()
{
    section ("the DRAG MIDI idle pulse breathes on a 2.6 s cycle and stops on hover");

    ForroBoxLookAndFeel lnf;
    DragMidiButton drag (lnf);

    // TOLD its elapsed time, never reading a clock — the law KitOverlay,
    // GainReductionMeter and HitVisualiser all carry, because three 04-04
    // checks failed on MSVC's clock rather than on the code. That is what lets
    // this drive the animation to an exact phase instead of sleeping.
    const auto amountAfter = [&drag] (double seconds)
    {
        drag.advancePulse (seconds);
        return drag.pulseAmount();
    };

    checkEqual (drag.pulseAmount(), 0.0, "the breath starts closed");

    // A quarter of the cycle is halfway up the triangle, and ease-in-out is
    // symmetric about its midpoint, so it reads exactly 0.5.
    checkEqual (amountAfter (dragmidi::kPulseSeconds * 0.25), 0.5,
                "a quarter cycle in, the breath is half open — ease-in-out's midpoint");

    checkEqual (amountAfter (dragmidi::kPulseSeconds * 0.25), 1.0,
                "half a cycle in, the breath is fully open — the keyframes' 50% stop");

    checkEqual (amountAfter (dragmidi::kPulseSeconds * 0.25), 0.5,
                "three quarters in, it is closing again");

    checkEqual (amountAfter (dragmidi::kPulseSeconds * 0.25), 0.0,
                "a full cycle returns to the start — 0%, 100% are the same stop");

    // ── the arrow rides the SAME cycle ────────────────────────────────────
    drag.advancePulse (dragmidi::kPulseSeconds * 0.5);

    checkEqual (drag.pulseAmount() * dragmidi::kArrowBobPx,
                static_cast<double> (dragmidi::kArrowBobPx),
                "at the top of the breath the arrow has bobbed its full 2 px — "
                "css:540 puts it on the same 2.6 s cycle as the glow");

    // ── hover stops it, and HOLDS the phase ───────────────────────────────
    //
    // css:534 and css:541 say `animation: none`, not "restart". A reset would
    // make releasing a hover snap the breath back to the start.
    const auto held = drag.pulseAmount();

    // The real box, asked of the control — not an invented 200x100. Both
    // ternaries here were dead: `drag` is default-constructed above, so its
    // width is always 0.
    const auto metrics = DragMidiButton::metrics();
    drag.setSize (DragMidiButton::boundsForBox ({ metrics.width, metrics.height }).getWidth(),
                  DragMidiButton::boundsForBox ({ metrics.width, metrics.height }).getHeight());
    drag.mouseEnter (mouseEventOn (drag, drag.getLocalBounds().getCentre().toFloat()));

    check (! drag.isPulsing(), "hovering stops the breath — css:534's `animation: none`");

    drag.advancePulse (dragmidi::kPulseSeconds * 0.25);

    checkEqual (drag.pulseAmount(), held,
                "time passing while hovered does not advance the breath, and the phase is "
                "HELD rather than reset so releasing resumes where it was");
}

/** DRAG MIDI exports — and exporting is a READ of the state, never a write. */
void testDragMidiExportsWithoutMutatingState()
{
    section ("DRAG MIDI exports the groove without touching the processor's state");

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    auto& bar = chassis.getFooterBar();

    DragMidiButton* drag = nullptr;

    for (auto d : collectChildren<DragMidiButton> (chassis))
        drag = d.control;

    check (drag != nullptr, "the footer carries a DRAG MIDI button");

    if (drag == nullptr)
        return;

    // ── the glow margin is REAL, and the pointer is kept out of it ─────────
    checkEqual (drag->getBounds().getWidth(),
                bar.getLayout().dragMidi.getWidth() + dragmidi::kGlowMargin * 2,
                "its bounds reserve the hover glow's margin on both sides, because a "
                "Component's paint is clipped to its own bounds");

    // ── how much of that margin the FOOTER ROW actually allows ────────────
    //
    // JUCE clips a child to its own bounds intersected with its PARENT's
    // (Component::paintComponentAndChildren), so a margin wider than the room
    // the bar has is reserved and then cut. Reported rather than asserted at a
    // number: the point is that the figure is measured and written down, the way
    // AC-1's 22 pixels are, instead of a comment claiming the glow "falls
    // outside the button" without saying how far.
    {
        const auto box = bar.getLayout().dragMidi;
        const auto above = box.getY();
        const auto below = bar.getHeight() - box.getBottom();

        check (above > 0 && below > 0,
               juce::String ("the footer row allows ") + juce::String (above) + " px above and "
                   + juce::String (below) + " px below the button, of the "
                   + juce::String (dragmidi::kGlowMargin)
                   + " px its hover glow reserves — the rest is clipped by the 56 px row, which "
                     "the browser does not do");
    }

    check (! drag->hitTest (1, drag->getHeight() / 2),
           "and the reserved margin does NOT take the pointer — hovering the empty space "
           "beside the button must not light it");
    check (drag->hitTest (drag->getWidth() / 2, drag->getHeight() / 2),
           "while the button itself does");

    const auto render = [&] { return renderComponent (chassis, ChassisLayout::kWidth,
                                                      ChassisLayout::kHeight); };

    // In the CHASSIS's coordinates, because that is what `render()` produces.
    // getBounds() is the FOOTER BAR's space, where this button sits at a small
    // y — scanning that rectangle of the chassis image reads the HEADER, which
    // has ink of its own, so the resting-mass check passed while the hover and
    // press comparisons measured pixels the button never touches.
    const auto box = boundsIn (chassis, *drag);

    /** The worst per-pixel difference inside ONE rectangle.

        maxPixelDifference compares whole images, and the whole chassis is not
        the subject here: the GR meter and the two readouts are live, so a
        whole-image comparison would answer "something on screen changed" rather
        than "the button changed". */
    const auto worstIn = [&box] (const juce::Image& a, const juce::Image& b)
    {
        auto worst = 0.0;

        for (int y = box.getY(); y < box.getBottom(); ++y)
            for (int x = box.getX(); x < box.getRight(); ++x)
                worst = juce::jmax (worst, colourDistance (a.getPixelAt (x, y),
                                                            b.getPixelAt (x, y)));

        return worst;
    };

    const auto resting = render();

    // No "it draws something" check: the button paints a tinted gradient over
    // its whole box, so any total-ink measure against the footer's ground is
    // large for a button that drew only its border. The cluster check below
    // subsumes it and can actually fail. Found by /simplify.

    // ── all THREE runs draw, not just one ──────────────────────────────────
    //
    // `restingMass > 0` passes for a button that drew only its border, and AC-4
    // asks for the arrow and BOTH labels. Measured as separated ink clusters
    // across the content strip rather than against the three reserved
    // sub-rectangles, which would be reading the constants the paint is built
    // from.
    {
        // Compared against the button's OWN ground, sampled from a column in
        // its left padding where nothing is drawn — and row by row, because that
        // ground is a vertical gradient. The first version compared against the
        // footer's --raised and found ONE cluster: every column of a tinted
        // gradient differs from the footer, so the whole strip read as inked.
        const auto inset = dragmidi::kGlowMargin + juce::roundToInt (dragmidi::kBorder);
        const auto referenceX = box.getX() + inset + dragmidi::kPadX / 2;

        const auto interior = juce::Rectangle<int>::leftTopRightBottom (
            box.getX() + inset + dragmidi::kPadX,
            box.getY() + inset + 1,
            box.getRight() - inset - dragmidi::kPadX,
            box.getBottom() - inset - 1);

        std::vector<bool> inked;

        for (int x = interior.getX(); x < interior.getRight(); ++x)
        {
            auto column = 0.0;

            for (int y = interior.getY(); y < interior.getBottom(); ++y)
                column += colourDistance (resting.getPixelAt (x, y),
                                          resting.getPixelAt (referenceX, y));

            inked.push_back (column > 0.08);
        }

        auto clusters = 0;

        for (size_t i = 0; i < inked.size(); ++i)
            if (inked[i] && (i == 0 || ! inked[i - 1]))
                ++clusters;

        check (clusters >= 3,
               fbtest::utf8 ("the arrow and BOTH labels draw — ") + juce::String (clusters)
                   + " separated ink clusters across the content strip, and the gap between "
                     "them is the 11px flex gap");
    }

    // ── hover ──────────────────────────────────────────────────────────────
    {
        const auto e = mouseEventOn (*drag, drag->getLocalBounds().getCentre().toFloat());
        drag->mouseEnter (e);

        check (drag->isHovered(), "the pointer entering marks it hovered");

        const auto hovered = render();

        check (worstIn (resting, hovered) > 0.0,
               "and hovering CHANGES what is drawn — the border goes solid accent, the tint "
               "rises to 26% and the glow appears");

        drag->mouseExit (e);
        check (! drag->isHovered(), "and leaving unmarks it");
    }

    // ── press ──────────────────────────────────────────────────────────────
    {
        const auto e = mouseEventOn (*drag, drag->getLocalBounds().getCentre().toFloat());
        drag->mouseDown (e);

        check (drag->isPressed(), "pressing marks it pressed");

        const auto pressed = render();

        check (worstIn (resting, pressed) > 0.0,
               "and the press CHANGES what is drawn — scale(0.98)");

        drag->mouseUp (e);
        check (! drag->isPressed(), "releasing unmarks it");
    }

    // ── the drag writes a real file, and writes NOTHING to the state ──────
    //
    // Until 07-02's close nothing drove `mouseDrag` past the threshold, so the
    // whole write path — render, create the folder, `replaceWithData`, sweep —
    // was reachable only by a human with a DAW. It is the plan's headline
    // deliverable and it had no automated coverage at all.
    {
        juce::MemoryBlock before, after;
        processor.getStateInformation (before);

        const auto exportRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("forrobox");
        const auto filesBefore = exportRoot.findChildFiles (juce::File::findFiles, true, "*.mid")
                                           .size();

        const auto centre = drag->getLocalBounds().getCentre().toFloat();
        const auto e = mouseEventOn (*drag, centre);

        drag->mouseDown (e);
        // A REAL drag: the down position is the centre and the pointer has moved
        // 40x20 from it, so `getDistanceFromDragStart()` is ~44 px and the 8 px
        // threshold is genuinely crossed. Passing one point for both — which is
        // what this read before 07-02 — made the distance zero, so the drag
        // returned at the threshold and this check passed because nothing
        // happened rather than because nothing changed.
        drag->mouseDrag (mouseEventOn (*drag, centre.translated (40.0f, 20.0f), {}, 1, centre));
        drag->mouseUp (e);
        settle();

        processor.getStateInformation (after);

        check (before == after,
               "exporting by drag READS the state and never writes it — the drag crosses "
               "the threshold, renders a file and hands the OS a path, and the processor's "
               "state is byte-identical afterwards");

        const auto written = exportRoot.findChildFiles (juce::File::findFiles, true, "*.mid");

        check (written.size() > filesBefore,
               "and the drag actually WROTE a .mid — the path a human with a DAW was "
               "the only thing exercising until this check existed");

        if (! written.isEmpty())
        {
            const auto newest = written.getLast();

            check (newest.getFileName().startsWith ("forrobox_")
                     && newest.getFileName().endsWith ("bpm.mid"),
                   "named forrobox_<profile>_<bpm>bpm.mid, on disk, not merely returned");

            juce::MemoryBlock onDisk;
            newest.loadFileAsData (onDisk);

            check (onDisk.getSize() > 22 && juce::String (juce::CharPointer_UTF8 (
                       static_cast<const char*> (onDisk.getData()))).startsWith ("MThd"),
                   "and what landed on disk is a Standard MIDI File, not an empty stub");
        }
    }

    {
    }
}

/** A read-only Segmented refuses the pointer.

    Proved on a LOCALLY BUILT control, not on OUTPUT. 04-05 asserted it there
    because OUTPUT was the only read-only Segmented in the plugin; 04-06 makes
    OUTPUT live, and deleting these checks with it would have retired a real
    capability that Phase 6 may want. The capability keeps its test; the control
    gets a different one. */
void testReadOnlySegmentedRefusesThePointer (theme::Mode mode, const juce::String& modeName)
{
    section (fbtest::utf8 ("a read-only Segmented dims, drops the cursor and ignores the pointer — ") + modeName);

    ForroBoxLookAndFeel lnf { mode };

    Ground holder;
    holder.ground = theme::colour (theme::Token::raised, mode);

    Segmented control { lnf, forrobox::outputModeLabels(), type::Style::outToggleLabel,
                        Segmented::Variant::outToggle };

    holder.addAndMakeVisible (control);
    holder.setSize (control.preferredWidth() + 16, control.preferredHeight() + 16);
    control.setBounds (8, 8, control.preferredWidth(), control.preferredHeight());

    auto clicks = 0;
    control.onSegmentClicked = [&clicks] (int) { ++clicks; };

    const auto render = [&] { return renderComponent (holder, holder.getWidth(), holder.getHeight()); };
    const auto other = control.segmentBounds (1);

    // ── live first, so the checks below are known to be reachable ───────────
    {
        const auto e = mouseEventOn (control, other.getCentre().toFloat());
        control.mouseDown (e);
        control.mouseUp (e);

        checkEqual (clicks, 1, modeName + ": while LIVE, a click fires onSegmentClicked");

        const auto before = render();
        control.mouseMove (e);

        check (maxPixelDifference (before, render()) > 0.0,
               modeName + ": and hovering CHANGES what is drawn");

        control.mouseExit (e);
    }

    // ── then read-only ─────────────────────────────────────────────────────
    control.setReadOnly (true);
    clicks = 0;

    // No `check (isReadOnly())` here: one line after `setReadOnly (true)` it can
    // only fail if the setter does not assign its own field. The three below are
    // the ones that mean anything.
    check (std::abs (control.getAlpha() - theme::kReadOnlyAlpha) <= 1.0f / 255.0f,
           modeName + ": and dims it to the read-only alpha");

    {
        const auto e = mouseEventOn (control, other.getCentre().toFloat());
        control.mouseDown (e);
        control.mouseUp (e);

        checkEqual (clicks, 0, modeName + ": a click fires nothing");

        const auto before = render();
        control.mouseMove (e);

        checkEqual (maxPixelDifference (before, render()), 0.0,
                    modeName + ": and hovering lights nothing — a segment that highlighted under "
                               "the pointer would be claiming to be clickable");
    }
}

/** ChoiceAttachment against a parameter with MORE THAN TWO choices.

    The normalised/denormalised trap is INVISIBLE on `output_mode`. Two choices
    means a range of 0..1, so the normalised value and the denormalised index are
    the same number — a control that wrote the normalised one went undetected
    (c120), which is precisely what ChoiceAttachment.cpp's comment predicts would
    "break silently the day a third output mode is added".

    `timbre` has three, so index 2 is denormalised 2.0 and normalised 1.0, and
    the two can finally be told apart. Bound to a bare Segmented rather than to
    the footer's OUTPUT, because the point is the ATTACHMENT's arithmetic. */
void testChoiceAttachmentWritesDenormalised()
{
    section ("a choice click writes the INDEX, not the normalised value");

    ForroBoxAudioProcessor processor;
    ForroBoxLookAndFeel lnf { theme::Mode::dark };

    auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
                          processor.getAPVTS().getParameter (forrobox::ids::timbre));

    check (parameter != nullptr, "ids::timbre is a ranged parameter");

    if (parameter == nullptr)
        return;

    const auto& range = parameter->getNormalisableRange();

    check (range.end >= 2.0f,
           juce::String ("and it has more than two choices (range 0..")
               + juce::String (range.end, 0) + ") — which is what makes the normalised and "
                 "denormalised forms different numbers, and this check able to fail");

    juce::StringArray labels;

    for (const auto& timbre : forrobox::timbreSpecs)
        labels.add (timbre.displayName);

    Segmented control { lnf, labels, type::Style::outToggleLabel,
                        Segmented::Variant::outToggle };

    control.setBounds (0, 0, control.preferredWidth(), control.preferredHeight());

    forrobox::ChoiceAttachment attachment { *parameter, control };

    // The LAST index, where the two forms differ most: denormalised 2.0 against
    // a normalised 1.0 that would land on index 1.
    const auto last = labels.size() - 1;
    const auto box = control.segmentBounds (last);
    const auto e = mouseEventOn (control, box.getCentre().toFloat());

    control.mouseDown (e);
    control.mouseUp (e);
    settle();

    // What a NORMALISED write would land on: index/(count-1) is the normalised
    // position of the last segment, which is 1.0, and denormalising that against
    // a 0..N range gives N... which is the right answer only because the last
    // index IS the range end. Computed for the segment BELOW the last, where the
    // two genuinely differ, so the message names a number that is actually wrong.
    //
    // The first version of this message computed `(last / range.end)` scaled back
    // up, which is identically `last` for any range starting at zero — so it read
    // "sets the parameter to index 2 … would land on index 2". Found by /simplify.
    const auto wrongIndex = juce::roundToInt (
        range.start + (range.end - range.start)
                          * (static_cast<float> (last) / static_cast<float> (labels.size())));

    checkEqual (juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue())), last,
                juce::String ("clicking segment ") + juce::String (last)
                    + " sets the parameter to index " + juce::String (last)
                    + " — setValueAsCompleteGesture takes a DENORMALISED value; writing the "
                      "normalised one would land on index " + juce::String (wrongIndex));

    checkEqual (control.getSelectedIndex(), last,
                "and the lit segment follows, from the PARAMETER rather than from the click");
}

/** OUTPUT drives ids::output_mode, and follows it. */
void testOutputToggleDrivesTheParameter()
{
    section ("OUTPUT drives ids::output_mode, one complete gesture, and follows the host");

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    const auto toggleBox = chassis.getFooterBar().getLayout().outputToggle;

    Segmented* output = nullptr;

    // FROM THE FOOTER, and by the carried bounds — the identical correction the
    // master fader got 600 lines above, in the same bar. `toggleBox` is
    // footer-space; collecting from the chassis made `seg.bounds` chassis-space
    // and threw it away for a hand-rolled `boundsIn`. /simplify found this one
    // after /code-review found the other, which is what a type is for.
    for (auto seg : collectChildren<Segmented> (chassis.getFooterBar()))
        if (toggleBox.contains (seg.bounds.getCentre()))
            output = seg.control;

    check (output != nullptr, "the footer carries the OUTPUT toggle");

    if (output == nullptr)
        return;

    auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
                          processor.getAPVTS().getParameter (forrobox::ids::outputMode));

    check (parameter != nullptr, "output_mode is a ranged parameter");

    if (parameter == nullptr)
        return;

    // ── it is LIVE, which is the thing 04-06 changed ───────────────────────
    check (! output->isReadOnly(),
           "it is no longer read-only — 04-06 put five real buses behind MULTI-OUT");
    checkEqual (output->getAlpha(), 1.0f, "and no longer dimmed");

    checkEqual (output->getSelectedIndex(), 0, "it opens on STEREO, the default");

    // ── a click drives the parameter, as ONE gesture ───────────────────────
    {
        GestureCounter counter;
        parameter->addListener (&counter);

        const auto multi = output->segmentBounds (1);
        const auto e = mouseEventOn (*output, multi.getCentre().toFloat());

        output->mouseDown (e);
        output->mouseUp (e);
        settle();

        checkEqual (output->getSelectedIndex(), 1, "clicking MULTI-OUT lights it");
        checkEqual (juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue())), 1,
                    "and moves ids::output_mode to index 1 — DENORMALISED, which is what "
                    "setValueAsCompleteGesture takes");

        checkEqual (counter.begins, 1, "the host sees exactly one gesture begin");
        checkEqual (counter.ends, 1, "and exactly one end — a click is ONE complete gesture");

        parameter->removeListener (&counter);
    }

    // ── and back ───────────────────────────────────────────────────────────
    {
        const auto stereo = output->segmentBounds (0);
        const auto e = mouseEventOn (*output, stereo.getCentre().toFloat());

        output->mouseDown (e);
        output->mouseUp (e);
        settle();

        checkEqual (output->getSelectedIndex(), 0, "clicking STEREO goes back");
        checkEqual (juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue())), 0,
                    "and the parameter follows");
    }

    // ── it still FOLLOWS the parameter, which is 04-05's regression guard ──
    //
    // The display half must survive the write half arriving: the lit segment
    // comes from the PARAMETER, never from the click, so a host automating it
    // with the editor open still moves it.
    for (const auto target : { 1, 0, 1 })
    {
        parameter->setValueNotifyingHost (
            parameter->convertTo0to1 (static_cast<float> (target)));
        settle();

        checkEqual (output->getSelectedIndex(), target,
                    juce::String ("a HOST moving output_mode to ")
                        + forrobox::ids::outputModes[(size_t) target]
                        + " still moves the lit segment");
    }

    // ── the segment text is still the parameter's own choice list ──────────
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
                           processor.getAPVTS().getParameter (forrobox::ids::outputMode)))
        for (int i = 0; i < juce::jmin (output->getNumSegments(), choice->choices.size()); ++i)
            checkEqual (output->getLabel (i), choice->choices[i],
                        juce::String ("segment ") + juce::String (i)
                            + " reads what the parameter's choice at that index is called");
}


/** Every box the footer reserves carries ink. */
void testEveryFooterBoxIsFilled()
{
    section ("every box the footer reserves carries content");

    ChassisRig rig;
    auto& chassis   = rig.chassis;

    // The meter is empty until something limits, so it is primed here rather
    // than exempted — an empty meter IS its correct resting state, and a box
    // that is allowed to be blank is a box this check cannot police.
    for (auto m : collectChildren<GainReductionMeter> (chassis))
        m->setReductionDb (grmeter::kRangeDb, 0.0f);

    const auto image = renderComponent (chassis, ChassisLayout::kWidth, ChassisLayout::kHeight);

    const auto footerRect = chassis.getLayout().footer;
    const auto& f = chassis.getFooterBar().getLayout();

    // The footer's boxes are in the BAR's coordinates; the render is the
    // chassis's. 04-05's aliasing lesson, applied to a rectangle rather than a
    // component.
    const auto inChassis = [&footerRect] (juce::Rectangle<int> box)
    {
        return box + footerRect.getPosition();
    };

    const std::array<std::pair<const char*, juce::Rectangle<int>>, 7> boxes {{
        { "masterLabel",   f.masterLabel },   { "masterFader",  f.masterFader },
        { "limiterButton", f.limiterButton }, { "grMeter",      f.grMeter },
        { "dragMidi",      f.dragMidi },      { "outputLabel",  f.outputLabel },
        { "outputToggle",  f.outputToggle },
    }};

    for (const auto& [name, localBox] : boxes)
    {
        const auto box = inChassis (localBox);

        // Compared row by row against the footer's own side padding at the same
        // y, the way the header's check is — the footer's ground is flat, but
        // the raised highlight makes its top row differ from the rest.
        auto ink = 0.0;

        for (int y = box.getY(); y < box.getBottom(); ++y)
        {
            const auto rowGround = image.getPixelAt (footerRect.getX() + footer::kPadX / 2, y);

            for (int x = box.getX(); x < box.getRight(); ++x)
                ink += colourDistance (image.getPixelAt (x, y), rowGround);
        }

        const auto perPixel = ink / juce::jmax (1.0, (double) box.getWidth() * box.getHeight());

        check (perPixel > 0.01,
               juce::String (name) + " carries content (" + juce::String (perPixel, 4)
                   + " per pixel)");
    }
}

/** Every box the sequencer reserves, and the gap the region forces. */
void testSequencerLayoutIsReserved()
{
    section ("the sequencer reserves its whole interior, and the row gap is DERIVED");

    ChassisRig rig;

    auto& grid = rig.chassis.getSequencerGrid();
    const auto& l = grid.getLayout();
    const auto region = grid.getLocalBounds();

    checkEqual (region.getHeight(), ChassisLayout::kSequencerHeight,
                "the grid fills the sequencer region");

    // ── the containers contain what they should ────────────────────────────
    //
    // Separated from the pairwise check below, because `head` and each row's
    // `label` are CONTAINERS — asserting them disjoint from their own contents
    // is the check I wrote first, and it failed for the right reason.
    for (const auto& [name, box] : { std::pair<const char*, juce::Rectangle<int>> { "sectionLabel", l.sectionLabel },
                                     { "isolateHint", l.isolateHint },
                                     { "stepsLabel", l.stepsLabel },
                                     { "steps16", l.steps16 },
                                     { "steps32", l.steps32 } })
        check (l.head.contains (box), juce::String (name) + " is inside the head row");

    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
    {
        const auto& row = l.rows[(size_t) i];
        const auto id = juce::String (forrobox::ids::channelInfos[(size_t) i].id);

        check (row.label.contains (row.chip), id + "'s chip is inside its label box");
        check (row.label.contains (row.name), id + "'s name is inside its label box");
        check (row.bounds.contains (row.label), id + "'s label is inside its row");
        check (row.bounds.contains (row.pads), id + "'s pad strip is inside its row");
        check (! row.label.intersects (row.pads), id + "'s label and pads do not overlap");
    }

    // ── every LEAF box is inside the region, non-empty, and overlaps nothing ──
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> boxes {
        { "sectionLabel", l.sectionLabel }, { "isolateHint", l.isolateHint },
        { "stepsLabel", l.stepsLabel }, { "steps16", l.steps16 }, { "steps32", l.steps32 },
    };

    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
    {
        const auto& row = l.rows[(size_t) i];
        const auto id = juce::String (forrobox::ids::channelInfos[(size_t) i].id);

        boxes.push_back ({ id + " chip", row.chip });
        boxes.push_back ({ id + " name", row.name });
        boxes.push_back ({ id + " pads", row.pads });
    }

    for (size_t i = 0; i < boxes.size(); ++i)
    {
        check (! boxes[i].second.isEmpty(), boxes[i].first + " has a box");
        check (region.contains (boxes[i].second), boxes[i].first + " is inside the region");

        for (size_t j = i + 1; j < boxes.size(); ++j)
            check (! boxes[i].second.intersects (boxes[j].second),
                   boxes[i].first + " does not overlap " + boxes[j].first);
    }

    // ── the rows tile, and the gap is what the region left ─────────────────
    //
    // The stylesheet says 7. It does not fit: PLANNING.md gives the region 196
    // and the pads 26, and the five rows need 21 px more than the region leaves.
    // The browser resolves that by compressing the ROW BOXES while each pad
    // keeps its fixed height, and this reproduces the resolution. Settled with
    // the user at planning.
    {
        const auto interior = ChassisLayout::kSequencerHeight - seq::kPadTop - seq::kPadBottom
                            - l.head.getHeight() - seq::kHeadMarginBottom;

        const auto gap = SequencerLayout::rowGap (interior);

        check (gap < seq::kDeclaredRowGap,
               juce::String ("the drawn row gap (") + juce::String (gap)
                   + ") is SMALLER than the stylesheet's " + juce::String (seq::kDeclaredRowGap)
                   + " — the five 26px rows need more than the 196px region leaves, and the pad "
                     "height is the number that survives");

        // The clamp, on an input that actually reaches it. `rowGap` ends in
        // jmax(0, ...) for "a future region change cannot produce a negative
        // stride" — and today's region is comfortably positive, so the check
        // that passed today's number through proved only that today's number is
        // positive. A region too small for the rows is the case the clamp is
        // for. Found by /simplify.
        checkEqual (SequencerLayout::rowGap (ChassisLayout::kNumStrips * pad::kHeight - 40), 0,
                    "a region too small for its rows clamps the gap at zero rather than going "
                    "negative");

        check (gap >= 0, "and today's region is on the positive side of that clamp");

        for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
            checkEqual (l.rows[(size_t) i].bounds.getHeight(), pad::kHeight,
                        juce::String ("row ") + juce::String (i) + " is exactly one pad tall");

        // Consecutive rows are one pad plus one gap apart, every time — which is
        // what proves the stride is uniform rather than the last row absorbing a
        // remainder.
        for (int i = 1; i < ChassisLayout::kNumStrips; ++i)
            checkEqual (l.rows[(size_t) i].bounds.getY() - l.rows[(size_t) i - 1].bounds.getY(),
                        pad::kHeight + gap,
                        juce::String ("row ") + juce::String (i) + " follows row "
                            + juce::String (i - 1) + " by one pad plus one gap");

        // And the whole stack fits, which is the thing the 21px shortfall
        // threatened. Compared against the region, not against a sum of the
        // heights it was built from.
        check (l.rows.back().bounds.getBottom()
                   <= ChassisLayout::kSequencerHeight - seq::kPadBottom,
               "the last row ends inside the region's bottom padding");
    }

    // ── a pad strip tiles exactly, at both step counts ─────────────────────
    for (const auto steps : { 16, 32 })
    {
        const auto strip = l.rows[0].pads;

        auto previousRight = strip.getX();

        for (int i = 0; i < steps; ++i)
        {
            const auto pad = SequencerLayout::padBounds (strip, i, steps);

            check (! pad.isEmpty(), juce::String ("pad ") + juce::String (i) + " of "
                                        + juce::String (steps) + " has a box");
            check (strip.contains (pad), "and is inside the strip");

            if (i > 0)
            {
                const auto gap = pad.getX() - previousRight;

                // 5 or 6, NOT exactly 5. Distributing the remainder is what puts
                // the odd pixel in a gap rather than in the last pad, so a
                // uniform-gap assertion contradicts the property it is checking
                // — which is how this check first failed.
                check (gap == pad::kGap || gap == pad::kGap + 1,
                       juce::String ("pad ") + juce::String (i) + " of " + juce::String (steps)
                           + " sits one gap after the last (" + juce::String (gap) + ")");
            }

            previousRight = pad.getRight();
        }

        // The real tiling invariant: the strip is covered end to end, with no
        // leftover at either edge. Summing the rounded widths is one pixel short
        // by construction and says nothing about where the pads actually sit.
        checkEqual (SequencerLayout::padBounds (strip, 0, steps).getX(), strip.getX(),
                    juce::String ("the first of ") + juce::String (steps)
                        + " pads starts at the strip's left edge");
        checkEqual (previousRight, strip.getRight(),
                    fbtest::utf8 ("and the last ends at its right edge — the remainder is "
                                  "distributed across the gaps, not accumulated in one pad"));
    }
}

/** The pads show the stored pattern, including BATERIA's four-into-one. */
void testGridShowsTheStoredPattern()
{
    section ("a pad shows the velocity that is actually stored");

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    auto& grid = chassis.getSequencerGrid();

    // ── the row -> lane derivation itself ──────────────────────────────────
    {
        auto covered = 0;

        for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
            covered += static_cast<int> (forrobox::lanesForRow (row).size());

        checkEqual (covered, forrobox::State::kNumLanes,
                    "the five rows cover all eight lanes between them, with none left over");

        checkEqual (static_cast<int> (forrobox::lanesForRow (4).size()), 4,
                    "and BATERIA's row covers four of them");

        for (int row = 0; row < 4; ++row)
            checkEqual (static_cast<int> (forrobox::lanesForRow (row).size()), 1,
                        juce::String (forrobox::ids::channelInfos[(size_t) row].id)
                            + "'s row covers exactly one");

        checkEqual (juce::String (forrobox::ids::lanes[(size_t) forrobox::writeLaneForRow (4)]),
                    juce::String ("cx"),
                    "and the BATERIA row WRITES caixa — the collapsed row edits the backbeat");
    }


    // ── a velocity across the full range reaches the pad ────────────────────
    {
        const std::array<int, 4> velocities { 0, 30, 100, 127 };

        {
            auto state = processor.lockPatternState();

            for (auto& lane : state->lanes)
                lane.fill (0);

            for (size_t i = 0; i < velocities.size(); ++i)
                state->lanes[0][i] = static_cast<std::uint8_t> (velocities[i]);
        }

        grid.refreshFromState();

        for (size_t i = 0; i < velocities.size(); ++i)
        {
            auto* pad = grid.padFor (0, static_cast<int> (i));

            check (pad != nullptr, juce::String ("ZABUMBA step ") + juce::String ((int) i)
                                       + " has a pad");

            if (pad != nullptr)
                checkEqual (pad->getVelocity(), velocities[i],
                            juce::String ("and it shows the stored velocity ")
                                + juce::String (velocities[i]));
        }
    }

    // ── BATERIA shows the MAX of its four, and cx is deliberately not it ────
    //
    // The case that makes this able to fail: if the row read caixa alone — the
    // lane it WRITES — it would report 20 where the true maximum is 90.
    {
        {
            auto state = processor.lockPatternState();

            for (auto& lane : state->lanes)
                lane.fill (0);

            const auto kit = forrobox::lanesForRow (4);
            const auto caixa = forrobox::writeLaneForRow (4);

            for (const auto lane : kit)
                state->lanes[(size_t) lane][0] =
                    static_cast<std::uint8_t> (lane == caixa ? 20 : 0);

            // The loudest piece is NOT caixa.
            for (const auto lane : kit)
                if (lane != caixa)
                {
                    state->lanes[(size_t) lane][0] = 90;
                    break;
                }
        }

        grid.refreshFromState();

        auto* pad = grid.padFor (4, 0);
        check (pad != nullptr, "BATERIA step 0 has a pad");

        if (pad != nullptr)
            checkEqual (pad->getVelocity(), 90,
                        "the BATERIA row shows the MAXIMUM of its four sub-lanes (90), not the "
                        "caixa it writes (20) — four lanes collapse into one row");
    }

    // ── WHICH steps carry the beat ring ────────────────────────────────────
    //
    // `app.js:353` — `i % 4 === 0`, so the FIRST step of each group of four,
    // zero-indexed. StepPad's own tests prove the ring is --line-strong and
    // that a lit pad hides it; nothing proved which steps get it, so the rule
    // lived in two files with only the CSS copy checked. An off-by-one would
    // have marked steps 1/5/9/13 — still four evenly spaced rings, still a
    // plausible-looking grid, and silently off the beat.
    //
    // Checked against every row, because setBeat is called inside the pad loop
    // and a row index leaking into the test would pass on row 0 alone.
    {
        for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
        {
            auto rings = 0;

            for (int step = 0; step < grid.getStepCount(); ++step)
            {
                auto* pad = grid.padFor (row, step);

                if (pad == nullptr)
                    continue;

                const auto shouldRing = (step % 4 == 0);

                if (pad->isBeat())
                    ++rings;

                check (pad->isBeat() == shouldRing,
                       juce::String (forrobox::ids::channelInfos[(size_t) row].id) + " step "
                           + juce::String (step) + (shouldRing ? " carries" : " does not carry")
                           + " the beat ring — app.js:353's `i % 4 === 0`");
            }

            checkEqual (rings, grid.getStepCount() / 4,
                        juce::String (forrobox::ids::channelInfos[(size_t) row].id)
                            + "'s row carries one ring per beat and no more");
        }
    }
}

/** AC-3's last line: a 32-step window shows 32 pads, from the SAME stored slots.

    `SequencerLayout::padBounds` was tested at both counts, but nothing built a
    GRID at 32 — so "16 or 32 pads per row, from the same 32 stored slots" was an
    acceptance criterion with no check behind it. Found while reconciling the
    plan at UNIFY.

    The parameter is set BEFORE `attachParameters`, because `rebuildPads` reads
    `ids::steps` once when it builds. That is also the limitation `/code-review`
    recorded: nothing re-reads it afterwards, so a host automating STEPS with the
    editor open is 05-02's to fix. This test pins the half that works. */
void testGridShowsTheFullStepWindow()
{
    section ("a 32-step window shows 32 pads, from the same 32 stored slots");

    ForroBoxAudioProcessor processor;
    ForroBoxLookAndFeel lnf { theme::Mode::dark };
    ValueTooltip tooltip { lnf };
    Chassis chassis { lnf };

    // The 32 choice, by VALUE rather than by index: ids::stepWindows decides the
    // order, and an index would silently select 16 the day it is reversed.
    const auto wide = std::find (forrobox::ids::stepWindows.begin(),
                                 forrobox::ids::stepWindows.end(), 32);

    check (wide != forrobox::ids::stepWindows.end(), "ids::stepWindows offers a 32-step window");

    auto* steps = processor.getAPVTS().getParameter (forrobox::ids::steps);
    check (steps != nullptr, "and ids::steps is a real parameter");

    if (wide == forrobox::ids::stepWindows.end() || steps == nullptr)
        return;

    const auto index = static_cast<int> (std::distance (forrobox::ids::stepWindows.begin(), wide));
    steps->setValueNotifyingHost (steps->convertTo0to1 (static_cast<float> (index)));

    chassis.setBounds (0, 0, ChassisLayout::kWidth, ChassisLayout::kHeight);
    chassis.attachParameters (processor.getAPVTS(), &tooltip);

    auto& grid = chassis.getSequencerGrid();

    checkEqual (grid.getStepCount(), 32, "the grid built the 32-step window");

    // Slot 31 exists in storage at BOTH window sizes — State::kMaxSteps is 32 —
    // so the wide window is showing more of the same lanes, not a second store.
    {
        auto state = processor.lockPatternState();

        for (auto& lane : state->lanes)
            lane.fill (0);

        state->lanes[0][31] = 77;
    }

    grid.refreshFromState();

    auto* last = grid.padFor (0, 31);

    check (last != nullptr, "there is a pad at step 31, which a 16-step window would not have");

    if (last != nullptr)
        checkEqual (last->getVelocity(), 77,
                    "and it shows what is stored in slot 31 — the same 32 slots the narrow window "
                    "shows the first half of");

    // Every row got the wide window, not only the first.
    for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
        check (grid.padFor (row, 31) != nullptr,
               juce::String (forrobox::ids::channelInfos[(size_t) row].id)
                   + "'s row has all 32 pads");
}

/** 05-02: a CLIPPED repaint draws the same pixels as a full one.

    `paintStrip` culls each of its pieces against the clip, because 05-02 gave it
    two things that move at 60 Hz and /simplify measured 85% of its cost as
    furniture redrawn identically — 475 us a frame across five strips, for an
    8 px dot and a 161 px bar.

    Every other render test in this file paints the WHOLE chassis, so the cull
    never fires and a wrong one would be invisible: a check whose subject is
    unreachable given how the test sets up. This one paints through a restricted
    clip and compares, which is the only way a dropped piece shows. */
void testClippedRepaintMatchesFullRepaint()
{
    section ("a clipped repaint draws what a full one draws, inside the clip");

    ForroBoxLookAndFeel lnf { theme::Mode::dark };
    Chassis chassis { lnf };

    chassis.setBounds (0, 0, ChassisLayout::kWidth, ChassisLayout::kHeight);

    const auto full = renderComponent (chassis, ChassisLayout::kWidth, ChassisLayout::kHeight);

    const auto& interior = chassis.getLayout().stripLayouts[0];

    // One box per culled piece, plus a band that crosses several.
    const std::array<std::pair<const char*, juce::Rectangle<int>>, 6> clips {{
        { "head row (both text runs)", interior.headRow },
        { "accent bar and its glow",   interior.accentBar },
        { "trigger LED",               interior.trigLed },
        { "activity meter",            interior.hitVisualiser },
        { "sample slot",               interior.sampleSlot },
        { "ghost label",               interior.ghostLabel },
    }};

    for (const auto& [name, box] : clips)
    {
        const auto region = boundsIn (chassis, chassis).getIntersection (
            box.expanded (12).getIntersection (chassis.getLocalBounds()));

        check (! region.isEmpty(), juce::String (name) + " has a region to clip to");

        if (region.isEmpty())
            continue;

        juce::Image clipped (juce::Image::ARGB, ChassisLayout::kWidth,
                             ChassisLayout::kHeight, true);
        {
            juce::Graphics g (clipped);
            g.reduceClipRegion (region);
            chassis.paintEntireComponent (g, false);
        }

        // Every pixel inside the clip must match the unclipped render. A piece
        // wrongly culled shows here as ground where the full render has ink.
        auto worst = 0;

        for (int y = region.getY(); y < region.getBottom(); ++y)
            for (int x = region.getX(); x < region.getRight(); ++x)
            {
                const auto a = full.getPixelAt (x, y);
                const auto b = clipped.getPixelAt (x, y);

                worst = juce::jmax (worst,
                                    std::abs (a.getRed()   - b.getRed()),
                                    std::abs (a.getGreen() - b.getGreen()),
                                    std::abs (a.getBlue()  - b.getBlue()));
            }

        checkEqual (worst, 0,
                    juce::String ("painting through a clip around the ") + name
                        + " gives the same pixels as painting everything");
    }

    // ── the kit panel, whose text runs are culled the same way ─────────────
    //
    // 05-04 gave `KitOverlay::paintPanel` the same guard for the same measured
    // reason — eleven tracked-text runs re-shaped for a playhead-sized repaint
    // that could not show any of them. A guard on the wrong rectangle culls
    // something that IS in the clip, and nothing else in the suite would see it:
    // every other overlay check renders the whole thing.
    //
    // WHAT THIS CAN AND CANNOT CATCH, established by running the mutants rather
    // than assumed. It catches a guard keyed to a DISTANT box — the copy-paste
    // error, and the one that actually blanks text: keying the last row's full
    // name to the first row's short name fails here by 40 levels. It cannot
    // catch a guard keyed to an ADJACENT box, because `paintPanel` expands each
    // box by a line height before testing it and that absorbs the difference;
    // and it cannot catch a cull that is unconditional, because the reference
    // render is produced by the same code and loses the text too. Those two are
    // performance defects rather than rendering ones, which is the trade the
    // conservative expansion deliberately makes.
    {
        auto& overlay = chassis.getKitOverlay();

        overlay.setOpen (true);
        overlay.advanceEntrance (1.0);

        // The OVERLAY, not the chassis. This test builds a bare chassis with no
        // `attachParameters`, and that is where `addChildComponent (*kitOverlay)`
        // happens — so painting the chassis here paints no overlay at all, in
        // BOTH renders, and every comparison below passed on two blank regions.
        // Three mutants went undetected before this line was right.
        const auto fullOverlay = renderComponent (overlay, ChassisLayout::kWidth,
                                                  ChassisLayout::kHeight);

        const auto& l = overlay.getLayout();

        const std::array<std::pair<const char*, juce::Rectangle<int>>, 4> panelClips {{
            { "panel title",        l.title },
            { "panel sub-line",     l.subLine },
            { "first kit row name", l.rows.front().name },
            { "last kit row name",  l.rows.back().full },
        }};

        for (const auto& [name, box] : panelClips)
        {
            // TIGHT, and each other box must fall OUTSIDE it. An `expanded (6)`
            // clip reached the neighbouring text box, so a guard keyed to the
            // wrong rectangle still passed and two mutants went undetected —
            // culling `.full` by `.name`'s box, and the sub-line by the title's.
            const auto region = box.getIntersection (overlay.getLocalBounds());

            check (! region.isEmpty(), juce::String (name) + " has a region to clip to");

            for (const auto& [otherName, other] : panelClips)
                if (other != box)
                    check (! other.intersects (region),
                           juce::String (otherName) + " is outside the clip around " + name
                               + ", so a guard keyed to it would show here");

            juce::Image clipped (juce::Image::ARGB, ChassisLayout::kWidth,
                                 ChassisLayout::kHeight, true);
            {
                juce::Graphics g (clipped);
                g.reduceClipRegion (region);
                overlay.paintEntireComponent (g, false);
            }

            auto worst = 0;

            for (int y = region.getY(); y < region.getBottom(); ++y)
                for (int x = region.getX(); x < region.getRight(); ++x)
                {
                    const auto a = fullOverlay.getPixelAt (x, y);
                    const auto b = clipped.getPixelAt (x, y);

                    worst = juce::jmax (worst,
                                        std::abs (a.getRed()   - b.getRed()),
                                        std::abs (a.getGreen() - b.getGreen()),
                                        std::abs (a.getBlue()  - b.getBlue()));
                }

            checkEqual (worst, 0,
                        juce::String ("painting through a clip around the ") + name
                            + " gives the same pixels as painting the whole panel");
        }

        overlay.setOpen (false);
    }
}

/** 05-02 AC-2: the playhead sweeps continuously, and its position is the clock's. */
void testPlayheadSweepsTheClocksPosition()
{
    section ("the playhead sweeps the pad strips at the clock's own position");

    ChassisRig rig;

    auto& grid = rig.chassis.getSequencerGrid();
    const auto strip = grid.getLayout().rows.front().pads;

    // ── it passes through each pad's CENTRE, at both window sizes ───────────
    //
    // The case that makes this able to fail: the prototype divides the strip
    // into EQUAL cells (`padGeom.width / state.steps`, app.js:724), which
    // ignores the 5 px gaps. At 16 steps those gaps are 75 px spread across the
    // sweep, so an equal-cell line drifts up to half a pad away from the pad it
    // is over. Comparing against padBounds is what catches that.
    {
        for (const auto steps : { 16, 32 })
        {
            auto worst = 0;

            for (int i = 0; i < steps; ++i)
            {
                const auto expected = SequencerLayout::padBounds (strip, i, steps).getCentreX();
                const auto actual   = forrobox::Playhead::lineCentreFor ((double) i, strip, steps);

                worst = juce::jmax (worst, std::abs (actual - expected));
            }

            checkEqual (worst, 0,
                        juce::String ("at ") + juce::String (steps)
                            + " steps the line sits exactly on every pad's centre");
        }
    }

    // ── it INTERPOLATES between them, which is what makes it continuous ─────
    {
        const auto a = SequencerLayout::padBounds (strip, 3, 16).getCentreX();
        const auto b = SequencerLayout::padBounds (strip, 4, 16).getCentreX();

        const auto half = forrobox::Playhead::lineCentreFor (3.5, strip, 16);

        check (half > a && half < b,
               "a position halfway between two steps puts the line between their centres ("
                   + juce::String (a) + " < " + juce::String (half) + " < " + juce::String (b)
                   + ") — a line that only ever sat on centres would jump, not sweep");

        checkEqual (half, juce::roundToInt ((a + b) / 2.0),
                    "and exactly halfway");
    }

    // ── a NEGATIVE position wraps, rather than landing off the left edge ────
    //
    // Not a hypothetical: the published position IS negative for the first
    // outputDelaySamples() after Play, because the correction pulls it behind
    // the origin — and again after a host loop wrap. std::fmod(-0.5, 16.0) is
    // -0.5, not 15.5, so a raw fmod puts the line off the strip exactly when the
    // user has just pressed play. Found by /code-review.
    {
        const auto atMinusHalf = forrobox::Playhead::lineCentreFor (-0.5, strip, 16);
        const auto atFifteenFive = forrobox::Playhead::lineCentreFor (15.5, strip, 16);

        checkEqual (atMinusHalf, atFifteenFive,
                    "position -0.5 lands where 15.5 does — wrapped the way Clock.cpp:104 wraps it");

        check (strip.contains (atMinusHalf, strip.getCentreY()),
               "and that is inside the strip, not off its left edge");

        checkEqual (forrobox::Playhead::lineCentreFor (-16.0, strip, 16),
                    forrobox::Playhead::lineCentreFor (0.0, strip, 16),
                    "a whole window behind lands on step 0");
    }

    // ── the line spans every row, not only the first ───────────────────────
    {
        const auto rows = grid.rowsArea();
        const auto box  = forrobox::Playhead::boundsForLineAt (strip.getCentreX(), rows);

        check (box.getY() < rows.getY(),
               "the line starts ABOVE the first row — `top: -3px` (css:487)");
        check (box.getBottom() > rows.getBottom(),
               "and ends below the last, so it spans all five rows rather than one");

        for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
            check (box.getVerticalRange().contains (
                       grid.getLayout().rows[(size_t) row].pads.getCentreY()),
                   juce::String (forrobox::ids::channelInfos[(size_t) row].id)
                       + "'s row is inside the line's vertical span");

        // The width is a CONSTANT — trail + line + two glow margins — whatever
        // the position. "wider than the line" compared 49 against 3 and could
        // not fail; this fails if the trail or a glow margin is dropped, which
        // is the thing the message claims to be about.
        checkEqual (box.getWidth(),
                    forrobox::playhead::kLineWidth + forrobox::playhead::kTrailWidth
                        + 2 * forrobox::playhead::kGlowRadius,
                    "the box reserves the trail and both glow margins — a Component's paint is "
                    "clipped to its bounds, so anything outside the line must be inside them");
    }
}

/** 05-02 AC-2: the playhead is driven by the PROCESSOR, and hides when stopped. */
void testPlayheadFollowsTheProcessor()
{
    section ("the playhead reads the processor's position, and hides when stopped");

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    auto& grid = chassis.getSequencerGrid();

    // ── stopped: no line at all ────────────────────────────────────────────
    {
        grid.updatePlayhead();

        check (grid.playheadBounds().isEmpty(),
               "a stopped transport draws no playhead — hidden, not frozen wherever the groove "
               "stopped (css:490 vs css:492)");
    }

    // ── playing: it appears, and it MOVES ──────────────────────────────────
    {
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;

        const auto render = [&] (int blocks)
        {
            for (int i = 0; i < blocks; ++i)
            {
                block.clear();
                midi.clear();
                processor.processBlock (block, midi);
            }
        };

        processor.prepareToPlay (48000.0, 512);
        processor.setPlaying (true);
        render (40);

        grid.updatePlayhead();
        const auto first = grid.playheadBounds();

        check (! first.isEmpty(), "a running transport draws one");

        render (12);
        grid.updatePlayhead();
        const auto second = grid.playheadBounds();

        check (second.getX() != first.getX(),
               "and it MOVES as the groove runs (" + juce::String (first.getX()) + " -> "
                   + juce::String (second.getX()) + ")");

        // The DISPLAY position, not the step index. The case that makes this
        // able to fail: a playhead driven by getCurrentStep() would sit on pad
        // centres and jump, so between two positions inside one step it would
        // not move at all.
        {
            const auto beforeX = grid.playheadBounds().getX();
            render (1);                       // well under one step at 132 BPM
            grid.updatePlayhead();

            check (grid.playheadBounds().getX() != beforeX,
                   "a single block moves it, so it is following the fractional position and not "
                   "the step index — a step-driven line would be still here");
        }

        // ── stopping hides it again ────────────────────────────────────────
        processor.setPlaying (false);
        render (1);
        grid.updatePlayhead();

        check (grid.playheadBounds().isEmpty(), "and stopping hides it again");
    }

    // ── and it is actually PAINTED where the arithmetic says ───────────────
    //
    // Everything above measures rectangles. A component that is positioned
    // correctly and paints nothing passes all of it. Measured in the rendered
    // chassis instead: the accent ink in the line's own column against the same
    // column with the transport stopped.
    {
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;

        processor.prepareToPlay (48000.0, 512);

        const auto stoppedImage = renderComponent (chassis, ChassisLayout::kWidth,
                                                   ChassisLayout::kHeight);

        processor.setPlaying (true);

        for (int i = 0; i < 40; ++i)
        {
            block.clear();
            midi.clear();
            processor.processBlock (block, midi);
        }

        grid.updatePlayhead();

        const auto box = grid.playheadBounds();
        check (! box.isEmpty(), "the line is placed before the render");

        const auto playingImage = renderComponent (chassis, ChassisLayout::kWidth,
                                                   ChassisLayout::kHeight);

        // The line's own column, in CHASSIS coordinates — boundsIn, because
        // Component::getBounds is parent-relative and the sequencer does not sit
        // at the chassis origin. That seam cost 04-05 two silently-wrong tests.
        const auto column = boundsIn (chassis, grid)
                                .getIntersection (box.translated (boundsIn (chassis, grid).getX(),
                                                                  boundsIn (chassis, grid).getY()));

        const auto ground = theme::colour (theme::Token::sunken, theme::Mode::dark);

        const auto lit  = contrastMass (playingImage, column, ground);
        const auto dark = contrastMass (stoppedImage, column, ground);

        check (lit > dark * 1.5 && lit > 1.0,
               "the playhead's column carries real ink while playing and not while stopped ("
                   + juce::String (dark, 2) + " -> " + juce::String (lit, 2) + ")");
    }
}

/** 05-02 AC-3: the level law — max on trigger, x0.82 per FRAME. */
void testHitVisualiserLevelLaw()
{
    section ("the activity level takes the max on a hit and decays 0.82 a frame");

    using forrobox::HitVisualiser;
    namespace hv = forrobox::hitviz;

    // The constant itself, against its source. app.js:253 is the only place
    // 0.82 is decided; four assertions below READ it, and until this line
    // nothing pinned it — the shape 05-01 shipped with kToggleOnVelocity.
    checkEqual ((double) hv::kDecayPerFrame, 0.82,
                "app.js:253 — the level is multiplied by 0.82 a frame, not 0.8");

    // ── max, not assignment ────────────────────────────────────────────────
    {
        HitVisualiser viz { juce::Colours::orange };

        viz.trigger (0.9f);
        viz.trigger (0.2f);

        check (std::abs (viz.getLevel() - 0.9f) < 1.0e-6f,
               "a quieter hit does not pull a louder one's meter down — app.js:237's "
               "Math.max(v.level, vel), which matters because a ghost lands between real hits");
    }

    // ── the decay, told frames rather than reading a clock ─────────────────
    {
        HitVisualiser viz { juce::Colours::orange };
        viz.trigger (1.0f);

        viz.advance (1);
        check (std::abs (viz.getLevel() - 0.82f) < 1.0e-6f, "one frame leaves 0.82");

        viz.advance (4);
        const auto expected = std::pow (0.82f, 5.0f);
        check (std::abs (viz.getLevel() - expected) < 1.0e-5f,
               "and five frames leave 0.82^5 = " + juce::String (expected, 5) + " (got "
                   + juce::String (viz.getLevel(), 5) + ")");
    }

    // ── a SKIPPED frame decays by what it missed, not by one frame ─────────
    //
    // The case that makes advance(n) different from advance(1) called n times
    // only when it is wrong. A busy message thread or a host that throttles an
    // inactive editor drops frames, and a meter that decayed one frame's worth
    // regardless would hang bright.
    {
        HitVisualiser stepped { juce::Colours::orange };
        HitVisualiser jumped  { juce::Colours::orange };

        stepped.trigger (1.0f);
        jumped.trigger (1.0f);

        for (int i = 0; i < 7; ++i)
            stepped.advance (1);

        jumped.advance (7);

        check (std::abs (stepped.getLevel() - jumped.getLevel()) < 1.0e-5f,
               "seven single frames and one seven-frame jump agree ("
                   + juce::String (stepped.getLevel(), 6) + " vs "
                   + juce::String (jumped.getLevel(), 6) + ")");
    }

    // ── the LED's formulas, PLANNING.md:481 ────────────────────────────────
    {
        HitVisualiser viz { juce::Colours::orange };

        // Against the LITERAL, like the three lit-path checks below it.
        // Comparing ledAlpha() to kLedRestingAlpha restates the implementation:
        // the function RETURNS that constant at rest, so the check held whatever
        // the constant became.
        checkEqual ((double) hv::kLedRestingAlpha, 0.22,
                    "css:279 — the LED rests at 0.22 opacity");

        check (std::abs (viz.ledAlpha() - 0.22f) < 1.0e-6f,
               "at rest the LED is 0.22 — dark, but still reading as a lamp");
        checkEqual ((double) viz.ledGlowRadius(), 0.0, "and carries no glow");

        viz.trigger (1.0f);

        check (std::abs (viz.ledAlpha() - 1.0f) < 1.0e-6f,
               "a full-velocity hit takes it to 0.25 + 1 x 0.75 = 1.0");
        check (std::abs (viz.ledGlowRadius() - 9.0f) < 1.0e-6f,
               "with a 2 + 1 x 7 = 9 px glow");

        HitVisualiser half { juce::Colours::orange };
        half.trigger (0.5f);

        check (std::abs (half.ledAlpha() - 0.625f) < 1.0e-6f,
               "and half velocity gives 0.25 + 0.5 x 0.75 = 0.625, so the LED reports HOW HARD "
               "rather than merely that something fired");
    }

    // ── silence is a floor, not an asymptote ───────────────────────────────
    {
        HitVisualiser viz { juce::Colours::orange };
        viz.trigger (1.0f);
        viz.advance (60);

        checkEqual ((double) viz.getLevel(), 0.0,
                    "a second of decay reaches exactly zero rather than a denormal that keeps "
                    "the strip repainting forever");
        check (! viz.isLit(), "and reads as dark");
    }
}

/** 05-02 AC-3: the meter is PAINTED as the law says, measured in the render.

    Everything in testHitVisualiserLevelLaw measures the level. A visualiser that
    computed every formula correctly and painted nothing would pass all of it —
    which is the same gap the playhead's rendered-ink check exists for. */
void testHitVisualiserIsPainted()
{
    section ("the activity meter is painted: the fill follows the level, the ticks are 16");

    namespace hv = forrobox::hitviz;

    ForroBoxLookAndFeel lnf { theme::Mode::dark };
    const auto accent = theme::accent (theme::Accent::zabumba);
    const auto ground = theme::colour (theme::Token::panel, theme::Mode::dark);

    // A meter on a known ground, sized as the strip reserves it.
    struct MeterHolder final : juce::Component
    {
        forrobox::HitVisualiser viz;
        ForroBoxLookAndFeel& lnf;
        juce::Rectangle<int> box;
        juce::Colour ground;

        MeterHolder (juce::Colour accentColour, ForroBoxLookAndFeel& l)
            : viz (accentColour), lnf (l) {}

        void paint (juce::Graphics& g) override
        {
            g.fillAll (ground);
            viz.paintMeter (g, box, lnf);
        }
    };

    constexpr int kWidth = 240;

    const auto render = [&] (float level)
    {
        MeterHolder holder { accent, lnf };
        holder.ground = ground;
        holder.setSize (kWidth + 16, ChassisLayout::kHitVisualiserHeight + 16);
        holder.box = { 8, 8, kWidth, ChassisLayout::kHitVisualiserHeight };

        if (level > 0.0f)
            holder.viz.trigger (level);

        return std::make_pair (renderComponent (holder, holder.getWidth(), holder.getHeight()),
                               holder.box);
    };

    // ── the fill's WIDTH follows the level ─────────────────────────────────
    //
    // Measured as the rightmost column carrying accent ink, not as a mass:
    // a mass rises with both width and opacity, so it could not tell a wider
    // quiet fill from a narrower loud one.
    // Against the WELL, not against the holder's panel: the meter paints
    // `--sunken` across its whole box, so comparing to the panel reports every
    // column as filled. The first version did exactly that and measured the
    // same 239 px at level 0.25 and at 1.0 — a check that could not fail.
    const auto well = theme::colour (theme::Token::sunken, theme::Mode::dark);

    const auto fillRight = [&] (const juce::Image& image, juce::Rectangle<int> box)
    {
        auto rightmost = box.getX();

        for (int x = box.getX(); x < box.getRight(); ++x)
        {
            auto column = 0.0;

            for (int y = box.getY() + 3; y < box.getBottom() - 3; ++y)
            {
                const auto p = image.getPixelAt (x, y);
                column += std::abs (p.getRed()   - well.getRed())
                        + std::abs (p.getGreen() - well.getGreen())
                        + std::abs (p.getBlue()  - well.getBlue());
            }

            if (column > 40.0)
                rightmost = x;
        }

        return rightmost;
    };

    {
        const auto [quarterImage, box] = render (0.25f);
        const auto [fullImage, _]      = render (1.0f);

        const auto quarter = fillRight (quarterImage, box) - box.getX();
        const auto full    = fillRight (fullImage, box) - box.getX();

        check (full > quarter,
               "a louder hit fills more of the meter (" + juce::String (quarter) + " px vs "
                   + juce::String (full) + " px of " + juce::String (kWidth) + ")");

        // scaleX(level): a quarter level fills a quarter of the width. Generous
        // tolerance because the fill fades to 30% alpha at its right end, so its
        // measured edge sits slightly inside its geometric one.
        check (std::abs (quarter - kWidth / 4) < kWidth / 8,
               "and a level of 0.25 fills about a quarter — transform: scaleX(level), css:310");
    }

    // ── the ticks: SIXTEEN divisions, counted in the render ────────────────
    //
    // `repeat(..., calc(100% / 16))` (css:317) is a fixed 16 even when the
    // sequencer shows 32 steps — it reads as a bar ruler, not a step ruler.
    // Deriving it from the step count is exactly the plausible-looking change
    // this counts against.
    {
        // Over a FULL fill, which is what the ticks read against. Over an empty
        // well they are nearly invisible by design: in the dark theme `--bg`
        // (0.078) and `--sunken` (0.059) differ by about one of 255 levels, so
        // 27.5% of one over the other is imperceptible. `.hv-ticks` is
        // `inset: 0` — it overlays the fill, not just the ground.
        const auto [image, box] = render (1.0f);

        // A LOCAL MINIMUM, not any decrease. The fill is a gradient that fades
        // rightward, so brightness falls monotonically across the whole meter —
        // a "darker than the pixel to my left" test counts the gradient itself
        // and reported 22 divisions where there are 15. A tick is one pixel
        // darker than the fill on BOTH sides of it.
        auto runs = 0;

        const auto y = box.getCentreY();

        const auto brightnessAt = [&] (int x)
        {
            return image.getPixelAt (x, y).getBrightness();
        };

        for (int x = box.getX() + 2; x < box.getRight() - 2; ++x)
        {
            const auto here = brightnessAt (x);
            const auto surround = juce::jmin (brightnessAt (x - 2), brightnessAt (x + 2));

            if (here < surround - 0.002f)
                ++runs;
        }

        checkEqual (runs, hv::kTickDivisions - 1,
                    "the meter carries 15 interior tick divisions, making 16 cells — counted in "
                    "the render, not in the arithmetic");
    }
}

/** 05-02 AC-3: a muted or soloed-out channel does not light up. */
void testMutedChannelsDoNotLightUp()
{
    section ("a muted or soloed-out channel's LED and meter stay dark");

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    // Every lane on every step, so nothing depends on which step is current.
    {
        auto state = processor.lockPatternState();

        for (auto& lane : state->lanes)
            lane.fill (110);
    }

    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;

    processor.prepareToPlay (48000.0, 512);

    // INTERLEAVED, because that is what an open editor does: the message thread
    // polls at 60 Hz while the audio thread renders. The first version rendered
    // every block and then polled, and that is not a slow editor — it is an
    // editor that does not exist. The publication is a latest-value snapshot, so
    // a reader that skips forty blocks between looks has genuinely missed the
    // steps in between, and no design can recover them.
    //
    // It mattered: the bulk version passed only because that earlier design
    // fired whatever step it last saw, at roughly the right time. With a real
    // pattern rather than this test's uniformly-filled one, that lights the
    // WRONG channels. Found when /simplify replaced the frame queue with
    // firing on the corrected position.
    const auto runAndSettle = [&] (int blocks, int pollsPerBlock)
    {
        for (int i = 0; i < blocks; ++i)
        {
            block.clear();
            midi.clear();
            processor.processBlock (block, midi);

            // CALLED, never waited for — 04-04's lesson.
            for (int p = 0; p < pollsPerBlock; ++p)
                chassis.pollVisualisersForTest();
        }
    };

    const auto levelOf = [&] (int channel)
    {
        return chassis.hitVisualiserLevelForTest (channel);
    };

    processor.setPlaying (true);

    // ── unmuted: every channel lights ──────────────────────────────────────
    runAndSettle (40, 2);

    auto litChannels = 0;
    for (int c = 0; c < ChassisLayout::kNumStrips; ++c)
        if (levelOf (c) > 0.0f)
            ++litChannels;

    checkEqual (litChannels, ChassisLayout::kNumStrips,
                "with nothing muted, every channel's visualiser lights");

    // ── MUTE zabumba: it goes dark, the others do not ──────────────────────
    {
        auto* mute = processor.getAPVTS().getParameter (
            forrobox::ids::channelParam ("zabumba", forrobox::ids::mute));

        check (mute != nullptr, "zabumba has a mute parameter");

        if (mute != nullptr)
        {
            mute->setValueNotifyingHost (1.0f);

            // Clear what is already glowing, so this measures the GATE and not
            // the tail of hits taken before the mute.
            for (int i = 0; i < 60; ++i)
                chassis.pollVisualisersForTest();

            runAndSettle (40, 2);

            checkEqual ((double) levelOf (0), 0.0,
                        "a MUTED channel does not light up — PLANNING.md:489, and the case that "
                        "makes this able to fail: a visualiser wired straight to the published "
                        "velocities passes every other check in this section");

            auto othersLit = 0;
            for (int c = 1; c < ChassisLayout::kNumStrips; ++c)
                if (levelOf (c) > 0.0f)
                    ++othersLit;

            checkEqual (othersLit, ChassisLayout::kNumStrips - 1,
                        "and the other four still do, so the gate is per channel rather than "
                        "global");

            mute->setValueNotifyingHost (0.0f);
        }
    }

    // ── SOLO triângulo: everything else goes dark ──────────────────────────
    {
        auto* solo = processor.getAPVTS().getParameter (
            forrobox::ids::channelParam ("triangulo", forrobox::ids::solo));

        // ASSERTED, not merely guarded. The mute block above checks its
        // parameter resolved and this one copied the `if` without the `check` —
        // so if the id ever stopped resolving, four assertions including the one
        // proving the gate uses the engine's own resolver would vanish with a
        // green result. Found by /simplify.
        check (solo != nullptr, "triângulo has a solo parameter");

        if (solo != nullptr)
        {
            solo->setValueNotifyingHost (1.0f);

            for (int i = 0; i < 60; ++i)
                chassis.pollVisualisersForTest();

            runAndSettle (40, 2);

            check (levelOf (1) > 0.0f, "the SOLOED channel lights");

            auto othersLit = 0;
            for (int c = 0; c < ChassisLayout::kNumStrips; ++c)
                if (c != 1 && levelOf (c) > 0.0f)
                    ++othersLit;

            checkEqual (othersLit, 0,
                        "and every soloed-OUT channel stays dark — the same resolution the engine "
                        "renders with, not a second answer written in the UI");

            solo->setValueNotifyingHost (0.0f);
        }
    }

    // ── the LEDs fire on the CORRECTED timeline, not the raw publication ───
    //
    // The whole reason this is not driven by getCurrentStep(). The playhead is
    // pulled back by the plugin's output delay so the sweep matches what is
    // heard; an LED fired on the raw publication flashes ~0.26 of a step ahead
    // of the line that is supposed to be reaching it.
    //
    // Nothing pinned it until now: replacing the corrected position with
    // getCurrentStep() failed ZERO checks. Found by a negative control.
    {
        processor.setPlaying (true);

        // Render one block at a time until the two timelines DISAGREE — they
        // differ for about a quarter of each step, so this lands quickly. A
        // test that asserted while they agreed could not tell them apart.
        auto found = false;

        for (int i = 0; i < 200 && ! found; ++i)
        {
            block.clear();
            midi.clear();
            processor.processBlock (block, midi);
            chassis.pollVisualisersForTest();

            const auto raw = processor.getCurrentStep();
            const auto fired = chassis.lastFiredStepForTest();

            if (raw != fired && raw >= 0 && fired >= 0)
            {
                found = true;

                checkEqual (fired, (raw + ChassisLayout::kNumStrips * 0 + 16 - 1) % 16,
                            "when the two timelines differ the LEDs are showing the step BEHIND "
                            "the published one — the corrected position, which is what the "
                            "playhead shows (published " + juce::String (raw) + ", fired "
                                + juce::String (fired) + ")");
            }
        }

        check (found,
               "the raw and corrected timelines were observed disagreeing at all — if they never "
               "did, this check could not tell which one drives the LEDs");
    }

    // ── stopping clears them ───────────────────────────────────────────────
    {
        processor.setPlaying (false);

        block.clear();
        midi.clear();
        processor.processBlock (block, midi);
        chassis.pollVisualisersForTest();

        auto anyLit = 0;
        for (int c = 0; c < ChassisLayout::kNumStrips; ++c)
            if (levelOf (c) > 0.0f)
                ++anyLit;

        checkEqual (anyLit, 0, "a stopped transport leaves every visualiser dark");
    }
}

/** Select a step window by VALUE, with the reachability check in one place.

    Six sites spelled out find / distance / convertTo0to1 / setValueNotifyingHost
    with their own guard, and one of them guarded WITHOUT a `check` — so if
    `ids::steps` were ever renamed that block would report success having
    asserted nothing. A silent skip is the shape this project keeps finding, and
    a helper that cannot be written without its check removes it. /simplify. */
void selectStepWindow (ForroBoxAudioProcessor& processor, int window)
{
    const auto it = std::find (forrobox::ids::stepWindows.begin(),
                               forrobox::ids::stepWindows.end(), window);

    auto* steps = processor.getAPVTS().getParameter (forrobox::ids::steps);

    check (steps != nullptr && it != forrobox::ids::stepWindows.end(),
           juce::String ("the ") + juce::String (window) + "-step window is reachable");

    if (steps == nullptr || it == forrobox::ids::stepWindows.end())
        return;

    steps->setValueNotifyingHost (steps->convertTo0to1 (
        static_cast<float> (std::distance (forrobox::ids::stepWindows.begin(), it))));
}

/** 05-03 AC-1/AC-2: the grid follows writers other than itself. */
void testGridFollowsExternalWriters()
{
    section ("the grid shows what the pattern IS, whoever wrote it");

    ChassisRig rig;
    auto& processor = rig.processor;

    auto& grid = rig.chassis.getSequencerGrid();

    // ── a write through the handle, by anyone ──────────────────────────────
    //
    // The shape 05-01 shipped without: refreshFromState ran on attach and after
    // toggleCell, and nothing else.
    {
        {
            auto state = processor.lockPatternState();

            for (auto& lane : state->lanes)
                lane.fill (0);

            state->lanes[0][2] = 88;
        }

        // CALLED, never waited for.
        grid.refreshIfStateChanged();

        auto* pad = grid.padFor (0, 2);
        check (pad != nullptr, "ZABUMBA step 2 has a pad");

        if (pad != nullptr)
            checkEqual (pad->getVelocity(), 88,
                        "a write through the state handle reaches the pads with no user action");
    }

    // ── a HOST RECALL, which is the path /code-review named ────────────────
    {
        // A donor carrying a different pattern, serialised the way a host does.
        juce::MemoryBlock blob;
        {
            ForroBoxAudioProcessor donor;

            {
                auto state = donor.lockPatternState();

                for (auto& lane : state->lanes)
                    lane.fill (0);

                state->lanes[0][5] = 119;
            }

            donor.getStateInformation (blob);
        }

        check (blob.getSize() > 0, "the donor produced state to recall");

        processor.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));

        grid.refreshIfStateChanged();

        auto* recalled = grid.padFor (0, 5);
        auto* cleared  = grid.padFor (0, 2);

        check (recalled != nullptr && cleared != nullptr, "both pads exist after the recall");

        if (recalled != nullptr && cleared != nullptr)
        {
            checkEqual (recalled->getVelocity(), 119,
                        "a host recall repaints the grid WITHOUT the user clicking a pad — the bug "
                        "05-01 shipped, where the editor kept showing the previous pattern");
            checkEqual (cleared->getVelocity(), 0,
                        "and the pattern it replaced is gone, rather than the two being merged");
        }
    }

    // A read not counting as a change is asserted in StateRoundTripTest, where
    // its subject lives — twenty acquisitions, and that they cost the audio
    // thread no copy either. It stood here too, weaker and against a processor
    // this test's grid is not involved with. /simplify.

    // ── a STEP-COUNT change rebuilds, not merely refreshes ─────────────────
    {
        checkEqual (grid.getStepCount(), 16, "the grid starts at the narrow window");
        check (grid.padFor (0, 31) == nullptr, "and has no pad at step 31");

        selectStepWindow (processor, 32);
        grid.refreshIfStateChanged();

        {
            checkEqual (grid.getStepCount(), 32,
                        "a parameter change REBUILDS the grid — 05-01 snapshotted the step count "
                        "once, so an automation left steps 16-31 invisible behind a clock already "
                        "playing them");

            check (grid.padFor (0, 31) != nullptr,
                   "and step 31 now has a pad, so the window is editable rather than merely wider");
        }
    }
}

/** 05-03: a write that lands DURING a refresh is not lost.

    `publishIfChanged` runs inside `~LockedState` while the lock is still held,
    so the generation advances at a moment the refresh cannot see if it records
    the count after releasing. A host loading a project from its own thread — the
    case this plan targets — lands exactly there: the grid paints the OLD pattern
    and records the NEW generation, and the next tick sees no change and never
    refreshes. The 05-01 bug in a narrower window.

    Threaded because it has to be: no single-threaded test can place a write
    between the snapshot and the record, which is why the first three negative
    controls for this fix reported nothing. */
void testRefreshDoesNotLoseAConcurrentWrite()
{
    section ("a write landing during a refresh is not lost");

    ChassisRig rig;
    auto& processor = rig.processor;

    auto& grid = rig.chassis.getSequencerGrid();

    // MANY INDEPENDENT TRIALS, not one long race.
    //
    // The first version ran a writer flat out and checked only the FINAL value,
    // so the bug showed only when the last write happened to land mid-refresh —
    // measured at 1 detection in 10 runs against a deliberately broken build. A
    // guard that misses the bug nine times out of ten is not a guard, which is
    // the same lesson 05-02's tear test learned at 7 in 10.
    //
    // Each trial releases exactly one write while this thread is refreshing,
    // then checks. Every trial is its own chance to catch it.
    // WHAT THIS CAN AND CANNOT SEE, measured at 06-01 rather than assumed. It
    // detects the real 05-03 shape — the generation recorded AFTER the pad loop,
    // which leaves a whole 160-pad paint for a writer to land in — at 113 of
    // 1500 trials. It does NOT detect a generation re-read immediately after the
    // lock releases: that window is nanoseconds, not a paint loop, and no
    // trial count this suite can afford would reach it. The ordering is
    // therefore held by `snapshotPattern` being the only way to take a snapshot,
    // which is why that function exists at all.
    constexpr int kTrials = 1500;

    std::atomic<int> value { 0 };
    std::atomic<bool> go { false };
    std::atomic<bool> done { false };
    std::atomic<bool> running { true };

    std::thread writer ([&]
    {
        while (running.load (std::memory_order_acquire))
        {
            while (! go.exchange (false, std::memory_order_acquire))
            {
                if (! running.load (std::memory_order_acquire))
                    return;

                std::this_thread::yield();
            }

            // A BURST, not one write. The window only exists inside an actual
            // refresh, and a refresh only happens after a generation change —
            // so the write that lands mid-refresh has to be the SECOND one,
            // arriving while the first one's refresh is still painting.
            //
            // One write per trial was measured at 0 detections in 10 runs
            // against a broken build, because there was never a second write to
            // land inside the first one's refresh.
            const auto target = value.load (std::memory_order_relaxed);

            // And each write changes MANY pads, not one. setVelocity early-outs
            // on no change, so a one-slot write leaves the refresh doing 160
            // integer compares in microseconds — a window too narrow to land in.
            // Touching every lane makes the refresh actually repaint, which is
            // what the real case (a host restoring a whole pattern) does.
            for (int i = 0; i < 3; ++i)
            {
                auto state = processor.lockPatternState();

                for (auto& lane : state->lanes)
                    for (int step = 0; step < forrobox::State::kMaxSteps; ++step)
                        lane[(size_t) step] = static_cast<std::uint8_t> ((target - 2 + i + step) % 100);
            }

            done.store (true, std::memory_order_release);
        }
    });

    auto stale = 0;

    for (int trial = 1; trial <= kTrials; ++trial)
    {
        value.store (3 + (trial % 100), std::memory_order_relaxed);
        done.store (false, std::memory_order_relaxed);
        go.store (true, std::memory_order_release);

        // Refresh WHILE the write lands.
        while (! done.load (std::memory_order_acquire))
            grid.refreshIfStateChanged();

        // Quiet now: one more poll must bring the grid level with the state.
        grid.refreshIfStateChanged();

        const auto expected = static_cast<int> (processor.lockPatternState()->lanes[0][0]);

        if (auto* p = grid.padFor (0, 0); p != nullptr && p->getVelocity() != expected)
            ++stale;
    }

    running.store (false, std::memory_order_release);
    go.store (true, std::memory_order_release);
    writer.join();

    grid.refreshIfStateChanged();

    const auto expected = static_cast<int> (processor.lockPatternState()->lanes[0][0]);

    auto* pad = grid.padFor (0, 0);
    check (pad != nullptr, "the pad exists");

    checkEqual (stale, 0,
                juce::String ("no trial of ") + juce::String (kTrials) + " left the grid showing a "
                "stale pattern with nothing in flight — recording the generation outside the lock "
                "lets a write be painted as old and counted as seen");

    if (pad != nullptr)
        checkEqual (pad->getVelocity(), expected,
                    "after the last write and one more poll the grid shows it — recording the "
                    "generation outside the lock would let the final write be painted as old and "
                    "counted as seen");
}

/** 05-03 AC-3: the step window tiles the pattern, with no editor in existence. */
void testStepChangeTilesWithoutAnEditor()
{
    section ("widening the step window tiles the pattern, whether or not a window is open");

    // ── the law itself, driven without a processor ─────────────────────────
    {
        forrobox::State state;

        for (auto& lane : state.lanes)
            lane.fill (0);

        // A pattern distinguishable from whatever is above it, and an upper half
        // deliberately WRONG — so "it tiled" cannot be confused with "it was
        // already like that", which is the shape that would make this pass on a
        // no-op implementation.
        state.lanes[0][1] = 90;
        state.lanes[0][6] = 40;
        state.lanes[0][17] = 123;
        state.lanes[0][22] = 7;

        state.tileToFullWidth();

        checkEqual (static_cast<int> (state.lanes[0][17]), 90,
                    "slot 17 repeats slot 1 — PLANNING.md:606's newArray[i] = oldArray[i % old]");
        checkEqual (static_cast<int> (state.lanes[0][22]), 40, "and slot 22 repeats slot 6");
        checkEqual (static_cast<int> (state.lanes[0][16]), 0,
                    "and a slot whose source is empty is CLEARED rather than left stale — the "
                    "upper half is a copy, not a merge");

        // Every lane, not only the first.
        forrobox::State all;
        for (size_t lane = 0; lane < all.lanes.size(); ++lane)
            all.lanes[lane][3] = static_cast<std::uint8_t> (11 * (lane + 1));

        all.tileToFullWidth();

        auto tiled = 0;
        for (size_t lane = 0; lane < all.lanes.size(); ++lane)
            if (all.lanes[lane][19] == static_cast<std::uint8_t> (11 * (lane + 1)))
                ++tiled;

        checkEqual (tiled, static_cast<int> (all.lanes.size()),
                    "all eight lanes tile, including the four bateria sub-lanes — PLANNING.md:608");
    }

    // ── through the PARAMETER, with NO editor constructed ──────────────────
    //
    // The case that separates processor-owned tiling from UI-owned. A UI-owned
    // implementation cannot pass this: there is no UI.
    {
        ForroBoxAudioProcessor processor;

        {
            auto state = processor.lockPatternState();

            for (auto& lane : state->lanes)
                lane.fill (0);

            state->lanes[0][2] = 77;
            state->lanes[0][18] = 5;    // stale, must be overwritten
        }

        selectStepWindow (processor, 32);

        {
            // Drained directly rather than waited for — 04-04's lesson. The
            // plugin's own drain is a 30 Hz timer.
            processor.applyPendingStepChange();

            checkEqual (static_cast<int> (processor.lockPatternState()->lanes[0][18]), 77,
                        "a step change made through the PARAMETER tiles the pattern with no editor "
                        "in existence — the property that makes host automation and a click "
                        "produce the same groove");
        }
    }

    // ── narrowing tiles NOTHING, and that is pinned rather than assumed ────
    {
        ForroBoxAudioProcessor processor;

        {
            selectStepWindow (processor, 32);
            processor.applyPendingStepChange();

            {
                auto state = processor.lockPatternState();

                for (auto& lane : state->lanes)
                    lane.fill (0);

                state->lanes[0][20] = 64;   // only reachable in the wide window
            }

            selectStepWindow (processor, 16);
            processor.applyPendingStepChange();

            checkEqual (static_cast<int> (processor.lockPatternState()->lanes[0][20]), 64,
                        "narrowing leaves the upper half INTACT — our storage is always 32 slots "
                        "with `steps` as a view, so truncating would destroy work that a later "
                        "widening tiles over anyway");
        }
    }
}

/** 05-03 AC-4: the STEPS buttons are the prototype's, and follow the parameter. */
void testStepsButtonsFollowTheParameter()
{
    section ("the STEPS buttons sit in their reserved boxes and light from the parameter");

    ChassisRig rig;

    auto& grid = rig.chassis.getSequencerGrid();
    const auto& layout = grid.getLayout();

    const auto buttons = collectChildren<Button> (grid);

    checkEqual (static_cast<int> (buttons.size()),
                static_cast<int> (forrobox::ids::stepWindows.size()),
                "the sequencer holds exactly one button per step window");

    // ── in the boxes 05-01 reserved, which sat empty until now ─────────────
    //
    // By INDEX, not by reverse-engineering identity from geometry.
    // `collectChildren` returns the grid's only Buttons in creation order, which
    // is `ids::stepWindows` order.
    {
        const std::array<juce::Rectangle<int>, 2> boxes { layout.steps16, layout.steps32 };

        for (size_t i = 0; i < buttons.size() && i < boxes.size(); ++i)
            check (buttons[i].bounds == boxes[i],
                   juce::String ("the ") + juce::String (forrobox::ids::stepWindows[i])
                       + " button sits exactly in its reserved box — not near it, IN it");
    }

    // ── the lit one follows the PARAMETER, not the click ───────────────────
    //
    // 04-04 found that failure twice in one plan and it is a recorded project
    // decision: a read-only control still needs the display half. Driven by
    // setting the parameter from outside, which no click can be confused with.
    //
    // Asserted by INDEX for the same reason as above. A `litLabel` helper stood
    // here returning `bounds == steps16 ? 16 : 32` — which reports 32 for any
    // bounds that is not steps16, INCLUDING nowhere and on top of the other
    // button. Half of it could not fail, which is a measurement instrument that
    // cannot report the difference it exists to measure. /simplify.
    {
        for (size_t want = 0; want < forrobox::ids::stepWindows.size(); ++want)
        {
            selectStepWindow (rig.processor, forrobox::ids::stepWindows[want]);

            auto lit = 0;
            auto litIndex = -1;

            for (size_t i = 0; i < buttons.size(); ++i)
                if (buttons[i]->isOn())
                {
                    ++lit;
                    litIndex = static_cast<int> (i);
                }

            checkEqual (lit, 1,
                        "exactly one button is lit — a choice expressed as two toggles could "
                        "light both or neither");
            checkEqual (litIndex, static_cast<int> (want),
                        juce::String ("and it is the ")
                            + juce::String (forrobox::ids::stepWindows[want])
                            + " one, set from the PARAMETER with no click involved");
        }
    }
}

/** 05-04: the entrance easing is the curve css:565 names, not a lookalike.

    `cubicBezierEase`'s own docstring said it was "public so a test can check it
    against the control points… a smoothstep would look plausible and be a
    different curve" — and that test did not exist, so the docstring described an
    intention rather than the suite. /code-review. */
void testEntranceEasingIsTheSpecCurve()
{
    section ("the entrance easing is cubic-bezier(.2,.7,.3,1), not a lookalike");

    using forrobox::kitEntranceEase;

    checkEqual (kitEntranceEase (0.0), 0.0, "it starts at 0");
    check (std::abs (kitEntranceEase (1.0) - 1.0) < 1.0e-9, "and ends at 1");

    // Monotonic — an easing that went backwards would make the panel jitter.
    {
        auto descents = 0;
        auto previous = -1.0;

        for (int i = 0; i <= 200; ++i)
        {
            const auto value = kitEntranceEase (i / 200.0);

            if (value < previous - 1.0e-12)
                ++descents;

            previous = value;
        }

        checkEqual (descents, 0, "and never goes backwards");
    }

    // Against the curve evaluated INDEPENDENTLY from the control points. The
    // implementation bisects; this samples the parametric form directly, so the
    // two agree only if both describe the same curve.
    {
        const auto independent = [] (double t)
        {
            const auto axis = [] (double a, double b, double s)
            {
                const auto u = 1.0 - s;
                return 3.0 * u * u * s * a + 3.0 * u * s * s * b + s * s * s;
            };

            auto best = 0.0, bestError = 2.0;

            for (int i = 0; i <= 20000; ++i)
            {
                const auto s = i / 20000.0;
                const auto error = std::abs (axis (0.2, 0.3, s) - t);

                if (error < bestError)
                {
                    bestError = error;
                    best = axis (0.7, 1.0, s);
                }
            }

            return best;
        };

        auto worst = 0.0;

        for (const auto t : { 0.1, 0.25, 0.5, 0.75, 0.9 })
            worst = juce::jmax (worst, std::abs (kitEntranceEase (t) - independent (t)));

        check (worst < 1.0e-3,
               "and matches the curve sampled independently from its control points (worst "
                   + juce::String (worst, 8) + ")");
    }

    // The case that makes this able to fail: a smoothstep is monotonic, starts
    // at 0, ends at 1, and IS A DIFFERENT CURVE. Without this, every check above
    // passes on the wrong easing.
    {
        auto worst = 0.0;

        for (const auto t : { 0.1, 0.25, 0.5, 0.75, 0.9 })
        {
            const auto smoothstep = t * t * (3.0 - 2.0 * t);
            worst = juce::jmax (worst, std::abs (kitEntranceEase (t) - smoothstep));
        }

        check (worst > 0.05,
               "and is measurably NOT a smoothstep, which would satisfy every other check here "
               "(max divergence " + juce::String (worst, 4) + ")");
    }
}

/** 05-04 AC-1/AC-2: the kit overlay edits four lanes the collapsed row cannot. */
void testKitOverlayEditsFourLanes()
{
    section ("the bateria kit overlay edits BB / CX / HH / TOM individually");

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    auto& overlay = chassis.getKitOverlay();
    auto& grid = chassis.getSequencerGrid();

    // ── the trigger: the BATERIA strip's sub-dots row ───────────────────────
    {
        check (! overlay.isVisible(), "the overlay starts hidden");

        const auto& interior = chassis.getLayout().stripLayouts[ChassisLayout::kNumStrips - 1];

        check (! interior.subDots.isEmpty(), "the bateria strip reserves a sub-dots row");

        // VISIBLE, because `getComponentAt` is a real hit-test and JUCE will not
        // route through a component whose visible flag is false. These rigs
        // never needed it while the chassis handled clicks itself — headless
        // renders go through `paintEntireComponent`, which does not care — and
        // `testRightClickChangesNothingAnywhere` already does exactly this for
        // exactly this reason.
        chassis.setVisible (true);

        check (dynamic_cast<forrobox::HitZone*> (
                   clickInside (chassis, interior.subDots.getCentre())) != nullptr,
               "the sub-dots row carries a HitZone a real click reaches");

        check (overlay.isVisible(), "clicking it opens the kit panel — PLANNING.md:518");
    }

    overlay.advanceEntrance (1.0);   // straight to rest; the animation is AC-3's

    // ── the panel's geometry ───────────────────────────────────────────────
    {
        const auto& l = overlay.getLayout();

        checkEqual (l.scrim.getWidth(), ChassisLayout::kWidth,
                    "the scrim covers the WHOLE chassis — app.js:37 appends the subview to the "
                    "window and css:554 is inset:0, which settles PLANNING.md:519's narrower prose");
        checkEqual (l.scrim.getHeight(), ChassisLayout::kHeight, "in both directions");

        checkEqual (l.panel.getWidth(), forrobox::kit::kPanelWidth, "the panel is 620 px — css:561");
        checkEqual (l.panel.getRight(), ChassisLayout::kWidth, "and right-aligned — css:558");

        checkEqual (static_cast<int> (l.rows.size()), 4, "four kit rows");

        for (const auto& row : l.rows)
            checkEqual (row.pads.getHeight(), forrobox::kit::kPadHeight,
                        "each row's pads are 26 px tall — css:583, taller than the grid's");
    }

    // ── FOUR LANES, edited individually ────────────────────────────────────
    //
    // The case that separates this from 05-01's collapsed row, which writes
    // caixa whatever you click. A version that wrote caixa from every kit row
    // would pass a "the pattern changed" check and fail this one.
    {
        {
            auto state = processor.lockPatternState();

            for (auto& lane : state->lanes)
                lane.fill (0);
        }

        overlay.refreshFromState();

        const auto& kitLanes = forrobox::lanesForRow (ChassisLayout::kNumStrips - 1);

        checkEqual (kitLanes.size(), 4, "the bateria row covers four lanes");

        for (int row = 0; row < 4; ++row)
        {
            auto* pad = overlay.padFor (row, row);   // a different step per row
            check (pad != nullptr, juce::String ("kit row ") + juce::String (row) + " has a pad");

            if (pad == nullptr)
                continue;

            pad->onClick();
        }

        auto state = processor.lockPatternState();

        auto wrote = 0;

        for (int row = 0; row < 4; ++row)
        {
            const auto lane = kitLanes.entries[(size_t) row];

            if (state->lanes[(size_t) lane][(size_t) row] == forrobox::seq::kToggleOnVelocity)
                ++wrote;
        }

        checkEqual (wrote, 4,
                    "each kit row wrote ITS OWN lane — the deep edit app.js:389 defers to, and the "
                    "four lanes 05-01 could show but never reach");

        // And nothing else moved: a row that wrote caixa four times would leave
        // caixa with four hits and the other three lanes empty.
        const auto caixa = forrobox::writeLaneForRow (ChassisLayout::kNumStrips - 1);

        auto caixaHits = 0;
        for (int step = 0; step < forrobox::State::kMaxSteps; ++step)
            if (state->lanes[(size_t) caixa][(size_t) step] > 0)
                ++caixaHits;

        checkEqual (caixaHits, 1,
                    "and caixa carries exactly ONE of them, not all four — the collapsed row's "
                    "write and the kit's are different operations");
    }

    // ── the main BATERIA row shows the max of the four ─────────────────────
    {
        {
            auto state = processor.lockPatternState();

            for (auto& lane : state->lanes)
                lane.fill (0);

            const auto& kitLanes = forrobox::lanesForRow (ChassisLayout::kNumStrips - 1);

            // A loud hit on a lane that is NOT caixa, at a step caixa is silent.
            for (const auto lane : kitLanes)
                if (lane != forrobox::writeLaneForRow (ChassisLayout::kNumStrips - 1))
                {
                    state->lanes[(size_t) lane][7] = 111;
                    break;
                }
        }

        grid.refreshFromState();

        auto* mainPad = grid.padFor (ChassisLayout::kNumStrips - 1, 7);
        check (mainPad != nullptr, "the main BATERIA row has a pad at step 7");

        if (mainPad != nullptr)
            checkEqual (mainPad->getVelocity(), 111,
                        "the collapsed row shows the kit's loudest piece, not caixa's silence");
    }

    // ── it is IN FRONT of everything, and takes the mouse first ────────────
    //
    // It was not. `addChildComponent` appends to the FRONT, and attachParameters
    // adds the overlay near the top and then ~50 strip controls after it — so
    // every knob, button and fader painted over the scrim, strips 4 and 5
    // painted inside the panel, and all of them still took clicks. A modal
    // overlay you could drag a knob through. Found by /code-review; nothing here
    // could see it, because no test asked about z-order.
    {
        check (overlay.isAlwaysOnTop(),
               "the overlay is always-on-top — z-order as a property of the component, not a "
               "consequence of the order attachParameters happens to add things in");

        const auto& children = chassis.getChildren();
        const auto index = children.indexOf (&overlay);

        check (index >= 0, "the overlay is a child of the chassis");

        auto behind = 0;

        for (auto* child : children)
            if (child != &overlay && children.indexOf (child) > index)
                ++behind;

        checkEqual (behind, 0,
                    "and nothing is in front of it — with ~50 strip controls added after it, this "
                    "counted 50 before setAlwaysOnTop");
    }

    // ── the row labels: BB / CX / HH / TOM and their Portuguese names ───────
    //
    // AC-2 asks for them and nothing read one. `kitPieceName` returns an EMPTY
    // string out of range, so a slipped row-to-lane mapping would have drawn
    // blanks and passed every other check here.
    {
        const std::array<std::pair<const char*, const char*>, 4> expected {{
            { "bb", "Bumbo" }, { "cx", "Caixa" }, { "hh", "Chimbal" }, { "tom", "Surdo" },
        }};

        const auto& kitLanes = forrobox::lanesForRow (ChassisLayout::kNumStrips - 1);

        for (size_t row = 0; row < expected.size(); ++row)
        {
            const auto lane = kitLanes.entries[row];

            checkEqual (juce::String (forrobox::ids::lanes[(size_t) lane]),
                        juce::String (expected[row].first),
                        juce::String ("kit row ") + juce::String ((int) row) + " is the "
                            + expected[row].first + " lane");

            checkEqual (forrobox::kitPieceName ((int) row),
                        juce::String::fromUTF8 (expected[row].second),
                        juce::String ("and its full name is ") + expected[row].second
                            + " — data.js:25-28");
        }

        check (forrobox::kitPieceName (4).isEmpty(),
               "and an out-of-range row returns empty rather than reading past the table");

        check (forrobox::subLineText().contains (juce::String::fromUTF8 ("peça")),
               "the sub-line is the prototype's Portuguese, accent intact — app.js:466");

        // The shape the check above USED to have: it pinned "peÃ§a", which is
        // the double-encoding of the same word, so it asserted the bug. U+00C3
        // cannot appear in any Portuguese this plugin shows, and its presence in
        // a decoded string means the source bytes were encoded twice.
        check (! forrobox::subLineText().containsChar (juce::juce_wchar (0x00C3)),
               "and it is encoded ONCE: a decoded Ã is what double-encoded UTF-8 looks like, "
               "which is what the panel was rendering");
    }

    // ── three ways to close ────────────────────────────────────────────────
    {
        check (overlay.isVisible(), "still open");

        // The scrim, outside the panel.
        overlay.mouseUp (mouseEventOn (overlay, { 10.0f, 10.0f }));
        check (! overlay.isVisible(), "clicking the scrim dismisses it — css:557");

        clickInside (chassis, chassis.getLayout()
                                  .stripLayouts[ChassisLayout::kNumStrips - 1]
                                  .subDots.getCentre());
        overlay.advanceEntrance (1.0);
        check (overlay.isVisible(), "and it reopens");

        // A click INSIDE the panel must not close it — ROUTED, because calling
        // `overlay.mouseUp` directly bypassed the mechanism this asserts. The
        // panel is a child with `setInterceptsMouseClicks (true, true)`
        // (KitOverlay.cpp:405), so a routed click inside it never reaches
        // `KitOverlay::mouseUp` at all; the direct call proved nothing and
        // would have passed against an overlay with no panel child and no
        // intercept flag. /simplify.
        overlay.setVisible (true);

        const auto* received = clickInside (overlay, overlay.getLayout().panel.getCentre());

        check (received != &overlay,
               "a click inside the panel is absorbed by the PANEL, not routed to the scrim");
        check (overlay.isVisible(),
               "so it does NOT close it — a missed pad must not dismiss the thing you were "
               "editing in");

        for (auto button : collectChildren<Button> (overlay))
            if (button->onClick != nullptr)
                button->onClick();

        check (! overlay.isVisible(), "and the close button dismisses it");
    }
}

/** No profile can reach full velocity, which is why the group-opacity threshold
    is not 1.0.

    `Profiles.h` decodes '1'-'9' as level x 14, so the loudest a groove can
    express is 126 and `opacityForVelocity (126)` is 0.99465. Against a strict
    `< 1.0f` that opened a transparency layer — an offscreen image allocation —
    on EVERY lit pad of every profile, measured by /simplify at +4.3 us per pad
    per paint. The comment there claimed "full velocity lands on exactly 1.0, so
    the common case takes no layer", which was true of a hand-typed 127 and of
    nothing the plugin ships. */
void testNoProfileReachesFullVelocity()
{
    section ("no profile reaches velocity 127, so the group-opacity threshold is below 1");

    auto loudest = 0;

    for (const auto& profile : forrobox::allProfiles())
        for (const auto* pattern : profile.patterns)
        {
            forrobox::DecodedPattern decoded {};

            check (forrobox::decodePattern (pattern, decoded), "the profile's pattern decodes");

            for (const auto velocity : decoded)
                loudest = juce::jmax (loudest, static_cast<int> (velocity));
        }

    check (loudest > 0, "the profiles carry hits at all");

    check (loudest < forrobox::State::kMaxVelocity,
           "and none reaches " + juce::String (forrobox::State::kMaxVelocity)
               + fbtest::utf8 (" — the loudest is ") + juce::String (loudest));

    check (forrobox::pad::opacityForVelocity (loudest) < 1.0f,
           "so its opacity is below 1");

    check (forrobox::pad::opacityForVelocity (loudest)
               >= forrobox::pad::kGroupOpacityThreshold,
           "and at or above the threshold, so the loudest a profile can play takes NO "
           "transparency layer (" + juce::String (forrobox::pad::opacityForVelocity (loudest), 5)
               + " >= " + juce::String (forrobox::pad::kGroupOpacityThreshold, 5) + ")");

    // The threshold must not be so loose that a genuinely quieter pad stops
    // being composited — a check that only ever moved one way would pass on a
    // threshold of 0.
    check (forrobox::pad::opacityForVelocity (forrobox::seq::kToggleOnVelocity)
               < forrobox::pad::kGroupOpacityThreshold,
           "while a click-toggled pad at velocity 100 still does");
}

/** 06-03 AC-4: a reload flashes the lit pads, and only a reload. */
void testProfileLoadFlashesTheLitPads()
{
    section ("loading a profile flashes every LIT pad, told its elapsed time");

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    auto& grid = chassis.getSequencerGrid();
    auto& panel = chassis.getSidePanel();

    processor.loadProfile (forrobox::allProfiles()[0]);
    grid.refreshFromState();

    const auto padInk = [] (forrobox::StepPad& pad)
    {
        const auto image = renderComponent (pad, pad.getWidth(), pad.getHeight());

        return contrastMass (image, image.getBounds(), juce::Colours::black);
    };

    // ── the reload fires it, on the pads the NEW profile lights ────────────
    //
    // The subjects are chosen AFTER the reload. They used to be picked from the
    // outgoing profile's grid and then asserted against the incoming one's, so
    // both checks passed only while those two coordinates happened to keep their
    // lit-ness across the two grooves — an edit to a pattern in `Profiles.h`
    // would have turned them into checks of nothing. /simplify.
    {
        panel.getProfileButton (1).onClick();

        forrobox::StepPad* litPad = nullptr;
        forrobox::StepPad* darkPad = nullptr;

        for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
            for (int step = 0; step < grid.getStepCount(); ++step)
                if (auto* pad = grid.padFor (row, step))
                {
                    if (pad->isLit() && litPad == nullptr)    litPad = pad;
                    if (! pad->isLit() && darkPad == nullptr) darkPad = pad;
                }

        check (litPad != nullptr && darkPad != nullptr,
               "the loaded profile gives us a lit pad and an unlit one");

        check (litPad->flashBrightness() > 1.0f, "a reload starts the flash");

        checkEqual (darkPad->flashBrightness(), 1.0f,
                    "and an UNLIT pad does not flash — app.js:546 selects .pad.on, and raising an "
                    "empty step's ground would make it blink");
    }

    // ── measured in INK, and driven by time it is TOLD ─────────────────────
    {
        // The pattern changed with the profile, so re-find a pad that is lit NOW.
        forrobox::StepPad* flashing = nullptr;

        for (int row = 0; row < ChassisLayout::kNumStrips && flashing == nullptr; ++row)
            for (int step = 0; step < grid.getStepCount(); ++step)
                if (auto* pad = grid.padFor (row, step); pad != nullptr && pad->isLit())
                {
                    flashing = pad;
                    break;
                }

        check (flashing != nullptr, "the newly loaded profile has a lit pad");

        // Sampled across the whole decay. MONOTONIC, not strictly decreasing:
        // `brightness()` multiplies per channel and CLAMPS, so a saturated accent
        // renders identically at 1.6x and 1.3x — the early samples are expected
        // to tie, and a strict `>` between adjacent ones failed on exactly that.
        // CSS's own `filter: brightness()` clamps the same way.
        std::vector<double> ink { padInk (*flashing) };

        for (int i = 0; i < 4; ++i)
        {
            flashing->advanceFlash (forrobox::pad::kFlashSeconds * 0.25);
            ink.push_back (padInk (*flashing));
        }

        checkEqual (flashing->flashBrightness(), 1.0f, "the flash ends at rest");

        auto monotonic = true;

        for (size_t i = 1; i < ink.size(); ++i)
            monotonic = monotonic && ink[i] <= ink[i - 1] + 1.0e-6;

        check (monotonic, "the flash decays without ever brightening again");

        check (ink.front() > ink.back() * 1.05,
               "and it is VISIBLE in ink, not just in the scalar that produced it ("
                   + juce::String (ink.front(), 1) + " -> " + juce::String (ink.back(), 1)
                   + ") — brightness 1.6 falling to 1 over 340 ms, PLANNING.md:615");
    }

    // ── the flash follows the NEW profile, not the one being replaced ──────
    //
    // The views refresh on their own polls, so at the instant a reload finishes
    // they still hold the outgoing profile's lit set. Flashing then lights the
    // pads the PREVIOUS groove had. Found by /code-review; it had looked right
    // only because an earlier `StepPad::flash` armed every pad regardless.
    {
        for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
            for (int step = 0; step < grid.getStepCount(); ++step)
                if (auto* pad = grid.padFor (row, step))
                    pad->advanceFlash (1.0);

        panel.getProfileButton (3).onClick();

        // REFRESHED AFTERWARDS, which is what makes this discriminate. Comparing
        // `flashBrightness()` against `isLit()` alone compares two readings of
        // the SAME pads — and if the flash ran before the refresh, both are
        // stale together and agree. Pulling the grid up to date first makes
        // `isLit()` the new profile's answer while the flash still carries
        // whatever it was armed from.
        grid.refreshIfStateChanged();

        auto flashingButDark = 0;
        auto litButNotFlashing = 0;

        for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
            for (int step = 0; step < grid.getStepCount(); ++step)
                if (auto* pad = grid.padFor (row, step))
                {
                    const auto flashing = pad->flashBrightness() > 1.0f;

                    if (flashing && ! pad->isLit())   ++flashingButDark;
                    if (pad->isLit() && ! flashing)   ++litButNotFlashing;
                }

        checkEqual (flashingButDark, 0, "no pad flashes that the new profile leaves dark");
        checkEqual (litButNotFlashing, 0,
                    "and every pad the new profile lights IS flashing — the views are refreshed "
                    "before the flash, so it confirms the groove that just arrived rather than "
                    "the one it replaced");
    }

    // ── a RIGHT-click loads nothing ────────────────────────────────────────
    //
    // Right-click belongs to the host, and `Button::mouseDown` states the rule.
    // Without the guard a right-click here overwrote all eight lanes, four
    // globals and ten channel gates with no undo, and swallowed the automation
    // menu the host was opening. /code-review.
    {
        processor.loadProfile (forrobox::allProfiles()[0]);

        juce::MemoryBlock before;
        processor.getStateInformation (before);

        auto& button = panel.getProfileButton (2);

        button.mouseUp (mouseEventOn (button, button.getLocalBounds().getCentre().toFloat(),
                                      juce::ModifierKeys::rightButtonModifier));

        juce::MemoryBlock after;
        processor.getStateInformation (after);

        check (after == before,
               "a right-click on a profile button changes NOTHING — it belongs to the host's "
               "automation menu, and a reload it triggered would destroy the whole state");

        // And the guard is not simply breaking the button.
        button.mouseUp (mouseEventOn (button, button.getLocalBounds().getCentre().toFloat()));

        juce::MemoryBlock afterLeft;
        processor.getStateInformation (afterLeft);

        check (afterLeft != before, "while a left-click still loads");
    }

    // ── an ordinary refresh does NOT flash ─────────────────────────────────
    {
        for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
            for (int step = 0; step < grid.getStepCount(); ++step)
                if (auto* pad = grid.padFor (row, step))
                    pad->advanceFlash (1.0);

        {
            auto state = processor.lockPatternState();
            state->lanes[0][3] = 100;
        }

        grid.refreshIfStateChanged();

        auto flashed = 0;

        for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
            for (int step = 0; step < grid.getStepCount(); ++step)
                if (auto* pad = grid.padFor (row, step); pad != nullptr
                                                          && pad->flashBrightness() > 1.0f)
                    ++flashed;

        checkEqual (flashed, 0,
                    "a pattern change that is NOT a reload flashes nothing — the flash is the "
                    "reload's confirmation, not the pattern's");
    }
}

/** A right-click changes nothing, anywhere in the editor.

    THE MECHANISM, not another per-control guard. `Button::mouseDown` has carried
    `isPopupMenu` since 04-03; /code-review found it missing on 06-03's two new
    controls and the fix was to paste it into both; /simplify then found it STILL
    missing on three containers nobody had looked at — `Chassis::mouseUp`
    (right-click the bateria sub-dots and the kit overlay opened),
    `KitOverlay::mouseUp` (right-click the scrim and it dismissed) and
    `SequencerGrid::mouseUp` (right-click a row label and it isolated). Three
    hand-copied guards had not stopped the fourth, fifth and sixth holes.

    This walks every component in a built editor, right-clicks it, and asserts
    the persisted state and every parameter are untouched. One assertion for
    every control, container and overlay that exists or is ever added. */
void testRightClickChangesNothingAnywhere()
{
    section ("a right-click anywhere in the editor changes no state and no parameter");

    // 8 px: fine enough to land inside the 26 px sub-dots row and a 19 px row
    // label, coarse enough that the whole editor is a few tens of thousands of
    // clicks. Measured at well under a second.
    constexpr int kRightClickPitch = 8;

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    // A top-level component is not visible until told, and the walk skips
    // anything invisible — so the CHASSIS itself, which has a mouseUp of its
    // own, was being stepped over. Its mutant was the one that kept passing.
    chassis.setVisible (true);

    processor.loadProfile (forrobox::allProfiles()[1]);
    chassis.getSequencerGrid().setIsolatedRow (2);

    // EVERYTHING A RIGHT-CLICK COULD MOVE, not just the persisted state. The
    // first version of this snapshot was `getStateInformation` alone — and all
    // three holes /simplify found change things that are deliberately NOT
    // persisted: the kit overlay's visibility and the grid's isolated row. Three
    // mutants passed against it. The subject has to be what the user can see.
    const auto snapshot = [&]
    {
        juce::MemoryBlock persisted;
        processor.getStateInformation (persisted);

        juce::String observable (persisted.toBase64Encoding());

        observable << "|overlay=" << (int) chassis.getKitOverlay().isVisible()
                   << "|isolated=" << chassis.getSequencerGrid().getIsolatedRow()
                   << "|profile=" << processor.selectedProfileIndex();

        for (auto* parameter : processor.getParameters())
            observable << "|" << parameter->getValue();

        return observable;
    };

    // SAFE POINTERS. A click can destroy components — an unguarded right-click on
    // the overlay's close button rebuilds its pads, and the raw pointers this
    // list used to hold then dangled. The SequencerGrid mutant segfaulted on
    // exactly that, which is a crash in the TEST rather than a detection.
    std::vector<juce::Component::SafePointer<juce::Component>> everything;

    {
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                everything.push_back (child);
                walk (*child);
            }
        };

        everything.push_back (&chassis);   // the chassis, whose sub-dots are a HitZone child
        walk (chassis);
    }

    check (everything.size() > 100,
           "the walk reaches the whole editor (" + juce::String ((int) everything.size())
               + " components) — a walk that found nothing would pass every check below");

    // TWICE: with the kit overlay shut and with it open. The overlay is
    // always-on-top and full-bounds, so while it is open it COVERS the
    // sub-dots zone and a single pass with it open never reaches it.
    //
    // That used to be an early return in `Chassis::mouseUp`, which 06-06
    // deleted when the sub-dots became a `HitZone` child. The pass still earns
    // its place — it now exercises z-order occlusion rather than a guard
    // clause — so the rationale is restated rather than the pass removed.
    // /code-review caught the stale version.
    for (const auto overlayOpen : { false, true })
    {
        chassis.getKitOverlay().setOpen (overlayOpen);
        chassis.getKitOverlay().advanceEntrance (1.0);

        const auto before = snapshot();

        auto clicked = 0;

        for (auto& safe : everything)
        {
            auto* c = safe.getComponent();

            if (c == nullptr || ! c->isVisible() || c->getLocalBounds().isEmpty())
                continue;

            // A GRID AT A FIXED PITCH, not the centre and not a fixed count. A
            // container's handler fires only over part of itself — the bateria
            // sub-dots, a sequencer row label, the overlay's scrim — so one
            // click at the middle reaches none of them, and a 5x5 grid over the
            // whole 1200x780 chassis still steps clean over the sub-dots row.
            // Both weaker versions let mutants through.
            const auto box = c->getLocalBounds();

            for (int y = box.getY(); y < box.getBottom(); y += kRightClickPitch)
                for (int x = box.getX(); x < box.getRight(); x += kRightClickPitch)
                {
                    const auto e = mouseEventOn (*c,
                                                 juce::Point<float> ((float) x, (float) y),
                                                 juce::ModifierKeys::rightButtonModifier);

                    if (auto* live = safe.getComponent())
                    {
                        live->mouseDown (e);
                        live->mouseUp (e);
                        ++clicked;
                    }
                }
        }

        check (clicked > 2000, juce::String ("right-clicked ") + juce::String (clicked)
                                  + (overlayOpen ? " components with the kit panel open"
                                                 : " components with it shut"));

        check (snapshot() == before,
               juce::String ("and nothing moved") + (overlayOpen ? " (panel open)" : " (panel shut)")
                   + " — no right-click anywhere in this editor loads a profile, edits a pad, "
                     "isolates a row, opens or dismisses the kit panel, or touches a parameter. "
                     "Right-click belongs to the host's automation menu");
    }
}

/** 06-03 AC-2/AC-3: selecting a profile is a full state reload. */
void testProfileLoadIsAFullReload()
{
    section ("loading a profile reloads every field, from either entry point");

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    auto& apvts = processor.getAPVTS();
    auto& panel = chassis.getSidePanel();

    const auto valueOf = [&] (juce::StringRef id)
    {
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id));

        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : -1.0f;
    };

    // ── EVERY field, for EVERY profile, against the tables ─────────────────
    //
    // Against `allProfiles()`, never against transcribed numbers: a test that
    // hard-codes 132 is a second copy of the table, and the cross-check against
    // data.js then guards only one of them.
    for (const auto& profile : forrobox::allProfiles())
    {
        // SET BEFORE THE LOAD, every time. Without this the "solo is cleared"
        // check below is unreachable — a solo that was never set is already 0,
        // and a reload that cleared nothing passed. The mutant proved it.
        for (const auto& info : forrobox::ids::channelInfos)
            if (auto* solo = dynamic_cast<juce::RangedAudioParameter*> (
                    apvts.getParameter (forrobox::ids::channelParam (info.id,
                                                                     forrobox::ids::solo))))
                solo->setValueNotifyingHost (1.0f);

        // And a mute that the profile does NOT ask for, so "the mute is the
        // profile's" has to actively turn one off rather than leave it.
        if (auto* mute = dynamic_cast<juce::RangedAudioParameter*> (
                apvts.getParameter (forrobox::ids::channelParam ("ganza", forrobox::ids::mute))))
            mute->setValueNotifyingHost (1.0f);

        processor.loadProfile (profile);

        checkEqual (valueOf (forrobox::ids::bpm), static_cast<float> (profile.bpm),
                    juce::String (profile.id()) + " loads its bpm");
        checkEqual (valueOf (forrobox::ids::swing), profile.swing, "its swing");
        checkEqual (valueOf (forrobox::ids::cachaca), profile.cachaca, "its cachaça");
        checkEqual (juce::roundToInt (valueOf (forrobox::ids::timbre)), profile.timbreIndex,
                    "and its timbre character");

        for (const auto& info : forrobox::ids::channelInfos)
        {
            const auto muted = profile.bateriaMuted
                            && juce::StringRef (info.id) == juce::StringRef ("bateria");

            checkEqual (valueOf (forrobox::ids::channelParam (info.id, forrobox::ids::mute)),
                        muted ? 1.0f : 0.0f,
                        juce::String (info.id) + "'s mute is the profile's");

            // Cleared, and NOT profile data — app.js:534. A solo left set would
            // silence the groove that was just loaded.
            checkEqual (valueOf (forrobox::ids::channelParam (info.id, forrobox::ids::solo)), 0.0f,
                        juce::String (info.id) + "'s solo is cleared");
        }

        {
            auto state = processor.lockPatternState();

            checkEqual (state->activeProfile, juce::String (profile.id()), "activeProfile names it");
            check (! state->dirty, "and the state is not dirty");

            auto lanesMatch = true;

            for (size_t lane = 0; lane < profile.patterns.size(); ++lane)
            {
                forrobox::DecodedPattern decoded {};

                check (forrobox::decodePattern (profile.patterns[lane], decoded),
                       "the profile's pattern decodes");

                // Every one of the 32 SLOTS, not the 16 the window shows: the
                // storage model fills all of them from the 16-step source, which
                // is what makes a load at 32 steps repeat the bar the way
                // `buildGroove (id, state.steps)` does.
                for (int step = 0; step < forrobox::State::kMaxSteps; ++step)
                    lanesMatch = lanesMatch
                              && state->lanes[lane][(size_t) step]
                                     == decoded[(size_t) (step % forrobox::kPatternLength)];
            }

            check (lanesMatch, juce::String (profile.id())
                                   + " loads its pattern into all 32 slots, tiled from 16");
        }
    }

    // ── the two entry points produce the SAME state, field for field ───────
    //
    // The failure this prevents is two reloads that agree today. They are one
    // call on the processor, and this is what says so.
    {
        const auto capture = [&]
        {
            juce::MemoryBlock block;
            processor.getStateInformation (block);

            return block;
        };

        processor.loadProfile (forrobox::allProfiles()[0]);

        // From the PANEL's button.
        panel.getProfileButton (2).onClick();

        const auto viaPanel = capture();

        processor.loadProfile (forrobox::allProfiles()[0]);

        // From the HEADER's STYLE control.
        auto& header = chassis.getHeaderBar();
        auto* style = header.getStyleControl();

        check (style != nullptr, "the header carries the STYLE control");

        // RETURNS rather than dereferencing. The three sibling sites guard and
        // this one did not — a null here segfaulted the whole suite, losing
        // every check after it instead of reporting one. Found by mutating the
        // accessor to return null, which is what that mutation is for.
        if (style == nullptr)
            return;

        const auto centre = style->segmentBounds (2).getCentre();
        style->mouseDown (mouseEventOn (*style, centre.toFloat()));
        style->mouseUp (mouseEventOn (*style, centre.toFloat()));

        check (capture() == viaPanel,
               "the side panel's list and the header's STYLE control load the same profile into "
               "a state that is identical, byte for byte — one reload with two callers, not two "
               "that agree today");
    }

    // ── an edited state stops being the profile it names ───────────────────
    {
        processor.loadProfile (forrobox::allProfiles()[1]);
        panel.refreshFromState();

        checkEqual (processor.selectedProfileIndex(), 1, "the loaded profile is selected");
        check (panel.getProfileButton (1).isActive(), "and its button is lit");

        {
            auto state = processor.lockPatternState();
            state->dirty = true;
        }

        panel.refreshFromState();

        checkEqual (processor.selectedProfileIndex(), -1,
                    "an edited state selects NOTHING — app.js:555 and PLANNING.md:601: the "
                    "highlight clears even though activeProfile still names it");

        check (! panel.getProfileButton (1).isActive(), "so the panel's button goes dark");

        chassis.getHeaderBar().refreshFromProcessor();

        auto& header = chassis.getHeaderBar();
        if (auto* styleControl = header.getStyleControl())
            checkEqual (styleControl->getSelectedIndex(), -1,
                        "and the header's STYLE segment with it");
        else
            check (false, "the header carries a STYLE control to check");

        processor.loadProfile (forrobox::allProfiles()[1]);
        panel.refreshFromState();

        check (panel.getProfileButton (1).isActive(), "reloading brings the highlight back");
    }

    // ── the audio does not glitch across a reload ──────────────────────────
    //
    // The phase's goal says "without a click, a glitch, or an audio-thread data
    // race", so the AUDIO is the subject and not the parameters.
    {
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;

        processor.loadProfile (forrobox::allProfiles()[0]);
        processor.prepareToPlay (48000.0, block.getNumSamples());
        processor.setPlaying (true);

        auto loudestBefore = 0.0f;

        for (int i = 0; i < 12; ++i)
        {
            block.clear();
            midi.clear();
            processor.processBlock (block, midi);
            loudestBefore = juce::jmax (loudestBefore, fbtest::bufferPeak (block));
        }

        check (loudestBefore > 1.0e-4f, "the first profile is audible");

        // Captured, so the reload can be shown to have REACHED the audio thread.
        // The first version asserted only "finite and still audible", which the
        // previous profile also satisfies — it would have passed with the
        // publication mechanism deleted. An instrument that cannot report the
        // difference it exists to measure. /code-review.
        const auto publicationsBefore = processor.getPatternPublicationCount();

        juce::AudioBuffer<float> firstProfile (2, 12 * block.getNumSamples());

        processor.setPlaying (false);
        processor.setPlaying (true);

        for (int i = 0; i < 12; ++i)
        {
            block.clear();
            midi.clear();
            processor.processBlock (block, midi);

            for (int c = 0; c < 2; ++c)
                firstProfile.copyFrom (c, i * block.getNumSamples(), block, c, 0,
                                       block.getNumSamples());
        }

        processor.loadProfile (forrobox::allProfiles()[2]);

        check (processor.getPatternPublicationCount() != publicationsBefore,
               "the reload published a new pattern to the audio thread");

        juce::AudioBuffer<float> secondProfile (2, 12 * block.getNumSamples());

        processor.setPlaying (false);
        processor.setPlaying (true);

        for (int i = 0; i < 12; ++i)
        {
            block.clear();
            midi.clear();
            processor.processBlock (block, midi);

            // `fbtest::isFinite`, not `std::isfinite (bufferPeak (...))`:
            // `bufferPeak` goes through `AudioBuffer::getMagnitude`, whose jmax
            // chain does not reliably propagate a NaN — so the hand-rolled form
            // is the WEAKER instrument for the very thing it checks. /simplify.
            check (fbtest::isFinite (block), "every block across the reload is finite");

            for (int c = 0; c < 2; ++c)
                secondProfile.copyFrom (c, i * block.getNumSamples(), block, c, 0,
                                        block.getNumSamples());
        }

        check (fbtest::bufferPeak (secondProfile) > 1.0e-4f,
               "and the plugin is still audible after the reload — silence here is the glitch "
               "the phase's goal names");

        // THE SUBJECT: the two profiles must SOUND different. This is what
        // separates "the reload reached the engine" from "nothing happened and
        // the old groove is still playing".
        check (fbtest::maxDifference (firstProfile, secondProfile) > 1.0e-3f,
               "and it is playing the NEW groove, not the old one");
    }
}

/** 06-02 AC-3/AC-4: the timbre rows, the MIX knob and the CUSTOM tag are live. */
void testSidePanelControlsAreLive()
{
    section ("the timbre rows follow the parameter and change the sound; the tag follows dirty");

    ChassisRig rig;
    auto& processor = rig.processor;

    auto& panel = rig.chassis.getSidePanel();
    auto& apvts = processor.getAPVTS();

    auto rows = collectChildren<forrobox::TimbreRow> (panel);

    checkEqual (static_cast<int> (rows.size()), static_cast<int> (forrobox::timbreSpecs.size()),
                "the panel carries one row per timbre");

    auto* timbre = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (forrobox::ids::timbre));
    check (timbre != nullptr, "the timbre parameter exists");

    const auto ledInk = [&] (int index)
    {
        const auto image = renderComponent (*rows[(size_t) index],
                                            rows[(size_t) index]->getWidth(),
                                            rows[(size_t) index]->getHeight());

        // Against the row's own unlit ground, sampled from the same frame.
        return contrastMass (image, image.getBounds(), pixelAt (image, 2, 2));
    };

    // ── the row follows the PARAMETER, with no click ───────────────────────
    for (int choice = 0; choice < static_cast<int> (forrobox::timbreSpecs.size()); ++choice)
    {
        timbre->setValueNotifyingHost (timbre->convertTo0to1 (static_cast<float> (choice)));

        for (auto row : rows)
            checkEqual (static_cast<int> (row->isSelected()),
                        static_cast<int> (row->getIndex() == choice),
                        juce::String ("host automation to choice ") + juce::String (choice)
                            + " lights exactly that row, with no editor gesture");
    }

    // ── and it is INK, not a flag ──────────────────────────────────────────
    {
        timbre->setValueNotifyingHost (timbre->convertTo0to1 (0.0f));

        const auto litFirst = ledInk (0);
        const auto darkSecond = ledInk (1);

        timbre->setValueNotifyingHost (timbre->convertTo0to1 (1.0f));

        check (ledInk (1) > darkSecond,
               "the row that became selected paints MORE — the LED lights and the ground shifts "
               "toward --active, which a check on isSelected() alone would not see");

        check (ledInk (0) < litFirst, "and the row that lost it paints less");
    }

    // ── a click writes the parameter ───────────────────────────────────────
    {
        timbre->setValueNotifyingHost (timbre->convertTo0to1 (0.0f));

        rows[2]->mouseUp (mouseEventOn (*rows[2],
                                        rows[2]->getLocalBounds().getCentre().toFloat()));

        checkEqual (juce::roundToInt (timbre->convertFrom0to1 (timbre->getValue())), 2,
                    "clicking a row writes its own choice index");
    }

    // ── and the SOUND changes, which is what makes these rows real ─────────
    {
        {
            auto state = processor.lockPatternState();

            for (auto& lane : state->lanes)
                for (int step = 0; step < forrobox::State::kMaxSteps; ++step)
                    lane[(size_t) step] = static_cast<std::uint8_t> (step % 4 == 0 ? 110 : 0);
        }

        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;

        const auto render = [&] (int choice)
        {
            timbre->setValueNotifyingHost (timbre->convertTo0to1 (static_cast<float> (choice)));

            juce::AudioBuffer<float> captured (2, 8 * block.getNumSamples());

            processor.prepareToPlay (48000.0, block.getNumSamples());
            processor.setPlaying (false);
            processor.setPlaying (true);

            for (int i = 0; i < 8; ++i)
            {
                block.clear();
                midi.clear();
                processor.processBlock (block, midi);

                for (int c = 0; c < 2; ++c)
                    captured.copyFrom (c, i * block.getNumSamples(), block, c, 0,
                                       block.getNumSamples());
            }

            return captured;
        };

        const auto hifi = render (0);

        checkEqual (fbtest::maxDifference (hifi, render (0)), 0.0f,
                    "two renders at one timbre are identical — the control for the comparison below");

        check (fbtest::bufferPeak (hifi) > 1.0e-4f, "and the render is not silence");

        check (fbtest::maxDifference (hifi, render (1)) > 1.0e-3f,
               "LO-FI renders audibly differently from HI-FI");
        check (fbtest::maxDifference (hifi, render (2)) > 1.0e-3f,
               "and CICLOTRON differently again — these rows are not decoration");
    }

    // ── the MIX knob drives ids::charMix ───────────────────────────────────
    {
        auto* mix = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (forrobox::ids::charMix));
        check (mix != nullptr, "the char_mix parameter exists");

        mix->setValueNotifyingHost (mix->convertTo0to1 (0.0f));
        checkEqual (panel.getMixKnob().getProportion(), 0.0f, "the knob follows the parameter to 0");

        mix->setValueNotifyingHost (mix->convertTo0to1 (100.0f));
        checkEqual (panel.getMixKnob().getProportion(), 1.0f, "and to full");
    }

    // ── what the poll COSTS, which AC-4 required measuring ─────────────────
    //
    // At 06-01's close I told the user 06-02 would have to widen the state's
    // revision counter, because `publishIfChanged` compares only `lanes` and so
    // `dirty` has no follower. On reading it I judged the opposite — that the
    // tag should read the truth where it lives — on the claim that taking the
    // handle costs about 29 ns. The plan then required the number, because a
    // claim like that is exactly the kind this project has been wrong about.
    {
        constexpr int kTrials = 20000;

        // Warm, so the first lock's cold cache is not the measurement.
        for (int i = 0; i < 1000; ++i)
            panel.refreshFromState();

        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (int i = 0; i < kTrials; ++i)
            panel.refreshFromState();

        const auto nanos = (juce::Time::getMillisecondCounterHiRes() - start)
                         * 1.0e6 / static_cast<double> (kTrials);

        std::cout << "  [06-02] one side-panel state poll: " << juce::String (nanos, 1)
                  << " ns (" << juce::String (nanos * forrobox::kUiPollHz / 1000.0, 2)
                  << " us per second at " << forrobox::kUiPollHz << " Hz)" << std::endl;

        // A LOOSE ceiling, deliberately: 04-04 is the plan where three checks
        // failed on MSVC's clock rather than on the code, so this is twenty
        // times the claim and only catches a catastrophe — the NUMBER above is
        // the record, not this bound.
        check (nanos < 2000.0,
               "reading the dirty flag through the pattern handle is cheap enough to poll — the "
               "judgement that dropped the publication idea, now measured rather than asserted");
    }

    // ── the CUSTOM tag follows State::dirty ────────────────────────────────
    {
        const auto tagInk = [&]
        {
            const auto image = renderComponent (panel, panel.getWidth(), panel.getHeight());
            const auto box = panel.getLayout().customTag;

            return contrastMass (image, box, pixelAt (image, 2, box.getCentreY()));
        };

        {
            auto state = processor.lockPatternState();
            state->dirty = false;
        }

        panel.refreshFromState();

        const auto atRest = tagInk();

        {
            auto state = processor.lockPatternState();
            state->dirty = true;
        }

        panel.refreshFromState();
        panel.advanceCustomTag (1.0);   // straight to rest; the fade is its own claim

        checkEqual (panel.customTagOpacity(), 1.0f, "a dirty state fades the tag in");

        check (tagInk() > atRest + 1.0,
               "and it is PAINTED — measured in ink, not in the flag that produced it");

        {
            auto state = processor.lockPatternState();
            state->dirty = false;
        }

        panel.refreshFromState();

        // BOTH WAYS, and half of this could not be tested before: the panel used
        // to snap the opacity to 0 whenever the flag cleared, so the fade-out arm
        // was unreachable and this check passed on the snap. css:411's transition
        // is symmetric.
        check (panel.customTagOpacity() > 0.0f,
               "clearing the flag does not snap the tag away — it fades, as css:411 says");

        panel.advanceCustomTag (forrobox::side::kCustomTagFadeSeconds * 0.5);

        check (panel.customTagOpacity() > 0.0f && panel.customTagOpacity() < 1.0f,
               "halfway out it is partly there (" + juce::String (panel.customTagOpacity(), 3)
                   + ")");

        panel.advanceCustomTag (1.0);

        checkEqual (panel.customTagOpacity(), 0.0f, "and it reaches zero");
        check (tagInk() <= atRest + 1.0, "and the ink goes with it");
    }
}

/** 06-02, rescoped at 06-04: the accented strings the plugin actually HOLDS,
    not the ones its source appears to spell.

    ORIGINALLY this existed for the greedy `\xNN` escape, which 06-04 deleted at
    the root by pinning the charset in CMakeLists.txt.

    It still earns its place, for a narrower and honestly smaller reason: it is
    the only check here that reads the COMPILED value, so it is the one that
    would see a compiler decoding the source on a lossy or DBCS code page.
    scripts/verify-charset.py covers the literals this function never walks, and
    states the full argument for both. */
void testAccentedStringsSurviveTheCompiler()
{
    section ("every accented string the panel shows is well-formed UTF-8 at runtime");

    const auto check8 = [] (const char* raw, const juce::String& what)
    {
        check (juce::CharPointer_UTF8::isValidString (raw, static_cast<int> (std::strlen (raw))),
               what + " is valid UTF-8");

        const auto decoded = juce::String::fromUTF8 (raw);

        check (! decoded.containsChar (juce::juce_wchar (0xFFFD)),
               what + " carries no replacement character");

        // Re-encodes to the SAME BYTES. A lossy decode still produces a string,
        // so "it decoded" is not the claim — "it decoded to the right thing" is,
        // and byte equality is what says so.
        //
        // NOT `juce::String (decoded.toRawUTF8())`: that constructor reads the
        // bytes as Latin-1 and hands back mojibake for every accent, so the
        // first version of this check failed on eleven correct strings. It is
        // the same confusion that had the kit overlay rendering "peÃ§a" for a
        // whole plan.
        const auto reencoded = decoded.toRawUTF8();

        check (std::strcmp (reencoded, raw) == 0,
               what + " re-encodes to the same UTF-8 bytes");
    };

    auto accented = 0;

    for (const auto& info : forrobox::ids::profileInfos)
    {
        check8 (info.displayName, juce::String (info.id) + ".displayName");

        for (size_t line = 0; line < info.description.size(); ++line)
        {
            const auto* raw = info.description[line];

            check8 (raw, juce::String (info.id) + ".description[" + juce::String ((int) line) + "]");

            for (const auto c : juce::String::fromUTF8 (raw))
                if (c > 127)
                    ++accented;
        }
    }

    for (const auto& spec : forrobox::timbreSpecs)
        check8 (spec.subLabel, juce::String (spec.displayName) + ".subLabel");

    // The descriptions are Brazilian Portuguese and MUST carry accents — a
    // version that stripped them all would pass every check above.
    check (accented >= 12,
           "the descriptions carry their accents at runtime (" + juce::String (accented)
               + " non-ASCII characters)");

    // AND EVERY ONE OF THEM IS A CHARACTER THIS TEXT COULD LEGITIMATELY USE.
    //
    // The bug this was written for — a truncated `\xNN` escape — is gone: 06-04
    // pinned the charset in CMakeLists.txt and the literals are real UTF-8, so
    // there is no escape left to truncate.
    //
    // It does NOT detect a missing `/utf-8` — the first version of this comment
    // claimed it did, and /code-review corrected that. What it catches is a code
    // page that is not lossless for these bytes. The full argument is stated
    // once, in scripts/verify-charset.py's module docstring.
    //
    // THE RULE IS THE REPERTOIRE, NOT AN INVENTORY. The old list held the
    // twelve characters the text happened to use on the day it was written, so
    // `É Ó Ç · × ÷ … ‹ › ↓ ↗` — eleven of the twenty-three this source already
    // draws — were outside it. It had not fired only because this check is
    // scoped to `profileInfos` and `timbreSpecs`; the first display name
    // carrying an `É` would have been a FALSE FAILURE on a correct build.
    //
    // Stated as a rule it cannot false-fail on a correct accent, and it still
    // rejects mojibake, whose `Ã`-plus-stray-byte pairs are not letters.
    //
    // AND NOTE THE SCOPE: the loops below walk `ids::profileInfos` and
    // `forrobox::timbreSpecs` only, which is not where most of this UI's
    // accented text lives. Every other literal is covered by
    // scripts/verify-charset.py, at the source level. /code-review.
    // AS CODE POINTS, NOT AS CHARACTERS — and this is the one place in 06-04
    // where that matters. Everywhere else the plan's rule holds: spell the
    // character, because the character is the meaning. Here the BYTE is the
    // meaning, because this list is the ORACLE of the only check that reads
    // compiled bytes.
    //
    // Written as UTF-8 characters it mangles in lockstep with the subject it
    // judges. /simplify simulated a uniform CP1252 misread: the oracle then
    // CONTAINS `Ã © ¡ ¢ £ ª „ ‚` — the mojibake alphabet itself — and every
    // corrupted subject reports zero offenders. The check would have been
    // exactly the thing this project keeps finding: one that cannot fail.
    //
    // A `\uXXXX` universal-character-name is NOT a substitute: it is converted
    // to the execution charset and mangles too. The precedent is :9552, which
    // already spells its one code point as `juce::juce_wchar (0x00C3)`.
    //
    // scripts/verify-charset.py parses THIS array and asserts its own set
    // matches, so the repertoire has one owner rather than two hand-kept
    // copies. Keep the `0xNNNN, // c` shape — that script reads it.
    static constexpr std::array<juce::juce_wchar, 36> allowed {
        // every accented letter Brazilian Portuguese is written with
        0x00E1, // á
        0x00E0, // à
        0x00E2, // â
        0x00E3, // ã
        0x00E9, // é
        0x00EA, // ê
        0x00ED, // í
        0x00F3, // ó
        0x00F4, // ô
        0x00F5, // õ
        0x00FA, // ú
        0x00FC, // ü
        0x00E7, // ç
        0x00C1, // Á
        0x00C0, // À
        0x00C2, // Â
        0x00C3, // Ã
        0x00C9, // É
        0x00CA, // Ê
        0x00CD, // Í
        0x00D3, // Ó
        0x00D4, // Ô
        0x00D5, // Õ
        0x00DA, // Ú
        0x00DC, // Ü
        0x00C7, // Ç
        // and the typographic characters this UI draws, each with its source
        0x00B7, // · css:341, the FORRO-BOX lockup and the sub-dot separators
        0x00D7, // x the header's double mini, and the kit overlay's close button
        0x00F7, // / the header's halve mini
        0x2014, // - the em dash in campina's first description line
        0x2026, // . LOAD IR... and Typography's truncation ellipsis
        0x2039, // < the preset cycler's previous arrow
        0x203A, // > the preset cycler's next arrow
        0x2193, // v DRAG MIDI, drawn as text so css:539's font-size is comparable
        0x2197, // ^ the bateria sub-dots' kit arrow
        0x2122, // (TM) CICLOTRON
    };

    // Reports rather than checks, so the negative control below can assert that
    // it FIRES. A predicate that can only call `check(false)` cannot be tested.
    const auto offendingChar = [&] (const char* raw) -> juce::juce_wchar
    {
        for (const auto c : juce::String::fromUTF8 (raw))
            if (c > 127 && std::find (allowed.begin(), allowed.end(), c) == allowed.end())
                return c;

        return 0;
    };

    const auto everyNonAscii = [&] (const char* raw, const juce::String& what)
    {
        if (const auto bad = offendingChar (raw); bad != 0)
            check (false, what + " carries U+"
                              + juce::String::toHexString (static_cast<int> (bad)).toUpperCase()
                              + ", which Brazilian Portuguese is not written with and this UI "
                                "does not draw — a source charset the compiler guessed wrong "
                                "looks exactly like this, and the source-text cross-check "
                                "cannot see it");
    };

    for (const auto& info : forrobox::ids::profileInfos)
    {
        everyNonAscii (info.displayName, juce::String (info.id) + ".displayName");

        for (const auto* line : info.description)
            everyNonAscii (line, juce::String (info.id) + ".description");
    }

    for (const auto& spec : forrobox::timbreSpecs)
        everyNonAscii (spec.subLabel, juce::String (spec.displayName) + ".subLabel");

    // ── the negative control the old check never had ───────────────────────
    //
    // TestHarness.h's law: the instrument is self-tested against a subject with
    // a known answer, including one it must REJECT. Twelve of Phase 3's audio
    // measurements were wrong before the code was, and Phase 6 alone produced
    // five checks that could not fail.
    //
    // "Ý" is U+00DD, which is what GCC's truncation of `\xa9d` to `\x9d`
    // actually produced in "médio" — the real historical failure, not an
    // invented one. It must be REPORTED.
    // The SUBJECTS are byte escapes for the oracle's reason: written as UTF-8
    // they mangle with everything else, and this control would then fail for the
    // wrong cause while naming the right one. `\xc3\x9d` is U+00DD, exactly.
    checkEqual (static_cast<int> (offendingChar ("Swing m\xc3\x9d" "dio.")), 0x00DD,
                "the repertoire check REJECTS U+00DD, the character GCC's truncation produced");

    // And it must ACCEPT the accents the old twelve-character list omitted, or
    // it is a false failure waiting for the next accented string.
    //
    // `fromUTF8` on the MESSAGE, not only on the subject: `check` takes a
    // juce::String, and `juce::String (const char*)` reads its bytes as Latin-1
    // (juce_String.cpp:308). Without it this check's own failure text prints
    // "Ã Ã Ã Ã­" — the trap ChassisLayout's accessors are declared to close,
    // reappearing in the test that exists to catch mangled text. Caught by
    // running the mutant and reading what it actually printed.
    checkEqual (static_cast<int> (offendingChar ("\xc3\x89" "\xc3\x93" "\xc3\x87"
                                                "\xc3\xad" "\xc3\xba" "\xc3\xa0"
                                                "\xc3\xb5")), 0,
                "and ACCEPTS U+00C9 U+00D3 U+00C7 U+00ED U+00FA U+00E0 U+00F5 "
                "(E-acute, O-acute, C-cedilla, i-acute, u-acute, a-grave, o-tilde) "
                "— every one of which the old twelve-character allowlist omitted");
}

/** 06-02 AC-1/AC-2: the side panel's boxes, and the profile it says is active. */
void testSidePanelLayoutAndActiveProfile()
{
    section ("the side panel reserves the prototype's boxes and names the stored profile");

    ChassisRig rig;
    auto& processor = rig.processor;

    auto& panel = rig.chassis.getSidePanel();

    checkEqual (panel.getWidth(), ChassisLayout::kSidePanelWidth,
                "the panel fills the region 04-01 reserved");

    // ── the gaps the stylesheet fixes ──────────────────────────────────────
    {
        const auto& l = panel.getLayout();

        for (size_t i = 1; i < l.profiles.size(); ++i)
            checkEqual (l.profiles[i].bounds.getY() - l.profiles[i - 1].bounds.getBottom(),
                        forrobox::side::kProfileGap,
                        "the profile buttons are 5 px apart — css:392");

        for (size_t i = 1; i < l.timbres.size(); ++i)
            checkEqual (l.timbres[i].bounds.getY() - l.timbres[i - 1].bounds.getBottom(),
                        forrobox::side::kTimbreGap,
                        "and the timbre rows 4 px — css:415");

        // The LED's size is NOT asserted here. It was — against the constant
        // `forBounds` had just used, on a rectangle nothing painted — and so was
        // "every row is the same height", which iterated over `front()` and
        // compared one call of `timbreHeight()` with itself. Both are the "a test
        // that reads the constant it is checking" shape. The LED's real claim is
        // made in INK, in testSidePanelControlsAreLive. /simplify.

        checkEqual (l.loadIr.getX() - l.mixKnob.getRight(), forrobox::side::kMixGap,
                    "LOAD IR… sits 10 px right of the MIX knob — css:431");
        checkEqual (l.mixKnob.getWidth(), forrobox::side::kMixKnobSize,
                    "and the knob is 28 px — app.js:302");
        checkEqual (l.loadIr.getRight(), l.content.getRight(),
                    "and takes the rest of the row — css:433's flex:1");
    }

    // ── `margin-top: auto`: the bundle is on the FLOOR, not at an offset ───
    //
    // The law css:436 states, and the one a hard-coded offset would fail. Proved
    // by changing what sits above it: the ACTIVE profile button is taller than
    // the others, so selecting a different one moves every box between them —
    // and must move the bundle by exactly nothing.
    {
        const auto bundleBefore = panel.getLayout().bundle;

        checkEqual (bundleBefore.getBottom(),
                    panel.getHeight() - forrobox::side::kPadY,
                    "the bundle's floor is the region's own padding");

        {
            auto state = processor.lockPatternState();
            state->activeProfile = "sp";
        }

        panel.refreshFromState();

        checkEqual (panel.activeProfileIndex(), 3, "the last profile is now the active one");

        check (panel.getLayout().profiles[3].bounds.getY()
                   != panel.getLayout().profiles[0].bounds.getY(),
               "which moved the column above the bundle");

        checkEqual (panel.getLayout().bundle.getY(), bundleBefore.getY(),
                    "and the bundle did not move — `margin-top: auto` puts it on the floor, so a "
                    "section above it changing height moves nothing else");
    }

    // ── which profile is active, and what that shows ───────────────────────
    for (size_t i = 0; i < forrobox::ids::profileInfos.size(); ++i)
    {
        {
            auto state = processor.lockPatternState();
            state->activeProfile = forrobox::ids::profileInfos[i].id;
        }

        panel.refreshFromState();

        checkEqual (panel.activeProfileIndex(), static_cast<int> (i),
                    juce::String ("storing ") + forrobox::ids::profileInfos[i].id
                        + " lights that button");

        // Through the CONTROL's own state, not through the emptiness of a
        // rectangle in the owner's layout — which was testing that `forBounds`
        // agreed with the argument it had just been passed.
        auto lit = 0;

        for (size_t row = 0; row < forrobox::ids::profileInfos.size(); ++row)
            if (panel.getProfileButton (static_cast<int> (row)).isActive())
                ++lit;

        checkEqual (lit, 1, "and EXACTLY one button is active — css:400 hides the description "
                            "on the others");

        check (panel.getProfileButton (static_cast<int> (i)).isActive(), "the right one");

        // And it is TALLER, which is what css:403 revealing the description
        // means geometrically.
        check (panel.getLayout().profiles[i].bounds.getHeight()
                   > panel.getLayout().profiles[(i + 1) % 4].bounds.getHeight(),
               "and taller than the others, because only it shows its description");
    }

    // A profile id this build does not know — what a project saved by a newer
    // one carries. `findProfile` returns nullptr rather than resolving to the
    // wrong groove; this must not light CAMPINA over a state that is not it.
    {
        {
            auto state = processor.lockPatternState();
            state->activeProfile = "forro-do-futuro";
        }

        panel.refreshFromState();

        checkEqual (panel.activeProfileIndex(), -1,
                    "an unknown profile id lights NOTHING — a fallback of 0 would show CAMPINA "
                    "over a state that is not campina");

        for (size_t row = 0; row < forrobox::ids::profileInfos.size(); ++row)
            check (! panel.getProfileButton (static_cast<int> (row)).isActive(),
                   "and no button is active");
    }
}

/** 06-01: what a rebuild has to put back — bounds, dimming, and z-order. */
void testARebuildRestoresWhatItReplaced()
{
    section ("a STEPS rebuild re-places the pads, keeps the dimming, and leaves the playhead on top");

    ChassisRig rig;

    auto& grid = rig.chassis.getSequencerGrid();

    // Counted rather than eyeballed, the way 05-04 counted what was in front of
    // the kit overlay (50 -> 0) — through the same helper, now that there are two.
    const auto playheads = collectChildren<forrobox::Playhead> (grid);

    check (playheads.size() == 1, "the grid has exactly one playhead");

    const auto padsInFrontOfPlayhead = [&]
    {
        return childrenInFrontOf<forrobox::StepPad> (grid, *playheads.front());
    };

    checkEqual (padsInFrontOfPlayhead(), 0, "no pad paints over the playhead at rest");

    // The rebuild path, from the parameter — which is how a host automating
    // STEPS reaches it, with or without anyone clicking.
    auto* steps = rig.processor.getAPVTS().getParameter (forrobox::ids::steps);
    check (steps != nullptr, "the steps parameter exists");

    const auto before = grid.getStepCount();

    // A row DIMMED before the rebuild, so the rebuild has something to lose. The
    // dim is edge-detected, so `refreshRowStates` will not put it back — only
    // the rebuild's own callback can.
    grid.setIsolatedRow (0);
    grid.refreshRowStates();

    check (grid.isRowDimmed (2), "row 2 is dimmed before the rebuild");

    steps->setValueNotifyingHost (1.0f);
    grid.refreshIfStateChanged();

    check (grid.getStepCount() != before,
           "the STEPS change rebuilt the pads (" + juce::String (before) + " -> "
               + juce::String (grid.getStepCount()) + ")");

    // BOUNDS. A fresh pad has none until the owner gives it some, and nothing
    // else in the suite noticed: removing the rebuild's callback entirely left
    // every pad at 0x0 and the whole suite stayed green.
    {
        auto placed = 0;
        const auto strip = grid.getLayout().rows[2].pads;

        for (int step = 0; step < grid.getStepCount(); ++step)
            if (auto* pad = grid.padFor (2, step))
                if (! pad->getBounds().isEmpty() && strip.intersects (pad->getBounds()))
                    ++placed;

        checkEqual (placed, grid.getStepCount(),
                    "every pad the rebuild created is placed inside its row's strip");
    }

    // DIMMING, which a fresh pad also does not carry.
    {
        auto dimmed = 0;

        for (int step = 0; step < grid.getStepCount(); ++step)
            if (auto* pad = grid.padFor (2, step); pad != nullptr && pad->isDimmed())
                ++dimmed;

        checkEqual (dimmed, grid.getStepCount(),
                    "and carries the row's dim — a STEPS change can land while a channel is muted "
                    "or another row is isolated, and refreshRowStates edge-detects, so it would "
                    "not put it back");
    }

    grid.setIsolatedRow (-1);

    checkEqual (padsInFrontOfPlayhead(), 0,
                "and STILL none after the rebuild — `rebuild` destroys every pad and adds the "
                "replacements, which appends them to the HOST's child list, so a z-order set once "
                "at attach left the sweep line under 162 pads for the rest of the session");
}

/** 06-01 AC-1: both views follow ONE publication, in lockstep.

    The check that a behaviour-agreement guard cannot make. Two independent
    followers reach the same generation EVENTUALLY — each one polls, each one
    notices, each one refreshes. A shared follower reaches it per view per poll,
    and the generations are equal at every point where both have been polled,
    because they are recorded by the same line of code against the same snapshot.

    This is still not a proof of one implementation — nothing observable is — but
    it is the only assertion here that exercises the shared follower rather than
    the two views' agreement about what a click means. `getGeneration()` exists
    for it; /code-review found the accessor documented "for the tests" with no
    test calling it. */
void testTheTwoViewsFollowOnePublication()
{
    section ("the grid and the overlay follow one publication, not each other");

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    auto& grid = chassis.getSequencerGrid();
    auto& overlay = chassis.getKitOverlay();

    overlay.setOpen (true);
    overlay.advanceEntrance (1.0);

    grid.refreshIfStateChanged();
    overlay.pollForTest();

    const auto gridGeneration = grid.getPadGeneration();

    checkEqual (overlay.getPadGeneration(), gridGeneration,
                "both views start on the same publication");

    // THREE writes that are neither view's, each one a real writer: a host
    // recall replaces the lanes wholesale, a profile load will do the same in
    // 06-03, and a click in the other view arrives the same way.
    for (int write = 1; write <= 3; ++write)
    {
        {
            auto state = processor.lockPatternState();
            state->lanes[0][static_cast<size_t> (write)] = static_cast<std::uint8_t> (40 * write);
        }

        grid.refreshIfStateChanged();
        overlay.pollForTest();

        const auto moved = grid.getPadGeneration();

        // The RIGHT value, not "not the wrong one". The first version carried a
        // `|| write == 1` escape hatch that made the opening iteration
        // unconditionally true. Each write changes one lane, so the publication
        // advances by exactly one. /simplify.
        checkEqual (moved, gridGeneration + static_cast<std::uint32_t> (write),
                    "the publication advanced by exactly one");

        checkEqual (overlay.getPadGeneration(), moved,
                    juce::String ("and after write ") + juce::String (write)
                        + " both views are on the SAME publication, not merely on the same "
                          "pattern — one follower records it, against the snapshot it belongs to");
    }

    // One view polled, the other not — which DISCRIMINATES. The earlier version
    // wrote a lane, polled nothing, and asserted the two generations were still
    // equal, which the checkEqual above had just established and which an
    // unpolled write cannot disturb. /simplify.
    {
        const auto both = grid.getPadGeneration();

        {
            auto state = processor.lockPatternState();
            state->lanes[0][7] = 99;
        }

        grid.refreshIfStateChanged();

        check (grid.getPadGeneration() != both, "the grid picked the write up");

        checkEqual (overlay.getPadGeneration(), both,
                    "and the overlay is still on the older publication until IT is polled — each "
                    "view records what it has seen, so one following does not silently mark the "
                    "other up to date");

        overlay.pollForTest();

        checkEqual (overlay.getPadGeneration(), grid.getPadGeneration(),
                    "and catches up when it is");
    }

    overlay.setOpen (false);
}

/** 06-01: the contract `PatternPads` has to keep, and the one check that can
    tell a shared follower from two that agree.

    THE FIRST HALF OF THIS PASSES AGAINST THE PRE-REFACTOR TREE, and saying so is
    the point. Every symbol it touches existed at 98cd6d8 and every assertion held
    there — the two `toggleCell` bodies did agree. So it is a CONTRACT GUARD: it
    pins what the extraction must not change, and it would have caught the
    extraction getting it wrong. It is not evidence that there is one
    implementation; nothing observable at runtime can be, because a correct
    refactor is by definition invisible. That claim is carried by the type system
    — both views hold a `PatternPads` and there is no second `toggleCell` — and by
    the twenty byte-identical reference renders. Found by /code-review, which
    pointed out the original title claimed what the body could not show. */
void testPatternPadsKeepsTheContract()
{
    section ("the grid and the kit overlay agree on every lane they both touch");

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    auto& grid = chassis.getSequencerGrid();
    auto& overlay = chassis.getKitOverlay();

    {
        auto state = processor.lockPatternState();

        for (auto& lane : state->lanes)
            lane.fill (0);
    }

    grid.refreshFromState();

    const auto velocityOf = [&] (int lane, int step)
    {
        return storedVelocity (processor, lane, step);
    };

    // THE MECHANISM, not a fourth copy of it. `detail::compositeEditLane()`
    // resolves caixa by name at compile time and is `static_assert`ed to exist,
    // which is also why no runtime check on it appears here — it could only ever
    // add a pass. A local lambda scanning `ids::lanes` with juce::String
    // comparisons named that helper in its own comment and then re-implemented
    // it. /simplify.
    constexpr auto caixa = forrobox::detail::compositeEditLane();

    // The collapsed BATERIA row's own write is NOT re-tested here: the existing
    // testGridEditsThePattern already clicks that row at this step and asserts
    // caixa carries the hit AND the other three kit lanes do not. /simplify.
    //
    // ── the two views write the SAME lane through the SAME path ────────────
    {
        overlay.setOpen (true);
        overlay.advanceEntrance (1.0);

        // The kit row whose lane IS caixa, found by lane rather than by index —
        // the row order is bound to `lanesForRow` by a static_assert, and a test
        // that hard-coded row 1 would pass for the wrong reason if it changed.
        const auto& covered = forrobox::lanesForRow (forrobox::detail::compositeChannel());

        auto caixaRow = -1;

        for (int row = 0; row < covered.size(); ++row)
            if (covered.entries[static_cast<size_t> (row)] == caixa)
                caixaRow = row;

        // Likewise pinned at compile time: `KitOverlay.cpp`'s static_assert binds
        // the composite cover to bb, cx, hh, tom in that order.

        auto* kitPad = overlay.padFor (caixaRow, 5);
        check (kitPad != nullptr, "and a pad at the same step");

        kitPad->onClick();

        checkEqual (velocityOf (caixa, 5), forrobox::seq::kToggleOnVelocity,
                    "clicking the overlay's CX row writes the same lane, at the same velocity, "
                    "through the same PatternPads::toggle");
    }

    // ── one lane, two views, and BOTH follow it ────────────────────────────
    //
    // The bug that was fixed twice: a view that reads the pattern once and never
    // again. With one follower there is one place for that to be wrong.
    {
        grid.refreshIfStateChanged();
        overlay.pollForTest();

        const auto bateriaRow = ChassisLayout::kNumStrips - 1;

        checkEqual (grid.padFor (bateriaRow, 5)->getVelocity(),
                    forrobox::seq::kToggleOnVelocity,
                    "the collapsed row shows the overlay's edit");

        // A writer that is NEITHER view — a host recall arrives this way.
        {
            auto state = processor.lockPatternState();
            state->lanes[static_cast<size_t> (caixa)][5] = 0;
        }

        grid.refreshIfStateChanged();
        overlay.pollForTest();

        checkEqual (grid.padFor (bateriaRow, 5)->getVelocity(), 0,
                    "and both views follow a writer that is neither of them");

        const auto& covered = forrobox::lanesForRow (forrobox::detail::compositeChannel());

        for (int row = 0; row < covered.size(); ++row)
            if (covered.entries[static_cast<size_t> (row)] == caixa)
                checkEqual (overlay.padFor (row, 5)->getVelocity(), 0, "including the overlay's");

        overlay.setOpen (false);
    }

    // Each kit row writing its OWN lane is not re-tested here either:
    // testKitOverlayEditsFourLanes already sequences all four individually and
    // asserts caixa carries exactly one of them. /simplify.
}

/** 05-04 AC-4/AC-5: mute dims a row, and the row-label isolate is visual only. */
void testRowDimmingAndIsolate()
{
    section ("a muted row dims to 32%, and the isolate dims the others without touching audio");

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    auto& grid = chassis.getSequencerGrid();
    auto& apvts = processor.getAPVTS();

    // A pattern, so the rows have LIT pads to measure. A fresh instance stores
    // an empty grid (ids::defaultProfile says so plainly), and an empty row's
    // dim is the hardest thing in this frame to see.
    {
        auto state = processor.lockPatternState();

        if (const auto* profile = forrobox::findProfile (forrobox::ids::defaultProfile))
            forrobox::applyProfile (*state, *profile);
    }

    grid.refreshFromState();

    const auto dimmedRows = [&]
    {
        std::array<bool, ChassisLayout::kNumStrips> out {};

        for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
            out[static_cast<size_t> (row)] = grid.isRowDimmed (row);

        return out;
    };

    // Every pad in the row, and measured in INK as well as in the flag: a
    // version that set the flag and painted at full brightness passes any check
    // that reads the flag back.
    const auto rowPadsDimmed = [&] (int row)
    {
        auto all = grid.getStepCount() > 0;

        for (int step = 0; step < grid.getStepCount(); ++step)
            if (auto* pad = grid.padFor (row, step))
                all = all && pad->isDimmed();

        return all;
    };

    // Rendered through the GRID, against its own opaque ground — not each pad
    // into its own transparent image. `inkMass` sums brightness, and
    // `Image::getPixelAt` returns an un-premultiplied colour, so a pad painted
    // at 0.32 alpha over transparency reports the SAME brightness as one painted
    // at 1.0. That instrument could not report the difference it exists to
    // measure, which is this project's own recurring shape.
    const auto rowPadInk = [&] (int row)
    {
        const auto strip = grid.getLayout().rows[static_cast<size_t> (row)].pads;
        const auto image = renderComponent (grid, grid.getWidth(), grid.getHeight());

        return contrastMass (image, strip,
                             pixelAt (image, strip.getX(), juce::jmax (0, strip.getY() - 3)));
    };

    grid.refreshRowStates();

    // ── nothing dimmed at rest ─────────────────────────────────────────────
    {
        for (const auto dim : dimmedRows())
            check (! dim, "no row is dimmed with nothing muted and nothing isolated");

        checkEqual (grid.getIsolatedRow(), -1, "and no row is isolated");
    }

    // ── MUTE ───────────────────────────────────────────────────────────────
    {
        auto* mute = apvts.getParameter (forrobox::ids::channelParam ("pandeiro",
                                                                     forrobox::ids::mute));
        check (mute != nullptr, "the pandeiro mute parameter exists");

        mute->setValueNotifyingHost (1.0f);
        grid.refreshRowStates();

        const auto dims = dimmedRows();

        check (dims[2], "muting PANDEIRO dims its row — PLANNING.md:589");
        check (rowPadsDimmed (2), "and every pad in it knows it");

        // Checked HERE, beside the snapshot it reads. It used to sit forty lines
        // and a mute round-trip below, holding only because that round trip
        // happened to be symmetric. /simplify.
        check (! dims[0] && ! dims[1] && ! dims[3] && ! dims[4],
               "and only that row: mute is per channel");

        // The LABEL too, measured off the pixels. css:461 dims `.seq-row`, which
        // is the chip and the name as well as the pads — and the label is
        // painted by the grid rather than being a component, so nothing about
        // the pads' component alpha reaches it. A version that dimmed only the
        // pads passes every check above.
        {
            const auto& box = grid.getLayout().rows[2].label;
            const auto image = renderComponent (grid, grid.getWidth(), grid.getHeight());

            // A known background taken from the same render, just left of the
            // label: `inkMass` is meaningful only over black, and this ground is
            // `--panel`-ish, where any threshold low enough to see text is one an
            // empty region also clears.
            const auto ground = pixelAt (image, juce::jmax (0, box.getX() - 2), box.getCentreY());

            const auto dimmedInk = contrastMass (image, box, ground);

            mute->setValueNotifyingHost (0.0f);
            grid.refreshRowStates();

            const auto fullInk = contrastMass (
                renderComponent (grid, grid.getWidth(), grid.getHeight()), box, ground);

            check (fullInk > 0.0, "the row label paints ink at all");

            const auto ratio = dimmedInk / fullInk;

            check (ratio > 0.2 && ratio < 0.5,
                   "and a muted row's label paints about a third of it (ratio "
                       + juce::String (ratio, 3) + ") — the chip and the name dim with the pads");

            mute->setValueNotifyingHost (1.0f);
            grid.refreshRowStates();
        }


        // SOLO's precedence, through the same resolver the engine renders with.
        auto* solo = apvts.getParameter (forrobox::ids::channelParam ("zabumba",
                                                                      forrobox::ids::solo));
        check (solo != nullptr, "the zabumba solo parameter exists");

        solo->setValueNotifyingHost (1.0f);
        grid.refreshRowStates();

        const auto soloed = dimmedRows();

        check (! soloed[0], "soloing ZABUMBA leaves its own row lit");
        check (soloed[1] && soloed[2] && soloed[3] && soloed[4],
               "and dims every other row — a soloed-out channel is silent, so it dims for the "
               "same reason a muted one does");

        solo->setValueNotifyingHost (0.0f);
        mute->setValueNotifyingHost (0.0f);
        grid.refreshRowStates();

        for (const auto dim : dimmedRows())
            check (! dim, "clearing both undims everything");
    }

    // ── ISOLATE, by clicking the row LABEL ─────────────────────────────────
    {
        const auto& label = grid.getLayout().rows[3].label;

        check (! label.isEmpty(), "the GANZA row reserves a label box");

        check (dynamic_cast<forrobox::HitZone*> (clickInside (grid, label.getCentre())) != nullptr,
               "a real click on the row label box lands on a HitZone — a zone at the wrong "
               "bounds, left invisible, or buried by a sibling would put something else here");

        checkEqual (grid.getIsolatedRow(), 3, "clicking a row label isolates it — PLANNING.md:591");

        const auto dims = dimmedRows();

        check (! dims[3], "the isolated row stays at full opacity");
        check (dims[0] && dims[1] && dims[2] && dims[4],
               "and the other four dim to 32% — app.js:518");

        // The PIXELS, not the flag. The same row measured dimmed and undimmed,
        // so the ratio is the dim itself and not a difference between two rows'
        // patterns.
        {
            const auto dimmedInk = rowPadInk (0);

            grid.setIsolatedRow (-1);
            const auto fullInk = rowPadInk (0);
            grid.setIsolatedRow (3);

            check (fullInk > 0.0, "the row's lit pads paint ink at all");

            const auto ratio = dimmedInk / fullInk;

            check (ratio > 0.2 && ratio < 0.5,
                   "and a non-isolated row's pads paint about a third of it (ratio "
                       + juce::String (ratio, 3)
                       + ") — the pads dim, not a rectangle painted over the row: the playhead "
                         "is a sibling that crosses all five");
        }

        // Only one at a time — app.js:509 assigns, it does not accumulate.
        clickInside (grid, grid.getLayout().rows[1].label.getCentre());

        checkEqual (grid.getIsolatedRow(), 1, "isolating another row moves the isolate");
        check (! grid.isRowDimmed (1) && grid.isRowDimmed (3),
               "and the one it was taken from dims: only ONE row can be isolated");

        // Clicking the isolated row again clears it.
        clickInside (grid, grid.getLayout().rows[1].label.getCentre());

        checkEqual (grid.getIsolatedRow(), -1, "clicking it again clears the isolate — app.js:509");

        for (const auto dim : dimmedRows())
            check (! dim, "and every row returns to full opacity");

        // ── the HOVER, which nothing else in the suite sends ───────────────
        //
        // css:458 raises a hovered row label to `--fg`, the same colour css:460
        // gives the isolated one. Measured in ink, because the whole behaviour
        // IS a colour: `--fg-dim` is `rgba(232,232,232,0.5)` against `--fg`'s
        // full value, so a hovered label paints about twice the contrast.
        {
            grid.setIsolatedRow (-1);

            const auto& box = grid.getLayout().rows[2].label;

            const auto labelInk = [&]
            {
                const auto image = renderComponent (grid, grid.getWidth(), grid.getHeight());

                return contrastMass (image, box,
                                     pixelAt (image, juce::jmax (0, box.getX() - 2),
                                              box.getCentreY()));
            };

            const auto atRest = labelInk();

            hoverInside (grid, box.getCentre(), true);

            const auto hovered = labelInk();

            check (atRest > 0.0, "the row label paints at rest");
            check (hovered > atRest * 1.2,
                   "and brighter under the pointer (" + juce::String (hovered / atRest, 3)
                       + "x) — css:458, `.seq-rowlabel:hover { color: var(--fg) }`");

            hoverInside (grid, box.getCentre(), false);

            check (std::abs (labelInk() - atRest) < 1.0e-6,
                   "and returns exactly to rest when the pointer leaves: enter and exit are one "
                   "`setHovered` on the zone, so the two cannot drift apart");

            // Moving WITHIN the head row is not moving onto a label. There is no
            // zone there, so this asserts the ABSENCE of one rather than a
            // handler declining — `hoverInside` returns null and paints nothing.
            check (dynamic_cast<forrobox::HitZone*> (
                       hoverInside (grid, grid.getLayout().stepsLabel.getCentre(), true)) == nullptr,
                   "the head row carries no hit zone — `getComponentAt` answers with the grid "
                   "itself where no child sits, so this is a claim about the TYPE");

            check (std::abs (labelInk() - atRest) < 1.0e-6,
                   "and a pointer somewhere else leaves every label at rest");
        }

        // A click on the PADS is not a click on the label.
        const auto& pads = grid.getLayout().rows[2].pads;

        // THROUGH the hierarchy. `grid.mouseUp` would now reach nothing at all,
        // so this check would pass against a grid with no zones anywhere —
        // which is the shape of check this project keeps finding. Routed, it
        // asserts that what sits over the pad strip is a PAD, not a label zone.
        check (dynamic_cast<forrobox::HitZone*> (componentAt (grid, pads.getCentre())) == nullptr,
               "no hit zone covers the pad strip");
        checkEqual (grid.getIsolatedRow(), -1,
                    "clicking the pad strip does not isolate — css:460 binds it to .seq-rowlabel");
    }

    // ── EITHER dims, not both ──────────────────────────────────────────────
    {
        auto* mute = apvts.getParameter (forrobox::ids::channelParam ("ganza",
                                                                     forrobox::ids::mute));

        mute->setValueNotifyingHost (1.0f);
        clickInside (grid, grid.getLayout().rows[3].label.getCentre());
        grid.refreshRowStates();

        checkEqual (grid.getIsolatedRow(), 3, "GANZA is isolated");
        check (grid.isRowDimmed (3),
               "and STILL dimmed, because it is muted: the row dims if EITHER says so, and an "
               "isolated channel that is silent has not stopped being silent");

        mute->setValueNotifyingHost (0.0f);
        clickInside (grid, grid.getLayout().rows[3].label.getCentre());
        grid.refreshRowStates();
    }

    // ── AUDIO-NEUTRAL ──────────────────────────────────────────────────────
    //
    // The claim in PLANNING.md:592 is "without affecting audio". Comparing the
    // two renders SAMPLE FOR SAMPLE is the only check that can carry it: an
    // isolate that silenced four channels would still produce a buffer, and one
    // that merely changed a gain would still produce sound.
    {
        {
            auto state = processor.lockPatternState();

            for (auto& lane : state->lanes)
                for (int step = 0; step < forrobox::State::kMaxSteps; ++step)
                    lane[static_cast<size_t> (step)] =
                        static_cast<std::uint8_t> (step % 2 == 0 ? 100 : 0);
        }

        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;

        const auto render = [&] (int blocks)
        {
            juce::AudioBuffer<float> captured (2, blocks * block.getNumSamples());

            // prepareToPlay AND a stop/start, because they clear different
            // things. The stop/start resets the clock and the humanisation key;
            // it deliberately does NOT cut sounding voices (scheduleBlock says
            // why — a tail finishing after the transport stops is what every
            // instrument does), so the previous render's zabumba would still be
            // ringing over the first blocks of the next one. prepareToPlay is
            // the only hard clear. Without it two IDENTICAL renders diverge by
            // 0.72, which is what the control below would have reported.
            processor.prepareToPlay (48000.0, block.getNumSamples());
            processor.setPlaying (false);
            processor.setPlaying (true);

            for (int i = 0; i < blocks; ++i)
            {
                block.clear();
                midi.clear();
                processor.processBlock (block, midi);

                for (int channel = 0; channel < 2; ++channel)
                    captured.copyFrom (channel, i * block.getNumSamples(),
                                       block, channel, 0, block.getNumSamples());
            }

            return captured;
        };

        // Humanisation is seeded, and `reset` rewinds it — the same reason
        // VoiceEngine::reset exists. So two renders from the same state are
        // sample-identical, and any difference is the isolate's doing.
        // `fbtest::maxDifference` (TestHarness.h:368), which VoiceTest.cpp uses for
        // the same "two renders are bit-identical" claim — and which also returns
        // -1 for mismatched buffer shapes, which a local lambda did not.
        const auto before = render (8);

        // THE CONTROL, and not a formality: two renders with nothing changed
        // between them diverged by 0.72 before this rig hard-cleared the voices,
        // and the isolate comparison below would have reported that as the
        // isolate's doing. A comparison instrument has to be shown capable of
        // reading zero before its zero means anything.
        checkEqual (fbtest::maxDifference (before, render (8)), 0.0f,
                    "two renders from the same state are sample-identical — the humanisation is "
                    "a hash of the step, not a stream, and prepareToPlay clears the tails");

        clickInside (grid, grid.getLayout().rows[0].label.getCentre());
        checkEqual (grid.getIsolatedRow(), 0, "ZABUMBA isolated for the audio comparison");

        const auto after = render (8);

        // Without this the comparison would pass on two silent buffers, which is
        // the shape 05-02's fill check had: identical at both levels because it
        // was measuring the wrong ground.
        check (fbtest::bufferPeak (before) > 1.0e-4f,
               "the render is not silence — otherwise the comparison proves nothing");

        checkEqual (fbtest::maxDifference (before, after), 0.0f,
                    "the isolate changes NOT ONE SAMPLE — PLANNING.md:592, 'without affecting "
                    "audio'. It is a focus aid for editing, so it lives in the grid and not in "
                    "State and not in a parameter");
    }
}

/** 05-04 AC-3: the entrance is driven, and the open overlay follows the pattern. */
void testKitOverlayEntranceIsDriven()
{
    section ("the kit overlay's entrance advances on the chassis poll, and it follows the state");

    ForroBoxLookAndFeel lnf { theme::Mode::dark };

    // ── the curve, with no processor at all ────────────────────────────────
    //
    // The overlay is TOLD an interval and never reads a clock, so this walks the
    // entrance to any point without waiting — 04-04, where three checks failed
    // on MSVC's clock rather than on the code.
    {
        forrobox::KitOverlay overlay { lnf };
        overlay.setBounds (0, 0, ChassisLayout::kWidth, ChassisLayout::kHeight);

        overlay.setOpen (true);
        checkEqual (overlay.getEntranceProgress(), 0.0, "opening starts the entrance from zero");

        overlay.advanceEntrance (forrobox::kit::kEntranceSeconds * 0.5);
        checkEqual (overlay.getEntranceProgress(), 0.5,
                    "half the duration is half the progress — the EASE is applied to the offset, "
                    "not to the clock");

        overlay.advanceEntrance (forrobox::kit::kEntranceSeconds);
        checkEqual (overlay.getEntranceProgress(), 1.0, "and it saturates at rest");
    }

    // ── what the entrance actually PAINTS, at 0, 0.5 and 1 ─────────────────
    //
    // css:564-566 animates BOTH `transform: translateX(24px)` and `opacity: 0`.
    // Progress is the number the code holds; these are the two the user sees, so
    // they are measured off the rendered pixels rather than read back off the
    // member that produced them.
    {
        forrobox::KitOverlay overlay { lnf };

        const auto renderAt = [&] (double point)
        {
            overlay.setOpen (false);
            overlay.setOpen (true);
            overlay.advanceEntrance (forrobox::kit::kEntranceSeconds * point);

            return renderComponent (overlay, ChassisLayout::kWidth, ChassisLayout::kHeight);
        };

        // The panel's left edge, as the column with the STEEPEST horizontal step
        // in brightness. Not "the first column that differs from the scrim": the
        // panel's `-20px 0 60px` drop shadow reaches 60 px to its left, and a
        // first-difference scan lands on the shadow's outer fringe — it reported
        // 562 for a panel at 580. The shadow is a smooth ramp and the panel's
        // 1 px `--line-strong` border is a hard edge, so the steepest step is
        // the border whatever the shadow is doing.
        //
        // Returns the strength with the column, so a frame where the panel is
        // not painted at all can be told from one where it is.
        struct Edge { int x; float strength; };

        const auto panelEdgeIn = [] (const juce::Image& image, int y)
        {
            Edge best { -1, 0.0f };

            for (int x = 1; x < image.getWidth(); ++x)
            {
                const auto step = std::abs (pixelAt (image, x, y).getBrightness()
                                            - pixelAt (image, x - 1, y).getBrightness());

                if (step > best.strength)
                    best = { x, step };
            }

            return best;
        };

        const auto y = ChassisLayout::kHeight / 2;

        // The rest frame, rendered ONCE. It was rendered three times in this block
        // — a full 1200x780 paintEntireComponent each. /simplify.
        const auto lit = renderAt (1.0);
        const auto resting = panelEdgeIn (lit, y);

        checkEqual (resting.x, ChassisLayout::kWidth - forrobox::kit::kPanelWidth,
                    "at rest the panel sits at its layout position — translateX(0), css:566");

        {
            // css:557 gives `.subview` no transition, so the scrim is at full
            // strength from the first frame and only the PANEL fades in. At
            // progress 0 the chassis is dimmed and there is no panel on it.
            const auto image = renderAt (0.0);

            // Everything inside the panel's box measured against the SCRIM
            // beside it. The edge finder above answers "where is the panel",
            // which one blank row of it can answer wrongly; this answers "is
            // there anything there at all", over the whole box, which is the
            // question the children make interesting.
            const auto box = overlay.getLayout().panel;
            const auto scrimGround = pixelAt (image, 10, y);

            const auto atStart = contrastMass (image, box, scrimGround);
            const auto atRest  = contrastMass (lit,   box, scrimGround);

            check (atRest > 0.0, "the panel paints something at rest");

            check (atStart < atRest * 0.02,
                   "and NOTHING at progress 0 (" + juce::String (atStart / atRest, 4)
                       + " of it) — css:565 starts the panel at opacity 0, and its CHILDREN start "
                         "there too: the pads and the close button are components, which "
                         "paintEntireComponent draws whatever the panel behind them is doing");

            check (panelEdgeIn (image, y).strength < resting.strength * 0.1f,
                   "so there is no panel edge to find either");

            check (std::abs (scrimGround.getBrightness()
                             - pixelAt (lit, 10, y).getBrightness()) < 1.0e-3f,
                   "the scrim, meanwhile, is identical at both ends — css:557 declares no "
                   "transition on .subview, so the chassis dims at once and the panel arrives "
                   "over a scrim that is already there");

            check (scrimGround.getAlpha() > 0,
                   "and it is actually painted, not merely unchanging");
        }

        {
            const auto image = renderAt (0.5);
            const auto half = panelEdgeIn (image, y).x;

            // Tied to the CURVE, not to whatever the implementation produced:
            // the expected offset is the spec's own easing evaluated at 0.5, the
            // same function testEntranceEasingIsTheSpecCurve pins against the
            // control points.
            const auto expected = resting.x + juce::roundToInt (
                (1.0 - forrobox::kitEntranceEase (0.5)) * forrobox::kit::kEntranceOffset);

            checkEqual (half, expected,
                        "and halfway through it is pushed right by the eased fraction of 24 px");

            const auto litMass = inkMass (lit);
            const auto halfMass = inkMass (image);

            check (halfMass > 0.0 && halfMass < litMass,
                   "and it is partly transparent: opacity animates alongside the transform "
                   "(css:565), so a panel that only slid would measure the same ink at both");
        }
    }

    // ── the DRIVER: the chassis poll, with no processor attached ───────────
    {
        Chassis chassis { lnf };
        chassis.setBounds (0, 0, ChassisLayout::kWidth, ChassisLayout::kHeight);

        auto& overlay = chassis.getKitOverlay();
        overlay.setOpen (true);

        checkEqual (overlay.getEntranceProgress(), 0.0, "the entrance is at zero");

        // What DRIVES it in the plugin: the overlay's own poll, started by
        // setOpen. A test that only called advanceEntrance by hand would pass on
        // an overlay nothing ever ticks, which is exactly what Task 1 shipped.
        check (overlay.isPolling(), "opening starts the overlay's own poll");

        // Two ticks with REAL time in between, because the tick reads the clock
        // (the animation does not) and two calls back to back would report an
        // interval of about zero — a check that could not fail.
        overlay.pollForTest();

        const auto start = juce::Time::getMillisecondCounterHiRes();

        while (juce::Time::getMillisecondCounterHiRes() - start < 6.0)
            {}

        overlay.pollForTest();

        // 6 ms of a 200 ms entrance is 0.03, so the bound is loose by a factor
        // of ten in both directions and still cannot pass on a driver that does
        // nothing: the ONLY way progress leaves zero is the poll advancing it.
        check (overlay.getEntranceProgress() > 0.0,
               "polling the chassis advances the entrance — nothing else drives it, and Task 1 "
               "shipped it undriven, so the panel painted at its 24 px offset forever");

        check (overlay.getEntranceProgress() < 1.0,
               "by the elapsed interval, not straight to rest: a driver that passed the whole "
               "duration every tick would finish on the first poll");

        // And it runs with NO processor attached at all.
        check (overlay.isVisible(), "with no processor attached at all");

        overlay.setOpen (false);

        check (! overlay.isPolling(),
               "and closing stops it: a shut panel has no entrance to advance and no pattern to "
               "follow, so it does no work at all rather than 60 wake-ups a second for the "
               "plugin's life");
    }

    // ── the open overlay FOLLOWS the pattern ───────────────────────────────
    {
        ChassisRig rig;
        auto& processor = rig.processor;
        auto& chassis   = rig.chassis;

        auto& overlay = chassis.getKitOverlay();

        {
            auto state = processor.lockPatternState();

            for (auto& lane : state->lanes)
                lane.fill (0);
        }

        overlay.setOpen (true);
        overlay.advanceEntrance (1.0);

        auto* pad = overlay.padFor (0, 3);
        check (pad != nullptr, "the overlay has a pad at kit row 0, step 3");
        checkEqual (pad->getVelocity(), 0, "cleared");

        // A writer that is NOT the overlay — a host recall, a profile load and a
        // click in the collapsed row all arrive this way.
        {
            auto state = processor.lockPatternState();
            const auto& covered = forrobox::lanesForRow (forrobox::detail::compositeChannel());

            state->lanes[static_cast<size_t> (covered.entries[0])][3] = 77;
        }

        check (overlay.padFor (0, 3)->getVelocity() == 0,
               "and the overlay has not seen it yet — nothing told it");

        overlay.pollForTest();

        checkEqual (overlay.padFor (0, 3)->getVelocity(), 77,
                    "the poll pulls it in: Task 1's overlay read the state once at open and never "
                    "again, so a host recall left the four kit rows stale. Found by /code-review");

        // And a closed overlay does not spin on it.
        overlay.setOpen (false);

        {
            auto state = processor.lockPatternState();
            const auto& covered = forrobox::lanesForRow (forrobox::detail::compositeChannel());

            state->lanes[static_cast<size_t> (covered.entries[0])][3] = 0;
        }

        overlay.setOpen (true);
        checkEqual (overlay.padFor (0, 3)->getVelocity(), 0,
                    "a change made while it was shut is picked up by the reopen, which rebuilds "
                    "and refreshes on the way in");
    }
}

/** Clicking a pad edits the pattern the audio thread plays. */
void testGridEditsThePattern()
{
    section ("clicking a pad edits the stored pattern, and BATERIA writes caixa");

    ChassisRig rig;
    auto& processor = rig.processor;
    auto& chassis   = rig.chassis;

    auto& grid = chassis.getSequencerGrid();

    {
        auto state = processor.lockPatternState();

        for (auto& lane : state->lanes)
            lane.fill (0);

        state->dirty = false;
    }

    grid.refreshFromState();


    const auto clickPad = [] (StepPad& pad)
    {
        const auto e = mouseEventOn (pad, pad.getLocalBounds().getCentre().toFloat());
        pad.mouseDown (e);
        pad.mouseUp (e);
    };

    const auto storedAt = [&processor] (int lane, int step)
    {
        auto state = processor.lockPatternState();
        return static_cast<int> (state->lanes[(size_t) lane][(size_t) step]);
    };

    // ── a simple row toggles its own lane, on then off ─────────────────────
    {
        auto* pad = grid.padFor (1, 3);
        check (pad != nullptr, juce::String ("TRIANGULO step 3 has a pad"));

        if (pad == nullptr)
            return;

        const auto lane = forrobox::writeLaneForRow (1);

        checkEqual (storedAt (lane, 3), 0, "it starts silent");

        clickPad (*pad);
        // The constant itself, against the design source. The four assertions
        // below read `seq::kToggleOnVelocity` — correct for proving a click
        // writes ON, but they would all still pass if the constant were 7.
        // app.js:392 is the only place the number is decided, and until now
        // nothing anywhere pinned it. Found by /simplify.
        checkEqual (forrobox::seq::kToggleOnVelocity, 100,
                    "app.js:392 — togglePad writes 100 on, not 127");
        checkEqual (forrobox::seq::kToggleOffVelocity, 0, "and 0 off");

        checkEqual (storedAt (lane, 3), forrobox::seq::kToggleOnVelocity,
                    "one click writes the toggle-on velocity");
        checkEqual (pad->getVelocity(), forrobox::seq::kToggleOnVelocity,
                    "and the pad follows the state it just wrote");

        clickPad (*pad);
        checkEqual (storedAt (lane, 3), 0, "a second click clears it");
        checkEqual (pad->getVelocity(), 0, "and the pad follows");
    }

    // ── the BATERIA row writes CAIXA and leaves the other three alone ──────
    {
        auto* pad = grid.padFor (4, 5);
        check (pad != nullptr, "BATERIA step 5 has a pad");

        if (pad == nullptr)
            return;

        clickPad (*pad);

        const auto caixa = forrobox::writeLaneForRow (4);

        checkEqual (storedAt (caixa, 5), forrobox::seq::kToggleOnVelocity,
                    "clicking the BATERIA row writes CAIXA");

        for (const auto lane : forrobox::lanesForRow (4))
            if (lane != caixa)
                checkEqual (storedAt (lane, 5), 0,
                            juce::String ("and leaves ") + forrobox::ids::lanes[(size_t) lane]
                                + " untouched — writing all four would make one click destroy "
                                  "a pattern");
    }

    // ── the edit sets `dirty` ──────────────────────────────────────────────
    {
        auto state = processor.lockPatternState();
        check (state->dirty,
               "an edited pattern is marked dirty — it no longer matches the profile it came "
               "from, which is what togglePad calls markCustom for");
    }

    // ── and it survives a save/reload round trip ───────────────────────────
    {
        juce::MemoryBlock saved;
        processor.getStateInformation (saved);

        ForroBoxAudioProcessor reopened;
        reopened.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

        const auto caixa = forrobox::writeLaneForRow (4);
        const auto triangulo = forrobox::writeLaneForRow (1);

        auto state = reopened.lockPatternState();

        checkEqual (static_cast<int> (state->lanes[(size_t) caixa][5]),
                    forrobox::seq::kToggleOnVelocity,
                    "the caixa edit survives a save and reload");
        checkEqual (static_cast<int> (state->lanes[(size_t) triangulo][3]), 0,
                    "and so does the cleared one");
        check (state->dirty, "and the dirty flag with them");
    }
}

void writeReferenceRenders()
{
    section ("reference renders for the listening-equivalent checkpoint");

    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile ("ui-renders");
    const auto created = out.createDirectory();

    check (created.wasOk(), "the render directory can be created: " + out.getFullPathName());

    const std::array<std::pair<theme::Mode, const char*>, 2> modes {{
        { theme::Mode::dark, "dark" }, { theme::Mode::light, "light" },
    }};

    const std::array<std::pair<const char*, float>, 3> scales {{
        { "1.0x", 1.0f }, { "1.5x", 1.5f }, { "2.0x", 2.0f },
    }};

    auto written = 0;

    for (const auto& [mode, modeName] : modes)
    {
        // DECLARATION ORDER IS LOAD-BEARING and this had it wrong. The chassis
        // holds a KnobAttachment per knob, and each deregisters from its
        // parameter when destroyed — so the processor that owns those
        // parameters must OUTLIVE the chassis. Declared the other way round,
        // the processor died first and the attachments deregistered from freed
        // parameters. Linux tolerated it; MSVC crashed the whole suite, which
        // is why three compilers are in the plan.
        ForroBoxAudioProcessor processor;
        ForroBoxLookAndFeel lnf { mode };
        ValueTooltip tooltip { lnf };
        Chassis chassis { lnf };

        // POPULATED, not bare. The first version rendered a chassis with no
        // attachParameters call, so the six PNGs handed to the visual
        // checkpoint showed empty strips — an artefact that understates the
        // work by exactly the thing 04-02 built. 04-01's rule: a checkpoint
        // artefact needs the same scrutiny as a test.
        //
        // And with a PATTERN, for the same reason one plan later. A fresh
        // instance NAMES campina as its active profile and stores an empty grid
        // — nothing applies the pattern at construction, which
        // ids::defaultProfile now says plainly and Phase 6 owns. Rendering that
        // would hand the checkpoint a sequencer of empty pads and understate
        // 05-01 exactly as the bare chassis understated 04-02.
        {
            auto state = processor.lockPatternState();

            if (const auto* profile = forrobox::findProfile (forrobox::ids::defaultProfile))
                forrobox::applyProfile (*state, *profile);
        }

        chassis.attachParameters (processor.getAPVTS(), &tooltip);
        chassis.getSequencerGrid().refreshFromState();

        chassis.setBounds (0, 0, ChassisLayout::kWidth, ChassisLayout::kHeight);

        // AND MID-GROOVE, for the reason the pattern is applied above. 05-02's
        // three deliverables — the playhead, the LEDs and the meters — are all
        // invisible on a stopped transport by design, so a render of one would
        // understate this plan exactly as the bare chassis understated 04-02
        // and the empty grid would have understated 05-01.
        //
        // Driven by RENDERING BLOCKS, not by poking the visualisers: what the
        // checkpoint is judging is the whole path from the audio thread's
        // publication to the pixels, and a render built by setting levels by
        // hand would look right while proving nothing about it.
        {
            processor.prepareToPlay (48000.0, 512);
            processor.setPlaying (true);

            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;

            // Enough blocks to land the sweep partway across the bar rather
            // than on a step boundary, so the render shows it BETWEEN pads —
            // which is the thing that distinguishes a continuous sweep from a
            // per-step jump.
            for (int i = 0; i < 26; ++i)
            {
                block.clear();
                midi.clear();
                processor.processBlock (block, midi);
            }

            chassis.getSequencerGrid().updatePlayhead();

            // The visualisers polled a few frames PAST their trigger, so the
            // meters are caught mid-decay at different levels per channel
            // rather than all at full — which is what the decay law looks like.
            for (int i = 0; i < 10; ++i)
                chassis.pollVisualisersForTest();
        }

        for (const auto& [scaleName, scale] : scales)
        {
            // Rendered THROUGH the transform, not by upscaling the 1x bitmap.
            // The first version did the latter under a comment claiming it was
            // what the editor does — and it is not: Component::setTransform
            // applies the transform to the Graphics context, so the chassis
            // paints at the target resolution and text and hairlines stay
            // crisp. Upscaling a bitmap makes both soft, which would have
            // handed the visual checkpoint an artefact that understates the
            // real thing.
            const auto width  = juce::roundToInt (ChassisLayout::kWidth * scale);
            const auto height = juce::roundToInt (ChassisLayout::kHeight * scale);

            juce::Image scaled (juce::Image::ARGB, width, height, true);
            {
                juce::Graphics g (scaled);

                // The transform goes on the GRAPHICS CONTEXT, not on the
                // component. `Component::setTransform` is applied by the parent
                // when it composites the child, so paintEntireComponent called
                // directly ignores it — the second attempt at this did that and
                // painted a 1200x780 chassis into the top-left quarter of a 2x
                // image. Adding it to the context is what the parent does, and
                // what makes the render vector-crisp rather than upscaled.
                g.addTransform (juce::AffineTransform::scale (scale));
                chassis.paintEntireComponent (g, false);
            }

            // The render is asserted, not just written. "Six PNGs exist" is an
            // assertion that cannot fail — and it did not fail while the 2x
            // render was a 1200x780 chassis in the corner of a 2400x1560 image,
            // which is exactly the artefact the visual checkpoint would have
            // been handed. So: the far corner must be painted, and the whole
            // frame must carry the regions it should.
            const auto farCorner = pixelAt (scaled, scaled.getWidth() - 2, scaled.getHeight() - 2);

            checkEqual (farCorner.getARGB(),
                        theme::colour (theme::Token::raised, mode).getARGB(),
                        juce::String ("the ") + modeName + " " + scaleName
                            + " render is painted all the way to its far corner (the footer)");

            // And the knobs are IN it. Measured in strip 1's first knob cell,
            // scaled — a render of a bare chassis would pass every check above
            // while showing the human empty strips.
            {
                const auto layout = ChassisLayout::forBounds ({ 0, 0, ChassisLayout::kWidth,
                                                                ChassisLayout::kHeight });
                const auto cell = layout.stripLayouts[0].knobCells[0];
                const auto centre = juce::Point<float> (static_cast<float> (cell.getCentreX()) * scale,
                                                        static_cast<float> (cell.getCentreY()) * scale);
                const auto knobScale = static_cast<double> (ChassisLayout::kStripKnobSize)
                                     / forrobox::knob::kViewBox;
                const auto arcR = forrobox::knob::kArcRadius * knobScale * scale;

                const auto band = arcProfile (scaled, centre,
                                              static_cast<float> (arcR - 3.0 * scale),
                                              static_cast<float> (arcR + 3.0 * scale),
                                              theme::colour (theme::Token::panel, mode));

                check (band.total > 0.0,
                       juce::String ("and the ") + modeName + " " + scaleName
                           + " render actually CONTAINS its knobs (arc ink "
                           + juce::String (band.total, 1) + ")");
            }

            const auto file = out.getChildFile (juce::String ("chassis-") + modeName + "-" + scaleName + ".png");
            file.deleteFile();

            juce::PNGImageFormat png;
            if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
                if (png.writeImageToStream (scaled, *stream))
                    ++written;
        }

        // ── 05-04's two states, at 1x, in the same populated chassis ────────
        //
        // The six above show neither: the kit panel is closed and no row is
        // dimmed, so a checkpoint handed only those would be judging 05-02's
        // work again. 04-01's rule — a checkpoint artefact needs the same
        // scrutiny as a test — so both are asserted, not merely written.
        const auto writeExtra = [&] (const juce::Image& image, const juce::String& stem)
        {
            const auto file = out.getChildFile (stem + "-" + modeName + ".png");
            file.deleteFile();

            juce::PNGImageFormat png;

            if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
                if (png.writeImageToStream (image, *stream))
                    ++written;
        };

        {
            auto& grid = chassis.getSequencerGrid();

            // BATERIA muted and TRIÂNGULO isolated: one row dim for each of the
            // two reasons, and three dim for the isolate, which is what makes
            // "dim if EITHER" visible in one frame.
            if (auto* mute = processor.getAPVTS().getParameter (
                    forrobox::ids::channelParam ("bateria", forrobox::ids::mute)))
                mute->setValueNotifyingHost (1.0f);

            grid.setIsolatedRow (1);
            grid.refreshRowStates();

            const auto image = renderComponent (chassis, ChassisLayout::kWidth,
                                                ChassisLayout::kHeight);

            const auto& bright = grid.getLayout().rows[1].label;
            const auto& dim    = grid.getLayout().rows[0].label;
            const auto origin  = chassis.getLayout().sequencer.getPosition();

            const auto inkIn = [&] (juce::Rectangle<int> box)
            {
                const auto shifted = box + origin;

                return contrastMass (image, shifted,
                                     pixelAt (image, juce::jmax (0, shifted.getX() - 2),
                                              shifted.getCentreY()));
            };

            check (inkIn (dim) < inkIn (bright) * 0.6,
                   juce::String ("the ") + modeName
                       + " isolate render actually shows a dimmed row beside the isolated one");

            writeExtra (image, "isolate");

            grid.setIsolatedRow (-1);

            if (auto* mute = processor.getAPVTS().getParameter (
                    forrobox::ids::channelParam ("bateria", forrobox::ids::mute)))
                mute->setValueNotifyingHost (0.0f);

            grid.refreshRowStates();
        }

        {
            auto& overlay = chassis.getKitOverlay();

            overlay.setOpen (true);
            overlay.advanceEntrance (1.0);   // at rest: the entrance is AC-3's, not this render's
            overlay.refreshFromState();

            const auto image = renderComponent (chassis, ChassisLayout::kWidth,
                                                ChassisLayout::kHeight);

            // The panel is really there AND really populated: a render of an
            // overlay that opened but painted no pads would pass "something is
            // at x=580" while showing the human an empty kit.
            const auto panel = overlay.getLayout().panel;

            check (contrastMass (image, panel, pixelAt (image, 10, ChassisLayout::kHeight / 2))
                       > 0.0,
                   juce::String ("the ") + modeName + " kit render has a panel on it");

            auto lit = 0;

            for (int row = 0; row < 4; ++row)
                for (int step = 0; step < 16; ++step)
                    if (auto* kitPad = overlay.padFor (row, step); kitPad != nullptr && kitPad->isLit())
                        ++lit;

            check (lit > 0, juce::String ("and the ") + modeName
                                + " kit panel shows the profile's own hits, not empty rows");

            writeExtra (image, "kit");

            overlay.setOpen (false);
        }
    }

    checkEqual (written, 10, "ten reference PNGs written (2 themes x 3 scales, + kit and isolate)");

    // ── the knob at its three specified sizes, for the same checkpoint ──────
    //
    // PLANNING.md:347 names 28 / 32 / 54 px, so all three are rendered rather
    // than just the strip's 32: the whole point of AC-1 is that one definition
    // serves three sizes, and a human can only see that if all three are here.
    // Each render carries a unipolar and a bipolar knob side by side, because
    // the arc difference is the thing worth looking at.
    auto knobsWritten = 0;

    for (const auto& [mode, modeName] : modes)
    {
        for (const auto dialSize : { 28, 32, 54 })
        {
            ForroBoxLookAndFeel lnf { mode };

            const auto pad = 10;
            const auto labelled = Knob::preferredHeight (dialSize, true);

            Ground row;
            row.ground = theme::colour (theme::Token::panel, mode);
            row.setSize (dialSize * 2 + pad * 3, labelled + pad * 2);

            Knob uni { lnf, dialSize, Knob::Polarity::unipolar,
                       theme::accent (theme::Accent::zabumba), "VOL" };
            Knob bip { lnf, dialSize, Knob::Polarity::bipolar,
                       theme::accent (theme::Accent::zabumba), "PAN" };

            uni.setProportion (0.7f);
            bip.setProportion (0.78f);

            row.addAndMakeVisible (uni);
            row.addAndMakeVisible (bip);
            uni.setBounds (pad, pad, dialSize, labelled);
            bip.setBounds (pad * 2 + dialSize, pad, dialSize, labelled);

            const auto image = renderComponent (row, row.getWidth(), row.getHeight());

            // Asserted, not merely written — 04-01's rule. Ink must exist in
            // BOTH knobs' arc bands, so a render with one knob missing (or
            // both painted at the same spot) fails here rather than at the
            // human's eye.
            const auto scale = static_cast<double> (dialSize) / forrobox::knob::kViewBox;
            const auto expected = forrobox::knob::kArcRadius * scale;
            const auto bandOf = [&] (const Knob& k)
            {
                return arcProfile (image,
                                   k.getBounds().toFloat().getTopLeft() + k.dialBounds().getCentre(),
                                   static_cast<float> (expected - 3.0),
                                   static_cast<float> (expected + 3.0),
                                   row.ground);
            };

            const auto uniBand = bandOf (uni);
            const auto bipBand = bandOf (bip);

            const auto label = juce::String (modeName) + " " + juce::String (dialSize) + " px";

            check (uniBand.total > 0.0 && bipBand.total > 0.0,
                   "the " + label + " knob render carries ink in BOTH knobs' arc bands");
            check (bipBand.over (10.0f, 135.0f) > bipBand.over (-135.0f, -10.0f),
                   "and its bipolar knob is past centre, sweeping right — so the render shows the "
                   "difference it exists to show (" + label + ": right "
                       + juce::String (bipBand.over (10.0f, 135.0f), 2) + " vs left "
                       + juce::String (bipBand.over (-135.0f, -10.0f), 2) + ")");

            const auto file = out.getChildFile (juce::String ("knob-") + modeName + "-"
                                                + juce::String (dialSize) + "px.png");
            file.deleteFile();

            juce::PNGImageFormat png;
            if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
                if (png.writeImageToStream (image, *stream))
                    ++knobsWritten;
        }
    }

    checkEqual (knobsWritten, 6, "six knob PNGs written (2 themes x 3 sizes)");

    // ── the pad's six states, side by side ─────────────────────────────────
    //
    // The artefact the checkpoint's first two steps read. Rendered at 4x, not
    // upscaled from a 1x bitmap: at 26 px the difference between "brightest at
    // 22% of the height" and "brightest in the middle" is five pixels, and the
    // whole point of step 2 is that a human can see it.
    auto padsWritten = 0;

    for (const auto& [mode, modeName] : modes)
    {
        constexpr int kPadWidth = 40;
        constexpr int kCaption = 14;
        constexpr int kScale = 4;

        ForroBoxLookAndFeel lnf { mode };

        const auto cellWidth = kPadWidth + pad::kLitGlowRadius * 2 + 10;
        const auto cellHeight = pad::kHeight + pad::kLitGlowRadius * 2 + kCaption + 10;

        Ground sheet;
        sheet.ground = theme::colour (theme::Token::panel, mode);
        sheet.setSize (cellWidth * static_cast<int> (padStates.size()), cellHeight);

        // Owned for the whole render; a vector of unique_ptr because a
        // std::array of StepPad would need a default constructor it has no
        // sensible value for.
        std::vector<std::unique_ptr<StepPad>> pads;

        for (size_t i = 0; i < padStates.size(); ++i)
        {
            auto stepPad = std::make_unique<StepPad> (lnf, theme::accent (theme::Accent::zabumba));

            stepPad->setVelocity (padStates[i].velocity);
            stepPad->setBeat (padStates[i].beat);

            const auto padRect = juce::Rectangle<int> (kPadWidth, pad::kHeight)
                                     .withCentre ({ static_cast<int> (i) * cellWidth + cellWidth / 2,
                                                    (cellHeight - kCaption) / 2 });

            stepPad->setBounds (StepPad::boundsForPadRect (padRect));
            sheet.addAndMakeVisible (*stepPad);

            if (padStates[i].hover)
            {
                stepPad->mouseEnter (mouseEventOn (*stepPad,
                                                   stepPad->getLocalBounds().getCentre().toFloat(),
                                                   {}, 0));
            }

            pads.push_back (std::move (stepPad));
        }

        juce::Image sheetImage (juce::Image::ARGB, sheet.getWidth() * kScale,
                                sheet.getHeight() * kScale, true);
        {
            juce::Graphics g (sheetImage);
            g.addTransform (juce::AffineTransform::scale (static_cast<float> (kScale)));
            sheet.paintEntireComponent (g, true);

            // The captions, drawn on top at 1x so they stay legible rather than
            // being blown up with the pads.
            g.addTransform (juce::AffineTransform::scale (1.0f / kScale));
            g.setColour (theme::colour (theme::Token::fgDim, mode));

            for (size_t i = 0; i < padStates.size(); ++i)
                type::drawTracked (g, type::Style::stripMicroLabel, padStates[i].caption,
                                   juce::Rectangle<int> (static_cast<int> (i) * cellWidth,
                                                         cellHeight - kCaption,
                                                         cellWidth, kCaption).toFloat() * (float) kScale,
                                   juce::Justification::centred);
        }

        // The artefact carries the claim step 2 asks a human to check. A
        // checkpoint image needs the same scrutiny as a test — 04-02 shipped
        // six PNGs of empty strips because it had not.
        const auto litPad = pads[1]->getBounds().reduced (pad::kLitGlowRadius) * kScale;

        const auto brightest = brightestRow (sheetImage, litPad.withTrimmedTop (kScale),
                                             theme::accent (theme::Accent::zabumba));

        check (kScale + brightest < litPad.getHeight() / 2,
               juce::String ("the ") + modeName + " pad sheet shows its lit pad brightest ABOVE "
                   "centre, which is what the checkpoint asks a human to confirm (row "
                   + juce::String (kScale + brightest) + " of " + juce::String (litPad.getHeight())
                   + ")");

        const auto file = out.getChildFile (juce::String ("pad-") + modeName + ".png");
        file.deleteFile();

        juce::PNGImageFormat png;
        if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
            if (png.writeImageToStream (sheetImage, *stream))
                ++padsWritten;
    }

    checkEqual (padsWritten, 2, "two step-pad sheets written, one per theme");

    // ── the logo mark, at 8x ───────────────────────────────────────────────
    //
    // 36x27 is too small to judge against the prototype by eye, and the
    // checkpoint's first step asks exactly that. The mark is scaled, not the
    // bitmap, so this also SHOWS the viewBox claim the tests measure.
    auto logosWritten = 0;

    for (const auto& [mode, modeName] : modes)
    {
        constexpr int kScale = 8;

        ForroBoxLookAndFeel lnf { mode };
        LogoMark mark { lnf };
        Ground sheet;

        sheet.ground = theme::colour (theme::Token::raised, mode);
        sheet.setSize (logo::kWidth + 8, logo::kHeight + 8);
        sheet.addAndMakeVisible (mark);
        mark.setBounds (4, 4, logo::kWidth, logo::kHeight);

        juce::Image image (juce::Image::ARGB, sheet.getWidth() * kScale,
                           sheet.getHeight() * kScale, true);
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (static_cast<float> (kScale)));
            sheet.paintEntireComponent (g, true);
        }

        const auto file = out.getChildFile (juce::String ("logo-") + modeName + ".png");
        file.deleteFile();

        juce::PNGImageFormat png;
        if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
            if (png.writeImageToStream (image, *stream))
                ++logosWritten;
    }

    checkEqual (logosWritten, 2, "two logo sheets written, one per theme");

    std::cout << "  renders: " << out.getFullPathName() << std::endl;
}

} // namespace

void runUiTests()
{
    std::cout << "\n=== UI ===" << std::endl;

    testMeasurementInstruments();
    testEmbeddedFonts();
    testTypeScale();
    testThemeTokens();
    testChassisGeometry();
    testChassisScalesAsOneTransform();
    testChassisSurfaces (theme::Mode::dark, "dark");
    testChassisSurfaces (theme::Mode::light, "light");
    testStripNamesAreDrawn();
    testKnobGeometryIsRelative();
    testKnobPolarities();
    testKnobGestures();
    testKnobAttachmentClearsOnlyItsOwnCallbacks();
    testKnobIsAViewOfItsParameter();
    testTwentyStripKnobsAreLive();
    testKnobGestureLifecycle();
    testButtonFamily (theme::Mode::dark, "dark");
    testButtonFamily (theme::Mode::light, "light");
    testStepPadInstruments();
    testStepPadStates (theme::Mode::dark, "dark");
    testStepPadStates (theme::Mode::light, "light");
    testEllipsis();
    testSegmented (theme::Mode::dark, "dark");
    testSegmented (theme::Mode::light, "light");
    testValueScreen (theme::Mode::dark, "dark");
    testValueScreen (theme::Mode::light, "light");
    testLogoMark (theme::Mode::dark, "dark");
    testLogoMark (theme::Mode::light, "light");
    testTransportButtonVariant (theme::Mode::dark, "dark");
    testTransportButtonVariant (theme::Mode::light, "light");
    testBpmFieldLaw();
    testBpmFieldUnderSync();
    testHostTempoIsPublished();
    testHostTransportGovernsUnderSync();
    testTransportButtonIsHostDrivenUnderSync();
    testTransportDrivesTheProcessor();
    testGlobalKnobGroup (theme::Mode::dark, "dark");
    testGlobalKnobGroup (theme::Mode::light, "light");
    testGlobalKnobsAreLive();
    testHeaderRightCluster();
    testEveryHeaderBoxIsFilled();
    testNonAsciiGlyphsExist();
    testCollectChildrenReportsRootSpaceBounds();
    testStripIsFinished();
    testMuteSoloAndGhostDriveParameters();
    testFaderIsAbsolute();
    testFaderPaintsItsValue();
    testGainReductionMeterInstrument();
    testGainReductionMeterGrowsFromTheRight (theme::Mode::dark, "dark");
    testGainReductionMeterGrowsFromTheRight (theme::Mode::light, "light");
    testFooterMasterAndLimiter();
    testGainReductionMeterReadsTheLimiter();
    testEveryFooterBoxIsReserved();
    testEveryFooterBoxIsFilled();
    testDragMidiIdlePulse();
    testDragMidiExportsWithoutMutatingState();
    testReadOnlySegmentedRefusesThePointer (theme::Mode::dark, "dark");
    testReadOnlySegmentedRefusesThePointer (theme::Mode::light, "light");
    testChoiceAttachmentWritesDenormalised();
    testOutputToggleDrivesTheParameter();
    testSequencerLayoutIsReserved();
    testGridShowsTheStoredPattern();
    testGridShowsTheFullStepWindow();
    testGridFollowsExternalWriters();
    testRefreshDoesNotLoseAConcurrentWrite();
    testStepChangeTilesWithoutAnEditor();
    testStepsButtonsFollowTheParameter();
    testEntranceEasingIsTheSpecCurve();
    testKitOverlayEditsFourLanes();
    testKitOverlayEntranceIsDriven();
    testARebuildRestoresWhatItReplaced();
    testAccentedStringsSurviveTheCompiler();
    testSidePanelControlsAreLive();
    testRightClickChangesNothingAnywhere();
    testProfileLoadIsAFullReload();
    testNoProfileReachesFullVelocity();
    testProfileLoadFlashesTheLitPads();
    testSidePanelLayoutAndActiveProfile();
    testPatternPadsKeepsTheContract();
    testTheTwoViewsFollowOnePublication();
    testRowDimmingAndIsolate();
    testClippedRepaintMatchesFullRepaint();
    testPlayheadSweepsTheClocksPosition();
    testPlayheadFollowsTheProcessor();
    testHitVisualiserLevelLaw();
    testHitVisualiserIsPainted();
    testMutedChannelsDoNotLightUp();
    testGridEditsThePattern();
    writeReferenceRenders();
}
