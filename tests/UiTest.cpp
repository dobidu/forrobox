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
#include "Button.h"
#include "Knob.h"
#include "StepPad.h"
#include "Fader.h"
#include "ToggleAttachment.h"
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
using forrobox::ForroBoxLookAndFeel;
using forrobox::Button;
using forrobox::Knob;
using forrobox::StepPad;
using forrobox::Fader;
using forrobox::KnobAttachment;
using forrobox::ValueTooltip;
namespace theme = forrobox::theme;
namespace type  = forrobox::type;
namespace pad   = forrobox::pad;
namespace fader = forrobox::fader;

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
        const auto declaredTotal =
              ChassisLayout::kSampleSlotMarginTop     + ChassisLayout::kSampleSlotHeight
            + ChassisLayout::kHitVisualiserMarginTop  + ChassisLayout::kHitVisualiserHeight
            + ChassisLayout::kStripDividerMargin      + ChassisLayout::kStripDividerHeight
            + ChassisLayout::kStripDividerMargin      + ChassisLayout::kKnobGridHeight
            + ChassisLayout::kStripDividerMargin      + ChassisLayout::kStripDividerHeight
            + ChassisLayout::kStripDividerMargin
            + ChassisLayout::kPatternRowMarginTop     + ChassisLayout::kPatternRowHeight
            + ChassisLayout::kMuteSoloMarginTop       + ChassisLayout::kMuteSoloHeight
            + ChassisLayout::kGhostRowMarginTop       + ChassisLayout::kGhostLabelHeight
            + ChassisLayout::kGhostLabelGap           + ChassisLayout::kFaderHeight
            + (isBateriaStrip ? ChassisLayout::kSubDotsMarginTop + ChassisLayout::kSubDotSize : 0);

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

        // Sized by the BUTTON's own preferred box, with a margin of ground
        // around it so a border or a ground that overflows is visible rather
        // than clipped away.
        const auto w = button.preferredWidth();
        const auto h = button.preferredHeight();
        holder.setSize (w + kMargin * 2, h + kMargin * 2);
        button.setBounds (kMargin, kMargin, w, h);
    }

    static constexpr int kMargin = 6;

    struct Ground final : juce::Component
    {
        void paint (juce::Graphics& g) override { g.fillAll (ground); }
        juce::Colour ground;
    };

    juce::Image render() { return renderComponent (holder, holder.getWidth(), holder.getHeight()); }

    /** The button's own area in the holder's coordinates. */
    juce::Rectangle<int> area() const { return button.getBounds(); }

    /** A pixel well inside the button, away from its border. */
    juce::Point<int> inside() const { return area().reduced (4).getCentre(); }

    ForroBoxLookAndFeel lnf;
    Button              button;
    Ground              holder;
};

