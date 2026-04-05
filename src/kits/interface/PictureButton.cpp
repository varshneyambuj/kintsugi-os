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
 *   Copyright 2001-2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Graham MacDonald (macdonag@btopenworld.com)
 */


/**
 * @file PictureButton.cpp
 * @brief Implementation of BPictureButton, a button that renders using BPicture objects
 *
 * BPictureButton is a BControl that uses two BPicture objects (enabled and disabled
 * states) for its appearance. This allows arbitrary drawing commands to define the
 * button face rather than a text label.
 *
 * @see BControl, BPicture
 */


#include <PictureButton.h>

#include <new>

#include <binary_compatibility/Interface.h>


/**
 * @brief Construct a BPictureButton with an explicit frame and pictures.
 *
 * @param frame       The position and size of the button in the parent's coordinate system.
 * @param name        The view name used for identification.
 * @param off         The picture drawn when the button is in the off (unpressed) state.
 * @param on          The picture drawn when the button is in the on (pressed) state.
 * @param message     The message sent to the target when the button is invoked.
 * @param behavior    Either B_ONE_STATE_BUTTON (momentary) or B_TWO_STATE_BUTTON (toggle).
 * @param resizingMode How the view resizes when its parent is resized.
 * @param flags       View flags controlling event handling and drawing behaviour.
 * @note  The supplied pictures are deep-copied; the caller retains ownership of
 *        the originals.
 * @see   BControl::BControl()
 */
BPictureButton::BPictureButton(BRect frame, const char* name,
	BPicture* off, BPicture* on, BMessage* message,
	uint32 behavior, uint32 resizingMode, uint32 flags)
	:
	BControl(frame, name, "", message, resizingMode, flags),
	fEnabledOff(new(std::nothrow) BPicture(*off)),
	fEnabledOn(new(std::nothrow) BPicture(*on)),
	fDisabledOff(NULL),
	fDisabledOn(NULL),
	fBehavior(behavior)
{
}


/**
 * @brief Unarchiving constructor; restores a BPictureButton from a BMessage.
 *
 * Reads the behavior mode and all four picture slots from the archive message.
 * Missing picture entries are left as NULL; the caller is responsible for
 * setting disabled pictures before the button is shown in a disabled state.
 *
 * @param data  The archive message previously produced by Archive().
 * @see   Instantiate(), Archive()
 */
BPictureButton::BPictureButton(BMessage* data)
	:
	BControl(data),
	fEnabledOff(NULL),
	fEnabledOn(NULL),
	fDisabledOff(NULL),
	fDisabledOn(NULL)
{
	BMessage pictureArchive;

	// Default to 1 state button if not here - is this valid?
	if (data->FindInt32("_behave", (int32*)&fBehavior) != B_OK)
		fBehavior = B_ONE_STATE_BUTTON;

	// Now expand the pictures:
	if (data->FindMessage("_e_on", &pictureArchive) == B_OK)
		fEnabledOn = new(std::nothrow) BPicture(&pictureArchive);

	if (data->FindMessage("_e_off", &pictureArchive) == B_OK)
		fEnabledOff = new(std::nothrow) BPicture(&pictureArchive);

	if (data->FindMessage("_d_on", &pictureArchive) == B_OK)
		fDisabledOn = new(std::nothrow) BPicture(&pictureArchive);

	if (data->FindMessage("_d_off", &pictureArchive) == B_OK)
		fDisabledOff = new(std::nothrow) BPicture(&pictureArchive);
}


/**
 * @brief Destroy the BPictureButton and release all owned picture objects.
 */
BPictureButton::~BPictureButton()
{
	delete fEnabledOn;
	delete fEnabledOff;
	delete fDisabledOn;
	delete fDisabledOff;
}


/**
 * @brief Instantiate a BPictureButton from an archived BMessage.
 *
 * @param data  The archive message to instantiate from.
 * @return A newly allocated BPictureButton, or NULL if @a data is not a valid
 *         BPictureButton archive.
 * @see   Archive()
 */
BArchivable*
BPictureButton::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BPictureButton"))
		return new (std::nothrow) BPictureButton(data);

	return NULL;
}


/**
 * @brief Archive the button's state and pictures into a BMessage.
 *
 * When @a deep is true, each non-NULL picture is recursively archived under
 * the keys "_e_on", "_e_off", "_d_on", and "_d_off". The behavior mode is
 * always written as "_behave".
 *
 * @param data  The message to archive into.
 * @param deep  If true, BPicture objects are archived recursively.
 * @return B_OK on success, or an error code if archiving fails.
 * @retval B_OK On success.
 * @see   Instantiate()
 */
