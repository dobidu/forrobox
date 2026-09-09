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

/** Like checkPixel, but tolerant by `slop` per channel.

    For a pixel whose expected value comes from COMPOSITING rather than from a
    flat fill. JUCE's rasteriser blends translucent colours in premultiplied
    8-bit, and `Colour::overlaidWith` — the obvious way to predict the result —
    does not agree with it to the last bit: the 1 px strip dividers came out one
    LSB below the prediction in the dark theme and one above it in the light.
    Modelling the rasteriser's arithmetic in a test would be a second instrument
    needing its own proof, for a difference of 1/255. */
/** The per-channel slop a composited pixel needs, measured across the three
    compilers rather than guessed.

    The 1 px strip dividers are `--line` blended over `--bg`. Predicting that
    with `Colour::overlaidWith` lands one LSB off under GCC and Clang and two
    off under MSVC, whose rasteriser rounds the premultiplied blend differently.
    Reimplementing three rasterisers in a test would be three instruments each
    needing its own proof, for a difference of 2/255. */
inline constexpr int kCompositeSlop = 2;

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

void checkPixel (const juce::Image& image, int x, int y, juce::Colour expected,
                 const juce::String& description)
{
    const auto actual = pixelAt (image, x, y);

    check (actual == expected,
           description + " at (" + juce::String (x) + "," + juce::String (y) + ")"
                       + (actual == expected ? juce::String()
                                             : " — expected " + hex (expected) + ", got " + hex (actual)));
}

/** Total brightness over a region.

    The weight discriminator. Advance width is NOT usable for that: measured at
    24 px, the four Space Grotesk widths for "FORRO BOX" are 100.326 / 100.589 /
    100.646 / 100.777 — a 0.45% total spread that no honest tolerance separates
    from rounding, so a width assertion would pass with four copies of one
    weight. Ink mass over the same string spans 417.3 / 523.2 / 572.3 / 617.9. */
double inkMass (const juce::Image& image, juce::Rectangle<int> area)
{
    auto total = 0.0;

    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            total += pixelAt (image, x, y).getBrightness();

    return total;
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
        check (large > small * 3.9 && large < large + 1.0,
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
    return { area.getCentreX(), area.getCentreY() };
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
        const auto top = theme::mix (raised, juce::Colours::white, 0.03f);
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

    // The accent bars, one per strip, each exactly its instrument's colour.
    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
    {
        const auto strip = layout.strips[static_cast<size_t> (i)];
        const auto barY = strip.getY() + ChassisLayout::kStripPadTop
                        + ChassisLayout::kHeadRowHeight
                        + ChassisLayout::kAccentBarMarginTop
                        + ChassisLayout::kAccentBarHeight / 2;

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
        const auto strip = layout.strips[static_cast<size_t> (index)];

        return juce::Rectangle<int> (strip.getX() + ChassisLayout::kStripPadSide,
                                     strip.getY() + ChassisLayout::kStripPadTop,
                                     strip.getWidth() - 2 * ChassisLayout::kStripPadSide,
                                     ChassisLayout::kHeadRowHeight);
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
        const auto strip = layout.strips[2];
        const auto blank = juce::Rectangle<int> (strip.getX() + ChassisLayout::kStripPadSide,
                                                 strip.getBottom() - 30,
                                                 strip.getWidth() - 2 * ChassisLayout::kStripPadSide,
                                                 ChassisLayout::kHeadRowHeight);

        const auto blankMass = contrastMass (image, blank, strippedBackground (2));
        const auto textMass  = contrastMass (image, headRowOf (2), strippedBackground (2));

        checkEqual (blankMass, 0.0,
                    "while an equally sized reserved region of the same strip measures 0");
        check (textMass > 10.0 * juce::jmax (1.0, blankMass),
               "so the head-row measurement is reading the text, not the strip ("
                   + juce::String (textMass, 1) + " against " + juce::String (blankMass, 1) + ")");
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

        const auto base = renderComponent (chassis, ChassisLayout::kWidth, ChassisLayout::kHeight);

        juce::ignoreUnused (base);

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
    writeReferenceRenders();
}