void testButtonFamily (theme::Mode mode, const juce::String& modeName)
{
    section ("the button family is one component with three variants — " + modeName);

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
        const std::array<std::pair<Button::Variant, const char*>, 4> all {{
            { Button::Variant::base,     "base" },
            { Button::Variant::muteSolo, "muteSolo" },
            { Button::Variant::arrow,    "arrow" },
            { Button::Variant::load,     "load" },
        }};

        for (const auto& [variant, name] : all)
        {
            ButtonRig rig { mode, variant, "M" };

            const auto resting = contrastMass (rig.render(), rig.area(), panel);

            rig.button.mouseEnter (juce::MouseEvent (
                juce::Desktop::getInstance().getMainMouseSource(),
                rig.inside().toFloat(), {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                &rig.button, &rig.button, juce::Time::getCurrentTime(),
                rig.inside().toFloat(), juce::Time::getCurrentTime(), 0, false));

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

            pressRig.button.mouseDown (juce::MouseEvent (
                juce::Desktop::getInstance().getMainMouseSource(),
                pressRig.inside().toFloat(), {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                &pressRig.button, &pressRig.button, juce::Time::getCurrentTime(),
                pressRig.inside().toFloat(), juce::Time::getCurrentTime(), 1, false));

            const auto difference = maxPixelDifference (beforePress, pressRig.render());
            const auto declared = Button::specFor (variant).pressScale;

            if (juce::approximatelyEqual (declared, Button::kNoPress))
                checkEqual (difference, 0.0,
                            modeName + ": the " + name + " variant does NOT scale on press — its "
                                                         "rule declares no :active");
            else
                check (difference > 0.0,
                       modeName + ": the " + name + " variant scales on press to "
                           + juce::String (declared, 2) + " (" + juce::String (difference, 4) + ")");
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
        const auto p = localPos.toFloat();

        return { juce::Desktop::getInstance().getMainMouseSource(), p, mods,
                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 const_cast<Knob*> (&knobComponent), const_cast<Knob*> (&knobComponent),
                 juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), numClicks, false };
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

    struct Holder final : juce::Component
    {
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
    };

    ForroBoxAudioProcessor      processor;
    ForroBoxLookAndFeel         lnf { theme::Mode::dark };
    juce::RangedAudioParameter& parameter;
    Knob                        knobComponent;
    ValueTooltip                tooltip { lnf };
    KnobAttachment              attachment;
    Holder                      holder;
};

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
        const auto down = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                            centre.toFloat(), {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                            &rig.knobComponent, &rig.knobComponent,
                                            juce::Time::getCurrentTime(), centre.toFloat(),
                                            juce::Time::getCurrentTime(), 1, false);
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
        struct GestureCounter final : juce::AudioProcessorParameter::Listener
        {
            void parameterValueChanged (int, float) override {}
            void parameterGestureChanged (int, bool starting) override
            {
                starting ? ++begins : ++ends;
            }
            int begins { 0 }, ends { 0 };
        };

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

    checkEqual (static_cast<int> (knobs.size()), 20,
                "the editor carries twenty strip knobs (5 channels x VOL/PITCH/DECAY/PAN)");

    if (knobs.size() != 20)
        return;

    const auto& layout = ChassisLayout::forBounds ({ 0, 0, ChassisLayout::kWidth,
                                                     ChassisLayout::kHeight });

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
    struct GestureCounter final : juce::AudioProcessorParameter::Listener
    {
        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int, bool starting) override
        {
            starting ? ++begins : ++ends;
        }
        int begins { 0 }, ends { 0 };
    };

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
        const auto altDown = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                               centre.toFloat(),
                                               juce::ModifierKeys (juce::ModifierKeys::altModifier),
                                               1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                               &rig.knobComponent, &rig.knobComponent,
                                               juce::Time::getCurrentTime(), centre.toFloat(),
                                               juce::Time::getCurrentTime(), 1, false);
        rig.knobComponent.mouseDown (altDown);
        AttachedKnobRig::settle();

        const auto afterReset = rig.value();

        // Same event without the modifier — as if Alt were released mid-press.
        const auto plain = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                             centre.translated (0, -60).toFloat(), {},
                                             1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                             &rig.knobComponent, &rig.knobComponent,
                                             juce::Time::getCurrentTime(), centre.toFloat(),
                                             juce::Time::getCurrentTime(), 1, false);
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

    struct Ground final : juce::Component
    {
        void paint (juce::Graphics& g) override { g.fillAll (ground); }
        juce::Colour ground;
    };

    juce::Image render() { return renderComponent (holder, holder.getWidth(), holder.getHeight()); }

    /** The PAD's rect in the holder's coordinates — not the component bounds,
        which carry the glow margin. */
    juce::Rectangle<int> area() const { return stepPad.getBounds().reduced (pad::kLitGlowRadius); }

    juce::MouseEvent eventAt (juce::Point<int> localPos) const
    {
        return { juce::Desktop::getInstance().getMainMouseSource(), localPos.toFloat(), {},
                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, const_cast<StepPad*> (&stepPad), const_cast<StepPad*> (&stepPad),
                 juce::Time::getCurrentTime(), localPos.toFloat(), juce::Time::getCurrentTime(),
                 0, false };
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
    section ("the step pad's six states are pairwise distinct — " + modeName);

    const auto colour = theme::accent (theme::Accent::zabumba);
    const auto panel = theme::colour (theme::Token::panel, mode);

    // ── AC-1: six states, and every pair must differ ────────────────────────
    struct State { const char* name; int velocity; bool beat; bool hover; };

    static constexpr std::array<State, 6> states {{
        { "off",        0,   false, false },
        { "on/full",    127, false, false },
        { "on/low",     60,  false, false },
        { "ghost",      20,  false, false },
        { "beat",       0,   true,  false },
        { "hover",      0,   false, true  },
    }};

    std::array<juce::Image, states.size()> renders;

    for (size_t i = 0; i < states.size(); ++i)
    {
        StepPadRig rig { mode, colour };
        rig.stepPad.setVelocity (states[i].velocity);
        rig.stepPad.setBeat (states[i].beat);

        if (states[i].hover)
            rig.stepPad.mouseEnter (rig.eventAt (rig.area().getCentre()));

        renders[i] = rig.render();
    }

    auto worstPair = 1.0;
    juce::String worstNames;

    for (size_t i = 0; i < states.size(); ++i)
    {
        for (size_t j = i + 1; j < states.size(); ++j)
        {
            const auto difference = maxPixelDifference (renders[i], renders[j]);

            if (difference < worstPair)
            {
                worstPair = difference;
                worstNames = juce::String (states[i].name) + " vs " + states[j].name;
            }
        }
    }

    check (worstPair > kPairwiseFloor,
           modeName + ": every one of the 15 state pairs renders differently — closest is "
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
               modeName + ": the gradient is WIDER than it is tall — " + juce::String (kProbe)
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

    struct Ground final : juce::Component
    {
        void paint (juce::Graphics& g) override { g.fillAll (ground); }
        juce::Colour ground;
    };

    static void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil (1); }

    float value() const { return parameter.convertFrom0to1 (parameter.getValue()); }

    void setValue (float denormalised)
    {
        parameter.setValueNotifyingHost (parameter.convertTo0to1 (denormalised));
        settle();
    }

    juce::MouseEvent eventAt (int localX, juce::ModifierKeys mods = {}) const
    {
        const juce::Point<float> p ((float) localX, (float) faderComponent.getHeight() * 0.5f);

        return { juce::Desktop::getInstance().getMainMouseSource(), p, mods,
                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 const_cast<Fader*> (&faderComponent), const_cast<Fader*> (&faderComponent),
                 juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false };
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

        checkEqual (bounds.getHeight(), fader::kHeight,
                    "the fader box is padding + track + padding, css:376-377");
        checkEqual (track.getHeight(), fader::kTrackHeight, "and its track is 4 px");
        checkEqual (track.getCentreY(), bounds.getCentreY(),
                    "centred in the box, which is what equal padding means");

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
        struct GestureCounter final : juce::AudioProcessorParameter::Listener
        {
            void parameterValueChanged (int, float) override {}
            void parameterGestureChanged (int, bool starting) override
            {
                starting ? ++begins : ++ends;
            }
            int begins { 0 }, ends { 0 };
        };

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
        AttachedFaderRig::settle();

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
        auto litColumns = 0;

        for (int x = track.getX(); x < track.getRight(); ++x)
            if (colourDistance (image.getPixelAt (x, row), fill) < 0.05)
                ++litColumns;

        checkEqual (litColumns, 0,
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

        // The thumb is --fg, which neither the track nor the fill is.
        const auto fg = theme::colour (theme::Token::fg, theme::Mode::dark);
        auto left = -1, right = -1;

        for (int x = 0; x < image.getWidth(); ++x)
        {
            if (colourDistance (image.getPixelAt (x, row), fg) > 0.05)
                continue;

            if (left < 0)
                left = x;

            right = x;
        }

        return left < 0 ? -1 : (left + right) / 2;
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
template <typename T>
std::vector<T*> collectChildren (juce::Component& root)
{
    std::vector<T*> found;

    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* typed = dynamic_cast<T*> (child))
                found.push_back (typed);

            walk (*child);
        }
    };

    walk (root);
    return found;
}

