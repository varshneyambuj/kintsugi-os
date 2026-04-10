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
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file CheckBox.cpp
 * @brief Implementation of BCheckBox, a two-state toggle control
 *
 * BCheckBox renders a labeled checkbox that toggles between checked
 * (B_CONTROL_ON) and unchecked (B_CONTROL_OFF) states when clicked,
 * invoking its associated BMessage.
 *
 * @see BControl, BControlLook
 */


#include <CheckBox.h>

#include <algorithm>
#include <new>

#include <Bitmap.h>
#include <ControlLook.h>
#include <LayoutUtils.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>


/**
 * @brief Constructs a BCheckBox with an explicit frame rectangle (legacy layout).
 *
 * Ensures the checkbox meets a minimum height derived from the current font
 * metrics, resizing vertically if the supplied frame is too short.
 *
 * @param frame        The position and size of the checkbox in the parent's
 *                     coordinate system.
 * @param name         The internal view name used for look-up.
 * @param label        The visible text drawn beside the checkbox.
 * @param message      The message sent when the checkbox is toggled. Ownership
 *                     is transferred to BControl.
 * @param resizingMode How the checkbox resizes with its parent (B_FOLLOW_* flags).
 * @param flags        View flags passed to BControl.
 */
BCheckBox::BCheckBox(BRect frame, const char* name, const char* label,
	BMessage* message, uint32 resizingMode, uint32 flags)
	:
	BControl(frame, name, label, message, resizingMode, flags),
	fPreferredSize(),
	fOutlined(false),
	fPartialToOff(false)
{
	// Resize to minimum height if needed
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	float minHeight = (float)ceil(6.0f + fontHeight.ascent
		+ fontHeight.descent);
	if (Bounds().Height() < minHeight)
		ResizeTo(Bounds().Width(), minHeight);
}


/**
 * @brief Constructs a BCheckBox without an explicit frame (layout-managed).
 *
 * @param name    The internal view name.
 * @param label   The visible text drawn beside the checkbox.
 * @param message The message sent when the checkbox is toggled.
 * @param flags   View flags (B_WILL_DRAW and B_NAVIGABLE are always added).
 */
BCheckBox::BCheckBox(const char* name, const char* label, BMessage* message,
	uint32 flags)
	:
	BControl(name, label, message, flags | B_WILL_DRAW | B_NAVIGABLE),
	fPreferredSize(),
	fOutlined(false),
	fPartialToOff(false)
{
}


/**
 * @brief Constructs a BCheckBox with only a label and message (simplest form).
 *
 * This is the recommended constructor when using the layout system.
 *
 * @param label   The visible text drawn beside the checkbox.
 * @param message The message sent when the checkbox is toggled.
 */
BCheckBox::BCheckBox(const char* label, BMessage* message)
	:
	BControl(NULL, label, message, B_WILL_DRAW | B_NAVIGABLE),
	fPreferredSize(),
	fOutlined(false),
	fPartialToOff(false)
{
}


/**
 * @brief Constructs a BCheckBox from an archived BMessage.
 *
 * @param data The archive message produced by Archive().
 * @see Instantiate()
 * @see Archive()
 */
BCheckBox::BCheckBox(BMessage* data)
	:
	BControl(data),
	fOutlined(false),
	fPartialToOff(false)
{
}


/**
 * @brief Destroys the BCheckBox.
 */
BCheckBox::~BCheckBox()
{
}


// #pragma mark - Archiving methods


/**
 * @brief Creates a new BCheckBox from an archived BMessage.
 *
 * @param data The archive message to instantiate from.
 * @return A newly allocated BCheckBox on success, or NULL if validation fails.
 * @see Archive()
 */
BArchivable*
BCheckBox::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BCheckBox"))
		return new(std::nothrow) BCheckBox(data);

	return NULL;
}


/**
 * @brief Archives the BCheckBox into a BMessage.
 *
 * Delegates entirely to BControl::Archive(); BCheckBox adds no extra fields.
 *
 * @param data The message to archive into.
 * @param deep If true, child objects are archived recursively.
 * @return B_OK on success, or an error code from BControl::Archive().
 */
status_t
BCheckBox::Archive(BMessage* data, bool deep) const
{
	return BControl::Archive(data, deep);
}


// #pragma mark - Hook methods


