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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2015 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 *       Mike Wilber
 *       Stefano Ceccherini (burton666@libero.it)
 *       Ivan Tonizza
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 */


/**
 * @file Button.cpp
 * @brief Implementation of BButton, a clickable push-button control
 *
 * BButton is a standard push button that sends a BMessage when clicked. It
 * supports default-button behavior (triggered by Enter), keyboard navigation,
 * and rendering via BControlLook.
 *
 * @see BControl, BControlLook
 */


#include <Button.h>

#include <algorithm>
#include <new>

#include <Bitmap.h>
#include <ControlLook.h>
#include <Font.h>
#include <LayoutUtils.h>
#include <String.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>


/** @brief Bit flag indicating this button is the window's default button. */
/** @brief Bit flag indicating flat (borderless) rendering style. */
/** @brief Bit flag indicating the pointer is currently inside the button bounds. */
/** @brief Bit flag recording the pressed state at the start of a toggle gesture. */
enum {
	FLAG_DEFAULT 		= 0x01,
	FLAG_FLAT			= 0x02,
	FLAG_INSIDE			= 0x04,
	FLAG_WAS_PRESSED	= 0x08,
};


/**
 * @brief Constructs a BButton with an explicit frame rectangle (legacy layout).
 *
 * Ensures the button meets a minimum height derived from the current font
 * metrics, resizing vertically if the supplied frame is too short.
 *
 * @param frame        The position and size of the button in the parent's
 *                     coordinate system.
 * @param name         The internal view name used for look-up.
 * @param label        The visible text drawn on the button face.
 * @param message      The message sent when the button is clicked. Ownership
 *                     is transferred to BControl.
 * @param resizingMode How the button resizes with its parent (B_FOLLOW_* flags).
 * @param flags        View flags (B_WILL_DRAW and B_FULL_UPDATE_ON_RESIZE are
 *                     always added).
 */
