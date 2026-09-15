/* ============================================================================
   FORRÓ BOX — the attachment lifetime guard

   Every attachment in this plugin installs `std::function` callbacks on the
   control it binds, and every one of those callbacks captures `this`. Leaving
   them on a control that outlives the attachment turns the next mouse event into
   a use-after-free — which AddressSanitizer confirmed exactly at 04-03, twenty
   times per call, through `StripControls::operator=`.

   THE TWO PATHS, because only one of them is what people think of:

     Destruction runs in REVERSE declaration order, so declaring attachments
     after the controls they bind is enough — on that path alone.

     Move-ASSIGNMENT does not. An implicitly-defined move-assignment assigns
     members in DECLARATION order, so `controls = {}` frees each control while
     its attachment still holds a reference. There is NO declaration order that
     is safe on both, which is why the control is held WEAKLY and the teardown
     null-checks rather than assuming.

   Five attachments each wrote that out, each re-explaining it. `/simplify` named
   the extraction at 04-06 and named its limit: this owns the LIFETIME law and not
   one value mapping. The read map, the write map and the callback signature all
   differ between them, and a merged class would need a bool-versus-int seam —
   which is the argument `ToggleAttachment.h` already makes against folding into
   `ProportionAttachment`.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <utility>

namespace forrobox
{

/** A weak handle to a control, which clears that control's callbacks when the
    attachment holding it dies.

    Declare it BEFORE the `juce::ParameterAttachment` in every owner. Destruction
    then runs the parameter attachment first — removing the listener, so no
    callback can still arrive — and this second. The reverse order would null the
    control's callbacks while the parameter listener was still installed. */
template <typename Control>
class ScopedControlCallbacks
{
public:
    /** `reset` says which of the control's callbacks this attachment installed.

        Taken once, at construction, so each attachment states its own list in
        ONE visible place instead of in a destructor several screens away.

        A plain function POINTER, not a std::function. Every reset list is a
        compile-time constant and all five lambdas are captureless, so the type
        erasure bought nothing — and it permitted a CAPTURING reset lambda, which
        is the lifetime hazard this class exists to remove: a capture could
        outlive what it captured and run during destruction. Now it will not
        compile. Found by /simplify. */
    using Reset = void (*) (Control&);

    ScopedControlCallbacks (Control& controlToUse, Reset reset)
        : control (&controlToUse), resetCallbacks (reset)
    {
    }

    /** A guard over a control that may not exist.

        For a container that must keep one SLOT per item even where the item is
        missing — `ChoiceButtonsAttachment` holds one guard per choice, and
        dropping the empty ones would shift every later choice down by one. A
        null guard clears nothing and `get()` returns nullptr, which every use
        site already checks for a control that died. */
    ScopedControlCallbacks (Control* controlToUse, Reset reset)
        : control (controlToUse), resetCallbacks (reset)
    {
    }

    ~ScopedControlCallbacks()
    {
        // Through the SafePointer: a control that died FIRST is simply gone
        // rather than written to.
        if (auto* c = control.getComponent(); c != nullptr && resetCallbacks != nullptr)
            resetCallbacks (*c);
    }

    /** The control, or nullptr if it died first. Every use site must check. */
    Control* get() const noexcept { return control.getComponent(); }

    ScopedControlCallbacks (const ScopedControlCallbacks&) = delete;
    ScopedControlCallbacks& operator= (const ScopedControlCallbacks&) = delete;

    /** MOVABLE, so a container can hold one guard per control.

        A guard is a destructor with state, so moving has to leave the source
        unable to fire: the moved-from object nulls its reset, and its destructor
        then does nothing. Copying stays deleted — two guards over one control
        would clear it twice, and the second clear would run after whatever
        re-installed the callbacks.

        Added at 05-03, where `ChoiceButtonsAttachment` holds one guard per
        button. Cheap now only because /simplify made the reset a plain function
        pointer at 05-02; with a std::function this would have been a heap move. */
    ScopedControlCallbacks (ScopedControlCallbacks&& other) noexcept
        : control (other.control), resetCallbacks (other.resetCallbacks)
    {
        other.resetCallbacks = nullptr;
    }

    ScopedControlCallbacks& operator= (ScopedControlCallbacks&&) = delete;

private:
    juce::Component::SafePointer<Control> control;
    Reset resetCallbacks { nullptr };
};

} // namespace forrobox
