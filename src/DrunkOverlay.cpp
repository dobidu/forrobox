#include "DrunkOverlay.h"

#include "Theme.h"

#include <array>
#include <cmath>

namespace forrobox
{

namespace
{

/** One `radial-gradient(<rx> <ry> at <cx> <cy>, <colour>, transparent <end>)`
    from css:92-93, with every length as a fraction of the box.

    The two colours are NOT hexes typed here: css writes `rgba(232,101,10,…)`
    and `rgba(242,194,0,…)`, which are `--c-zabumba` and `--c-pandeiro` spelled
    out. Reading them from `theme::accentSpecs` puts them under the cross-check
    that already compares that table against the stylesheet, where a literal
    here would be a fourth copy of a colour nothing compares. */
struct RadialWashLayer
{
    theme::Accent accent;
    float alpha;      ///< the alpha at stop 0
    float endStop;    ///< where it reaches `transparent`, as a fraction of the radius
    float centreX, centreY, radiusX, radiusY;
};

/** FIRST layer on top — the order `background` lists them, and the order
    `compositeOver` below is called in. */
constexpr std::array<RadialWashLayer, 2> kRadialLayers { {
    { theme::Accent::zabumba,  0.42f, 0.58f, 0.50f,  1.18f, 1.20f, 0.80f },
    { theme::Accent::pandeiro, 0.16f, 0.52f, 0.50f, -0.20f, 1.40f, 1.20f },
} };

/** `linear-gradient(180deg, rgba(232,101,10,0.06), rgba(232,101,10,0.13))` —
    css:95, the bottom layer. */
constexpr theme::Accent kLinearAccent     = theme::Accent::zabumba;
constexpr float         kLinearTopAlpha   = 0.06f;
constexpr float         kLinearEndAlpha   = 0.13f;

juce::uint8 toByte (float v) noexcept
{
    return static_cast<juce::uint8> (juce::jlimit (0, 255, juce::roundToInt (v * 255.0f)));
}

/** Which byte of a pixel the three colour channels start at.

    JUCE lays `PixelARGB` out as {b,g,r,a} on a little-endian machine and
    {a,r,g,b} on a big-endian one, and `PixelRGB` carries no alpha byte either
    way — so in both formats the three colour channels are CONTIGUOUS and in the
    same order as each other. This pass does the identical thing to each of
    them, so which one is red never has to be known. Only where they begin. */
int firstColourByte (const juce::Image::BitmapData& data) noexcept
{
   #if JUCE_BIG_ENDIAN
    return data.pixelStride >= 4 ? 1 : 0;
   #else
    juce::ignoreUnused (data);
    return 0;
   #endif
}

} // namespace

float DrunkOverlay::amountFor (float cachacaPercent) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (cachacaPercent - kOnsetPercent) / kSpanPercent);
}

DrunkOverlay::DrunkOverlay()
{
    // css:91 — `pointer-events: none`. The wash covers the entire chassis, so a
    // layer that took the mouse would take ALL of it.
    setInterceptsMouseClicks (false, false);

    // css:91's `z-index: 60` — above the kit and ABOUT overlays, which css:554
    // puts at 40. `Chassis` adds fifty strip controls after its overlays, so
    // "the last child" is not a z-order claim here; this is.
    setAlwaysOnTop (true);
}

void DrunkOverlay::setAmount (float newAmount)
{
    const auto clamped = juce::jlimit (0.0f, 1.0f, newAmount);

    if (! juce::approximatelyEqual (amount, clamped))
    {
        amount = clamped;

        // Repaint when it turns OFF as well as when it changes while on: the
        // frame that drops to zero still has the previous frame's wash on
        // screen, and this layer draws nothing to replace it.
        if (amount <= 0.0f)
        {
            // Device-resolution pixels are the one heavy thing this layer
            // holds, and a dormant easter egg has nothing to show for them.
            //
            // The WASH is deliberately kept. CACHAÇA is quantised to whole
            // percent, so 65 and 66 flip `amount` between exactly 0 and 1/35 —
            // a knob dragged back and forth across the onset would rebuild
            // three ellipse solves per pixel over 936k pixels (3.7M at 2x) on
            // the message thread on every upward crossing. /code-review.
            scratch = {};
        }

        // Unconditional: both values are in [0, 1] and they differ, so either
        // the new one is non-zero or the old one was. The frame that drops to
        // zero still has the previous frame's wash on screen and this layer
        // draws nothing to replace it, so it needs the repaint just as much.
        repaint();
    }
}

