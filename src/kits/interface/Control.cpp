/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors:
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers, mflerackers@androme.be
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 */


/**
 * @file Control.cpp
 * @brief Implementation of BControl, the base class for user-event-driven controls
 *
 * BControl adds value storage, enabled/disabled state, label management, and
 * invocation of a target BMessage to BView. It is the common ancestor of
 * BButton, BCheckBox, BSlider, and other interactive controls.
 *
 * @see BView, BInvoker, BButton, BCheckBox, BSlider
 */


#include <stdlib.h>
#include <string.h>

#include <Control.h>
#include <PropertyInfo.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>
#include <Icon.h>


/** @brief Scripting property table exposing Enabled, Label, and Value. */
static property_info sPropertyList[] = {
	{
		"Enabled",
		{ B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		NULL, 0,
		{ B_BOOL_TYPE }
	},
	{
		"Label",
		{ B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		NULL, 0,
		{ B_STRING_TYPE }
	},
	{
		"Value",
		{ B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		NULL, 0,
		{ B_INT32_TYPE }
	},

	{ 0 }
};


/**
 * @brief Construct a BControl with a frame-based layout.
 *
 * @param frame      The control's initial frame rectangle in the parent's
 *                   coordinate system.
 * @param name       The view name used for scripting and identification.
 * @param label      The human-readable label displayed by the control, or NULL.
 * @param message    The BMessage to send when the control is invoked, or NULL.
 * @param resizingMode Resizing flags passed to BView.
 * @param flags      View flags passed to BView.
 * @see BView::BView()
 */
BControl::BControl(BRect frame, const char* name, const char* label,
	BMessage* message, uint32 resizingMode, uint32 flags)
	:
	BView(frame, name, resizingMode, flags)
{
	InitData(NULL);

	SetLabel(label);
	SetMessage(message);
}


/**
 * @brief Construct a layout-friendly BControl without an explicit frame.
 *
 * @param name    The view name used for scripting and identification.
 * @param label   The human-readable label displayed by the control, or NULL.
 * @param message The BMessage to send when the control is invoked, or NULL.
 * @param flags   View flags passed to BView.
 * @see BView::BView()
 */
BControl::BControl(const char* name, const char* label, BMessage* message,
	uint32 flags)
	:
	BView(name, flags)
{
	InitData(NULL);

	SetLabel(label);
	SetMessage(message);
}


/**
 * @brief Destroy the BControl, freeing its label, icon, and invocation message.
 */
BControl::~BControl()
{
	free(fLabel);
	delete fIcon;
	SetMessage(NULL);
}


/**
 * @brief Unarchiving constructor — restore a BControl from a BMessage archive.
 *
 * Reads the invocation message ("_msg"), label ("_label"), value ("_val"),
 * enabled state ("_disable"), and keyboard-navigation flag ("be:wants_nav")
 * from \a data.
 *
 * @param data The archive message produced by Archive().
 * @see Archive(), Instantiate()
 */
BControl::BControl(BMessage* data)
	:
	BView(data)
{
	InitData(data);

	BMessage message;
	if (data->FindMessage("_msg", &message) == B_OK)
		SetMessage(new BMessage(message));

	const char* label;
	if (data->FindString("_label", &label) == B_OK)
		SetLabel(label);

	int32 value;
	if (data->FindInt32("_val", &value) == B_OK)
		SetValue(value);

	bool toggle;
	if (data->FindBool("_disable", &toggle) == B_OK)
		SetEnabled(!toggle);

	if (data->FindBool("be:wants_nav", &toggle) == B_OK)
		fWantsNav = toggle;
}


/**
 * @brief Create a BControl from an archive message.
 *
 * @param data The archive message to instantiate from.
 * @return A newly allocated BControl on success, or NULL if validation fails.
 * @see Archive()
 */
BArchivable*
BControl::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BControl"))
		return new BControl(data);

	return NULL;
}


/**
 * @brief Archive this BControl into a BMessage.
 *
 * Stores the invocation message, label, value (when non-zero), and disabled
 * state into \a data in addition to the base BView archive fields.
 *
 * @param data The message to archive into.
 * @param deep If true, child views are archived recursively.
 * @return B_OK on success, or an error code on the first failure.
 * @see Instantiate()
 */
status_t
BControl::Archive(BMessage* data, bool deep) const
{
	status_t status = BView::Archive(data, deep);

	if (status == B_OK && Message())
		status = data->AddMessage("_msg", Message());

	if (status == B_OK && fLabel)
		status = data->AddString("_label", fLabel);

	if (status == B_OK && fValue != B_CONTROL_OFF)
		status = data->AddInt32("_val", fValue);

	if (status == B_OK && !fEnabled)
		status = data->AddBool("_disable", true);

	return status;
}


/**
 * @brief Respond to the parent window gaining or losing focus.
 *
 * Redraws the control when it holds keyboard focus so that the focus ring
 * can reflect the new window-active state.
 *
 * @param active True if the window just became active, false if it lost focus.
 */
void
BControl::WindowActivated(bool active)
{
	BView::WindowActivated(active);

	if (IsFocus())
		Invalidate();
}


/**
 * @brief Hook called when the control is attached to a window.
 *
 * If no explicit invocation target has been set, the control automatically
 * targets its own window so that invoking the control sends messages there.
 *
 * @see BInvoker::SetTarget(), DetachedFromWindow()
 */
void
BControl::AttachedToWindow()
{
	if (!Messenger().IsValid())
		SetTarget(Window());

	BView::AttachedToWindow();
}


/**
 * @brief Hook called when the control is detached from its window.
 *
 * Delegates to BView::DetachedFromWindow(); subclasses may override to release
 * window-specific resources.
 *
 * @see AttachedToWindow()
 */
void
BControl::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


/**
 * @brief Hook called after all views in the hierarchy have been attached.
 */
void
BControl::AllAttached()
{
	BView::AllAttached();
}


/**
 * @brief Hook called after all views in the hierarchy have been detached.
 */
void
BControl::AllDetached()
{
	BView::AllDetached();
}


/**
 * @brief Dispatch incoming scripting messages for Label, Value, and Enabled.
 *
 * Handles B_GET_PROPERTY and B_SET_PROPERTY for the "Label", "Value", and
 * "Enabled" properties, replying directly to the sender. All other messages
 * are forwarded to BView::MessageReceived().
 *
 * @param message The message to process.
 * @see ResolveSpecifier(), GetSupportedSuites()
 */
void
BControl::MessageReceived(BMessage* message)
{
	if (message->what == B_GET_PROPERTY || message->what == B_SET_PROPERTY) {
		BMessage reply(B_REPLY);
		bool handled = false;

		BMessage specifier;
		int32 index;
		int32 form;
		const char* property;
		if (message->GetCurrentSpecifier(&index, &specifier, &form, &property) == B_OK) {
			if (strcmp(property, "Label") == 0) {
				if (message->what == B_GET_PROPERTY) {
					reply.AddString("result", fLabel);
					handled = true;
				} else {
					// B_SET_PROPERTY
					const char* label;
					if (message->FindString("data", &label) == B_OK) {
						SetLabel(label);
						reply.AddInt32("error", B_OK);
						handled = true;
					}
				}
			} else if (strcmp(property, "Value") == 0) {
				if (message->what == B_GET_PROPERTY) {
					reply.AddInt32("result", fValue);
					handled = true;
				} else {
					// B_SET_PROPERTY
					int32 value;
					if (message->FindInt32("data", &value) == B_OK) {
						SetValue(value);
						reply.AddInt32("error", B_OK);
						handled = true;
					}
				}
			} else if (strcmp(property, "Enabled") == 0) {
				if (message->what == B_GET_PROPERTY) {
					reply.AddBool("result", fEnabled);
					handled = true;
				} else {
					// B_SET_PROPERTY
					bool enabled;
					if (message->FindBool("data", &enabled) == B_OK) {
						SetEnabled(enabled);
						reply.AddInt32("error", B_OK);
						handled = true;
					}
				}
			}
		}

		if (handled) {
			message->SendReply(&reply);
			return;
		}
	}

	BView::MessageReceived(message);
}


/**
 * @brief Give or remove keyboard focus for the control.
 *
 * Sets the fFocusChanging flag around an Invalidate() call so that Draw()
 * implementations can tell whether they are being called because focus changed
 * versus because content changed.
 *
 * @param focus True to give focus, false to remove it.
 * @see IsFocusChanging()
 */
void
BControl::MakeFocus(bool focus)
{
	if (focus == IsFocus())
		return;

	BView::MakeFocus(focus);

	if (Window() != NULL) {
		fFocusChanging = true;
		Invalidate(Bounds());
		Flush();
		fFocusChanging = false;
	}
}


/**
 * @brief Handle a key-down event by toggling the control's value.
 *
 * B_ENTER and B_SPACE toggle the control between B_CONTROL_ON and
 * B_CONTROL_OFF and invoke it. All other keys are forwarded to BView.
 *
 * @param bytes    Pointer to the UTF-8 byte sequence for the pressed key.
 * @param numBytes Length of the byte sequence.
 * @see Invoke()
 */
void
BControl::KeyDown(const char* bytes, int32 numBytes)
{
	if (*bytes == B_ENTER || *bytes == B_SPACE) {
		if (!fEnabled)
			return;

		SetValue(Value() ? B_CONTROL_OFF : B_CONTROL_ON);
		Invoke();
	} else
		BView::KeyDown(bytes, numBytes);
}


/**
 * @brief Hook called when a mouse button is pressed over the control.
 *
 * Default implementation delegates to BView::MouseDown(). Subclasses
 * typically override this to begin tracking mouse activity.
 *
 * @param where The click point in the view's local coordinate system.
 * @see MouseUp(), MouseMoved()
 */
void
BControl::MouseDown(BPoint where)
{
	BView::MouseDown(where);
}


/**
 * @brief Hook called when a mouse button is released over the control.
 *
 * Default implementation delegates to BView::MouseUp().
 *
 * @param where The release point in the view's local coordinate system.
 * @see MouseDown(), MouseMoved()
 */
void
BControl::MouseUp(BPoint where)
{
	BView::MouseUp(where);
}


/**
 * @brief Hook called when the mouse moves over the control.
 *
 * Default implementation delegates to BView::MouseMoved().
 *
 * @param where       Current mouse position in local coordinates.
 * @param code        One of B_ENTERED_VIEW, B_INSIDE_VIEW, B_EXITED_VIEW, or
 *                    B_OUTSIDE_VIEW.
 * @param dragMessage The drag-and-drop message if a drag is in progress, or
 *                    NULL otherwise.
 * @see MouseDown(), MouseUp()
 */
void
BControl::MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage)
{
	BView::MouseMoved(where, code, dragMessage);
}


/**
 * @brief Set the control's visible label string.
 *
 * An empty string is treated identically to NULL (no label). The layout and
 * drawing are both invalidated if the label actually changes.
 *
 * @param label The new label text, or NULL to clear the label.
 * @see Label()
 */
void
BControl::SetLabel(const char* label)
{
	if (label != NULL && !label[0])
		label = NULL;

	// Has the label been changed?
	if ((fLabel && label && !strcmp(fLabel, label))
		|| ((fLabel == NULL || !fLabel[0]) && label == NULL))
		return;

	free(fLabel);
	fLabel = label ? strdup(label) : NULL;

	InvalidateLayout();
	Invalidate();
}


/**
 * @brief Return the control's current label string.
 *
 * @return The label text, or NULL if no label has been set.
 * @see SetLabel()
 */
const char*
BControl::Label() const
{
	return fLabel;
}


/**
 * @brief Set the control's integer value and redraw if the value changed.
 *
 * @param value The new value. Use B_CONTROL_ON or B_CONTROL_OFF for boolean
 *              controls.
 * @see Value(), SetValueNoUpdate()
 */
void
BControl::SetValue(int32 value)
{
	if (value == fValue)
		return;

	fValue = value;
	Invalidate();
}


/**
 * @brief Set the control's integer value without triggering a redraw.
 *
 * Useful when multiple state changes need to be batched before the next draw
 * cycle.
 *
 * @param value The new value to store.
 * @see SetValue(), Value()
 */
void
BControl::SetValueNoUpdate(int32 value)
{
	fValue = value;
}


/**
 * @brief Return the control's current integer value.
 *
 * @return The stored value, which may be B_CONTROL_ON, B_CONTROL_OFF, or any
 *         custom integer set by SetValue().
 * @see SetValue()
 */
int32
BControl::Value() const
{
	return fValue;
}


/**
 * @brief Enable or disable the control.
 *
 * A disabled control ignores user input. When the control is re-enabled and
 * holds the B_NAVIGABLE flag, keyboard navigation is restored. The control is
 * redrawn immediately if it is attached to a window.
 *
 * @param enabled True to enable the control, false to disable it.
 * @see IsEnabled()
 */
void
BControl::SetEnabled(bool enabled)
{
	if (fEnabled == enabled)
		return;

	fEnabled = enabled;

	if (fEnabled && fWantsNav)
		SetFlags(Flags() | B_NAVIGABLE);
	else if (!fEnabled && (Flags() & B_NAVIGABLE)) {
		fWantsNav = true;
		SetFlags(Flags() & ~B_NAVIGABLE);
	} else
		fWantsNav = false;

	if (Window()) {
		Invalidate(Bounds());
		Flush();
	}
}


/**
 * @brief Return whether the control is currently enabled.
 *
 * @return True if the control accepts user input, false if it is disabled.
 * @see SetEnabled()
 */
bool
BControl::IsEnabled() const
{
	return fEnabled;
}


/**
 * @brief Report the control's preferred size.
 *
 * Delegates to BView::GetPreferredSize(). Subclasses should override this
 * to return a size appropriate for their content.
 *
 * @param _width  Set to the preferred width on return.
 * @param _height Set to the preferred height on return.
 */
void
BControl::GetPreferredSize(float* _width, float* _height)
{
	BView::GetPreferredSize(_width, _height);
}


/**
 * @brief Resize the control to its preferred size.
 *
 * Delegates to BView::ResizeToPreferred().
 */
void
BControl::ResizeToPreferred()
{
	BView::ResizeToPreferred();
}


/**
 * @brief Send the control's invocation message to its target.
 *
 * Augments the message with the current time ("when"), a "source" pointer to
 * this control, the current value ("be:value"), and a messenger back to this
 * control ("be:sender"). Observer notifications are sent for the same kind.
 *
 * @param message The message to send, or NULL to use the control's own
 *                message (set via BInvoker::SetMessage()).
 * @return B_OK on success.
 * @retval B_BAD_VALUE If no message is available and no observers are watching.
 * @see InvokeNotify(), BInvoker::SetMessage()
 */
status_t
BControl::Invoke(BMessage* message)
{
	bool notify = false;
	uint32 kind = InvokeKind(&notify);

	if (!message && !notify)
		message = Message();

	BMessage clone(kind);

	if (!message) {
		if (!IsWatched())
			return B_BAD_VALUE;
	} else
		clone = *message;

	clone.AddInt64("when", (int64)system_time());
	clone.AddPointer("source", this);
	clone.AddInt32("be:value", fValue);
	clone.AddMessenger("be:sender", BMessenger(this));

	// ToDo: is this correct? If message == NULL (even if IsWatched()), we always return B_BAD_VALUE
	status_t err;
	if (message)
		err = BInvoker::Invoke(&clone);
	else
		err = B_BAD_VALUE;

	// TODO: asynchronous messaging
	SendNotices(kind, &clone);

	return err;
}


/**
 * @brief Resolve a scripting specifier to the appropriate handler.
 *
 * Returns this control if the specifier names a property listed in
 * sPropertyList; otherwise delegates to BView::ResolveSpecifier().
 *
 * @param message   The scripting message being processed.
 * @param index     Index of the current specifier in the message.
 * @param specifier The specifier message extracted from \a message.
 * @param what      The specifier's type constant.
 * @param property  The property name string.
 * @return This handler if the property is known, otherwise the result of
 *         BView::ResolveSpecifier().
 * @see GetSupportedSuites()
 */
BHandler*
BControl::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	BPropertyInfo propInfo(sPropertyList);

	if (propInfo.FindMatch(message, 0, specifier, what, property) >= B_OK)
		return this;

	return BView::ResolveSpecifier(message, index, specifier, what,
		property);
}