/**
 * @brief Draws the checkbox and its label into the current view.
 *
 * Renders the checkbox glyph (with an optional outlined/pressed state) and
 * then the label and icon to the right of the glyph, using BControlLook.
 *
 * @param updateRect The invalid rectangle that needs to be redrawn.
 */
void
BCheckBox::Draw(BRect updateRect)
{
	rgb_color base = ViewColor();

	uint32 flags = be_control_look->Flags(this);
	if (fOutlined)
		flags |= BControlLook::B_CLICKED;

	BRect checkBoxRect(_CheckBoxFrame());
	BRect rect(checkBoxRect);
	be_control_look->DrawCheckBox(this, rect, updateRect, base, flags);

	BRect labelRect(Bounds());
	labelRect.left = checkBoxRect.right + 1
		+ be_control_look->DefaultLabelSpacing();

	const BBitmap* icon = IconBitmap(
		B_INACTIVE_ICON_BITMAP | (IsEnabled() ? 0 : B_DISABLED_ICON_BITMAP));
	const BAlignment alignment = BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER);

	be_control_look->DrawLabel(this, Label(), icon, labelRect, updateRect, base, flags, alignment);
}


/**
 * @brief Called when the checkbox is attached to a window.
 *
 * Delegates to BControl::AttachedToWindow() for standard view setup.
 */
void
BCheckBox::AttachedToWindow()
{
	BControl::AttachedToWindow();
}


/**
 * @brief Called when the checkbox is detached from its window.
 *
 * Delegates to BControl::DetachedFromWindow() for cleanup.
 */
void
BCheckBox::DetachedFromWindow()
{
	BControl::DetachedFromWindow();
}


/**
 * @brief Called after all sibling views have been attached to the window.
 */
void
BCheckBox::AllAttached()
{
	BControl::AllAttached();
}


/**
 * @brief Called after all sibling views have been detached from the window.
 */
void
BCheckBox::AllDetached()
{
	BControl::AllDetached();
}


/**
 * @brief Called when the checkbox's frame position changes.
 * @param newPosition The new top-left corner position in the parent's coordinates.
 */
void
BCheckBox::FrameMoved(BPoint newPosition)
{
	BControl::FrameMoved(newPosition);
}


/**
 * @brief Called when the checkbox's frame size changes.
 * @param newWidth  The new width in pixels.
 * @param newHeight The new height in pixels.
 */
void
BCheckBox::FrameResized(float newWidth, float newHeight)
{
	BControl::FrameResized(newWidth, newHeight);
}


/**
 * @brief Called when the checkbox's window gains or loses focus.
 * @param active true if the window just became active, false if deactivated.
 */
void
BCheckBox::WindowActivated(bool active)
{
	BControl::WindowActivated(active);
}


/**
 * @brief Handles an incoming BMessage not dispatched by the default mechanism.
 * @param message The message to process.
 */
void
BCheckBox::MessageReceived(BMessage* message)
{
	BControl::MessageReceived(message);
}


/**
 * @brief Handles a key-press event.
 *
 * B_ENTER and B_SPACE advance the checkbox to the next logical state and
 * invoke the action. All other keys bypass BControl and go directly to
 * BView to avoid unintended value toggling.
 *
 * @param bytes    Pointer to the UTF-8 key data.
 * @param numBytes Number of bytes in \a bytes.
 */
void
BCheckBox::KeyDown(const char* bytes, int32 numBytes)
{
	if (*bytes == B_ENTER || *bytes == B_SPACE) {
		if (!IsEnabled())
			return;

		SetValue(_NextState());
		Invoke();
	} else {
		// skip the BControl implementation
		BView::KeyDown(bytes, numBytes);
	}
}


/**
 * @brief Handles a mouse-button-press event.
 *
 * Sets the outlined (pressed) visual state and begins tracking. In
 * synchronous mode, polls the mouse until the button is released; if the
 * pointer is still inside the checkbox at release, the state is advanced
 * and the action is invoked.
 *
 * @param where The location of the click in the view's coordinate system.
 */