juce::Image DrunkOverlay::buildUnitWash (int width, int height)
{
    juce::Image wash (juce::Image::ARGB, juce::jmax (1, width), juce::jmax (1, height), true);

    const juce::Image::BitmapData data (wash, juce::Image::BitmapData::writeOnly);

    const auto w = static_cast<float> (wash.getWidth());
    const auto h = static_cast<float> (wash.getHeight());

    // ── everything that does not vary per pixel, resolved once ─────────────
    //
    // This loop runs 936,000 times at 1x and 3.7 million at 2x, and it is
    // rebuilt whenever the window is resized while the wash is on. The first
    // version left six float divisions, two ellipse centres, four radii and
    // NINE `juce::Colour::getFloat*` unpacks inside it — all of them constant
    // across the image. /simplify measured the shape at 2-3x.
    struct ResolvedLayer
    {
        float centreX, centreY;      ///< in pixels
        float invRadiusX, invRadiusY;
        float alpha, invEndStop;
        float red, green, blue;
    };

    std::array<ResolvedLayer, kRadialLayers.size()> layers {};

    for (size_t i = 0; i < kRadialLayers.size(); ++i)
    {
        const auto& spec   = kRadialLayers[i];
        const auto  colour = theme::accent (spec.accent);

        layers[i] = { spec.centreX * w, spec.centreY * h,
                      1.0f / (spec.radiusX * w), 1.0f / (spec.radiusY * h),
                      spec.alpha, 1.0f / spec.endStop,
                      colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue() };
    }

    const auto linear = theme::accent (kLinearAccent);

    const auto linearRed   = linear.getFloatRed();
    const auto linearGreen = linear.getFloatGreen();
    const auto linearBlue  = linear.getFloatBlue();

    const auto invHeight = 1.0f / h;

    for (int y = 0; y < wash.getHeight(); ++y)
    {
        const auto py = static_cast<float> (y) + 0.5f;

        // `180deg` is top to bottom, over the box rather than over the gradient
        // line's projection — the two coincide for a vertical gradient.
        const auto linearAlpha = kLinearTopAlpha
                               + (kLinearEndAlpha - kLinearTopAlpha) * (py * invHeight);

        // Row-invariant: the vertical term of each ellipse.
        std::array<float, kRadialLayers.size()> dySquared {};

        for (size_t i = 0; i < layers.size(); ++i)
        {
            const auto dy = (py - layers[i].centreY) * layers[i].invRadiusY;
            dySquared[i] = dy * dy;
        }

        // One line pointer, advanced — not `getPixelPointer (x, y)` per pixel,
        // which is two integer multiplies and, because the store aliases, a
        // reload of both strides after every write. `screenOnto` below already
        // has the right shape.
        auto* pixel = reinterpret_cast<juce::PixelARGB*> (data.getLinePointer (y));

        for (int x = 0; x < wash.getWidth(); ++x)
        {
            const auto px = static_cast<float> (x) + 0.5f;

            // Source-over, accumulated PREMULTIPLIED: that is the form the
            // screen pass wants, and it is the form `PixelARGB` stores, so
            // nothing is divided by an alpha that can be zero.
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;

            const auto compositeOver = [&] (float red, float green, float blue, float layerAlpha)
            {
                if (layerAlpha <= 0.0f)
                    return;

                const auto contribution = (1.0f - a) * layerAlpha;

                r += contribution * red;
                g += contribution * green;
                b += contribution * blue;
                a += contribution;
            };

            for (size_t i = 0; i < layers.size(); ++i)
            {
                const auto& layer = layers[i];

                // The gradient's 100% IS the ellipse, so the stop positions are
                // fractions of this same normalised radius.
                const auto dx = (px - layer.centreX) * layer.invRadiusX;
                const auto t  = std::sqrt (dx * dx + dySquared[i]);

                // `transparent` is rgba(0,0,0,0), and CSS interpolates gradient
                // stops PREMULTIPLIED — so the run fades the alpha out and does
                // not slide the colour towards black on the way.
                compositeOver (layer.red, layer.green, layer.blue,
                               layer.alpha * juce::jmax (0.0f, 1.0f - t * layer.invEndStop));
            }

            compositeOver (linearRed, linearGreen, linearBlue, linearAlpha);

            // `setARGB` does NOT premultiply — which is exactly right, these
            // already are — and it places the components in whatever order this
            // machine's PixelARGB uses.
            (pixel++)->setARGB (toByte (a), toByte (r), toByte (g), toByte (b));
        }
    }

    return wash;
}