BButton::BButton(BRect frame, const char* name, const char* label,
	BMessage* message, uint32 resizingMode, uint32 flags)
	:
	BControl(frame, name, label, message, resizingMode,
		flags | B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fPreferredSize(-1, -1),
	fFlags(0),
	fBehavior(B_BUTTON_BEHAVIOR),
	fPopUpMessage(NULL)
{
	// Resize to minimum height if needed
	BFont font;
	GetFont(&font);
	font_height fh;
	font.GetHeight(&fh);
	float minHeight = font.Size() + (float)ceil(fh.ascent + fh.descent);
	if (Bounds().Height() < minHeight)
		ResizeTo(Bounds().Width(), minHeight);
}


/**
 * @brief Constructs a BButton without an explicit frame (layout-managed).
 *
 * @param name    The internal view name.
 * @param label   The visible text drawn on the button face.
 * @param message The message sent when the button is clicked.
 * @param flags   View flags (B_WILL_DRAW and B_FULL_UPDATE_ON_RESIZE are
 *                always added).
 */
BButton::BButton(const char* name, const char* label, BMessage* message,
	uint32 flags)
	:
	BControl(name, label, message,
		flags | B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fPreferredSize(-1, -1),
	fFlags(0),
	fBehavior(B_BUTTON_BEHAVIOR),
	fPopUpMessage(NULL)
{
}


/**
 * @brief Constructs a BButton with only a label and message (simplest form).
 *
 * The button uses the standard navigable, will-draw, and full-update flags.
 * This is the recommended constructor when using the layout system.
 *
 * @param label   The visible text drawn on the button face.
 * @param message The message sent when the button is clicked.
 */
BButton::BButton(const char* label, BMessage* message)
	:
	BControl(NULL, label, message,
		B_WILL_DRAW | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE),
	fPreferredSize(-1, -1),
	fFlags(0),
	fBehavior(B_BUTTON_BEHAVIOR),
	fPopUpMessage(NULL)
{
}


/**
 * @brief Destroys the BButton and releases the pop-up message.
 */
BButton::~BButton()
{
	SetPopUpMessage(NULL);
}


/**
 * @brief Constructs a BButton from an archived BMessage.
 *
 * Reads the optional "_default" boolean field to restore default-button
 * state. The window synchronization happens later in AttachedToWindow().
 *
 * @param data The archive message produced by Archive().
 * @see Instantiate()
 * @see Archive()
 */
BButton::BButton(BMessage* data)
	:
	BControl(data),
	fPreferredSize(-1, -1),
	fFlags(0),
	fBehavior(B_BUTTON_BEHAVIOR),
	fPopUpMessage(NULL)
{
	bool isDefault = false;
	if (data->FindBool("_default", &isDefault) == B_OK && isDefault)
		_SetFlag(FLAG_DEFAULT, true);
	// NOTE: Default button state will be synchronized with the window
	// in AttachedToWindow().
}


/**
 * @brief Creates a new BButton from an archived BMessage.
 *
 * @param data The archive message to instantiate from.
 * @return A newly allocated BButton on success, or NULL if validation fails.
 * @see Archive()
 */
BArchivable*
BButton::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BButton"))
		return new(std::nothrow) BButton(data);

	return NULL;
}


/**
 * @brief Archives the BButton into a BMessage.
 *
 * Calls the base-class archive, then adds the "_default" field when the
 * button is currently the window's default button.
 *
 * @param data  The message to archive into.
 * @param deep  If true, child objects are archived recursively.
 * @return B_OK on success, or an error code from BControl::Archive().
 */
status_t
BButton::Archive(BMessage* data, bool deep) const
{
	status_t err = BControl::Archive(data, deep);

	if (err != B_OK)
		return err;

	if (IsDefault())
		err = data->AddBool("_default", true);

	return err;
}


/**
 * @brief Draws the button into the current view.
 *
 * Builds the appropriate BControlLook flags (default, flat, hover, pressed)
 * and delegates all rendering to BControlLook helpers, including the frame,
 * background, optional pop-up affordance, icon, and label.
 *
 * @param updateRect The invalid rectangle that needs to be redrawn.
 */
void
BButton::Draw(BRect updateRect)
{
	BRect rect(Bounds());
	rgb_color background = ViewColor();
	rgb_color base = LowColor();
	rgb_color text = HighColor();

	uint32 flags = be_control_look->Flags(this);
	if (_Flag(FLAG_DEFAULT))
		flags |= BControlLook::B_DEFAULT_BUTTON;
	if (_Flag(FLAG_FLAT) && !IsTracking())
		flags |= BControlLook::B_FLAT;
	if (_Flag(FLAG_INSIDE))
		flags |= BControlLook::B_HOVER;

	be_control_look->DrawButtonFrame(this, rect, updateRect, base, background, flags);

	if (fBehavior == B_POP_UP_BEHAVIOR)
		be_control_look->DrawButtonWithPopUpBackground(this, rect, updateRect, base, flags);
	else
		be_control_look->DrawButtonBackground(this, rect, updateRect, base, flags);

	const BBitmap* icon = IconBitmap(
		(Value() == B_CONTROL_OFF
				? B_INACTIVE_ICON_BITMAP : B_ACTIVE_ICON_BITMAP)
			| (IsEnabled() ? 0 : B_DISABLED_ICON_BITMAP));

	be_control_look->DrawLabel(this, Label(), icon, rect, updateRect, base, flags,
		BAlignment(B_ALIGN_CENTER, B_ALIGN_MIDDLE), &text);
}


/**
 * @brief Handles a mouse-button-press event.
 *
 * For pop-up behavior, a click in the pop-up affordance area invokes the
 * pop-up message instead of the normal action. For toggle behavior the
 * current pressed state is recorded before visual feedback begins. In
 * synchronous mode the method polls the mouse until the button is released,
 * then invokes if the pointer is still inside the button.
 *
 * @param where The location of the click in the view's coordinate system.
 */
void
BButton::MouseDown(BPoint where)
{
	if (!IsEnabled())
		return;

	if (fBehavior == B_POP_UP_BEHAVIOR && _PopUpRect().Contains(where)) {
		InvokeNotify(fPopUpMessage, B_CONTROL_MODIFIED);
		return;
	}

	bool toggleBehavior = fBehavior == B_TOGGLE_BEHAVIOR;

	if (toggleBehavior) {
		bool wasPressed = Value() == B_CONTROL_ON;
		_SetFlag(FLAG_WAS_PRESSED, wasPressed);
		SetValue(wasPressed ? B_CONTROL_OFF : B_CONTROL_ON);
		Invalidate();
	} else
		SetValue(B_CONTROL_ON);

	if (Window()->Flags() & B_ASYNCHRONOUS_CONTROLS) {
		SetTracking(true);
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	} else {
		BRect bounds = Bounds();
		uint32 buttons;
		bool inside = false;

		do {
			Window()->UpdateIfNeeded();
			snooze(40000);

			GetMouse(&where, &buttons, true);
			inside = bounds.Contains(where);

			if (toggleBehavior) {
				bool pressed = inside ^ _Flag(FLAG_WAS_PRESSED);
				SetValue(pressed ? B_CONTROL_ON : B_CONTROL_OFF);
			} else {
				if ((Value() == B_CONTROL_ON) != inside)
					SetValue(inside ? B_CONTROL_ON : B_CONTROL_OFF);
			}
		} while (buttons != 0);

		if (inside) {
			if (toggleBehavior) {
				SetValue(
					_Flag(FLAG_WAS_PRESSED) ? B_CONTROL_OFF : B_CONTROL_ON);
			}

			Invoke();
		} else if (_Flag(FLAG_FLAT))
			Invalidate();
	}
}

/**
 * @brief Called when the button is attached to a window.
 *
 * Sets the low and high UI colors for correct theme rendering, and
 * registers this button as the window's default button if IsDefault()
 * is true.
 *
 * @see BControl::AttachedToWindow()
 * @see BWindow::SetDefaultButton()
 */
void
BButton::AttachedToWindow()
{
	BControl::AttachedToWindow();
	SetLowUIColor(B_CONTROL_BACKGROUND_COLOR);
	SetHighUIColor(B_CONTROL_TEXT_COLOR);

	if (IsDefault())
		Window()->SetDefaultButton(this);
}


/**
 * @brief Handles a key-press event.
 *
 * B_ENTER and B_SPACE activate and immediately invoke the button, providing
 * brief visual feedback before the invocation. All other keys are forwarded
 * to BControl.
 *
 * @param bytes    Pointer to the UTF-8 key data.
 * @param numBytes Number of bytes in \a bytes.
 */
void
BButton::KeyDown(const char* bytes, int32 numBytes)
{
	if (*bytes == B_ENTER || *bytes == B_SPACE) {
		if (!IsEnabled())
			return;

		SetValue(B_CONTROL_ON);

		// make sure the user saw that
		Window()->UpdateIfNeeded();
		snooze(25000);

		Invoke();
	} else
		BControl::KeyDown(bytes, numBytes);
}


/**
 * @brief Makes this button the window's default button, or removes that role.
 *
 * When promoted to default, the button grows by 6x6 points (3 on each side)
 * unless it uses the layout system, in which case InvalidateLayout() is called
 * instead. When demoted, the opposite size adjustment is applied and the
 * window's default is cleared.
 *
 * @param flag true to become the default button, false to resign.
 * @see BWindow::SetDefaultButton()
 */
void
BButton::MakeDefault(bool flag)
{
	BButton* oldDefault = NULL;
	BWindow* window = Window();

	if (window != NULL)
		oldDefault = window->DefaultButton();

	if (flag) {
		if (_Flag(FLAG_DEFAULT) && oldDefault == this)
			return;

		if (_SetFlag(FLAG_DEFAULT, true)) {
			if ((Flags() & B_SUPPORTS_LAYOUT) != 0)
				InvalidateLayout();
			else {
				ResizeBy(6.0f, 6.0f);
				MoveBy(-3.0f, -3.0f);
			}
		}

		if (window && oldDefault != this)
			window->SetDefaultButton(this);
	} else {
		if (!_SetFlag(FLAG_DEFAULT, false))
			return;

		if ((Flags() & B_SUPPORTS_LAYOUT) != 0)
			InvalidateLayout();
		else {
			ResizeBy(-6.0f, -6.0f);
			MoveBy(3.0f, 3.0f);
		}

		if (window && oldDefault == this)
			window->SetDefaultButton(NULL);
	}
}


/**
 * @brief Sets the text label displayed on the button face.
 *
 * Delegates directly to BControl::SetLabel(), which triggers a redraw.
 *
 * @param label The new label string, or NULL to clear the label.
 */
void
BButton::SetLabel(const char* label)
{
	BControl::SetLabel(label);
}


/**
 * @brief Returns whether this button is currently the window's default button.
 * @return true if this is the default button, false otherwise.
 */
bool
BButton::IsDefault() const
{
	return _Flag(FLAG_DEFAULT);
}


/**
 * @brief Returns whether the button is rendered in flat (borderless) style.
 * @return true if flat mode is active, false otherwise.
 */
bool
BButton::IsFlat() const
{
	return _Flag(FLAG_FLAT);
}


/**
 * @brief Enables or disables flat (borderless) rendering style.
 *
 * In flat mode the button border is hidden when not tracking, giving a
 * toolbar-button appearance. Invalidates the view if the state changed.
 *
 * @param flat true to enable flat style, false to restore normal style.
 */
void
BButton::SetFlat(bool flat)
{
	if (_SetFlag(FLAG_FLAT, flat))
		Invalidate();
}


/**
 * @brief Returns the current interaction behavior mode.
 * @return The active BBehavior constant (B_BUTTON_BEHAVIOR, B_TOGGLE_BEHAVIOR,
 *         or B_POP_UP_BEHAVIOR).
 */
BButton::BBehavior
BButton::Behavior() const
{
	return fBehavior;
}


/**
 * @brief Sets the interaction behavior mode.
 *
 * Changes how the button responds to clicks: normal momentary press,
 * latching toggle, or split pop-up. Triggers layout invalidation and a
 * redraw if the behavior actually changed.
 *
 * @param behavior The new BBehavior value to apply.
 */
void
BButton::SetBehavior(BBehavior behavior)
{
	if (behavior != fBehavior) {
		fBehavior = behavior;
		InvalidateLayout();
		Invalidate();
	}
}


/**
 * @brief Returns the message sent when the pop-up affordance is clicked.
 * @return A pointer to the pop-up BMessage, or NULL if none is set.
 */
BMessage*
BButton::PopUpMessage() const
{
	return fPopUpMessage;
}


/**
 * @brief Sets the message sent when the pop-up affordance area is clicked.
 *
 * Takes ownership of the provided message and deletes the previous one.
 *
 * @param message The new pop-up message, or NULL to remove it.
 */
void
BButton::SetPopUpMessage(BMessage* message)
{
	delete fPopUpMessage;
	fPopUpMessage = message;
}


/**
 * @brief Handles an incoming BMessage not dispatched by the default mechanism.
 *
 * Delegates to BControl::MessageReceived() for standard message handling.
 *
 * @param message The message to process.
 */
void
BButton::MessageReceived(BMessage* message)
{
	BControl::MessageReceived(message);
}


/**
 * @brief Called when the button's window gains or loses focus.
 *
 * Delegates to BControl::WindowActivated() so focus-ring rendering is
 * updated correctly.
 *
 * @param active true if the window just became active, false if deactivated.
 */
void
BButton::WindowActivated(bool active)
{
	BControl::WindowActivated(active);
}


/**
 * @brief Called when the pointer moves over the button or enters/exits its bounds.
 *
 * Updates the FLAG_INSIDE state and invalidates if it changed. In asynchronous
 * tracking mode, also updates the pressed visual state for toggle and normal
 * button behaviors.
 *
 * @param where       Current pointer position in view coordinates.
 * @param code        B_ENTERED_VIEW, B_INSIDE_VIEW, or B_EXITED_VIEW.
 * @param dragMessage Non-NULL if a drag-and-drop operation is in progress.
 */
void
BButton::MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage)
{
	bool inside = (code != B_EXITED_VIEW) && Bounds().Contains(where);
	if (_SetFlag(FLAG_INSIDE, inside))
		Invalidate();

	if (!IsTracking())
		return;

	if (fBehavior == B_TOGGLE_BEHAVIOR) {
		bool pressed = inside ^ _Flag(FLAG_WAS_PRESSED);
		SetValue(pressed ? B_CONTROL_ON : B_CONTROL_OFF);
	} else {
		if ((Value() == B_CONTROL_ON) != inside)
			SetValue(inside ? B_CONTROL_ON : B_CONTROL_OFF);
	}
}