void
BCheckBox::MouseDown(BPoint where)
{
	if (!IsEnabled())
		return;

	fOutlined = true;

	if (Window()->Flags() & B_ASYNCHRONOUS_CONTROLS) {
		Invalidate();
		SetTracking(true);
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	} else {
		BRect bounds = Bounds();
		uint32 buttons;

		Invalidate();
		Window()->UpdateIfNeeded();

		do {
			snooze(40000);

			GetMouse(&where, &buttons, true);

			bool inside = bounds.Contains(where);
			if (fOutlined != inside) {
				fOutlined = inside;
				Invalidate();
				Window()->UpdateIfNeeded();
			}
		} while (buttons != 0);

		if (fOutlined) {
			fOutlined = false;
			SetValue(_NextState());
			Invoke();
		} else {
			Invalidate();
			Window()->UpdateIfNeeded();
		}
	}
}


/**
 * @brief Called when the mouse button is released over this checkbox.
 *
 * Completes asynchronous tracking: advances the state and invokes the action
 * if the release point is inside the checkbox bounds, otherwise just
 * redraws. Stops tracking regardless.
 *
 * @param where The release position in view coordinates.
 */
void
BCheckBox::MouseUp(BPoint where)
{
	if (!IsTracking())
		return;

	bool inside = Bounds().Contains(where);

	if (fOutlined != inside) {
		fOutlined = inside;
		Invalidate();
	}

	if (fOutlined) {
		fOutlined = false;
		SetValue(_NextState());
		Invoke();
	} else {
		Invalidate();
	}

	SetTracking(false);
}


/**
 * @brief Called when the pointer moves while the checkbox is being tracked.
 *
 * Updates the outlined (hover-press) state and invalidates the view if
 * the pointer has entered or exited the checkbox bounds.
 *
 * @param where       Current pointer position in view coordinates.
 * @param code        B_ENTERED_VIEW, B_INSIDE_VIEW, or B_EXITED_VIEW.
 * @param dragMessage Non-NULL if a drag-and-drop operation is in progress.
 */
void
BCheckBox::MouseMoved(BPoint where, uint32 code,
	const BMessage* dragMessage)
{
	if (!IsTracking())
		return;

	bool inside = Bounds().Contains(where);

	if (fOutlined != inside) {
		fOutlined = inside;
		Invalidate();
	}
}


// #pragma mark -


/**
 * @brief Returns the checkbox's preferred width and height.
 *
 * Results are cached after the first computation; the cache is invalidated
 * via LayoutInvalidated(). Pass NULL for dimensions you do not need.
 *
 * @param _width  Receives the preferred width, or ignored if NULL.
 * @param _height Receives the preferred height, or ignored if NULL.
 */
void
BCheckBox::GetPreferredSize(float* _width, float* _height)
{
	_ValidatePreferredSize();

	if (_width)
		*_width = fPreferredSize.width;

	if (_height)
		*_height = fPreferredSize.height;
}


/**
 * @brief Resizes the checkbox to its preferred size.
 *
 * Delegates to BControl::ResizeToPreferred().
 */
void
BCheckBox::ResizeToPreferred()
{
	BControl::ResizeToPreferred();
}


/**
 * @brief Returns the minimum layout size of the checkbox.
 *
 * Composes the explicit minimum size (if any) with the computed preferred
 * size so the layout system never makes the checkbox smaller than needed.
 *
 * @return The minimum BSize for layout purposes.
 */
BSize
BCheckBox::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		_ValidatePreferredSize());
}


/**
 * @brief Returns the maximum layout size of the checkbox.
 *
 * Composes the explicit maximum size (if any) with the computed preferred
 * size.
 *
 * @return The maximum BSize for layout purposes.
 */
BSize
BCheckBox::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		_ValidatePreferredSize());
}


/**
 * @brief Returns the preferred layout size of the checkbox.
 *
 * Composes the explicit preferred size (if any) with the internally
 * computed preferred size based on the glyph, icon, label, and font metrics.
 *
 * @return The preferred BSize for layout purposes.
 */
BSize
BCheckBox::PreferredSize()
{
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		_ValidatePreferredSize());
}


/**
 * @brief Returns the preferred layout alignment of the checkbox.
 *
 * Composes the explicit alignment (if any) with left-horizontal /
 * vertical-center defaults suited to inline label controls.
 *
 * @return The BAlignment for layout purposes.
 */
BAlignment
BCheckBox::LayoutAlignment()
{
	return BLayoutUtils::ComposeAlignment(ExplicitAlignment(),
		BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER));
}