DrunkOverlay::RenderedRegion DrunkOverlay::renderRegion (juce::Component& source,
                                                         juce::Image& scratch,
                                                         juce::Rectangle<int> area, float scale)
{
    const auto fullWidth  = juce::roundToInt (scale * static_cast<float> (source.getWidth()));
    const auto fullHeight = juce::roundToInt (scale * static_cast<float> (source.getHeight()));

    if (fullWidth <= 0 || fullHeight <= 0 || area.isEmpty())
        return {};


    const auto format = source.isOpaque() ? juce::Image::RGB : juce::Image::ARGB;

    if (scratch.getWidth() != fullWidth || scratch.getHeight() != fullHeight
        || scratch.getFormat() != format)
        // NOT cleared: `paintEntireComponent` overwrites every pixel inside
        // the clip — the chassis is opaque and fills its bounds — and nothing
        // outside the clip is ever read, because only `getClippedImage (device)`
        // is blitted. The memset was 3.7 MB at 1x and 15 MB at 2x. /simplify.
        scratch = juce::Image (format, fullWidth, fullHeight, false);

    {
        juce::Graphics g (scratch);

        // Only when it is genuinely not 1: an identity transform is enough to
        // take the software renderer off its integer-translation path.
        if (! juce::approximatelyEqual (scale, 1.0f))
            g.addTransform (juce::AffineTransform::scale (scale));

        // In the SOURCE's coordinates, which is the whole point — the clip
        // bounds the work without moving what is drawn.
        g.reduceClipRegion (area);

        // `true` — ignore the component's own alpha, as JUCE's own snapshot
        // does. This is a rendering of what the chassis draws, not a
        // compositing of it.
        source.paintEntireComponent (g, true);
    }

    // The SMALLEST INTEGER CONTAINER of the scaled area, which is how
    // `Rectangle<int>::transformedBy` — and therefore JUCE's own clip — rounds
    // it. Rounding the origin and the size separately gives a right edge of
    // `round(s·x) + round(s·w)`, which is not `round(s·(x + w))`, and the caller
    // then blits a strip of the wrong width half a device pixel out.
    const auto device = area.toFloat()
                            .transformedBy (juce::AffineTransform::scale (scale))
                            .getSmallestIntegerContainer()
                            .getIntersection (scratch.getBounds());

    // A view onto the same pixels, not a copy — `SubsectionPixelData` carries
    // the parent's line stride, so the screen pass writes straight through it.
    return { scratch.getClippedImage (device), device };
}