/**
 * @brief Called when the mouse button is released over this button.
 *
 * Completes the click: if the pointer is inside the button bounds the
 * action is invoked (with toggle-state correction for B_TOGGLE_BEHAVIOR),
 * otherwise the button is simply reset. Stops tracking regardless.
 *
 * @param where The release position in view coordinates.
 */
void
BButton::MouseUp(BPoint where)
{
	if (!IsTracking())
		return;

	if (Bounds().Contains(where)) {
		if (fBehavior == B_TOGGLE_BEHAVIOR)
			SetValue(_Flag(FLAG_WAS_PRESSED) ? B_CONTROL_OFF : B_CONTROL_ON);

		Invoke();
	} else if (_Flag(FLAG_FLAT))
		Invalidate();

	SetTracking(false);
}


/**
 * @brief Called when the button is detached from its window.
 *
 * Delegates to BControl::DetachedFromWindow() for cleanup.
 */
void
BButton::DetachedFromWindow()
{
	BControl::DetachedFromWindow();
}


/**
 * @brief Sets the integer value of the button control.
 *
 * Only forwards the change to BControl if the value is actually different,
 * avoiding unnecessary redraws.
 *
 * @param value The new value (typically B_CONTROL_ON or B_CONTROL_OFF).
 */
void
BButton::SetValue(int32 value)
{
	if (value != Value())
		BControl::SetValue(value);
}