status_t
BPictureButton::Archive(BMessage* data, bool deep) const
{
	status_t err = BControl::Archive(data, deep);
	if (err != B_OK)
		return err;

	// Fill out message, depending on whether a deep copy is required or not.
	if (deep) {
		BMessage pictureArchive;
		if (fEnabledOn->Archive(&pictureArchive, deep) == B_OK) {
			err = data->AddMessage("_e_on", &pictureArchive);
			if (err != B_OK)
				return err;
		}

		pictureArchive.MakeEmpty();
		if (fEnabledOff->Archive(&pictureArchive, deep) == B_OK) {
			err = data->AddMessage("_e_off", &pictureArchive);
			if (err != B_OK)
				return err;
		}

		pictureArchive.MakeEmpty();
		if (fDisabledOn && fDisabledOn->Archive(&pictureArchive, deep) == B_OK) {
			err = data->AddMessage("_d_on", &pictureArchive);
			if (err != B_OK)
				return err;
		}

		pictureArchive.MakeEmpty();
		if (fDisabledOff && fDisabledOff->Archive(&pictureArchive, deep) == B_OK) {
			err = data->AddMessage("_d_off", &pictureArchive);
			if (err != B_OK)
				return err;
		}
	}

	return data->AddInt32("_behave", fBehavior);
}


void
BPictureButton::AttachedToWindow()
{
	BControl::AttachedToWindow();
}


void
BPictureButton::DetachedFromWindow()
{
	BControl::DetachedFromWindow();
}


void
BPictureButton::AllAttached()
{
	BControl::AllAttached();
}


void
BPictureButton::AllDetached()
{
	BControl::AllDetached();
}


void
BPictureButton::ResizeToPreferred()
{
	BControl::ResizeToPreferred();
}


void
BPictureButton::GetPreferredSize(float* _width, float* _height)
{
	BControl::GetPreferredSize(_width, _height);
}


void
BPictureButton::FrameMoved(BPoint newPosition)
{
	BControl::FrameMoved(newPosition);
}


void
BPictureButton::FrameResized(float newWidth, float newHeight)
{
	BControl::FrameResized(newWidth, newHeight);
}


void
BPictureButton::WindowActivated(bool active)
{
	BControl::WindowActivated(active);
}


void
BPictureButton::MakeFocus(bool focus)
{
	BControl::MakeFocus(focus);
}


/**
 * @brief Draw the button by rendering the appropriate BPicture for the current state.
 *
 * Selects between the enabled-on, enabled-off, disabled-on, and disabled-off
 * pictures based on IsEnabled() and Value(). If the button has keyboard focus,
 * a navigation-colour rectangle is stroked around the bounds.
 *
 * @param updateRect  The rectangle that needs to be redrawn (passed by the
 *                    drawing system; the picture always covers the full bounds).
 * @note  Calls debugger() if the required disabled pictures have not been set.
 * @see   SetDisabledOn(), SetDisabledOff()
 */
void
BPictureButton::Draw(BRect updateRect)
{
	if (IsEnabled()) {
		if (Value() == B_CONTROL_ON)
			DrawPicture(fEnabledOn);
		else
			DrawPicture(fEnabledOff);
	} else {

		if (fDisabledOff == NULL
			|| (fDisabledOn == NULL && fBehavior == B_TWO_STATE_BUTTON))
			debugger("Need to set the 'disabled' pictures for this BPictureButton ");

		if (Value() == B_CONTROL_ON)
			DrawPicture(fDisabledOn);
		else
			DrawPicture(fDisabledOff);
	}

	if (IsFocus()) {
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		StrokeRect(Bounds(), B_SOLID_HIGH);
	}
}


void
BPictureButton::MessageReceived(BMessage* message)
{
	BControl::MessageReceived(message);
}


/**
 * @brief Handle keyboard activation of the button.
 *
 * Pressing B_ENTER or B_SPACE triggers the button: momentary buttons
 * flash on then off, toggle buttons flip their state. Invoke() is called
 * in either case. All other keys are forwarded to BControl.
 *
 * @param bytes     Pointer to the raw key bytes.
 * @param numBytes  Number of bytes in @a bytes.
 */
void
BPictureButton::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes == 1) {
		switch (bytes[0]) {
			case B_ENTER:
			case B_SPACE:
				if (fBehavior == B_ONE_STATE_BUTTON) {
					SetValue(B_CONTROL_ON);
					snooze(50000);
					SetValue(B_CONTROL_OFF);
				} else {
					if (Value() == B_CONTROL_ON)
						SetValue(B_CONTROL_OFF);
					else
						SetValue(B_CONTROL_ON);
				}
				Invoke();
				return;
		}
	}

	BControl::KeyDown(bytes, numBytes);
}


