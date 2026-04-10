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
 *   Copyright 2001-2008 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file RadioButton.cpp
 * @brief Implementation of BRadioButton, a mutually exclusive selection control
 *
 * BRadioButton is a toggle control that, when turned on, automatically turns off all
 * other BRadioButton siblings within the same parent view. It implements the
 * radio-button group behavior expected for single-choice option sets.
 *
 * @see BControl, BCheckBox, BControlLook
 */


#include <RadioButton.h>

#include <algorithm>

#include <Box.h>
#include <ControlLook.h>
#include <Debug.h>
#include <LayoutUtils.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>


/**
 * @brief Constructs a BRadioButton with an explicit frame rectangle (legacy layout).
 *
 * Ensures the radio button meets a minimum height derived from its preferred
 * size, resizing vertically if the supplied frame is too short.
 *
 * @param frame        The position and size of the radio button in the parent's
 *                     coordinate system.
 * @param name         The internal view name used for look-up.
 * @param label        The visible text drawn beside the radio button knob.
 * @param message      The message sent when the button is selected. Ownership
 *                     is transferred to BControl.
 * @param resizingMode How the view resizes with its parent (B_FOLLOW_* flags).
 * @param flags        View flags (B_FRAME_EVENTS is always added).
 */
BRadioButton::BRadioButton(BRect frame, const char* name, const char* label,
	BMessage* message, uint32 resizingMode, uint32 flags)
	:
	BControl(frame, name, label, message, resizingMode, flags | B_FRAME_EVENTS),
	fOutlined(false)
{
	// Resize to minimum height if needed for BeOS compatibility
	float minHeight;
	GetPreferredSize(NULL, &minHeight);
	if (Bounds().Height() < minHeight)
		ResizeTo(Bounds().Width(), minHeight);
}


/**
 * @brief Constructs a BRadioButton without an explicit frame (layout-managed).
 *
 * @param name    The internal view name.
 * @param label   The visible text drawn beside the radio button knob.
 * @param message The message sent when the button is selected.
 * @param flags   View flags (B_FRAME_EVENTS is always added).
 */
BRadioButton::BRadioButton(const char* name, const char* label,
	BMessage* message, uint32 flags)
	:
	BControl(name, label, message, flags | B_FRAME_EVENTS),
	fOutlined(false)
{
}


/**
 * @brief Constructs a BRadioButton with only a label and message (simplest form).
 *
 * This is the recommended constructor when using the layout system.
 *
 * @param label   The visible text drawn beside the radio button knob.
 * @param message The message sent when the button is selected.
 */
BRadioButton::BRadioButton(const char* label, BMessage* message)
	:
	BControl(NULL, label, message, B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS),
	fOutlined(false)
{
}


/**
 * @brief Constructs a BRadioButton from an archived BMessage.
 *
 * @param data The archive message produced by Archive().
 * @see Instantiate()
 * @see Archive()
 */
BRadioButton::BRadioButton(BMessage* data)
	:
	BControl(data),
	fOutlined(false)
{
}


/**
 * @brief Destroys the BRadioButton.
 */
BRadioButton::~BRadioButton()
{
}


/**
 * @brief Creates a new BRadioButton from an archived BMessage.
 *
 * @param data The archive message to instantiate from.
 * @return A newly allocated BRadioButton on success, or NULL if validation
 *         fails.
 * @see Archive()
 */
BArchivable*
BRadioButton::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BRadioButton"))
		return new BRadioButton(data);

	return NULL;
}


/**
 * @brief Archives the BRadioButton into a BMessage.
 *
 * Delegates entirely to BControl::Archive(); BRadioButton adds no extra fields.
 *
 * @param data The message to archive into.
 * @param deep If true, child objects are archived recursively.
 * @return B_OK on success, or an error code from BControl::Archive().
 */
status_t
BRadioButton::Archive(BMessage* data, bool deep) const
{
	return BControl::Archive(data, deep);
}


/**
 * @brief Draws the radio button knob and its label into the current view.
 *
 * Renders the circular knob (with an optional outlined/pressed state) and
 * then the label and icon to the right of the knob, using BControlLook.
 * The knob size is derived from the current font's ascent.
 *
 * @param updateRect The invalid rectangle that needs to be redrawn.
 */
