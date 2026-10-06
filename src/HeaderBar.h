/* ============================================================================
   FORRÓ BOX — the header bar

   The 72 px row across the top: the logo lockup, the BPM cluster, the transport,
   the two signature 54 px knobs in their recessed group and the preset cycler.
   (The STYLE control, CAM CAR PET UNI, was removed at 18-02 by the user's
   decision: the side panel's tabs do its job. Its room widened the cycler's
   screen.)

   Split out of Chassis at 04-05, before the footer was added. /simplify recorded
   the reason at 04-04's UNIFY: Chassis was 1354 lines and ~41% header-only, and
   HeaderControls/StripControls were structurally identical — two instances is a
   coincidence, and the footer was the third. Adding a fourth
   build/paint/refresh/resize triad to one class was the thing to avoid.

   What did NOT move: ChassisLayout::HeaderLayout and headerInteriorOf, and the
   header's constants, which are that function's inputs. The layout is the one
   thing genuinely shared — Chassis needs the header's rectangle to place this
   bar, this bar needs the clusters inside it, and verify-geometry.py reads the
   constants from Chassis.h. So this component ASKS for its layout from its own
   local bounds and holds no geometry of its own.
============================================================================ */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "BpmAttachment.h"
#include "BpmField.h"
#include "Button.h"
#include "Chassis.h"
#include "Knob.h"
#include "GearButton.h"
#include "LogoMark.h"
#include "LookAndFeel.h"
#include "Surface.h"
#include "ToggleAttachment.h"
#include "ValueScreen.h"

class ForroBoxAudioProcessor;

namespace forrobox
{

class HeaderBar final : public juce::Component
{
public:
    explicit HeaderBar (ForroBoxLookAndFeel&);
    ~HeaderBar() override;

    /** Builds the header's controls and binds them to real parameters.

        A separate step rather than a constructor argument, for the reason
        Chassis::attachParameters is one: the bar is a surface, and every
        geometry test builds one with no processor at all. */
    void attachParameters (juce::AudioProcessorValueTreeState&);

    /** What the gear does when clicked.

        Set by `Chassis`, because the menu needs a `Settings` store and a
        chassis to repaint and this bar has neither. A bar that built its own
        menu would be a bar that knows about preferences, which is the coupling
        06-01 rejected the channel gate over. */
    std::function<void()> onGearClicked;

    /** The gear itself, for the tests. Every other header control is reachable
        through `collectChildren`; this one is named because a test asserting
        "clicking the gear opens the menu" has to click THIS component and not a
        rectangle that happens to be in the right place. */
    GearButton* getGearButton() const noexcept { return headerControls.gear.get(); }

    /** Pull the header into step with the processor: the transport's lit and
        read-only state, and the BPM field under SYNC.

        Public because the TIMER is a scheduling detail, not the behaviour. The
        tests used to pump a real message loop and hope the 30 Hz tick landed
        inside it — which it did on GCC and Clang and did NOT on MSVC, where
        three checks failed on the clock rather than on the code. A poll whose
        logic can only be reached through a timer is a poll that can only be
        tested flakily. */
    void refreshFromProcessor();

    /** The two preset arrows, for a test that must CLICK them.
        Asked for rather than hunted (06-06's rule); 09-05's review found
        a cycler test that drove the processor and never the buttons. */
    Button& getPresetPrev() const { return *headerControls.presetPrev; }
    Button& getPresetNext() const { return *headerControls.presetNext; }

    /** Called after the cycler loads a groove, so the owner can refresh the grid.
        Set by `Chassis`; null in the tests that build a bare header. */
    std::function<void()> onGrooveChanged;

    /** css:100-101 and app.js:598-605 — past 88% CACHAÇA the label becomes
        `♪ NO PONTO` in `--c-zabumba` with a 1.6 s opacity pulse, and the
        readout turns the same colour.

        Told by `Chassis`, which owns the threshold: the wash, the sway and this
        are one decision, and a bar that read CACHAÇA itself would be a second
        place that knows what 88 means. */
    void setTipsy (bool);