void DrunkOverlay::screenOnto (juce::Image& destination, juce::Point<int> originInWash,
                               const juce::Image& unitWash, float amount)
{
    if (amount <= 0.0f || destination.isNull() || unitWash.isNull())
        return;

    const juce::Image::BitmapData dst (destination, juce::Image::BitmapData::readWrite);
    const juce::Image::BitmapData wash (unitWash, juce::Image::BitmapData::readOnly);

    const auto dstOffset  = firstColourByte (dst);
    const auto washOffset = firstColourByte (wash);

    // 8 fractional bits, so the inner loop is integer.
    const auto scale = static_cast<int> (juce::jlimit (0.0f, 1.0f, amount) * 256.0f + 0.5f);

    // A negative origin means the destination starts OUTSIDE the wash, so those
    // destination pixels are skipped rather than given the wash's edge: clamping
    // the source alone would slide the whole gradient sideways by |origin|.
    // Unreachable from `paint`, whose area is a clipped non-negative rectangle —
    // but this is public and static so the tests and any later caller can use
    // it, and the parameter is signed. /code-review.
    const auto left = juce::jmax (0, originInWash.x);
    const auto top  = juce::jmax (0, originInWash.y);

    const auto skipX = left - originInWash.x;
    const auto skipY = top  - originInWash.y;

    const auto width  = juce::jmin (dst.width  - skipX, wash.width  - left);
    const auto height = juce::jmin (dst.height - skipY, wash.height - top);

    // Hoisted out of the loop: the stores below are through a `uint8*`, which
    // aliases everything — so left on the BitmapData these two would be reloaded
    // from memory after every pixel, ~1.9 million times a frame, and the channel
    // loop could not vectorise. /simplify.
    const auto dstStride  = dst.pixelStride;
    const auto washStride = wash.pixelStride;

    for (int y = 0; y < height; ++y)
    {
        auto* out = dst.getLinePointer (y + skipY) + skipX * dstStride + dstOffset;
        const auto* src = wash.getLinePointer (y + top) + left * washStride + washOffset;

        for (int x = 0; x < width; ++x)
        {
            for (int channel = 0; channel < 3; ++channel)
            {
                const int d = out[channel];
                const int s = (src[channel] * scale) >> 8;

                out[channel] = static_cast<juce::uint8> (d + (s * (255 - d) + 127) / 255);
            }

            out += dstStride;
            src += washStride;
        }
    }
}

void DrunkOverlay::paint (juce::Graphics& g)
{
    // Not "a cheap pass when off" — NO pass, and no second rendering. AC-5
    // counts the re-renders rather than timing the frame, because a timing
    // threshold passes on a fast machine with the work still being done.
    if (insideRerender || amount <= 0.0f)
        return;

    auto* chassis = getParentComponent();

    if (chassis == nullptr)
        return;

    const auto area = g.getClipBounds().getIntersection (getLocalBounds());

    if (area.isEmpty())
        return;

    // The chassis carries the editor's one scale transform, so this renders at
    // the DEVICE resolution and draws back at 1:1 through it. Snapshotting at
    // design size instead would soften the entire interface at 2x — every
    // pixel of it, whenever CACHAÇA is past 65.
    // NOT `AffineTransform::getScaleFactor()`, which JUCE deprecates precisely
    // because it returns the wrong answer for a transform carrying a rotation —
    // and the sway 08-04 Task 2 adds puts one there. The determinant's root is
    // the area scale, which a rotation leaves alone.
    const auto scale = std::sqrt (std::abs (chassis->getTransform().getDeterminant()));

    const auto washWidth  = juce::roundToInt (scale * static_cast<float> (getWidth()));
    const auto washHeight = juce::roundToInt (scale * static_cast<float> (getHeight()));

    if (unitWash.getWidth() != washWidth || unitWash.getHeight() != washHeight)
    {
        unitWash = buildUnitWash (washWidth, washHeight);
        ++washBuilds;
    }

    const juce::ScopedValueSetter<bool> suppressSelf (insideRerender, true);

    auto region = renderRegion (*chassis, scratch, area + getPosition(), scale);
    ++rerendersDone;

    if (region.image.isNull())
        return;

    screenOnto (region.image, region.device.getPosition(), unitWash, amount);

    // Back at exactly the logical rectangle those device pixels came from, so
    // the blit is a 1:1 copy at any scale rather than a resample. The outer clip
    // trims whatever the integer container gained.
    g.drawImage (region.image,
                 region.device.toFloat()
                     .transformedBy (juce::AffineTransform::scale (1.0f / scale)));
}

} // namespace forrobox