void
BRadioButton::Draw(BRect updateRect)
{
	rgb_color base = ViewColor();

	// its size depends on the text height
	font_height fontHeight;
	GetFontHeight(&fontHeight);

	uint32 flags = be_control_look->Flags(this);
	if (fOutlined)
		flags |= BControlLook::B_CLICKED;

	BRect knobRect(_KnobFrame(fontHeight));
	BRect rect(knobRect);
	be_control_look->DrawRadioButton(this, rect, updateRect, base, flags);

	BRect labelRect(Bounds());
	labelRect.left = knobRect.right + 1 + be_control_look->DefaultLabelSpacing();

	const BBitmap* icon = IconBitmap(
		B_INACTIVE_ICON_BITMAP | (IsEnabled() ? 0 : B_DISABLED_ICON_BITMAP));
	const BAlignment alignment = BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER);

	be_control_look->DrawLabel(this, Label(), icon, labelRect, updateRect, base, flags, alignment);
}


/**
 * @brief Handles a mouse-button-press event.
 *
 * Sets the outlined (pressed) visual state and begins tracking. In
 * synchronous mode, polls the mouse until the button is released; if the
 * pointer is still inside the radio button at release, the value is set
 * to B_CONTROL_ON and the action is invoked.
 *
 * @param where The location of the click in the view's coordinate system.
 */
void
BRadioButton::MouseDown(BPoint where)
{
	if (!IsEnabled())
		return;

	fOutlined = true;

	if ((Window()->Flags() & B_ASYNCHRONOUS_CONTROLS) != 0) {
		Invalidate();
		SetTracking(true);
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	} else {
		_Redraw();

		BRect bounds = Bounds();
		uint32 buttons;

		do {
			snooze(40000);
			GetMouse(&where, &buttons, true);
			bool inside = bounds.Contains(where);

			if (fOutlined != inside) {
				fOutlined = inside;
				_Redraw();
			}
		} while (buttons != 0);

		if (fOutlined) {
			fOutlined = false;
			_Redraw();
			SetValue(B_CONTROL_ON);
			Invoke();
		} else
			_Redraw();
	}
}


/**
 * @brief Called when the radio button is attached to a window.
 *
 * Delegates to BControl::AttachedToWindow() for standard view setup.
 */
void
BRadioButton::AttachedToWindow()
{
	BControl::AttachedToWindow();
}


/**
 * @brief Handles a key-press event.
 *
 * B_SPACE (and B_RETURN, which is overridden to prevent toggling off) set
 * the value to B_CONTROL_ON and invoke if the button is not already on.
 * All other keys are forwarded to BControl.
 *
 * @param bytes    Pointer to the UTF-8 key data.
 * @param numBytes Number of bytes in \a bytes.
 * @note Cursor-key navigation between radio buttons in a group is not yet
 *       implemented.
 */
void
BRadioButton::KeyDown(const char* bytes, int32 numBytes)
{
	// TODO: Add selecting the next button functionality (navigating radio
	// buttons with the cursor keys)!

	switch (bytes[0]) {
		case B_RETURN:
			// override B_RETURN, which BControl would use to toggle the value
			// but we don't allow setting the control to "off", only "on"
		case B_SPACE: {
			if (IsEnabled() && !Value()) {
				SetValue(B_CONTROL_ON);
				Invoke();
			}
			break;
		}

		default:
			BControl::KeyDown(bytes, numBytes);
	}
}


/**
 * @brief Sets the value of the radio button and deselects all siblings.
 *
 * When set to B_CONTROL_ON, iterates all sibling views (including BBox label
 * views) and calls SetValue(B_CONTROL_OFF) on any other BRadioButton found,
 * implementing the mutually exclusive group behavior. When set to
 * B_CONTROL_OFF no sibling deselection occurs.
 *
 * @param value B_CONTROL_ON to select this button, B_CONTROL_OFF to deselect.
 * @see BControl::SetValueNoUpdate()
 */
void
BRadioButton::SetValue(int32 value)
{
	if (value != Value()) {
		BControl::SetValueNoUpdate(value);
		Invalidate(_KnobFrame());
	}

	if (value == 0)
		return;

	BView* parent = Parent();
	BView* child = NULL;

	if (parent != NULL) {
		// If the parent is a BBox, the group parent is the parent of the BBox
		BBox* box = dynamic_cast<BBox*>(parent);

		if (box != NULL && box->LabelView() == this)
			parent = box->Parent();

		if (parent != NULL) {
			BBox* box = dynamic_cast<BBox*>(parent);

			// If the parent is a BBox, skip the label if there is one
			if (box != NULL && box->LabelView())
				child = parent->ChildAt(1);
			else
				child = parent->ChildAt(0);
		} else
			child = Window()->ChildAt(0);
	} else if (Window() != NULL)
		child = Window()->ChildAt(0);

	while (child != NULL) {
		BRadioButton* radio = dynamic_cast<BRadioButton*>(child);

		if (radio != NULL && (radio != this))
			radio->SetValue(B_CONTROL_OFF);
		else {
			// If the child is a BBox, check if the label is a radiobutton
			BBox* box = dynamic_cast<BBox*>(child);

			if (box != NULL && box->LabelView()) {
				radio = dynamic_cast<BRadioButton*>(box->LabelView());

				if (radio != NULL && (radio != this))
					radio->SetValue(B_CONTROL_OFF);
			}
		}

		child = child->NextSibling();
	}

	ASSERT(Value() == B_CONTROL_ON);
}


