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

#include "TestHarness.h"
#include "TestSuites.h"

#include <FontData.h>

#include "Chassis.h"
#include "Knob.h"
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
using forrobox::ForroBoxLookAndFeel;
using forrobox::Knob;
namespace theme = forrobox::theme;
namespace type  = forrobox::type;

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
                                 : " — expected " + hex (expected) + " ±" + juce::String (slop)
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

        check (uni.over (-135.0f, -5.0f) > 0.0, "a unipolar half-travel arc fills the left sweep");
        checkEqual (uni.over (5.0f, 135.0f), 0.0, "and nothing to the right of centre");

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
        struct Row { const char* name; juce::Rectangle<int> box; int marginAbove; };

        const std::array<Row, 9> stack {{
            { "sampleSlot",    interior.sampleSlot,    ChassisLayout::kSampleSlotMarginTop },
            { "hitVisualiser", interior.hitVisualiser, ChassisLayout::kHitVisualiserMarginTop },
            { "dividerTop",    interior.dividerTop,    ChassisLayout::kStripDividerMargin },
            { "knobGrid",      interior.knobGrid,      ChassisLayout::kStripDividerMargin },
            { "dividerBottom", interior.dividerBottom, ChassisLayout::kStripDividerMargin },
            { "patternCycler", interior.patternCycler, ChassisLayout::kStripDividerMargin
                                                         + ChassisLayout::kPatternRowMarginTop },
            { "muteSolo",      interior.muteSolo,      ChassisLayout::kMuteSoloMarginTop },
            { "ghostLabel",    interior.ghostLabel,    ChassisLayout::kGhostRowMarginTop },
            { "ghostFader",    interior.ghostFader,    ChassisLayout::kGhostLabelGap },
            // subDots is strip-5-only, so it is asserted separately below.
        }};

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

        // Nothing overflows the strip. The slack that remains is the flex
        // column's own leftover and is expected to be positive; asserting it is
        // NON-NEGATIVE is what catches a stack that grew past the bottom pad.
        const auto lastBottom = interior.subDots.isEmpty() ? interior.ghostFader.getBottom()
                                                           : interior.subDots.getBottom();
        const auto slack = interior.controls.getBottom() - lastBottom;

        check (slack >= 0,
               label + "'s stack fits inside the strip with " + juce::String (slack)
                   + " px of flex slack left at the bottom");

        // The bateria sub-dots row exists on exactly one strip.
        const auto isBateria = (static_cast<theme::Accent> (i) == theme::Accent::bateria);

        if (isBateria)
        {
            check (! interior.subDots.isEmpty(), label + " is bateria, so its sub-dots row exists");
            checkEqual (interior.subDots.getY(),
                        interior.ghostFader.getBottom() + ChassisLayout::kSubDotsMarginTop,
                        label + "'s sub-dots follow the ghost fader by their margin");
            checkEqual (interior.subDots.getHeight(), ChassisLayout::kSubDotSize,
                        label + "'s sub-dots row is one 8 px circle tall");
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
            checkEqual (cell.getHeight(), ChassisLayout::kKnobCellHeight,
                        what + " is the dial plus its gap plus its micro-label");
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
    section ("the chassis surfaces read as their tokens — " + modeName);

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

    probe (layout.sidePanel, theme::Token::raised, "the side panel is --raised");
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
          knobComponent (lnf, dialSize, polarity, arcColour, std::move (label))
    {
        knobComponent.setProportion (proportion);

        // A black holder: `renderComponent` paints into a transparent image, and
        // the knob itself is not opaque, so without a ground the brightness
        // instruments would be measuring against alpha rather than a colour.
        holder.addAndMakeVisible (knobComponent);
        holder.setSize (dialSize, Knob::preferredHeight (dialSize, false));
        knobComponent.setBounds (holder.getLocalBounds());
    }

    struct BlackHolder final : juce::Component
    {
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
    };

    juce::Image render() { return renderComponent (holder, holder.getWidth(), holder.getHeight()); }

    juce::Point<float> centre() const
    {
        return knobComponent.dialBounds().getCentre();
    }

    ForroBoxLookAndFeel lnf;
    Knob                knobComponent;
    BlackHolder         holder;
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
               "a bipolar knob at centre is left/right symmetric — its value arc has no extent ("
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
        ForroBoxLookAndFeel lnf { mode };
        Chassis chassis { lnf };

        // Only the sizing was ever needed here — the full-size render it used
        // to do was discarded through `ignoreUnused`, left over from the
        // upscaling version the comment below describes removing.
        chassis.setBounds (0, 0, ChassisLayout::kWidth, ChassisLayout::kHeight);

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

            const auto file = out.getChildFile (juce::String ("chassis-") + modeName + "-" + scaleName + ".png");
            file.deleteFile();

            juce::PNGImageFormat png;
            if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
                if (png.writeImageToStream (scaled, *stream))
                    ++written;
        }
    }

    checkEqual (written, 6, "six reference PNGs written (2 themes x 3 scales)");

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

            struct Row final : juce::Component
            {
                void paint (juce::Graphics& g) override { g.fillAll (ground); }
                juce::Colour ground;
            };

            Row row;
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
    writeReferenceRenders();
}