/**
 * @brief Returns the button's preferred width and height.
 *
 * Results are cached after the first computation; the cache is invalidated
 * via LayoutInvalidated(). Pass NULL for dimensions you do not need.
 *
 * @param _width  Receives the preferred width, or ignored if NULL.
 * @param _height Receives the preferred height, or ignored if NULL.
 */
void
BButton::GetPreferredSize(float* _width, float* _height)
{
	_ValidatePreferredSize();

	if (_width)
		*_width = fPreferredSize.width;

	if (_height)
		*_height = fPreferredSize.height;
}


/**
 * @brief Resizes the button to its preferred size.
 *
 * Delegates to BControl::ResizeToPreferred().
 */
void
BButton::ResizeToPreferred()
{
	BControl::ResizeToPreferred();
}


/**
 * @brief Invokes the button's action message.
 *
 * Synchronises the display, waits briefly so the pressed state is visible,
 * then calls BControl::Invoke(). For non-toggle behavior the value is reset
 * to B_CONTROL_OFF afterwards.
 *
 * @param message The message to send, or NULL to use the button's own message.
 * @return B_OK on success, or an error code from BControl::Invoke().
 */
status_t
BButton::Invoke(BMessage* message)
{
	Sync();
	snooze(50000);

	status_t err = BControl::Invoke(message);

	if (fBehavior != B_TOGGLE_BEHAVIOR)
		SetValue(B_CONTROL_OFF);

	return err;
}