/**
 * @brief Returns the radio button's preferred width and height.
 *
 * Computes the size from the knob frame (font-metric-dependent), optional
 * icon dimensions, and label string width. Pass NULL for dimensions you do
 * not need.
 *
 * @param _width  Receives the preferred width, or ignored if NULL.
 * @param _height Receives the preferred height, or ignored if NULL.
 */
void
BRadioButton::GetPreferredSize(float* _width, float* _height)
{
	font_height fontHeight;
	GetFontHeight(&fontHeight);

	BRect rect(_KnobFrame(fontHeight));
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

	if (_width != NULL)
		*_width = width;

	if (_height != NULL)
		*_height = height;
}


/**
 * @brief Resizes the radio button to its preferred size.
 *
 * Delegates to BControl::ResizeToPreferred().
 */
void
BRadioButton::ResizeToPreferred()
{
	BControl::ResizeToPreferred();
}


/**
 * @brief Invokes the radio button's action message.
 *
 * Delegates to BControl::Invoke().
 *
 * @param message The message to send, or NULL to use the button's own message.
 * @return B_OK on success, or an error code from BControl::Invoke().
 */
status_t
BRadioButton::Invoke(BMessage* message)
{
	return BControl::Invoke(message);
}


/**
 * @brief Handles an incoming BMessage not dispatched by the default mechanism.
 * @param message The message to process.
 */
void
BRadioButton::MessageReceived(BMessage* message)
{
	BControl::MessageReceived(message);
}


/**
 * @brief Called when the radio button's window gains or loses focus.
 * @param active true if the window just became active, false if deactivated.
 */
void
BRadioButton::WindowActivated(bool active)
{
	BControl::WindowActivated(active);
}


/**
 * @brief Called when the mouse button is released over this radio button.
 *
 * Completes asynchronous tracking: if the release point is inside the button,
 * sets the value to B_CONTROL_ON and invokes (only if the value changed).
 * Invalidates and stops tracking regardless.
 *
 * @param where The release position in view coordinates.
 */
void
BRadioButton::MouseUp(BPoint where)
{
	if (!IsTracking())
		return;

	fOutlined = Bounds().Contains(where);
	if (fOutlined) {
		fOutlined = false;
		if (Value() != B_CONTROL_ON) {
			SetValue(B_CONTROL_ON);
			Invoke();
		}
	}
	Invalidate();

	SetTracking(false);
}


/**
 * @brief Called when the pointer moves while the radio button is being tracked.
 *
 * Updates the outlined (hover-press) state and invalidates the view if the
 * pointer has entered or exited the button bounds.
 *
 * @param where       Current pointer position in view coordinates.
 * @param code        B_ENTERED_VIEW, B_INSIDE_VIEW, or B_EXITED_VIEW.
 * @param dragMessage Non-NULL if a drag-and-drop operation is in progress.
 */
void
BRadioButton::MouseMoved(BPoint where, uint32 code,
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


/**
 * @brief Called when the radio button is detached from its window.
 *
 * Delegates to BControl::DetachedFromWindow() for cleanup.
 */
void
BRadioButton::DetachedFromWindow()
{
	BControl::DetachedFromWindow();
}


/**
 * @brief Called when the radio button's frame position changes.
 * @param newPosition The new top-left corner position in the parent's coordinates.
 */
void
BRadioButton::FrameMoved(BPoint newPosition)
{
	BControl::FrameMoved(newPosition);
}


/**
 * @brief Called when the radio button's frame size changes.
 *
 * Invalidates the entire view before delegating to BControl so the knob
 * and label are fully redrawn at the new size.
 *
 * @param newWidth  The new width in pixels.
 * @param newHeight The new height in pixels.
 */
void
BRadioButton::FrameResized(float newWidth, float newHeight)
{
	Invalidate();
	BControl::FrameResized(newWidth, newHeight);
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
BRadioButton::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	return BControl::ResolveSpecifier(message, index, specifier, what,
		property);
}


/**
 * @brief Gives or removes keyboard focus from the radio button.
 * @param focus true to grant focus, false to remove it.
 */
void
BRadioButton::MakeFocus(bool focus)
{
	BControl::MakeFocus(focus);
}


/**
 * @brief Called after all sibling views have been attached to the window.
 */
void
BRadioButton::AllAttached()
{
	BControl::AllAttached();
}


/**
 * @brief Called after all sibling views have been detached from the window.
 */
void
BRadioButton::AllDetached()
{
	BControl::AllDetached();
}


/**
 * @brief Fills a BMessage with the scripting suites supported by BRadioButton.
 *
 * @param message The message to populate with suite information.
 * @return B_OK on success, or an error code.
 */
status_t
BRadioButton::GetSupportedSuites(BMessage* message)
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
BRadioButton::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BRadioButton::MinSize();
			return B_OK;

		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BRadioButton::MaxSize();
			return B_OK;

		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BRadioButton::PreferredSize();
			return B_OK;

		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BRadioButton::LayoutAlignment();
			return B_OK;

		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BRadioButton::HasHeightForWidth();
			return B_OK;

		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BRadioButton::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}

		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BRadioButton::SetLayout(data->layout);
			return B_OK;
		}

		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BRadioButton::LayoutInvalidated(data->descendants);
			return B_OK;
		}

		case PERFORM_CODE_DO_LAYOUT:
		{
			BRadioButton::DoLayout();
			return B_OK;
		}

		case PERFORM_CODE_SET_ICON:
		{
			perform_data_set_icon* data = (perform_data_set_icon*)_data;
			return BRadioButton::SetIcon(data->icon, data->flags);
		}
	}

	return BControl::Perform(code, _data);
}