/**
 * @brief Add the BControl scripting suite to \a message.
 *
 * Appends "suite/vnd.Be-control" and the property descriptor list to the
 * reply message before chaining to BView::GetSupportedSuites().
 *
 * @param message The reply message to populate with suite information.
 * @return B_OK on success, or an error forwarded from BView.
 * @see ResolveSpecifier()
 */
status_t
BControl::GetSupportedSuites(BMessage* message)
{
	message->AddString("suites", "suite/vnd.Be-control");

	BPropertyInfo propInfo(sPropertyList);
	message->AddFlat("messages", &propInfo);

	return BView::GetSupportedSuites(message);
}


/**
 * @brief Binary-compatibility hook for calling overridden virtual methods.
 *
 * Dispatches perform codes for layout queries (MinSize, MaxSize,
 * PreferredSize, LayoutAlignment, HasHeightForWidth, GetHeightForWidth,
 * SetLayout, LayoutInvalidated, DoLayout) and PERFORM_CODE_SET_ICON.
 * Unknown codes are forwarded to BView::Perform().
 *
 * @param code  The perform code identifying the operation to execute.
 * @param _data Pointer to a perform_data structure corresponding to \a code.
 * @return B_OK on success, or an error from BView::Perform().
 */
status_t
BControl::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BControl::MinSize();
			return B_OK;
		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BControl::MaxSize();
			return B_OK;
		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BControl::PreferredSize();
			return B_OK;
		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BControl::LayoutAlignment();
			return B_OK;
		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BControl::HasHeightForWidth();
			return B_OK;
		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BControl::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
}
		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BControl::SetLayout(data->layout);
			return B_OK;
		}
		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BControl::LayoutInvalidated(data->descendants);
			return B_OK;
		}
		case PERFORM_CODE_DO_LAYOUT:
		{
			BControl::DoLayout();
			return B_OK;
		}
		case PERFORM_CODE_SET_ICON:
		{
			perform_data_set_icon* data = (perform_data_set_icon*)_data;
			return BControl::SetIcon(data->icon, data->flags);
		}
	}

	return BView::Perform(code, _data);
}