/**
 * @brief Called when the button's frame position changes.
 * @param newPosition The new top-left corner position in the parent's coordinates.
 */
void
BButton::FrameMoved(BPoint newPosition)
{
	BControl::FrameMoved(newPosition);
}


/**
 * @brief Called when the button's frame size changes.
 * @param newWidth  The new width in pixels.
 * @param newHeight The new height in pixels.
 */
void
BButton::FrameResized(float newWidth, float newHeight)
{
	BControl::FrameResized(newWidth, newHeight);
}


/**
 * @brief Gives or removes keyboard focus from the button.
 * @param focus true to grant focus, false to remove it.
 */
void
BButton::MakeFocus(bool focus)
{
	BControl::MakeFocus(focus);
}


/**
 * @brief Called after all sibling views have been attached to the window.
 */
void
BButton::AllAttached()
{
	BControl::AllAttached();
}


/**
 * @brief Called after all sibling views have been detached from the window.
 */
void
BButton::AllDetached()
{
	BControl::AllDetached();
}


/**
 * @brief Resolves a scripting specifier to the handler that should process it.
 *
 * @param message   The scripting message.
 * @param index     Index of the specifier within the message.
 * @param specifier The specifier message.
 * @param what      The specifier constant.
 * @param property  The property name being accessed.
 * @return The BHandler that should handle the scripting message.
 */
BHandler*
BButton::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	return BControl::ResolveSpecifier(message, index, specifier, what,
		property);
}


