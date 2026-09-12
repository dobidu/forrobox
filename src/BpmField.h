/* ============================================================================
   FORRÓ BOX — BpmField

   The header's tempo readout, and the third interaction law in this plugin.

     knob    `(dy / 160) x range`, anchored at mouse-down   — controls.js:130
     fader   the pointer's x within the track, absolute     — controls.js:236
     BPM     `0.5 BPM per pixel`, anchored at mouse-down    — app.js:138

   The BPM law is the KNOB's shape in BPM units, not the fader's — `wireBPM`
   captures `sv = state.bpm` on mousedown and works from it, so dragging out and
   back lands exactly where it started. Getting this wrong is easy: a header
   full of screens invites the fader's absolute law, which would make the field
   jump to wherever the pointer happened to be.

   And the wheel is `Math.sign(deltaY)` — exactly +/-1 per notch, NOT the knob's
   `max(1, range/50)`, which on a 260-wide range would move 5.

   The field reports PIXELS and notches. What a pixel is worth in BPM lives in
   `BpmAttachment`, which is the only thing here that knows the parameter.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "Theme.h"
#include "Typography.h"
#include "ValueScreen.h"

namespace forrobox
{

namespace bpmfield
{
inline constexpr int kPadX = 10;      ///< css:175 .bpm padding 3px 10px
inline constexpr int kPadY = 3;
inline constexpr int kMinWidth = 78;  ///< css:175

/// `0.5 BPM per pixel` — app.js:138, `sv + (sy - ev.clientY) * 0.5`.
inline constexpr float kBpmPerPixel = 0.5f;

/// `gap: 8px` in the BPM cluster — css:170.
inline constexpr int kClusterGap = 8;

/// `gap: 4px` between the two mini buttons — css:179.
inline constexpr int kMiniGap = 4;
} // namespace bpmfield

class BpmField final : public juce::Component
{
public:
    explicit BpmField (ForroBoxLookAndFeel&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The text to show. Written only by the attachment, or — while SYNC is on
        — by whatever is showing the host's tempo. The field holds no value. */
    void setValueText (juce::String);

    /** Under SYNC the field displays the host's tempo and refuses every
        gesture. `PLANNING.md:400` and its stub table at `:838` both say so, and
        it is the one header behaviour the spec states twice.

        Read-only is VISIBLE, not silent: the cursor stops being `ns-resize`
        and the text dims, so a user who drags and gets nothing can see why. */
    void setReadOnly (bool);
    bool isReadOnly() const noexcept { return readOnly; }

    /** What the field is currently SHOWING — the parameter's text, or the
        host's tempo while synced. Exposed so a test can assert what a user
        sees rather than measuring ink through a 55% read-only alpha. */
    const juce::String& displayedText() const noexcept { return screen.getText(); }

    int preferredWidth() const { return screen.preferredWidth(); }
    int preferredHeight() const { return screen.preferredHeight(); }

    /** Dragged `pixelsUp` from the press anchor. Positive is upward, which
        raises the tempo, as `(sy - ev.clientY)` does. */
    std::function<void (int pixelsUp)> onDragBy;

    /** One wheel notch: +1 or -1, and the attachment decides what one step is.

        No arrow keys. The field takes no keyboard focus and has no keyPressed,
        where the Knob has both — `PLANNING.md:871` gives Space to the transport
        and says nothing about arrow keys on the BPM field, and a header comment
        promising a binding that does not exist is worse than its absence. */
    std::function<void (int direction)> onNudge;

    std::function<void()> onGestureStart, onGestureEnd;

    /** Typed. Returns false to REJECT, which leaves the value untouched and
        keeps the editor open so the typist can correct it — the Knob's
        behaviour, and what this seam is for. */
    std::function<bool (const juce::String&)> onTextEntered;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void showEditor();

    ForroBoxLookAndFeel& lnf;
    ValueScreen          screen;

    bool readOnly { false };
    bool gestureActive { false };
    int  anchorY { 0 };

    std::unique_ptr<juce::TextEditor> editor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BpmField)
};

} // namespace forrobox