/**
 * @brief Set the control's icon from a BBitmap.
 *
 * Replaces or creates the internal BIcon, then invalidates the layout and
 * triggers a redraw so the new icon appears immediately.
 *
 * @param bitmap The source bitmap to use as the icon, or NULL to clear.
 * @param flags  Icon creation flags forwarded to BIcon::UpdateIcon().
 * @return B_OK on success, or an error if the bitmap could not be converted.
 * @see SetIconBitmap(), IconBitmap()
 */
status_t
BControl::SetIcon(const BBitmap* bitmap, uint32 flags)
{
	status_t error = BIcon::UpdateIcon(bitmap, flags, fIcon);

	if (error == B_OK) {
		InvalidateLayout();
		Invalidate();
	}

	return error;
}


/**
 * @brief Set a specific icon state bitmap for this control.
 *
 * Sets one state (e.g. normal, disabled, pressed) of the internal multi-state
 * icon. Invalidates the layout and schedules a redraw on failure so that stale
 * icon state is cleared.
 *
 * @param bitmap The bitmap for the given state, or NULL to clear it.
 * @param which  The icon state constant (e.g. B_INACTIVE_ICON_BITMAP).
 * @param flags  Flags forwarded to BIcon::SetIconBitmap().
 * @return B_OK on success, or an error code on failure.
 * @see SetIcon(), IconBitmap()
 */