/**
 * @brief Handle a mouse-button press on the picture button.
 *
 * Ignored when the button is disabled. Otherwise, captures pointer events and
 * updates the visual state: momentary buttons switch on, toggle buttons flip
 * their current state. Tracking mode is enabled so that MouseUp() can finalise
 * the action.
 *
 * @param where  The cursor position in the view's coordinate system.
 * @see   MouseUp(), MouseMoved()
 */
void
BPictureButton::MouseDown(BPoint where)
{
	if (!IsEnabled()) {
		BControl::MouseDown(where);
		return;
	}

	SetMouseEventMask(B_POINTER_EVENTS,
		B_NO_POINTER_HISTORY | B_SUSPEND_VIEW_FOCUS);

	if (fBehavior == B_ONE_STATE_BUTTON) {
		SetValue(B_CONTROL_ON);
	} else {
		if (Value() == B_CONTROL_ON)
			SetValue(B_CONTROL_OFF);
		else
			SetValue(B_CONTROL_ON);
	}
	SetTracking(true);
}


/**
 * @brief Handle a mouse-button release on the picture button.
 *
 * If the button is enabled and tracking, and the release occurs inside the
 * view bounds, a momentary button is reset to the off state (after a short
 * delay) and Invoke() is called. Tracking mode is cleared unconditionally.
 *
 * @param where  The cursor position in the view's coordinate system at release time.
 * @see   MouseDown(), Invoke()
 */
void
BPictureButton::MouseUp(BPoint where)
{
	if (IsEnabled() && IsTracking()) {
		if (Bounds().Contains(where)) {
			if (fBehavior == B_ONE_STATE_BUTTON) {
				if (Value() == B_CONTROL_ON) {
					snooze(75000);
					SetValue(B_CONTROL_OFF);
				}
			}
			Invoke();
		}

		SetTracking(false);
	}
}


/**
 * @brief Track cursor movement while the mouse button is held down.
 *
 * When tracking, exiting the view resets the button to off, and re-entering
 * restores it to on, providing the standard button-cancel-by-dragging-away
 * behaviour. When not tracking, the event is forwarded to BControl.
 *
 * @param where        Current cursor position in the view's coordinate system.
 * @param code         Transit code: B_ENTERED_VIEW, B_EXITED_VIEW, etc.
 * @param dragMessage  Drag-and-drop payload, if any (may be NULL).
 * @see   MouseDown(), MouseUp()
 */
void
BPictureButton::MouseMoved(BPoint where, uint32 code,
	const BMessage* dragMessage)
{
	if (IsEnabled() && IsTracking()) {
		if (code == B_EXITED_VIEW)
			SetValue(B_CONTROL_OFF);
		else if (code == B_ENTERED_VIEW)
			SetValue(B_CONTROL_ON);
	} else
		BControl::MouseMoved(where, code, dragMessage);
}


// #pragma mark -


/**
 * @brief Replace the picture shown when the button is enabled and in the off state.
 *
 * The existing picture is deleted and replaced with a deep copy of @a picture.
 *
 * @param picture  The new picture to use for the enabled-off state.
 */
void
BPictureButton::SetEnabledOff(BPicture* picture)
{
	delete fEnabledOff;
	fEnabledOff = new (std::nothrow) BPicture(*picture);
}


/**
 * @brief Replace the picture shown when the button is enabled and in the on state.
 *
 * The existing picture is deleted and replaced with a deep copy of @a picture.
 *
 * @param picture  The new picture to use for the enabled-on state.
 */
void
BPictureButton::SetEnabledOn(BPicture* picture)
{
	delete fEnabledOn;
	fEnabledOn = new (std::nothrow) BPicture(*picture);
}


/**
 * @brief Replace the picture shown when the button is disabled and in the on state.
 *
 * The existing picture is deleted and replaced with a deep copy of @a picture.
 * Required for B_TWO_STATE_BUTTON in the disabled state.
 *
 * @param picture  The new picture to use for the disabled-on state.
 * @see   SetDisabledOff()
 */
void
BPictureButton::SetDisabledOn(BPicture* picture)
{
	delete fDisabledOn;
	fDisabledOn = new (std::nothrow) BPicture(*picture);
}


/**
 * @brief Replace the picture shown when the button is disabled and in the off state.
 *
 * The existing picture is deleted and replaced with a deep copy of @a picture.
 * This picture must be set before the button is disabled; Draw() calls
 * debugger() if it is missing.
 *
 * @param picture  The new picture to use for the disabled-off state.
 * @see   SetDisabledOn()
 */