/**
 * @brief Returns the maximum layout size of the radio button.
 *
 * Composes the explicit maximum size (if any) with the computed preferred
 * size so the layout system allows the button to grow as needed.
 *
 * @return The maximum BSize for layout purposes.
 */
BSize
BRadioButton::MaxSize()
{
	float width, height;
	GetPreferredSize(&width, &height);

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		BSize(width, height));
}


/**
 * @brief Returns the preferred layout alignment of the radio button.
 *
 * Composes the explicit alignment (if any) with left-horizontal /
 * vertical-unset defaults, which allow the enclosing layout to decide
 * the vertical alignment.
 *
 * @return The BAlignment for layout purposes.
 */
BAlignment
BRadioButton::LayoutAlignment()
{
	return BLayoutUtils::ComposeAlignment(ExplicitAlignment(),
		BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_UNSET));
}


/**
 * @brief Sets the radio button's icon bitmap.
 *
 * Extends BControl::SetIcon() by always requesting disabled icon variants so
 * the radio button can render them when it is disabled.
 *
 * @param icon  The source bitmap to use as the icon.
 * @param flags Icon creation flags (B_CREATE_DISABLED_ICON_BITMAPS is always
 *              added).
 * @return B_OK on success, or an error code.
 */
status_t
BRadioButton::SetIcon(const BBitmap* icon, uint32 flags)
{
	return BControl::SetIcon(icon, flags | B_CREATE_DISABLED_ICON_BITMAPS);
}


void BRadioButton::_ReservedRadioButton1() {}
void BRadioButton::_ReservedRadioButton2() {}


BRadioButton&
BRadioButton::operator=(const BRadioButton &)
{
	return *this;
}


/**
 * @brief Returns the bounding rectangle of the radio knob using current font.
 *
 * Convenience overload that fetches the current font height automatically.
 *
 * @return The BRect of the radio knob in view coordinates.
 */
BRect
BRadioButton::_KnobFrame() const
{
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	return _KnobFrame(fontHeight);
}


/**
 * @brief Returns the bounding rectangle of the radio knob for a given font.
 *
 * The rectangle is sized to match the font's ascent so the knob aligns with
 * the text baseline. Uses the same geometry as BCheckBox::_CheckBoxFrame().
 *
 * @param fontHeight The font metrics used to compute the knob size.
 * @return The BRect of the radio knob in view coordinates.
 */
BRect
BRadioButton::_KnobFrame(const font_height& fontHeight) const
{
	// Same as BCheckBox...
	return BRect(0.0f, 2.0f, ceilf(3.0f + fontHeight.ascent),
		ceilf(5.0f + fontHeight.ascent));
}


/**
 * @brief Redraws the radio button by clearing the background and calling Draw().
 *
 * Fills the view with the background color, then calls Draw() and Flush()
 * to produce a clean, flicker-free repaint. Used in synchronous mouse-
 * tracking loops where UpdateIfNeeded() is not available.
 */
void
BRadioButton::_Redraw()
{
	BRect bounds(Bounds());

	// fill background with ViewColor()
	rgb_color highColor = HighColor();
	SetHighColor(ViewColor());
	FillRect(bounds);

	// restore previous HighColor()
	SetHighColor(highColor);
	Draw(bounds);
	Flush();
}