status_t
BControl::SetIconBitmap(const BBitmap* bitmap, uint32 which, uint32 flags)
{
	status_t error = BIcon::SetIconBitmap(bitmap, which, flags, fIcon);

	if (error != B_OK) {
		InvalidateLayout();
		Invalidate();
	}

	return error;
}


/**
 * @brief Return the bitmap for a specific icon state.
 *
 * @param which The icon state constant identifying which bitmap to retrieve.
 * @return The corresponding BBitmap, or NULL if no icon or no bitmap for that
 *         state is set.
 * @see SetIcon(), SetIconBitmap()
 */
const BBitmap*
BControl::IconBitmap(uint32 which) const
{
	return fIcon != NULL ? fIcon->Bitmap(which) : NULL;
}


/**
 * @brief Return whether the control is currently redrawing due to a focus change.
 *
 * Draw() implementations can call this to decide whether to render a focus
 * ring without re-drawing other content.
 *
 * @return True between the Invalidate() and Flush() calls inside MakeFocus().
 * @see MakeFocus()
 */
bool
BControl::IsFocusChanging() const
{
	return fFocusChanging;
}


/**
 * @brief Return whether the control is currently tracking the mouse.
 *
 * @return True if SetTracking(true) has been called and SetTracking(false) has
 *         not yet been called.
 * @see SetTracking()
 */