// #pragma mark -


/**
 * @brief Gives or removes keyboard focus from the checkbox.
 * @param focused true to grant focus, false to remove it.
 */
void
BCheckBox::MakeFocus(bool focused)
{
	BControl::MakeFocus(focused);
}


/**
 * @brief Sets the integer value of the checkbox control.
 *
 * Accepts only B_CONTROL_OFF, B_CONTROL_ON, and B_CONTROL_PARTIALLY_ON;
 * any other value is treated as B_CONTROL_ON. Only the checkbox glyph area
 * is invalidated when the value changes.
 *
 * @param value The new value to set.
 */
void
BCheckBox::SetValue(int32 value)
{
	// We only accept three possible values.
	switch (value) {
		case B_CONTROL_OFF:
		case B_CONTROL_ON:
		case B_CONTROL_PARTIALLY_ON:
			break;
		default:
			value = B_CONTROL_ON;
			break;
	}

	if (value != Value()) {
		BControl::SetValueNoUpdate(value);
		Invalidate(_CheckBoxFrame());
	}
}


/**
 * @brief Invokes the checkbox's action message.
 *
 * Delegates to BControl::Invoke().
 *
 * @param message The message to send, or NULL to use the checkbox's own message.
 * @return B_OK on success, or an error code from BControl::Invoke().
 */
status_t
BCheckBox::Invoke(BMessage* message)
{
	return BControl::Invoke(message);
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
BCheckBox::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	return BControl::ResolveSpecifier(message, index, specifier, what,
		property);
}


/**
 * @brief Fills a BMessage with the scripting suites supported by BCheckBox.
 *
 * @param message The message to populate with suite information.
 * @return B_OK on success, or an error code.
 */
status_t
BCheckBox::GetSupportedSuites(BMessage* message)
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
BCheckBox::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BCheckBox::MinSize();
			return B_OK;
		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BCheckBox::MaxSize();
			return B_OK;
		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BCheckBox::PreferredSize();
			return B_OK;
		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BCheckBox::LayoutAlignment();
			return B_OK;
		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BCheckBox::HasHeightForWidth();
			return B_OK;
		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BCheckBox::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}
		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BCheckBox::SetLayout(data->layout);
			return B_OK;
		}
		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BCheckBox::LayoutInvalidated(data->descendants);
			return B_OK;
		}
		case PERFORM_CODE_DO_LAYOUT:
		{
			BCheckBox::DoLayout();
			return B_OK;
		}
		case PERFORM_CODE_SET_ICON:
		{
			perform_data_set_icon* data = (perform_data_set_icon*)_data;
			return BCheckBox::SetIcon(data->icon, data->flags);
		}
	}

	return BControl::Perform(code, _data);
}


/**
 * @brief Sets the checkbox's icon bitmap.
 *
 * Extends BControl::SetIcon() by always requesting disabled icon variants so
 * the checkbox can render them when it is disabled.
 *
 * @param icon  The source bitmap to use as the icon.
 * @param flags Icon creation flags (B_CREATE_DISABLED_ICON_BITMAPS is always
 *              added).
 * @return B_OK on success, or an error code.
 */
status_t
BCheckBox::SetIcon(const BBitmap* icon, uint32 flags)
{
	return BControl::SetIcon(icon, flags | B_CREATE_DISABLED_ICON_BITMAPS);
}


/**
 * @brief Clears the cached preferred size when the layout is invalidated.
 *
 * @param descendants true if descendant layouts were also invalidated.
 */
void
BCheckBox::LayoutInvalidated(bool descendants)
{
	// invalidate cached preferred size
	fPreferredSize.Set(B_SIZE_UNSET, B_SIZE_UNSET);
}


/**
 * @brief Returns whether the partial state transitions to off instead of on.
 * @return true if B_CONTROL_PARTIALLY_ON cycles to B_CONTROL_OFF, false if
 *         it cycles to B_CONTROL_ON.
 */
bool
BCheckBox::IsPartialStateToOff() const
{
	return fPartialToOff;
}


/**
 * @brief Controls whether the partial state cycles to off or on.
 *
 * When true, a checkbox in B_CONTROL_PARTIALLY_ON will transition to
 * B_CONTROL_OFF on the next toggle. When false (the default), it
 * transitions to B_CONTROL_ON.
 *
 * @param partialToOff true to transition partial to off, false to transition
 *                     partial to on.
 */