/**
 * @brief Fills a BMessage with the scripting suites supported by BButton.
 *
 * @param message The message to populate with suite information.
 * @return B_OK on success, or an error code.
 */
status_t
BButton::GetSupportedSuites(BMessage* message)
{
	return BControl::GetSupportedSuites(message);
}


/**
 * @brief Executes a binary-compatibility perform code.
 *
 * Handles the PERFORM_CODE_* constants required for ABI stability across
 * shared-library versions, dispatching to the appropriate virtual method.
 * Unrecognised codes are forwarded to BControl::Perform().
 *
 * @param code  The perform code identifying the operation.
 * @param _data Pointer to the perform-specific data structure.
 * @return B_OK on success, or an error code from the dispatched method.
 */
status_t
BButton::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BButton::MinSize();
			return B_OK;

		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BButton::MaxSize();
			return B_OK;

		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BButton::PreferredSize();
			return B_OK;

		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BButton::LayoutAlignment();
			return B_OK;

		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BButton::HasHeightForWidth();
			return B_OK;

		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BButton::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}

		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BButton::SetLayout(data->layout);
			return B_OK;
		}

		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BButton::LayoutInvalidated(data->descendants);
			return B_OK;
		}

		case PERFORM_CODE_DO_LAYOUT:
		{
			BButton::DoLayout();
			return B_OK;
		}

		case PERFORM_CODE_SET_ICON:
		{
			perform_data_set_icon* data = (perform_data_set_icon*)_data;
			return BButton::SetIcon(data->icon, data->flags);
		}
	}

	return BControl::Perform(code, _data);
}


/**
 * @brief Returns the minimum layout size of the button.
 *
 * Composes the explicit minimum size (if any) with the computed preferred
 * size so the layout system never makes the button smaller than it needs.
 *
 * @return The minimum BSize for layout purposes.
 */
BSize
BButton::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		_ValidatePreferredSize());
}


/**
 * @brief Returns the maximum layout size of the button.
 *
 * Composes the explicit maximum size (if any) with the computed preferred
 * size. By default the button can be made as large as desired.
 *
 * @return The maximum BSize for layout purposes.
 */
BSize
BButton::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		_ValidatePreferredSize());
}


/**
 * @brief Returns the preferred layout size of the button.
 *
 * Composes the explicit preferred size (if any) with the internally
 * computed preferred size based on label, icon, and font metrics.
 *
 * @return The preferred BSize for layout purposes.
 */
BSize
BButton::PreferredSize()
{
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		_ValidatePreferredSize());
}


/**
 * @brief Sets the button's icon bitmap.
 *
 * Extends BControl::SetIcon() by always requesting active and disabled icon
 * variants so the button can render them in all states.
 *
 * @param icon  The source bitmap to use as the icon.
 * @param flags Icon creation flags (B_CREATE_ACTIVE_ICON_BITMAP and
 *              B_CREATE_DISABLED_ICON_BITMAPS are always added).
 * @return B_OK on success, or an error code.
 */
status_t
BButton::SetIcon(const BBitmap* icon, uint32 flags)
{
	return BControl::SetIcon(icon,
		flags | B_CREATE_ACTIVE_ICON_BITMAP | B_CREATE_DISABLED_ICON_BITMAPS);
}


/**
 * @brief Clears the cached preferred size when the layout is invalidated.
 *
 * @param descendants true if descendant layouts were also invalidated.
 */
void
BButton::LayoutInvalidated(bool descendants)
{
	// invalidate cached preferred size
	fPreferredSize.Set(-1, -1);
}


void BButton::_ReservedButton1() {}
void BButton::_ReservedButton2() {}
void BButton::_ReservedButton3() {}


BButton &
BButton::operator=(const BButton &)
{
	return *this;
}


/**
 * @brief Computes and caches the button's preferred size if not already valid.
 *
 * Calculates the preferred width from label string width, icon width, and
 * BControlLook insets. Calculates the preferred height from font metrics and
 * icon height. Enforces minimum dimensions. The result is stored in
 * fPreferredSize and returned.
 *
 * @return The validated preferred BSize.
 */