    bool isTipsy() const noexcept { return tipsy; }

    /** Advance the label's pulse by a known number of seconds.

        TOLD its elapsed time, never reading a clock — the law this bar's own
        `refreshFromProcessor` docstring records, where three checks failed on
        MSVC's clock rather than on the code. Below 88% this does nothing at
        all, whoever calls it. */
    void advancePulse (double seconds);

    float getPulseOpacityForTest() const noexcept { return pulseOpacityNow(); }

    /** The CACHAÇA readout, for the tests: a check that it turns orange has to
        look at THIS screen rather than at a rectangle in the right place. */
    ValueScreen* getCachacaReadout() const noexcept { return headerControls.cachacaRead.get(); }

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The header's clusters as last laid out, in THIS BAR's coordinates.

        The one copy. `ChassisLayout` used to carry a second, derived from the
        chassis's own rectangle, and `Chassis` no longer paints the header at
        all — so that copy had no painter, was recomputed on every resize for
        tests alone (~36 us, which measurement showed to be most of the whole
        chassis layout), and its correctness rested on the header happening to
        sit at the chassis origin. That coincidence is what let four tests read
        the FOOTER's controls as the header's, so promoting it to an invariant
        was the wrong direction. `/simplify` found it from three angles at once. */
    const ChassisLayout::HeaderLayout& getLayout() const noexcept { return headerLayout; }

    // ── the preset screen's inline prompt (18-02) ──────────────────────────
    //
    //  The user's decision at 18-02 planning: a groove's name is typed WHERE IT
    //  WILL BE SHOWN, and a delete is confirmed there too, rather than in a
    //  window. A field over the preset screen, styled as `BpmField`'s inline
    //  editor is. Enter commits, Esc or a click elsewhere cancels.

    /** Name entry, pre-filled with `initial` and all selected. `onCommit` gets
        the typed text and returns whether it was ACCEPTED: false keeps the
        field open so the typist can correct it (`BpmField`'s rule). */
    void promptForName (const juce::String& initial, std::function<bool (const juce::String&)> onCommit);

    /** A yes/no on the screen: `question` shown, Enter runs `onConfirm`, Esc or
        a click elsewhere cancels. Typing does nothing. */
    void promptForConfirm (const juce::String& question, std::function<void()> onConfirm);

    /** `text` on the preset screen for `kScreenMessageTicks` polls, then the
        groove name again. Counted in polls, never read off a clock — this
        bar's own law (`refreshFromProcessor`). */
    void showScreenMessage (const juce::String& text);

    static constexpr int kScreenMessageTicks = 2 * kUiPollHz;   ///< ~2 s

    /** One tick of the header's own poll: the message's countdown, then
        `refreshFromProcessor`. Public for the tests, as that one is. */
    void pollTick();

    bool isPromptOpen() const noexcept;

    /** The open prompt's field, for the tests; null when none is open. */
    juce::TextEditor* getPromptEditor() const noexcept;

    /** Whether `text` fits the preset screen as drawn — its face, its padding,
        its current width. What a caller asks before choosing a longer or a
        shorter wording (the delete confirm, 18-02). */
    bool fitsPresetScreen (const juce::String& text) const;

    /** The preset screen itself, for the tests. */
    ValueScreen* getPresetScreen() const noexcept { return headerControls.presetScreen.get(); }

private:
    void buildHeaderControls (juce::AudioProcessorValueTreeState&);
    void paintGlobalKnobGroup (juce::Graphics&) const;
    void paintHeaderText (juce::Graphics&, juce::Rectangle<int> clip) const;

    ForroBoxLookAndFeel& lnf;

    class InlinePrompt;

    void openPrompt (std::unique_ptr<InlinePrompt>);

    /** A press ANYWHERE outside the open prompt cancels it. Not left to focus:
        the arrows and most of this chassis never take keyboard focus, so a
        click on them would leave the field open. Registered only while a
        prompt is open. */
    void mouseDown (const juce::MouseEvent&) override;
    void closePrompt();

    std::unique_ptr<InlinePrompt> prompt;