void
BCheckBox::SetPartialStateToOff(bool partialToOff)
{
	fPartialToOff = partialToOff;
}


// #pragma mark - FBC padding


void BCheckBox::_ReservedCheckBox1() {}
void BCheckBox::_ReservedCheckBox2() {}
void BCheckBox::_ReservedCheckBox3() {}


/**
 * @brief Returns the bounding rectangle of the checkbox glyph for a given font.
 *
 * The rectangle is sized to match the font's ascent so the glyph aligns with
 * the text baseline.
 *
 * @param fontHeight The font metrics used to compute the glyph size.
 * @return The BRect of the checkbox glyph in view coordinates.
 */
BRect
BCheckBox::_CheckBoxFrame(const font_height& fontHeight) const
{
	return BRect(0.0f, 2.0f, ceilf(3.0f + fontHeight.ascent),
		ceilf(5.0f + fontHeight.ascent));
}


/**
 * @brief Returns the bounding rectangle of the checkbox glyph using current font.
 *
 * Convenience overload that fetches the current font height automatically.
 *
 * @return The BRect of the checkbox glyph in view coordinates.
 */
BRect
BCheckBox::_CheckBoxFrame() const
{
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	return _CheckBoxFrame(fontHeight);
}


/**
 * @brief Computes and caches the checkbox's preferred size if not already valid.
 *
 * Calculates the preferred width from the glyph size, optional icon, and
 * label string width. Calculates the preferred height from the glyph and
 * font metrics. The result is stored in fPreferredSize and returned.
 *
 * @return The validated preferred BSize.
 */
BSize
BCheckBox::_ValidatePreferredSize()
{
	if (!fPreferredSize.IsWidthSet()) {
		font_height fontHeight;
		GetFontHeight(&fontHeight);

		BRect rect(_CheckBoxFrame(fontHeight));
		float width = rect.right + rect.left;
		float height = rect.bottom + rect.top;

		const BBitmap* icon = IconBitmap(B_INACTIVE_ICON_BITMAP);
		if (icon != NULL) {
			width += be_control_look->DefaultLabelSpacing()
				+ icon->Bounds().Width() + 1;
			height = std::max(height, icon->Bounds().Height());
		}

		if (const char* label = Label()) {
			width += be_control_look->DefaultLabelSpacing()
				+ ceilf(StringWidth(label));
			height = std::max(height,
				ceilf(6.0f + fontHeight.ascent + fontHeight.descent));
		}

		fPreferredSize.Set(width, height);

		ResetLayoutInvalidation();
	}

	return fPreferredSize;
}


/**
 * @brief Returns the next logical state following the current value.
 *
 * Implements the three-state cycle: OFF -> ON, ON -> OFF, and
 * PARTIALLY_ON -> OFF (when fPartialToOff is true) or ON (otherwise).
 *
 * @return The next B_CONTROL_* state value.
 */
int32
BCheckBox::_NextState() const
{
	switch (Value()) {
		case B_CONTROL_OFF:
			return B_CONTROL_ON;
		case B_CONTROL_PARTIALLY_ON:
			return fPartialToOff ? B_CONTROL_OFF : B_CONTROL_ON;
		case B_CONTROL_ON:
		default:
			return B_CONTROL_OFF;
	}
}


BCheckBox &
BCheckBox::operator=(const BCheckBox &)
{
	return *this;
}


/**
 * @brief Binary-compatibility thunk for BCheckBox::InvalidateLayout().
 *
 * Provided for GCC 2 ABI compatibility. Calls Perform() with
 * PERFORM_CODE_LAYOUT_INVALIDATED so the virtual dispatch reaches the
 * correct BCheckBox override even when called through an old vtable.
 *
 * @param box         The BCheckBox whose layout should be invalidated.
 * @param descendants If true, descendant layouts are also invalidated.
 */
extern "C" void
B_IF_GCC_2(InvalidateLayout__9BCheckBoxb, _ZN9BCheckBox16InvalidateLayoutEb)(
	BCheckBox* box, bool descendants)
{
	perform_data_layout_invalidated data;
	data.descendants = descendants;

	box->Perform(PERFORM_CODE_LAYOUT_INVALIDATED, &data);
}