BSize
BButton::_ValidatePreferredSize()
{
	if (fPreferredSize.width < 0) {
		BControlLook::background_type backgroundType
			= fBehavior == B_POP_UP_BEHAVIOR
				? BControlLook::B_BUTTON_WITH_POP_UP_BACKGROUND
				: BControlLook::B_BUTTON_BACKGROUND;
		float left, top, right, bottom;
		be_control_look->GetInsets(BControlLook::B_BUTTON_FRAME, backgroundType,
			IsDefault() ? BControlLook::B_DEFAULT_BUTTON : 0,
			left, top, right, bottom);

		// width
		const float labelSpacing = be_control_look->DefaultLabelSpacing();
		float width = left + right + labelSpacing - 1;

		const char* label = Label();
		if (label != NULL) {
			width = std::max(width, ceilf(labelSpacing * 3.3f));
			width += ceilf(StringWidth(label));
		}

		const BBitmap* icon = IconBitmap(B_INACTIVE_ICON_BITMAP);
		if (icon != NULL)
			width += icon->Bounds().Width() + 1;

		if (label != NULL && icon != NULL)
			width += labelSpacing;

		// height
		float minHorizontalMargins = top + bottom + labelSpacing;
		float height = -1;

		if (label != NULL) {
			font_height fontHeight;
			GetFontHeight(&fontHeight);
			float textHeight = fontHeight.ascent + fontHeight.descent;
			height = ceilf(textHeight * 1.8);
			float margins = height - ceilf(textHeight);
			if (margins < minHorizontalMargins)
				height += minHorizontalMargins - margins;
		}

		if (icon != NULL) {
			height = std::max(height,
				icon->Bounds().Height() + minHorizontalMargins);
		}

		// force some minimum width/height values
		width = std::max(width, label != NULL ? (labelSpacing * 12.5f) : labelSpacing);
		height = std::max(height, labelSpacing);

		fPreferredSize.Set(width, height);

		ResetLayoutInvalidation();
	}

	return fPreferredSize;
}


/**
 * @brief Returns the bounding rectangle of the pop-up affordance area.
 *
 * Only meaningful when fBehavior is B_POP_UP_BEHAVIOR. Returns an empty
 * BRect for all other behavior modes.
 *
 * @return The pop-up area within the button's bounds, or an empty BRect.
 */
BRect
BButton::_PopUpRect() const
{
	if (fBehavior != B_POP_UP_BEHAVIOR)
		return BRect();

	float left, top, right, bottom;
	be_control_look->GetInsets(BControlLook::B_BUTTON_FRAME,
		BControlLook::B_BUTTON_WITH_POP_UP_BACKGROUND,
		IsDefault() ? BControlLook::B_DEFAULT_BUTTON : 0,
		left, top, right, bottom);

	BRect rect(Bounds());
	rect.left = rect.right - right + 1;
	return rect;
}


/**
 * @brief Returns whether the given internal flag bit is set.
 * @param flag One of the FLAG_* constants.
 * @return true if the flag is set, false otherwise.
 */
inline bool
BButton::_Flag(uint32 flag) const
{
	return (fFlags & flag) != 0;
}


/**
 * @brief Sets or clears an internal flag bit, reporting whether the value changed.
 *
 * @param flag One of the FLAG_* constants.
 * @param set  true to set the flag, false to clear it.
 * @return true if the flag state actually changed, false if it was already
 *         in the requested state.
 */
inline bool
BButton::_SetFlag(uint32 flag, bool set)
{
	if (((fFlags & flag) != 0) == set)
		return false;

	if (set)
		fFlags |= flag;
	else
		fFlags &= ~flag;

	return true;
}


/**
 * @brief Binary-compatibility thunk for BButton::InvalidateLayout().
 *
 * Provided for GCC 2 ABI compatibility. Calls Perform() with
 * PERFORM_CODE_LAYOUT_INVALIDATED so the virtual dispatch reaches the
 * correct BButton override even when called through an old vtable.
 *
 * @param view        The BView (BButton) whose layout should be invalidated.
 * @param descendants If true, descendant layouts are also invalidated.
 */
extern "C" void
B_IF_GCC_2(InvalidateLayout__7BButtonb, _ZN7BButton16InvalidateLayoutEb)(
	BView* view, bool descendants)
{
	perform_data_layout_invalidated data;
	data.descendants = descendants;

	view->Perform(PERFORM_CODE_LAYOUT_INVALIDATED, &data);
}