void testStripIsFinished()
{
    section ("every reserved box is filled, and what is real drives a parameter");

    ForroBoxAudioProcessor processor;
    ForroBoxAudioProcessorEditor editor { processor };
    editor.setSize (ChassisLayout::kWidth, ChassisLayout::kHeight);

    const auto buttons = collectChildren<Button> (editor);
    const auto faders = collectChildren<Fader> (editor);

    // Five strips x (LOAD + two arrows + M + S).
    checkEqual (static_cast<int> (buttons.size()), ChassisLayout::kNumStrips * 5,
                "the editor carries five buttons per strip — LOAD, two pattern arrows, M and S");
    checkEqual (static_cast<int> (faders.size()), ChassisLayout::kNumStrips,
                "and one ghost fader per strip");

    const auto image = renderComponent (editor, ChassisLayout::kWidth, ChassisLayout::kHeight);
    const auto& layout = ChassisLayout::forBounds ({ 0, 0, ChassisLayout::kWidth,
                                                     ChassisLayout::kHeight });

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

        // Still empty, and deliberately: the hit visualiser is Phase 5's
        // activity meter and has nothing to show until there are triggers.
        // Asserted rather than left unmentioned, so the day it IS drawn this
        // test is what says the plan that drew it owns it.
        checkEqual (contrastMass (image, interior.hitVisualiser, ground), 0.0,
                    label + "'s hit visualiser is still empty — Phase 5's box, not this plan's");
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
    // The header, sequencer, footer and side panel are 04-04's, Phase 5's and
    // Phase 6's. Attaching parameters must not change a single pixel of any of
    // them.
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

        const std::array<std::pair<const char*, juce::Rectangle<int>>, 4> untouched {{
            { "header",     layout.header },
            { "side panel", layout.sidePanel },
            { "sequencer",  layout.sequencer },
            { "footer",     layout.footer },
        }};

        for (const auto& [name, region] : untouched)
        {
            auto worst = 0.0;

            for (int y = region.getY(); y < region.getBottom(); ++y)
                for (int x = region.getX(); x < region.getRight(); ++x)
                    worst = juce::jmax (worst,
                                        colourDistance (populatedImage.getPixelAt (x, y),
                                                        bareImage.getPixelAt (x, y)));

            checkEqual (worst, 0.0,
                        juce::String ("attaching parameters changes no pixel of the ") + name
                            + " — it belongs to a later plan, and a strip painting outside its "
                              "own bounds is what this catches");
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

    // ── AC-5: the geometry did not move, except where it was agreed to ──────
    {
        const auto& interior = layout.stripLayouts[0];

        checkEqual (interior.patternCycler.getHeight(), Button::kArrowHeight,
                    "the pattern row is as tall as its TALLEST child, which is the 26 px arrow and "
                    "not the 20 px screen — the 6 px this plan corrected, with the stack below it "
                    "moving down by exactly that");
        checkEqual (ChassisLayout::kPatternScreenHeight, 20,
                    "and the screen itself is still the 20 px css:329-333 declares");
    }
}

void testMuteSoloAndGhostDriveParameters()
{
    section ("MUTE, SOLO and GHOST PROB drive real parameters");

    struct GestureCounter final : juce::AudioProcessorParameter::Listener
    {
        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int, bool starting) override { starting ? ++begins : ++ends; }
        int begins { 0 }, ends { 0 };
    };

    const auto settle = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (1); };

    const auto clickCentreOf = [] (juce::Component& c, juce::ModifierKeys mods = {})
    {
        const auto p = c.getLocalBounds().getCentre().toFloat();
        const juce::MouseEvent e { juce::Desktop::getInstance().getMainMouseSource(), p, mods,
                                   1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
                                   juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(),
                                   1, false };
        c.mouseDown (e);
        c.mouseUp (e);
    };

    ForroBoxAudioProcessor processor;
    ForroBoxAudioProcessorEditor editor { processor };
    editor.setSize (ChassisLayout::kWidth, ChassisLayout::kHeight);

    const auto buttons = collectChildren<Button> (editor);
    const auto faders = collectChildren<Fader> (editor);

    if (buttons.size() != static_cast<size_t> (ChassisLayout::kNumStrips) * 5 || faders.empty())
    {
        check (false, "the editor did not carry the controls these assertions need");
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
        const auto chassisLayout = ChassisLayout::forBounds ({ 0, 0, ChassisLayout::kWidth,
                                                               ChassisLayout::kHeight });
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

        const auto p = load.getLocalBounds().getCentre().toFloat();
        load.mouseEnter ({ juce::Desktop::getInstance().getMainMouseSource(), p, {},
                           1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &load, &load,
                           juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 0, false });

        check (restingInk (load) > resting,
               "and LOAD still lifts on hover, so it reads as a control rather than as dead paint ("
                   + juce::String (resting, 1) + " -> " + juce::String (restingInk (load), 1) + ")");
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
        chassis.attachParameters (processor.getAPVTS(), &tooltip);

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

    // ── the pad's six states, side by side ─────────────────────────────────
    //
    // The artefact the checkpoint's first two steps read. Rendered at 4x, not
    // upscaled from a 1x bitmap: at 26 px the difference between "brightest at
    // 22% of the height" and "brightest in the middle" is five pixels, and the
    // whole point of step 2 is that a human can see it.
    auto padsWritten = 0;

    for (const auto& [mode, modeName] : modes)
    {
        struct State { const char* name; int velocity; bool beat; bool hover; };

        static constexpr std::array<State, 6> states {{
            { "OFF",      0,   false, false },
            { "ON 127",   127, false, false },
            { "ON 60",    60,  false, false },
            { "GHOST 20", 20,  false, false },
            { "BEAT",     0,   true,  false },
            { "HOVER",    0,   false, true  },
        }};

        constexpr int kPadWidth = 40;
        constexpr int kCaption = 14;
        constexpr int kScale = 4;

        ForroBoxLookAndFeel lnf { mode };

        const auto cellWidth = kPadWidth + pad::kLitGlowRadius * 2 + 10;
        const auto cellHeight = pad::kHeight + pad::kLitGlowRadius * 2 + kCaption + 10;

        struct Sheet final : juce::Component
        {
            void paint (juce::Graphics& g) override { g.fillAll (ground); }
            juce::Colour ground;
        };

        Sheet sheet;
        sheet.ground = theme::colour (theme::Token::panel, mode);
        sheet.setSize (cellWidth * static_cast<int> (states.size()), cellHeight);

        // Owned for the whole render; a vector of unique_ptr because a
        // std::array of StepPad would need a default constructor it has no
        // sensible value for.
        std::vector<std::unique_ptr<StepPad>> pads;

        for (size_t i = 0; i < states.size(); ++i)
        {
            auto stepPad = std::make_unique<StepPad> (lnf, theme::accent (theme::Accent::zabumba));

            stepPad->setVelocity (states[i].velocity);
            stepPad->setBeat (states[i].beat);

            const auto padRect = juce::Rectangle<int> (kPadWidth, pad::kHeight)
                                     .withCentre ({ static_cast<int> (i) * cellWidth + cellWidth / 2,
                                                    (cellHeight - kCaption) / 2 });

            stepPad->setBounds (StepPad::boundsForPadRect (padRect));
            sheet.addAndMakeVisible (*stepPad);

            if (states[i].hover)
            {
                const auto centre = stepPad->getLocalBounds().getCentre().toFloat();
                stepPad->mouseEnter ({ juce::Desktop::getInstance().getMainMouseSource(), centre, {},
                                       1.0f, 0.0f, 0.0f, 0.0f, 0.0f, stepPad.get(), stepPad.get(),
                                       juce::Time::getCurrentTime(), centre,
                                       juce::Time::getCurrentTime(), 0, false });
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

            for (size_t i = 0; i < states.size(); ++i)
                type::drawTracked (g, type::Style::stripMicroLabel, states[i].name,
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
    testKnobIsAViewOfItsParameter();
    testTwentyStripKnobsAreLive();
    testKnobGestureLifecycle();
    testButtonFamily (theme::Mode::dark, "dark");
    testButtonFamily (theme::Mode::light, "light");
    testStepPadInstruments();
    testStepPadStates (theme::Mode::dark, "dark");
    testStepPadStates (theme::Mode::light, "light");
    testEllipsis();
    testStripIsFinished();
    testMuteSoloAndGhostDriveParameters();
    testFaderIsAbsolute();
    testFaderPaintsItsValue();
    writeReferenceRenders();
}