    /** Closed prompts, kept until the message loop is past the key handler or
        focus change that closed them — deleting a TextEditor from inside its
        own `keyPressed` is a use-after-free. */
    std::vector<std::unique_ptr<InlinePrompt>> retiredPrompts;

    int screenMessageTicksLeft { 0 };

    /** Derived from this component's OWN local bounds in `resized`, by the same
        function Chassis uses for the copy its tests read. Both are given the
        same 1200x72 rectangle, because the header sits at the chassis's origin. */
    ChassisLayout::HeaderLayout headerLayout;

    /** The header's controls.

        All of them are real —
        and `play`/`stop` are the only controls in this plugin bound to
        something that is NOT a parameter, because `playing` is deliberately
        neither automatable nor persisted (Phase 2's decision). They read the
        processor's atomic on a timer instead.

        Each attachment is declared AFTER the control it binds, so destruction,
        which runs in reverse, tears the binding down first. That is the ONLY
        path the ordering covers: an implicitly-defined move-assignment assigns
        in DECLARATION order, so `headerControls = {}` frees each control while
        its attachment still holds a reference. The attachments hold their
        controls through juce::Component::SafePointer for exactly that reason —
        confirmed under AddressSanitizer at 04-04. */
    struct HeaderControls
    {
        std::unique_ptr<LogoMark>  logo;
        std::unique_ptr<GearButton> gear;   ///< 08-02, and the only invented control here
        std::unique_ptr<BpmField>  bpm;
        std::unique_ptr<Button>    sync, half, doubleUp;
        std::unique_ptr<Button>    play, stop;
        std::unique_ptr<Knob>      swing, cachaca;
        std::unique_ptr<ValueScreen> swingRead, cachacaRead;
        std::unique_ptr<Button>    presetPrev, presetNext;   ///< wired at 09-06
        std::unique_ptr<ValueScreen> presetScreen;           ///< wired at 09-06

        std::unique_ptr<BpmAttachment>    bpmAttachment;
        std::unique_ptr<ToggleAttachment> syncAttachment;
        std::unique_ptr<class KnobAttachment> swingAttachment, cachacaAttachment;
    };

    HeaderControls headerControls;

    /** Polls what has no attachment: the transport's `playing` atomic, the
        host's tempo, and the active groove's name — none of which is a
        parameter. Runs at `kUiPollHz`, which Surface.h owns and states the
        reason for; this used to declare its own copy of 30. */
    PollTimer headerPoll;

    /** css:100-101's track, running only while the chassis is tipsy.

        Not folded into `headerPoll`: that one starts with `attachParameters`
        and exists to read a processor, where this must run on a bar that has one
        and on a bar that does not.

        A `KeyframeLoop` rather than a hand-rolled poll-plus-phase-plus-value.
        The hand-rolled version stored the opacity alongside the phase and reset
        it to 1.0 where the curve at phase 0 is 0.55 — so the label painted one
        frame at full brightness and snapped down on the next tick, and the check
        that was supposed to prove it "starts from rest" asserted the wrong one
        of the two. `KeyframeLoop` holds no value at all. /simplify. */
    KeyframeLoop pulse { drunk::kLabelPulseSeconds,
                         { { 0.0, drunk::kLabelPulseLowOpacity },
                           { 0.5, drunk::kLabelPulseHighOpacity },
                           { 1.0, drunk::kLabelPulseLowOpacity } },
                         [this] { repaint (headerLayout.cachacaName); },
                         KeyframeTiming::easeInOut };

    bool tipsy { false };

    /** The label's opacity NOW: the track's value while tipsy, and full
        brightness otherwise — where the label is `CACHAÇA` and does not pulse
        at all. */
    float pulseOpacityNow() const noexcept;

    // Global scope, not forrobox:: — a forward declaration inside this
    // namespace would name a different, incomplete type.
    /** What `refreshFromProcessor` and the timer both call. Null until
        attachParameters has run. */
    ::ForroBoxAudioProcessor*           polledProcessor { nullptr };
    juce::AudioProcessorValueTreeState* polledApvts { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};

} // namespace forrobox