void
BPictureButton::SetDisabledOff(BPicture* picture)
{
	delete fDisabledOff;
	fDisabledOff = new (std::nothrow) BPicture(*picture);
}


/**
 * @brief Return the picture used when the button is enabled and in the on state.
 *
 * @return Pointer to the enabled-on BPicture, or NULL if none has been set.
 */
BPicture*
BPictureButton::EnabledOn() const
{
	return fEnabledOn;
}


/**
 * @brief Return the picture used when the button is enabled and in the off state.
 *
 * @return Pointer to the enabled-off BPicture, or NULL if none has been set.
 */
BPicture*
BPictureButton::EnabledOff() const
{
	return fEnabledOff;
}


/**
 * @brief Return the picture used when the button is disabled and in the on state.
 *
 * @return Pointer to the disabled-on BPicture, or NULL if none has been set.
 */
BPicture*
BPictureButton::DisabledOn() const
{
	return fDisabledOn;
}


/**
 * @brief Return the picture used when the button is disabled and in the off state.
 *
 * @return Pointer to the disabled-off BPicture, or NULL if none has been set.
 */
BPicture*
BPictureButton::DisabledOff() const
{
	return fDisabledOff;
}


/**
 * @brief Set the button's interaction behaviour.
 *
 * @param behavior  B_ONE_STATE_BUTTON for a momentary button, or
 *                  B_TWO_STATE_BUTTON for a toggle button.
 * @see   Behavior()
 */
void
BPictureButton::SetBehavior(uint32 behavior)
{
	fBehavior = behavior;
}


/**
 * @brief Return the button's current interaction behaviour.
 *
 * @return B_ONE_STATE_BUTTON or B_TWO_STATE_BUTTON.
 * @see    SetBehavior()
 */
uint32
BPictureButton::Behavior() const
{
	return fBehavior;
}


void
BPictureButton::SetValue(int32 value)
{
	BControl::SetValue(value);
}


status_t
BPictureButton::Invoke(BMessage* message)
{
	return BControl::Invoke(message);
}


BHandler*
BPictureButton::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	return BControl::ResolveSpecifier(message, index, specifier,
		what, property);
}


status_t
BPictureButton::GetSupportedSuites(BMessage* data)
{
	return BControl::GetSupportedSuites(data);
}


/**
 * @brief Dispatch a binary-compatibility perform code to the appropriate virtual method.
 *
 * Enables calling layout-related virtual methods (MinSize, MaxSize,
 * PreferredSize, LayoutAlignment, HasHeightForWidth, GetHeightForWidth,
 * SetLayout, LayoutInvalidated, DoLayout, SetIcon) through the stable ABI
 * perform() mechanism without requiring a vtable entry for each.
 *
 * @param code   One of the PERFORM_CODE_* constants identifying the method to invoke.
 * @param _data  Pointer to a perform_data_* struct whose fields carry both the
 *               input arguments and receive the return value.
 * @return B_OK if the code was handled, or the result of BControl::Perform()
 *         for unrecognised codes.
 * @retval B_OK On success.
 * @see   BControl::Perform()
 */
status_t
BPictureButton::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BPictureButton::MinSize();
			return B_OK;
		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BPictureButton::MaxSize();
			return B_OK;
		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BPictureButton::PreferredSize();
			return B_OK;
		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BPictureButton::LayoutAlignment();
			return B_OK;
		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BPictureButton::HasHeightForWidth();
			return B_OK;
		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BPictureButton::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}
		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BPictureButton::SetLayout(data->layout);
			return B_OK;
		}
		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BPictureButton::LayoutInvalidated(data->descendants);
			return B_OK;
		}
		case PERFORM_CODE_DO_LAYOUT:
		{
			BPictureButton::DoLayout();
			return B_OK;
		}
		case PERFORM_CODE_SET_ICON:
		{
			perform_data_set_icon* data = (perform_data_set_icon*)_data;
			return BPictureButton::SetIcon(data->icon, data->flags);
		}
	}

	return BControl::Perform(code, _data);
}


status_t
BPictureButton::SetIcon(const BBitmap* icon, uint32 flags)
{
	return BControl::SetIcon(icon, flags);
}


// #pragma mark - BPictureButton private methods


void BPictureButton::_ReservedPictureButton1() {}
void BPictureButton::_ReservedPictureButton2() {}
void BPictureButton::_ReservedPictureButton3() {}


BPictureButton&
BPictureButton::operator=(const BPictureButton &button)
{
	return *this;
}