bool
BControl::IsTracking() const
{
	return fTracking;
}


/**
 * @brief Set the mouse-tracking state of the control.
 *
 * Subclasses call this inside MouseDown() / MouseUp() to signal that a drag
 * gesture is in progress.
 *
 * @param state True to mark the control as tracking, false to clear it.
 * @see IsTracking()
 */
void
BControl::SetTracking(bool state)
{
	fTracking = state;
}


/**
 * @brief GCC 2 binary-compatibility thunk for BControl::SetIcon().
 *
 * Routes through Perform() with PERFORM_CODE_SET_ICON so that shared
 * libraries built against older ABIs can call the virtual SetIcon() method.
 *
 * @param control The target BControl instance.
 * @param icon    The icon bitmap, or NULL to clear.
 * @param flags   Icon flags forwarded to SetIcon().
 * @return B_OK on success, or an error from Perform().
 */
extern "C" status_t
B_IF_GCC_2(_ReservedControl1__8BControl, _ZN8BControl17_ReservedControl1Ev)(
	BControl* control, const BBitmap* icon, uint32 flags)
{
	// SetIcon()
	perform_data_set_icon data;
	data.icon = icon;
	data.flags = flags;
	return control->Perform(PERFORM_CODE_SET_ICON, &data);
}


void BControl::_ReservedControl2() {}
void BControl::_ReservedControl3() {}
void BControl::_ReservedControl4() {}


/**
 * @brief Copy-assignment operator — intentionally unimplemented.
 *
 * BControl objects are not copyable. The operator is declared to prevent
 * accidental shallow copies; it returns *this without copying any state.
 *
 * @return Reference to this object (unchanged).
 */
BControl &
BControl::operator=(const BControl &)
{
	return *this;
}


/**
 * @brief Shared initialisation helper called by all constructors.
 *
 * Sets the view's UI colour, zeros all control fields, and optionally
 * restores the font family/style from an archive message.
 *
 * @param data Archive message to read font information from, or NULL for a
 *             fresh construction.
 */
void
BControl::InitData(BMessage* data)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(ViewUIColor());

	fLabel = NULL;
	SetLabel(B_EMPTY_STRING);
	fValue = B_CONTROL_OFF;
	fEnabled = true;
	fFocusChanging = false;
	fTracking = false;
	fWantsNav = Flags() & B_NAVIGABLE;
	fIcon = NULL;

	if (data && data->HasString("_fname"))
		SetFont(be_plain_font, B_FONT_FAMILY_AND_STYLE);
}
