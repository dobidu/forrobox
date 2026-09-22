#include "NoteGlyph.h"

namespace forrobox::noteglyph
{

namespace
{

/** The glyph at cap height 1, with its ink box normalised to
    `(0, -1) .. (width, 0)` — so the baseline is y = 0 and the left edge is
    x = 0, whatever the flag's curve and the head's tilt actually reach.

    NORMALISED rather than trusted, because the parts do not stay inside the
    box the ratios imply: rotating the head about its own centre pushes the
    ellipse's corners past both the baseline and the left edge, by half a pixel
    at 12 px type. Small, and exactly the kind of small that puts a descender
    where a baseline should be. Built once — this is a constant shape. */
const juce::Path& unitNote()
{
    static const juce::Path note = []
    {
        juce::Path path;

        const auto headWidth  = kHeadWidthRatio;
        const auto headHeight = kHeadHeightRatio;
        const auto stemWidth  = kStemWidthRatio;

        const auto stemX = headWidth - stemWidth;

        // The stem first, from the top of the glyph down to the head's centre,
        // so the head — added last — covers where the two meet.
        path.addRectangle (stemX, -1.0f, stemWidth, 1.0f - headHeight * 0.5f);

        // The flag: off the top of the stem, falling to the right. Two cubics
        // rather than one quadratic, so it has the hook an eighth note's flag
        // has instead of reading as a triangle.
        const auto reach = kFlagWidthRatio;
        const auto drop  = kFlagDropRatio;

        path.startNewSubPath (stemX + stemWidth, -1.0f);
        path.cubicTo (stemX + stemWidth + reach, -1.0f + drop * 0.25f,
                      stemX + stemWidth + reach, -1.0f + drop * 0.65f,
                      stemX + stemWidth,         -1.0f + drop);
        path.cubicTo (stemX + stemWidth + reach * 0.72f, -1.0f + drop * 0.60f,
                      stemX + stemWidth + reach * 0.72f, -1.0f + drop * 0.30f,
                      stemX + stemWidth,                 -1.0f + drop * 0.34f);
        path.closeSubPath();

        // The head, tilted about its OWN centre — a path rotated about the
        // origin would walk off to the left.
        juce::Path head;
        head.addEllipse (0.0f, -headHeight, headWidth, headHeight);
        head.applyTransform (juce::AffineTransform::rotation (
                                 juce::degreesToRadians (kHeadTiltDegrees),
                                 headWidth * 0.5f, -headHeight * 0.5f));

        path.addPath (head);

        // NON-ZERO winding, which is JUCE's default and is load-bearing here:
        // the stem, the flag and the head OVERLAP, and under even-odd those
        // overlaps would be cut out as holes. `GearButton::shapeFor` sets the
        // opposite for the opposite reason — its bore is meant to be a hole.

        // Normalise: exactly one cap height tall, sitting on y = 0 at x = 0.
        const auto bounds = path.getBounds();

        if (bounds.getHeight() > 0.0f)
            path.applyTransform (
                juce::AffineTransform::scale (1.0f / bounds.getHeight())
                    .translated (-bounds.getX() / bounds.getHeight(),
                                 -bounds.getBottom() / bounds.getHeight()));

        return path;
    }();

    return note;
}

} // namespace

float widthFor (float capHeight) noexcept
{
    // The INK's own width, measured from the normalised shape, plus the space
    // that would follow the note in `"♪ NO PONTO"` — a space this has to draw
    // because the note is not part of the string.
    return capHeight * (unitNote().getBounds().getWidth() + kTrailingGapRatio);
}

juce::Path shapeFor (float left, float baseline, float capHeight)
{
    if (capHeight <= 0.0f)
        return {};

    auto note = unitNote();
    note.applyTransform (juce::AffineTransform::scale (capHeight).translated (left, baseline));

    return note;
}

} // namespace forrobox::noteglyph
