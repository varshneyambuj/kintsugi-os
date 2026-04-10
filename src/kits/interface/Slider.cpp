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
 *   Copyright 2001-2016 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Axel Dörfler, axeld@pinc-software.de
 *       Marc Flerackers (mflerackers@androme.be)
 */


/**
 * @file Slider.cpp
 * @brief Implementation of BSlider, a draggable slider control
 *
 * BSlider lets the user select a numeric value within a range by dragging a
 * thumb along a track. It supports horizontal and vertical orientations, hash
 * marks, and custom thumb and bar images. Tick update modes allow notifications
 * during or after dragging.
 *
 * @see BControl, BControlLook
 */


#include <Slider.h>

#include <algorithm>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Bitmap.h>
#include <ControlLook.h>
#include <Errors.h>
#include <LayoutUtils.h>
#include <Message.h>
#include <Region.h>
#include <String.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>


/** @brief When non-zero, rendering is done to an off-screen BBitmap first. */
#define USE_OFF_SCREEN_VIEW 0


/**
 * @brief Constructs a frame-based BSlider with a horizontal orientation.
 *
 * Creates a slider that occupies the given \a frame rectangle. The orientation
 * is fixed to B_HORIZONTAL. Use the orientation-aware constructor to choose a
 * different axis.
 *
 * @param frame         The position and size of the slider in its parent's
 *                      coordinate system.
 * @param name          The internal name used to identify this view.
 * @param label         The label text displayed above the bar, or NULL.
 * @param message       The message sent to the target on value changes. Takes
 *                      ownership.
 * @param minValue      The minimum selectable integer value.
 * @param maxValue      The maximum selectable integer value.
 * @param thumbType     The visual style of the thumb (B_BLOCK_THUMB or
 *                      B_TRIANGLE_THUMB).
 * @param resizingMode  The resizing mode flags passed to BView.
 * @param flags         The view flags passed to BView.
 */
BSlider::BSlider(
	BRect frame, const char* name, const char* label, BMessage* message,
	int32 minValue, int32 maxValue, thumb_style thumbType, uint32 resizingMode,
	uint32 flags)
	:
	BControl(frame, name, label, message, resizingMode, flags),
	fModificationMessage(NULL),
	fSnoozeAmount(20000),

	fMinLimitLabel(NULL),
	fMaxLimitLabel(NULL),

	fMinValue(minValue),
	fMaxValue(maxValue),
	fKeyIncrementValue(1),

	fHashMarkCount(0),
	fHashMarks(B_HASH_MARKS_NONE),

	fStyle(thumbType),

	fOrientation(B_HORIZONTAL),
	fBarThickness(6.0)
{
	_InitBarColor();

	_InitObject();
	SetValue(0);
}


/**
 * @brief Constructs a frame-based BSlider with an explicit orientation.
 *
 * Identical to the basic frame constructor but allows the caller to select
 * B_HORIZONTAL or B_VERTICAL for the slider axis.
 *
 * @param frame         The position and size of the slider in its parent's
 *                      coordinate system.
 * @param name          The internal name used to identify this view.
 * @param label         The label text displayed along the bar, or NULL.
 * @param message       The message sent to the target on value changes. Takes
 *                      ownership.
 * @param minValue      The minimum selectable integer value.
 * @param maxValue      The maximum selectable integer value.
 * @param posture       The axis along which the thumb travels (B_HORIZONTAL or
 *                      B_VERTICAL).
 * @param thumbType     The visual style of the thumb.
 * @param resizingMode  The resizing mode flags passed to BView.
 * @param flags         The view flags passed to BView.
 */
BSlider::BSlider(BRect frame, const char* name, const char* label,
	BMessage* message, int32 minValue, int32 maxValue, orientation posture,
	thumb_style thumbType, uint32 resizingMode, uint32 flags)
	:
	BControl(frame, name, label, message, resizingMode, flags),
	fModificationMessage(NULL),
	fSnoozeAmount(20000),

	fMinLimitLabel(NULL),
	fMaxLimitLabel(NULL),

	fMinValue(minValue),
	fMaxValue(maxValue),
	fKeyIncrementValue(1),

	fHashMarkCount(0),
	fHashMarks(B_HASH_MARKS_NONE),

	fStyle(thumbType),

	fOrientation(posture),
	fBarThickness(6.0)
{
	_InitBarColor();

	_InitObject();
	SetValue(0);
}


/**
 * @brief Constructs a layout-managed BSlider with no fixed frame.
 *
 * This constructor is intended for use with the layout system. The slider
 * acquires its bounds from the attached layout rather than from a BRect.
 *
 * @param name      The internal name used to identify this view.
 * @param label     The label text displayed along the bar, or NULL.
 * @param message   The message sent to the target on value changes. Takes
 *                  ownership.
 * @param minValue  The minimum selectable integer value.
 * @param maxValue  The maximum selectable integer value.
 * @param posture   The axis along which the thumb travels.
 * @param thumbType The visual style of the thumb.
 * @param flags     The view flags passed to BView.
 */
BSlider::BSlider(const char* name, const char* label, BMessage* message,
	int32 minValue, int32 maxValue, orientation posture, thumb_style thumbType,
	uint32 flags)
	:
	BControl(name, label, message, flags),
	fModificationMessage(NULL),
	fSnoozeAmount(20000),

	fMinLimitLabel(NULL),
	fMaxLimitLabel(NULL),

	fMinValue(minValue),
	fMaxValue(maxValue),
	fKeyIncrementValue(1),

	fHashMarkCount(0),
	fHashMarks(B_HASH_MARKS_NONE),

	fStyle(thumbType),

	fOrientation(posture),
	fBarThickness(6.0)
{
	_InitBarColor();

	_InitObject();
	SetValue(0);
}


/**
 * @brief Reconstructs a BSlider from a BMessage archive.
 *
 * Restores all slider properties that were previously stored by Archive(),
 * including the bar color, fill color, orientation, limit labels, value range,
 * key increment, hash mark settings, thumb style, and bar thickness. Missing
 * fields fall back to sensible defaults.
 *
 * @param archive The archive message produced by a prior call to Archive().
 * @see BSlider::Archive(), BSlider::Instantiate()
 */
BSlider::BSlider(BMessage* archive)
	:
	BControl(archive)
{
	fModificationMessage = NULL;

	if (archive->HasMessage("_mod_msg")) {
		BMessage* message = new BMessage;

		archive->FindMessage("_mod_msg", message);

		SetModificationMessage(message);
	}

	if (archive->FindInt32("_sdelay", &fSnoozeAmount) != B_OK)
		SetSnoozeAmount(20000);

	rgb_color color;
	if (archive->FindInt32("_fcolor", (int32*)&color) == B_OK)
		UseFillColor(true, &color);
	else
		UseFillColor(false);

	int32 orient;
	if (archive->FindInt32("_orient", &orient) == B_OK)
		fOrientation = (orientation)orient;
	else
		fOrientation = B_HORIZONTAL;

	fMinLimitLabel = NULL;
	fMaxLimitLabel = NULL;

	const char* minlbl = NULL;
	const char* maxlbl = NULL;

	archive->FindString("_minlbl", &minlbl);
	archive->FindString("_maxlbl", &maxlbl);

	SetLimitLabels(minlbl, maxlbl);

	if (archive->FindInt32("_min", &fMinValue) != B_OK)
		fMinValue = 0;

	if (archive->FindInt32("_max", &fMaxValue) != B_OK)
		fMaxValue = 100;

	if (archive->FindInt32("_incrementvalue", &fKeyIncrementValue) != B_OK)
		fKeyIncrementValue = 1;

	if (archive->FindInt32("_hashcount", &fHashMarkCount) != B_OK)
		fHashMarkCount = 11;

	int16 hashloc;
	if (archive->FindInt16("_hashloc", &hashloc) == B_OK)
		fHashMarks = (hash_mark_location)hashloc;
	else
		fHashMarks = B_HASH_MARKS_NONE;

	int16 sstyle;
	if (archive->FindInt16("_sstyle", &sstyle) == B_OK)
		fStyle = (thumb_style)sstyle;
	else
		fStyle = B_BLOCK_THUMB;

	if (archive->FindInt32("_bcolor", (int32*)&color) != B_OK)
		color = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_4_TINT);
	SetBarColor(color);

	float bthickness;
	if (archive->FindFloat("_bthickness", &bthickness) == B_OK)
		fBarThickness = bthickness;
	else
		fBarThickness = 6.0f;

	_InitObject();
}


/**
 * @brief Destroys the BSlider and frees all owned resources.
 *
 * Releases the modification message, the update-text buffer, and the
 * min/max limit label strings. When off-screen rendering is enabled the
 * backing BBitmap is also deleted here.
 */
BSlider::~BSlider()
{
#if USE_OFF_SCREEN_VIEW
	delete fOffScreenBits;
#endif

	delete fModificationMessage;
	free(fUpdateText);
	free(fMinLimitLabel);
	free(fMaxLimitLabel);
}


/**
 * @brief Initialises the bar and fill colors to their system defaults.
 *
 * Asks BControlLook for the standard slider bar color derived from the panel
 * background, and disables the optional fill-color feature. Called from every
 * public constructor before _InitObject().
 */
void
BSlider::_InitBarColor()
{
	SetBarColor(be_control_look->SliderBarColor(
		ui_color(B_PANEL_BACKGROUND_COLOR)));
	UseFillColor(false, NULL);
}


/**
 * @brief Initialises non-color transient state shared by all constructors.
 *
 * Zeroes the thumb location, clears the off-screen view pointers when
 * off-screen rendering is compiled in, and resets the update-text buffer and
 * cached minimum-size fields to their invalid sentinel values.
 */
void
BSlider::_InitObject()
{
	fLocation.x = 0;
	fLocation.y = 0;
	fInitialLocation.x = 0;
	fInitialLocation.y = 0;

#if USE_OFF_SCREEN_VIEW
	fOffScreenBits = NULL;
	fOffScreenView = NULL;
#endif

	fUpdateText = NULL;
	fMinSize.Set(-1, -1);
	fMaxUpdateTextWidth = -1.0;
}


/**
 * @brief Creates a new BSlider from an archive message (factory method).
 *
 * Called by the archiving infrastructure. Validates that \a archive was
 * produced by BSlider before constructing the object.
 *
 * @param archive The archive message to restore from.
 * @return A newly allocated BSlider, or NULL if the archive is invalid.
 * @see BSlider::Archive()
 */
BArchivable*
BSlider::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BSlider"))
		return new BSlider(archive);

	return NULL;
}


/**
 * @brief Serialises the slider's state into a BMessage archive.
 *
 * Stores the modification message, snooze delay, bar color, fill color,
 * orientation, limit labels, value range, key increment, hash-mark settings,
 * thumb style, and bar thickness. Delegates the BControl base fields to the
 * parent implementation.
 *
 * @param archive The message to write the state into.
 * @param deep    If true, child views are archived recursively.
 * @return B_OK on success, or the first error code encountered.
 * @see BSlider::Instantiate()
 */
status_t
BSlider::Archive(BMessage* archive, bool deep) const
{
	status_t ret = BControl::Archive(archive, deep);

	if (ModificationMessage() && ret == B_OK)
		ret = archive->AddMessage("_mod_msg", ModificationMessage());

	if (ret == B_OK)
		ret = archive->AddInt32("_sdelay", fSnoozeAmount);

	if (ret == B_OK)
		ret = archive->AddInt32("_bcolor", (const uint32&)fBarColor);

	if (FillColor(NULL) && ret == B_OK)
		ret = archive->AddInt32("_fcolor", (const uint32&)fFillColor);

	if (ret == B_OK && fMinLimitLabel != NULL)
		ret = archive->AddString("_minlbl", fMinLimitLabel);

	if (ret == B_OK && fMaxLimitLabel != NULL)
		ret = archive->AddString("_maxlbl", fMaxLimitLabel);

	if (ret == B_OK)
		ret = archive->AddInt32("_min", fMinValue);

	if (ret == B_OK)
		ret = archive->AddInt32("_max", fMaxValue);

	if (ret == B_OK)
		ret = archive->AddInt32("_incrementvalue", fKeyIncrementValue);

	if (ret == B_OK)
		ret = archive->AddInt32("_hashcount", fHashMarkCount);

	if (ret == B_OK)
		ret = archive->AddInt16("_hashloc", fHashMarks);

	if (ret == B_OK)
		ret = archive->AddInt16("_sstyle", fStyle);

	if (ret == B_OK)
		ret = archive->AddInt32("_orient", fOrientation);

	if (ret == B_OK)
		ret = archive->AddFloat("_bthickness", fBarThickness);

	return ret;
}


/**
 * @brief Dispatches binary-compatibility perform codes to the correct virtual method.
 *
 * Handles layout-related perform codes (MinSize, MaxSize, PreferredSize,
 * LayoutAlignment, HasHeightForWidth, GetHeightForWidth, SetLayout,
 * LayoutInvalidated, DoLayout, SetIcon) by calling the corresponding
 * BSlider virtual. Unrecognised codes are forwarded to BControl::Perform().
 *
 * @param code  The perform code identifying the operation.
 * @param _data Pointer to the code-specific data structure.
 * @return B_OK on success, or an error code from the delegated call.
 */
status_t
BSlider::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value = BSlider::MinSize();
			return B_OK;

		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value = BSlider::MaxSize();
			return B_OK;

		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BSlider::PreferredSize();
			return B_OK;

		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BSlider::LayoutAlignment();
			return B_OK;

		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BSlider::HasHeightForWidth();
			return B_OK;

		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BSlider::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}

		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BSlider::SetLayout(data->layout);
			return B_OK;
		}

		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BSlider::LayoutInvalidated(data->descendants);
			return B_OK;
		}

		case PERFORM_CODE_DO_LAYOUT:
		{
			BSlider::DoLayout();
			return B_OK;
		}

		case PERFORM_CODE_SET_ICON:
		{
			perform_data_set_icon* data = (perform_data_set_icon*)_data;
			return BSlider::SetIcon(data->icon, data->flags);
		}
	}

	return BControl::Perform(code, _data);
}


/**
 * @brief Notifies the slider that its window's active state has changed.
 *
 * Forwards the notification to BControl so that focus rendering and
 * keyboard delivery are updated correctly.
 *
 * @param state true if the window became active, false if it became inactive.
 */
void
BSlider::WindowActivated(bool state)
{
	BControl::WindowActivated(state);
}


/**
 * @brief Called when the slider is added to a window's view hierarchy.
 *
 * Adopts the system color scheme, resizes the view to its preferred dimensions,
 * clamps the current value to the valid range, recomputes the thumb location,
 * and refreshes the update-text label. When off-screen rendering is enabled,
 * this method also allocates the backing BBitmap and its associated BView.
 */
void
BSlider::AttachedToWindow()
{
	BControl::AttachedToWindow();

	AdoptSystemColors();
	ResizeToPreferred();

#if USE_OFF_SCREEN_VIEW
	BRect bounds(Bounds());

	if (fOffScreenView == NULL)
		fOffScreenView = new BView(bounds, "", B_FOLLOW_ALL, 0);

	if (fOffScreenBits == NULL)
		fOffScreenBits = new BBitmap(bounds, B_RGBA32, true, false);

	if (fOffScreenView != NULL && fOffScreenBits != NULL) {
		BFont font;
		GetFont(&font);
		fOffScreenView->SetFont(&font);

		fOffScreenView->SetFlags(Flags());
		fOffScreenView->AdoptViewColors(this);
		fOffScreenBits->AddChild(fOffScreenView);
	}
#endif // USE_OFF_SCREEN_VIEW

	int32 value = Value();
	SetValue(value);
		// makes sure the value is within valid bounds
	_SetLocationForValue(Value());
		// makes sure the location is correct
	UpdateTextChanged();
}


/**
 * @brief Called after all children of the slider have been attached to the window.
 *
 * When the slider is used inside a layout and therefore has no parent view,
 * this method manually applies the panel background color so that the view
 * colors are always correct regardless of the attachment order.
 */
void
BSlider::AllAttached()
{
	BControl::AllAttached();

	// When using a layout we may not have a parent, so we need to employ the
	// standard system colors manually. Due to how layouts work, this must
	// happen here, rather than in AttachedToWindow().
	if (Parent() == NULL)
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


/**
 * @brief Called after all children of the slider have been detached from the window.
 *
 * Forwards the notification to BControl for any base-class cleanup.
 */
void
BSlider::AllDetached()
{
	BControl::AllDetached();
}


/**
 * @brief Called when the slider is removed from a window's view hierarchy.
 *
 * Releases the off-screen BBitmap and clears the associated BView pointer when
 * off-screen rendering is enabled, preventing stale references.
 */
void
BSlider::DetachedFromWindow()
{
	BControl::DetachedFromWindow();

#if USE_OFF_SCREEN_VIEW
	delete fOffScreenBits;
	fOffScreenBits = NULL;
	fOffScreenView = NULL;
#endif
}


/**
 * @brief Handles messages delivered to the slider's message queue.
 *
 * Forwards unhandled messages to BControl::MessageReceived() for default
 * processing (scripting, B_SET_PROPERTY, etc.).
 *
 * @param message The received message.
 */
void
BSlider::MessageReceived(BMessage* message)
{
	BControl::MessageReceived(message);
}


/**
 * @brief Called when the slider's frame has been moved to a new position.
 *
 * Forwards the notification to BControl so that any dependent geometry
 * (e.g. cached screen coordinates) can be updated.
 *
 * @param new_position The slider's new origin in its parent's coordinate system.
 */
void
BSlider::FrameMoved(BPoint new_position)
{
	BControl::FrameMoved(new_position);
}


/**
 * @brief Called when the slider's frame has been resized.
 *
 * Resizes the off-screen BBitmap to match the new bounds when off-screen
 * rendering is enabled, then invalidates the entire view so it is repainted
 * at the correct dimensions. No-ops when the new size is degenerate.
 *
 * @param w The new width of the slider's bounds rectangle.
 * @param h The new height of the slider's bounds rectangle.
 */
void
BSlider::FrameResized(float w,float h)
{
	BControl::FrameResized(w, h);

	BRect bounds(Bounds());

	if (bounds.right <= 0.0f || bounds.bottom <= 0.0f)
		return;

#if USE_OFF_SCREEN_VIEW
	if (fOffScreenView != NULL && fOffScreenBits != NULL) {
		fOffScreenBits->RemoveChild(fOffScreenView);
		delete fOffScreenBits;

		fOffScreenView->ResizeTo(bounds.Width(), bounds.Height());

		fOffScreenBits = new BBitmap(Bounds(), B_RGBA32, true, false);
		fOffScreenBits->AddChild(fOffScreenView);
	}
#endif

	Invalidate();
}


/**
 * @brief Handles keyboard navigation for the slider.
 *
 * Arrow keys and Home/End adjust the value by the key-increment amount and
 * clamp it to [fMinValue, fMaxValue]. A modification message is posted
 * whenever the value actually changes. All other key codes are forwarded to
 * BControl.
 *
 * @param bytes    Pointer to the UTF-8 byte sequence of the pressed key.
 * @param numBytes Length of the byte sequence.
 * @note The slider must be enabled and visible for key events to take effect.
 */
void
BSlider::KeyDown(const char* bytes, int32 numBytes)
{
	if (!IsEnabled() || IsHidden())
		return;

	int32 newValue = Value();

	switch (bytes[0]) {
		case B_LEFT_ARROW:
		case B_DOWN_ARROW:
			newValue -= KeyIncrementValue();
			break;

		case B_RIGHT_ARROW:
		case B_UP_ARROW:
			newValue += KeyIncrementValue();
			break;

		case B_HOME:
			newValue = fMinValue;
			break;

		case B_END:
			newValue = fMaxValue;
			break;

		default:
			BControl::KeyDown(bytes, numBytes);
			return;
	}

	if (newValue < fMinValue)
		newValue = fMinValue;

	if (newValue > fMaxValue)
		newValue = fMaxValue;

	if (newValue != Value()) {
		fInitialLocation = _Location();
		SetValue(newValue);
		InvokeNotify(ModificationMessage(), B_CONTROL_MODIFIED);
	}
}

/**
 * @brief Finalises a keyboard value change by invoking the main message.
 *
 * If the thumb location has moved since the key was pressed, Invoke() is
 * called to deliver the final notification, mirroring the behaviour of a
 * mouse-up event at the end of a drag.
 *
 * @param bytes    Pointer to the UTF-8 byte sequence of the released key.
 * @param numBytes Length of the byte sequence.
 */
void
BSlider::KeyUp(const char* bytes, int32 numBytes)
{
	if (fInitialLocation != _Location()) {
		// The last KeyDown event triggered the modification message or no
		// notification at all, we may also have sent the modification message
		// continually while the user kept pressing the key. In either case,
		// finish with the final message to make the behavior consistent with
		// changing the value by mouse.
		Invoke();
	}
}


/**
 * @brief Clamps \a point to the bar's draggable range along the active axis.
 *
 * Only the coordinate relevant to the current orientation (x for horizontal,
 * y for vertical) is modified. The function reports whether that coordinate
 * actually differs from the corresponding coordinate of \a comparePoint, which
 * is used by mouse-tracking code to decide whether to update the value.
 *
 * @param point        The point to constrain, modified in place if out of range.
 * @param comparePoint Reference point used to detect a meaningful axis change.
 * @return true if the relevant axis coordinate differs from \a comparePoint.
 */
bool
BSlider::_ConstrainPoint(BPoint& point, BPoint comparePoint) const
{
	if (fOrientation == B_HORIZONTAL) {
		if (point.x != comparePoint.x) {
			if (point.x < _MinPosition())
				point.x = _MinPosition();
			else if (point.x > _MaxPosition())
				point.x = _MaxPosition();

			return true;
		}
	} else {
		if (point.y != comparePoint.y) {
			if (point.y > _MinPosition())
				point.y = _MinPosition();
			else if (point.y < _MaxPosition())
				point.y = _MaxPosition();

			return true;
		}
	}

	return false;
}


/**
 * @brief Begins a thumb-drag interaction on a mouse-button press.
 *
 * If the press falls within the bar or thumb frame the current location is
 * saved as the initial position. In asynchronous mode the view enters
 * tracking mode so that subsequent MouseMoved() and MouseUp() calls handle
 * the drag. In synchronous mode a polling loop runs until all buttons are
 * released, sending modification messages each time the value changes and a
 * final Invoke() when the drag completes.
 *
 * @param point The location of the mouse press in the view's coordinate system.
 * @note The slider must be enabled; disabled sliders ignore mouse events.
 */
void
BSlider::MouseDown(BPoint point)
{
	if (!IsEnabled())
		return;

	if (BarFrame().Contains(point) || ThumbFrame().Contains(point))
		fInitialLocation = _Location();

	uint32 buttons;
	GetMouse(&point, &buttons, true);

	_ConstrainPoint(point, fInitialLocation);
	SetValue(ValueForPoint(point));

	if (_Location() != fInitialLocation)
		InvokeNotify(ModificationMessage(), B_CONTROL_MODIFIED);

	if (Window()->Flags() & B_ASYNCHRONOUS_CONTROLS) {
		SetTracking(true);
		SetMouseEventMask(B_POINTER_EVENTS,
			B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
	} else {
		// synchronous mouse tracking
		BPoint prevPoint;

		while (buttons) {
			prevPoint = point;

			snooze(SnoozeAmount());
			GetMouse(&point, &buttons, true);

			if (_ConstrainPoint(point, prevPoint)) {
				int32 value = ValueForPoint(point);
				if (value != Value()) {
					SetValue(value);
					InvokeNotify(ModificationMessage(), B_CONTROL_MODIFIED);
				}
			}
		}
		if (_Location() != fInitialLocation)
			Invoke();
	}
}


/**
 * @brief Finalises a thumb drag when the mouse button is released.
 *
 * If the slider is in tracking (asynchronous) mode and the thumb has moved
 * from its initial position, Invoke() is called to deliver the final value
 * notification before tracking is cleared. Otherwise the event is forwarded
 * to BControl.
 *
 * @param point The location of the mouse release in the view's coordinate system.
 */
void
BSlider::MouseUp(BPoint point)
{
	if (IsTracking()) {
		if (_Location() != fInitialLocation)
			Invoke();

		SetTracking(false);
	} else
		BControl::MouseUp(point);
}


/**
 * @brief Tracks pointer movement during an asynchronous thumb drag.
 *
 * While the slider is in tracking mode, each movement is constrained to the
 * bar's valid range. If the constrained position maps to a different value a
 * modification message is sent. Non-tracking events are forwarded to BControl.
 *
 * @param point   The current pointer position in the view's coordinate system.
 * @param transit The transit code (B_ENTERED_VIEW, B_INSIDE_VIEW, etc.).
 * @param message The drag-and-drop message in transit, or NULL.
 */
void
BSlider::MouseMoved(BPoint point, uint32 transit, const BMessage* message)
{
	if (IsTracking()) {
		if (_ConstrainPoint(point, _Location())) {
			int32 value = ValueForPoint(point);
			if (value != Value()) {
				SetValue(value);
				InvokeNotify(ModificationMessage(), B_CONTROL_MODIFIED);
			}
		}
	} else
		BControl::MouseMoved(point, transit, message);
}


/**
 * @brief Called periodically by the window when the slider has B_PULSE_NEEDED.
 *
 * Forwards to BControl::Pulse() for any base-class periodic behavior.
 */
void
BSlider::Pulse()
{
	BControl::Pulse();
}


/**
 * @brief Sets the slider's main label text.
 *
 * The label is rendered above the bar (horizontal) or at the top (vertical).
 * Delegates storage and invalidation to BControl::SetLabel().
 *
 * @param label The new label string, or NULL to remove the label.
 */
void
BSlider::SetLabel(const char* label)
{
	BControl::SetLabel(label);
}


/**
 * @brief Sets the text labels shown at the minimum and maximum ends of the bar.
 *
 * The strings are copied internally; the caller retains ownership of the
 * pointers. Setting either label to NULL removes it. After updating the labels
 * the layout is invalidated and, for views not using the layout system, the
 * view is immediately resized to its new preferred size.
 *
 * @param minLabel Label displayed at the low end of the range, or NULL.
 * @param maxLabel Label displayed at the high end of the range, or NULL.
 */
void
BSlider::SetLimitLabels(const char* minLabel, const char* maxLabel)
{
	free(fMinLimitLabel);
	fMinLimitLabel = minLabel ? strdup(minLabel) : NULL;

	free(fMaxLimitLabel);
	fMaxLimitLabel = maxLabel ? strdup(maxLabel) : NULL;

	InvalidateLayout();

	// TODO: This is for backwards compatibility and should
	// probably be removed when breaking binary compatiblity.
	// Applications like our own Mouse rely on this behavior.
	if ((Flags() & B_SUPPORTS_LAYOUT) == 0)
		ResizeToPreferred();

	Invalidate();
}


/**
 * @brief Returns the label displayed at the minimum end of the bar.
 *
 * @return A pointer to the internally stored string, or NULL if no label is set.
 * @see SetLimitLabels()
 */
const char*
BSlider::MinLimitLabel() const
{
	return fMinLimitLabel;
}


/**
 * @brief Returns the label displayed at the maximum end of the bar.
 *
 * @return A pointer to the internally stored string, or NULL if no label is set.
 * @see SetLimitLabels()
 */
const char*
BSlider::MaxLimitLabel() const
{
	return fMaxLimitLabel;
}


/**
 * @brief Sets the slider's current value and updates the display.
 *
 * Clamps \a value to [fMinValue, fMaxValue], moves the thumb to the
 * corresponding pixel position, and invalidates the union of the old and new
 * thumb frames (extended slightly for anti-aliasing with triangle thumbs and
 * for the focus mark). Also refreshes the update-text label. No redraw occurs
 * if the value is unchanged.
 *
 * @param value The desired value; clamped to the configured range.
 */
void
BSlider::SetValue(int32 value)
{
	if (value < fMinValue)
		value = fMinValue;

	if (value > fMaxValue)
		value = fMaxValue;

	if (value == Value())
		return;

	_SetLocationForValue(value);

	BRect oldThumbFrame = ThumbFrame();

	// While it would be enough to do this dependent on fUseFillColor,
	// that doesn't work out if DrawBar() has been overridden by a sub class
	if (fOrientation == B_HORIZONTAL)
		oldThumbFrame.top = BarFrame().top;
	else
		oldThumbFrame.left = BarFrame().left;

	BControl::SetValueNoUpdate(value);
	BRect invalid = oldThumbFrame | ThumbFrame();

	if (Style() == B_TRIANGLE_THUMB) {
		// 1) We need to take care of pixels touched because of anti-aliasing.
		// 2) We need to update the region with the focus mark as well. (A
		// method BSlider::FocusMarkFrame() would be nice as well.)
		if (fOrientation == B_HORIZONTAL) {
			if (IsFocus())
				invalid.bottom += 2;
			invalid.InsetBy(-1, 0);
		} else {
			if (IsFocus())
				invalid.left -= 2;
			invalid.InsetBy(0, -1);
		}
	}

	Invalidate(invalid);

	UpdateTextChanged();
}


/**
 * @brief Converts a pixel coordinate to the nearest slider value.
 *
 * Projects \a location onto the slider's active axis, clamps the result to
 * the bar's pixel range, and performs a linear mapping to the integer value
 * range [fMinValue, fMaxValue].
 *
 * @param location A point in the view's coordinate system.
 * @return The integer value corresponding to the given position.
 * @see PointForValue(), SetValue()
 */
int32
BSlider::ValueForPoint(BPoint location) const
{
	float min;
	float max;
	float position;
	if (fOrientation == B_HORIZONTAL) {
		min = _MinPosition();
		max = _MaxPosition();
		position = location.x;
	} else {
		max = _MinPosition();
		min = _MaxPosition();
		position = min + (max - location.y);
	}

	if (position < min)
		position = min;

	if (position > max)
		position = max;

	return (int32)roundf(((position - min) * (fMaxValue - fMinValue)
		/ (max - min)) + fMinValue);
}


/**
 * @brief Sets the slider value using a normalised position in [0.0, 1.0].
 *
 * Values at or below 0.0 map to fMinValue; values at or above 1.0 map to
 * fMaxValue. Intermediate values are linearly interpolated.
 *
 * @param position The normalised position; clamped to [0.0, 1.0].
 * @see Position(), SetValue()
 */
void
BSlider::SetPosition(float position)
{
	if (position <= 0.0f)
		SetValue(fMinValue);
	else if (position >= 1.0f)
		SetValue(fMaxValue);
	else
		SetValue((int32)(position * (fMaxValue - fMinValue) + fMinValue));
}


/**
 * @brief Returns the current slider position as a normalised value in [0.0, 1.0].
 *
 * When the range is zero the function avoids a division-by-zero by treating
 * the range as 1.
 *
 * @return The normalised position of the thumb.
 * @see SetPosition()
 */
float
BSlider::Position() const
{
	float range = (float)(fMaxValue - fMinValue);
	if (range == 0.0f)
		range = 1.0f;

	return (float)(Value() - fMinValue) / range;
}


/**
 * @brief Enables or disables the slider.
 *
 * Delegates to BControl::SetEnabled(), which triggers a redraw so the
 * thumb and bar are rendered in the appropriate enabled/disabled state.
 *
 * @param on true to enable the slider, false to disable it.
 */
void
BSlider::SetEnabled(bool on)
{
	BControl::SetEnabled(on);
}


/**
 * @brief Retrieves the minimum and maximum values of the slider's range.
 *
 * @param minimum If not NULL, receives the minimum value.
 * @param maximum If not NULL, receives the maximum value.
 * @see SetLimits()
 */
void
BSlider::GetLimits(int32* minimum, int32* maximum) const
{
	if (minimum != NULL)
		*minimum = fMinValue;

	if (maximum != NULL)
		*maximum = fMaxValue;
}


// #pragma mark - drawing


/**
 * @brief Paints the slider in response to an expose event.
 *
 * Fills the background region (excluding the bar frame) with the low color
 * when the view is not transparent and is not drawn on a parent that draws on
 * its children, then delegates all slider-specific rendering to DrawSlider().
 *
 * @param updateRect The dirty rectangle that needs to be repainted.
 */
void
BSlider::Draw(BRect updateRect)
{
	// clear out background
	BRegion background(updateRect);
	background.Exclude(BarFrame());

	bool drawBackground = background.Frame().IsValid();
	if (drawBackground) {
		bool hasTransparentBG = (Flags() & B_TRANSPARENT_BACKGROUND) != 0;
		bool drawOnChildren = Parent() != NULL
			&& (Parent()->Flags() & B_DRAW_ON_CHILDREN) != 0;
		drawBackground = !hasTransparentBG && !drawOnChildren;
	}

	if (drawBackground) {
#if USE_OFF_SCREEN_VIEW
		if (fOffScreenView != NULL && fOffScreenBits != NULL && fOffScreenBits->Lock()) {
			fOffScreenView->FillRegion(&background, B_SOLID_LOW);
			fOffScreenView->Sync();
			fOffScreenBits->Unlock();
		}
#else
		FillRegion(&background, B_SOLID_LOW);
#endif
	}

	DrawSlider();
}


/**
 * @brief Renders all slider components in the correct paint order.
 *
 * Acquires the looper lock and then calls DrawBar(), DrawHashMarks(),
 * DrawThumb(), DrawFocusMark(), and DrawText() in sequence. When off-screen
 * rendering is enabled the result is composited onto the view via
 * DrawBitmap() after the off-screen view is synchronised.
 */
void
BSlider::DrawSlider()
{
	if (LockLooper()) {
#if USE_OFF_SCREEN_VIEW
		if (fOffScreenView != NULL && fOffScreenBits != NULL && fOffScreenBits->Lock()) {
#endif
			DrawBar();
			DrawHashMarks();
			DrawThumb();
			DrawFocusMark();
			DrawText();

#if USE_OFF_SCREEN_VIEW
			fOffScreenView->Sync();
			fOffScreenBits->Unlock();

			DrawBitmap(fOffScreenBits, B_ORIGIN);
		}
#endif
		UnlockLooper();
	}
}


/**
 * @brief Draws the slider bar (track) using BControlLook.
 *
 * Retrieves the current bar frame and control-look flags, then calls
 * BControlLook::DrawSliderBar(). When the fill color is enabled the left
 * portion of the bar (behind the thumb) is rendered in fFillColor; otherwise
 * both portions use fBarColor.
 */
void
BSlider::DrawBar()
{
	BView* view = OffscreenView();
	BRect frame = BarFrame();
	rgb_color base = view->LowColor();
	uint32 flags = be_control_look->Flags(this);
	rgb_color rightFillColor = fBarColor;
	rgb_color leftFillColor = fUseFillColor ? fFillColor : fBarColor;

	be_control_look->DrawSliderBar(view, frame, frame, base, leftFillColor,
		rightFillColor, Position(), flags, fOrientation);
}


/**
 * @brief Draws tick marks alongside the slider bar.
 *
 * Returns immediately when fHashMarks is B_HASH_MARKS_NONE. Otherwise
 * delegates to BControlLook::DrawSliderHashMarks() using the hash-marks
 * frame, mark count, and current orientation.
 */
void
BSlider::DrawHashMarks()
{
	if (fHashMarks == B_HASH_MARKS_NONE)
		return;

	BView* view = OffscreenView();
	BRect frame = HashMarksFrame();
	rgb_color base = view->LowColor();
	uint32 flags = be_control_look->Flags(this);

	be_control_look->DrawSliderHashMarks(view, frame, frame, base,
		fHashMarkCount, fHashMarks, flags, fOrientation);
}


/**
 * @brief Draws the draggable thumb in the style configured by SetStyle().
 *
 * Dispatches to _DrawBlockThumb() for B_BLOCK_THUMB or _DrawTriangleThumb()
 * for B_TRIANGLE_THUMB. Subclasses may override this method to supply a
 * completely custom thumb appearance.
 */
void
BSlider::DrawThumb()
{
	if (Style() == B_BLOCK_THUMB)
		_DrawBlockThumb();
	else
		_DrawTriangleThumb();
}


/**
 * @brief Draws the keyboard-focus indicator on the thumb.
 *
 * Only draws when the slider has focus. For block thumbs a rectangle is
 * stroked 2-3 pixels inside the thumb frame; for triangle thumbs a line is
 * drawn just outside the thumb, parallel to the bar axis, using the system
 * keyboard-navigation highlight color.
 */
void
BSlider::DrawFocusMark()
{
	if (!IsFocus())
		return;

	BView* view = OffscreenView();
	BRect frame = ThumbFrame();

	view->PushState();
	view->SetHighUIColor(B_KEYBOARD_NAVIGATION_COLOR);

	if (fStyle == B_BLOCK_THUMB) {
		frame.left += 2.0f;
		frame.top += 2.0f;
		frame.right -= 3.0f;
		frame.bottom -= 3.0f;
		view->StrokeRect(frame);
	} else {
		if (fOrientation == B_HORIZONTAL) {
			view->StrokeLine(BPoint(frame.left, frame.bottom + 2.0f),
				BPoint(frame.right, frame.bottom + 2.0f));
		} else {
			view->StrokeLine(BPoint(frame.left - 2.0f, frame.top),
				BPoint(frame.left - 2.0f, frame.bottom));
		}
	}

	view->PopState();
}


/**
 * @brief Draws all text elements: the label, update text, and limit labels.
 *
 * For horizontal sliders the label and update text appear above the bar on
 * opposite ends, with the limit labels below. For vertical sliders all text
 * is centred horizontally, with the label and max-limit label at the top and
 * the min-limit label and update text at the bottom. Rendering is performed
 * via BControlLook::DrawLabel() to pick up system theming.
 */
void
BSlider::DrawText()
{
	BRect bounds(Bounds());
	BView* view = OffscreenView();
	rgb_color base = view->LowColor();
	rgb_color text = view->HighColor();
	uint32 flags = be_control_look->Flags(this);

	font_height fontHeight;
	GetFontHeight(&fontHeight);
	if (Orientation() == B_HORIZONTAL) {
		if (Label() != NULL) {
			be_control_look->DrawLabel(view, Label(), base, flags,
				BPoint(0.0f, ceilf(fontHeight.ascent)), &text);
		}

		// the update text is updated in SetValue() only
		if (fUpdateText != NULL) {
			be_control_look->DrawLabel(view, fUpdateText, base, flags,
				BPoint(bounds.right - StringWidth(fUpdateText),
					ceilf(fontHeight.ascent)), &text);
		}

		if (fMinLimitLabel != NULL) {
			be_control_look->DrawLabel(view, fMinLimitLabel, base, flags,
				BPoint(0.0f, bounds.bottom - fontHeight.descent), &text);
		}

		if (fMaxLimitLabel != NULL) {
			be_control_look->DrawLabel(view, fMaxLimitLabel, base, flags,
				BPoint(bounds.right - StringWidth(fMaxLimitLabel),
					bounds.bottom - fontHeight.descent), &text);
		}
	} else {
		float lineHeight = ceilf(fontHeight.ascent) + ceilf(fontHeight.descent)
			+ ceilf(fontHeight.leading);
		float baseLine = ceilf(fontHeight.ascent);

		if (Label() != NULL) {
			be_control_look->DrawLabel(view, Label(), base, flags,
				BPoint((bounds.Width() - StringWidth(Label())) / 2.0,
					baseLine), &text);
			baseLine += lineHeight;
		}

		if (fMaxLimitLabel != NULL) {
			be_control_look->DrawLabel(view, fMaxLimitLabel, base, flags,
				BPoint((bounds.Width() - StringWidth(fMaxLimitLabel)) / 2.0,
					baseLine), &text);
		}

		baseLine = bounds.bottom - ceilf(fontHeight.descent);

		if (fMinLimitLabel != NULL) {
			be_control_look->DrawLabel(view, fMinLimitLabel, base, flags,
				BPoint((bounds.Width() - StringWidth(fMinLimitLabel)) / 2.0,
					baseLine), &text);
				baseLine -= lineHeight;
		}

		if (fUpdateText != NULL) {
			be_control_look->DrawLabel(view, fUpdateText, base, flags,
				BPoint((bounds.Width() - StringWidth(fUpdateText)) / 2.0,
					baseLine), &text);
		}
	}
}


// #pragma mark -


/**
 * @brief Returns the dynamically generated text shown at the opposite end from the label.
 *
 * The default implementation returns NULL (no update text). Subclasses can
 * override this method to return a string that reflects the current value
 * (e.g. a formatted percentage or measurement). The returned pointer must
 * remain valid until the next call to UpdateText() or until the slider is
 * destroyed.
 *
 * @return The text to display, or NULL to show nothing.
 * @see UpdateTextChanged()
 */
const char*
BSlider::UpdateText() const
{
	return NULL;
}


/**
 * @brief Refreshes the cached update-text string and repaints the affected region.
 *
 * Called by SetValue() whenever the value changes. Fetches the new string from
 * UpdateText(), computes the dirty rectangle based on the wider of the old and
 * new string widths, and invalidates that region. If the maximum update-text
 * width changes the layout is also invalidated so the minimum size is
 * recalculated.
 *
 * @see UpdateText(), SetValue()
 */
void
BSlider::UpdateTextChanged()
{
	// update text label
	float oldWidth = 0.0;
	if (fUpdateText != NULL)
		oldWidth = StringWidth(fUpdateText);

	const char* oldUpdateText = fUpdateText;
	free(fUpdateText);

	fUpdateText = strdup(UpdateText());
	bool updateTextOnOff = (fUpdateText == NULL && oldUpdateText != NULL)
		|| (fUpdateText != NULL && oldUpdateText == NULL);

	float newWidth = 0.0;
	if (fUpdateText != NULL)
		newWidth = StringWidth(fUpdateText);

	float width = ceilf(std::max(newWidth, oldWidth)) + 2.0f;
	if (width != 0) {
		font_height fontHeight;
		GetFontHeight(&fontHeight);

		float height = ceilf(fontHeight.ascent) + ceilf(fontHeight.descent);
		float lineHeight = height + ceilf(fontHeight.leading);
		BRect invalid(Bounds());
		if (fOrientation == B_HORIZONTAL)
			invalid = BRect(invalid.right - width, 0, invalid.right, height);
		else {
			if (!updateTextOnOff) {
				invalid.left = (invalid.left + invalid.right - width) / 2;
				invalid.right = invalid.left + width;
				if (fMinLimitLabel != NULL)
					invalid.bottom -= lineHeight;

				invalid.top = invalid.bottom - height;
			}
		}
		Invalidate(invalid);
	}

	float oldMaxUpdateTextWidth = fMaxUpdateTextWidth;
	fMaxUpdateTextWidth = MaxUpdateTextWidth();
	if (oldMaxUpdateTextWidth != fMaxUpdateTextWidth)
		InvalidateLayout();
}


/**
 * @brief Returns the bounding rectangle of the slider bar (track).
 *
 * Computes the frame in the view's local coordinate system taking into account
 * the current orientation, font metrics, bar thickness, thumb style, and all
 * visible text areas. The returned rectangle is used by Draw(), DrawBar(),
 * HashMarksFrame(), ThumbFrame(), and hit-testing in MouseDown().
 *
 * @return The bar's frame in local coordinates.
 */
BRect
BSlider::BarFrame() const
{
	BRect frame(Bounds());

	font_height fontHeight;
	GetFontHeight(&fontHeight);

	float textHeight = ceilf(fontHeight.ascent) + ceilf(fontHeight.descent);
	float leading = ceilf(fontHeight.leading);

	float thumbInset;
	if (fStyle == B_BLOCK_THUMB)
		thumbInset = 8.0;
	else
		thumbInset = 7.0;

	if (Orientation() == B_HORIZONTAL) {
		frame.left = thumbInset;
		frame.top = 6.0 + (Label() || fUpdateText ? textHeight + 4.0 : 0.0);
		frame.right -= thumbInset;
		frame.bottom = frame.top + fBarThickness;
	} else {
		frame.left = floorf((frame.Width() - fBarThickness) / 2.0);
		frame.top = thumbInset;
		if (Label() != NULL)
			frame.top += textHeight;

		if (fMaxLimitLabel != NULL) {
			frame.top += textHeight;
			if (Label())
				frame.top += leading;
		}

		frame.right = frame.left + fBarThickness;
		frame.bottom = frame.bottom - thumbInset;
		if (fMinLimitLabel != NULL)
			frame.bottom -= textHeight;

		if (fUpdateText != NULL) {
			frame.bottom -= textHeight;
			if (fMinLimitLabel != NULL)
				frame.bottom -= leading;
		}
	}

	return frame;
}


/**
 * @brief Returns the bounding rectangle within which hash marks are drawn.
 *
 * Extends the bar frame by 6 pixels on both sides perpendicular to the bar
 * axis (top and bottom for horizontal, left and right for vertical).
 *
 * @return The hash-marks region in local coordinates.
 * @see DrawHashMarks(), BarFrame()
 */
BRect
BSlider::HashMarksFrame() const
{
	BRect frame(BarFrame());

	if (fOrientation == B_HORIZONTAL) {
		frame.top -= 6.0;
		frame.bottom += 6.0;
	} else {
		frame.left -= 6.0;
		frame.right += 6.0;
	}

	return frame;
}


/**
 * @brief Returns the bounding rectangle of the draggable thumb at its current position.
 *
 * Computes the frame in local coordinates based on the current Position(),
 * the bar thickness, the thumb style, and font metrics. The block thumb is
 * 17 x (barThickness + 7) pixels; the triangle thumb is 12 x 8 pixels (plus
 * bar thickness padding) in horizontal orientation, with axes transposed for
 * vertical. Used by Draw(), MouseDown() hit-testing, and SetValue()
 * invalidation.
 *
 * @return The thumb's frame in local coordinates.
 */
BRect
BSlider::ThumbFrame() const
{
	// TODO: The slider looks really ugly and broken when it is too little.
	// I would suggest using BarFrame() here to get the top and bottom coords
	// and spread them further apart for the thumb

	BRect frame = Bounds();

	font_height fontHeight;
	GetFontHeight(&fontHeight);

	float textHeight = ceilf(fontHeight.ascent) + ceilf(fontHeight.descent);

	if (fStyle == B_BLOCK_THUMB) {
		if (Orientation() == B_HORIZONTAL) {
			frame.left = floorf(Position() * (_MaxPosition()
				- _MinPosition()) + _MinPosition()) - 8;
			frame.top = 2 + (Label() || fUpdateText ? textHeight + 4 : 0);
			frame.right = frame.left + 17;
			frame.bottom = frame.top + fBarThickness + 7;
		} else {
			frame.left = floor((frame.Width() - fBarThickness) / 2) - 4;
			frame.top = floorf(Position() * (_MaxPosition()
				- _MinPosition()) + _MinPosition()) - 8;
			frame.right = frame.left + fBarThickness + 7;
			frame.bottom = frame.top + 17;
		}
	} else {
		if (Orientation() == B_HORIZONTAL) {
			frame.left = floorf(Position() * (_MaxPosition()
				- _MinPosition()) + _MinPosition()) - 6;
			frame.right = frame.left + 12;
			frame.top = 3 + fBarThickness + (Label() ? textHeight + 4 : 0);
			frame.bottom = frame.top + 8;
		} else {
			frame.left = floorf((frame.Width() + fBarThickness) / 2) - 3;
			frame.top = floorf(Position() * (_MaxPosition()
				- _MinPosition())) + _MinPosition() - 6;
			frame.right = frame.left + 8;
			frame.bottom = frame.top + 12;
		}
	}

	return frame;
}


/**
 * @brief Sets the view flags for the slider.
 *
 * Forwards to BControl::SetFlags() to update the underlying BView flags,
 * which control properties such as mouse and keyboard event delivery.
 *
 * @param flags The new set of B_* view flag constants.
 */
void
BSlider::SetFlags(uint32 flags)
{
	BControl::SetFlags(flags);
}


/**
 * @brief Sets the resizing mode of the slider view.
 *
 * Forwards to BControl::SetResizingMode() to update how the view responds
 * to its parent being resized.
 *
 * @param mode A combination of B_FOLLOW_* resizing mode constants.
 */
void
BSlider::SetResizingMode(uint32 mode)
{
	BControl::SetResizingMode(mode);
}


/**
 * @brief Returns the preferred width and height for the slider.
 *
 * Queries PreferredSize() and writes the result into the caller's pointers.
 * For horizontal sliders the returned width is the maximum of the current
 * bounds width and the layout-preferred width, so the view never shrinks
 * below its current size in legacy (non-layout) applications. A symmetric
 * rule applies for the height of vertical sliders.
 *
 * @param _width  Set to the preferred width, or left unchanged if NULL.
 * @param _height Set to the preferred height, or left unchanged if NULL.
 */
void
BSlider::GetPreferredSize(float* _width, float* _height)
{
	BSize preferredSize = PreferredSize();

	if (Orientation() == B_HORIZONTAL) {
		if (_width != NULL) {
			// NOTE: For compatibility reasons, a horizontal BSlider
			// never shrinks horizontally. This only affects applications
			// which do not use the new layout system.
			*_width = std::max(Bounds().Width(), preferredSize.width);
		}

		if (_height != NULL)
			*_height = preferredSize.height;
	} else {
		if (_width != NULL)
			*_width = preferredSize.width;

		if (_height != NULL) {
			// NOTE: Similarly, a vertical BSlider never shrinks
			// vertically. This only affects applications which do not
			// use the new layout system.
			*_height = std::max(Bounds().Height(), preferredSize.height);
		}
	}
}


/**
 * @brief Resizes the slider to its preferred dimensions.
 *
 * Delegates to BControl::ResizeToPreferred(), which calls GetPreferredSize()
 * and performs the resize.
 */
void
BSlider::ResizeToPreferred()
{
	BControl::ResizeToPreferred();
}


/**
 * @brief Sends the slider's invocation message to its target.
 *
 * Forwards to BControl::Invoke(), which stamps the message with the current
 * value and dispatches it via the configured messenger. Typically called at
 * the end of a drag (mouse-up or key-up) to signal the final committed value.
 *
 * @param message The message to send, or NULL to use the slider's own message.
 * @return B_OK on success, or an error code from the messenger.
 * @see InvokeNotify(), SetModificationMessage()
 */
status_t
BSlider::Invoke(BMessage* message)
{
	return BControl::Invoke(message);
}


/**
 * @brief Resolves a scripting specifier to the handler that owns the named property.
 *
 * Forwards unhandled specifiers to BControl::ResolveSpecifier().
 *
 * @param message   The scripting message being processed.
 * @param index     The index of the current specifier in the message.
 * @param specifier The specifier message extracted from \a message.
 * @param command   The scripting command (B_GET_PROPERTY, B_SET_PROPERTY, etc.).
 * @param property  The name of the property being accessed.
 * @return The BHandler responsible for the property, or NULL on failure.
 */
BHandler*
BSlider::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 command, const char* property)
{
	return BControl::ResolveSpecifier(message, index, specifier, command,
		property);
}


/**
 * @brief Fills \a message with the scripting suites supported by this slider.
 *
 * Forwards to BControl::GetSupportedSuites() to append the standard control
 * suite information.
 *
 * @param message The message to receive the suite names and property descriptions.
 * @return B_OK on success, or an error code.
 */
status_t
BSlider::GetSupportedSuites(BMessage* message)
{
	return BControl::GetSupportedSuites(message);
}


/**
 * @brief Sets the message sent while the user is actively dragging the thumb.
 *
 * This message is delivered via InvokeNotify() with kind B_CONTROL_MODIFIED
 * each time the value changes during a drag, before the final Invoke() at
 * mouse-up. Takes ownership of \a message and deletes the previous one.
 *
 * @param message The modification message, or NULL to clear it.
 * @see ModificationMessage(), Invoke()
 */
void
BSlider::SetModificationMessage(BMessage* message)
{
	delete fModificationMessage;
	fModificationMessage = message;
}


/**
 * @brief Returns the message sent during active thumb dragging.
 *
 * @return The modification message, or NULL if none is set.
 * @see SetModificationMessage()
 */
BMessage*
BSlider::ModificationMessage() const
{
	return fModificationMessage;
}


/**
 * @brief Sets the polling interval used during a synchronous mouse drag.
 *
 * In synchronous tracking mode the event loop sleeps for this many
 * microseconds between GetMouse() calls. The value is clamped to
 * [10000, 1000000] microseconds (10 ms – 1 s).
 *
 * @param snoozeTime The desired sleep interval in microseconds.
 * @see SnoozeAmount()
 */
void
BSlider::SetSnoozeAmount(int32 snoozeTime)
{
	if (snoozeTime < 10000)
		snoozeTime = 10000;
	else if (snoozeTime > 1000000)
		snoozeTime = 1000000;

	fSnoozeAmount = snoozeTime;
}


/**
 * @brief Returns the synchronous-drag polling interval in microseconds.
 *
 * @return The current snooze amount, guaranteed to be in [10000, 1000000].
 * @see SetSnoozeAmount()
 */
int32
BSlider::SnoozeAmount() const
{
	return fSnoozeAmount;
}


/**
 * @brief Sets the amount by which each arrow-key press changes the value.
 *
 * @param incrementValue The integer delta applied per key event.
 * @see KeyIncrementValue(), KeyDown()
 */
void
BSlider::SetKeyIncrementValue(int32 incrementValue)
{
	fKeyIncrementValue = incrementValue;
}


/**
 * @brief Returns the value delta applied by each arrow-key press.
 *
 * @return The current key increment value.
 * @see SetKeyIncrementValue()
 */
int32
BSlider::KeyIncrementValue() const
{
	return fKeyIncrementValue;
}


/**
 * @brief Sets the number of tick marks drawn along the bar.
 *
 * Triggers a full repaint. Has no visible effect when fHashMarks is
 * B_HASH_MARKS_NONE.
 *
 * @param hashMarkCount The desired number of tick marks (including end marks).
 * @see HashMarkCount(), SetHashMarks()
 */
void
BSlider::SetHashMarkCount(int32 hashMarkCount)
{
	fHashMarkCount = hashMarkCount;
	Invalidate();
}


/**
 * @brief Returns the number of tick marks configured for the slider.
 *
 * @return The current hash-mark count.
 * @see SetHashMarkCount()
 */
int32
BSlider::HashMarkCount() const
{
	return fHashMarkCount;
}


/**
 * @brief Sets which side(s) of the bar display tick marks.
 *
 * Accepts B_HASH_MARKS_NONE, B_HASH_MARKS_TOP, B_HASH_MARKS_LEFT,
 * B_HASH_MARKS_BOTTOM, B_HASH_MARKS_RIGHT, or B_HASH_MARKS_BOTH.
 * Triggers a repaint.
 *
 * @param where The desired hash-mark placement.
 * @see HashMarks(), SetHashMarkCount()
 */
void
BSlider::SetHashMarks(hash_mark_location where)
{
	fHashMarks = where;
// TODO: enable if the hashmark look is influencing the control size!
//	InvalidateLayout();
	Invalidate();
}


/**
 * @brief Returns the current hash-mark placement setting.
 *
 * @return A hash_mark_location constant indicating where marks are drawn.
 * @see SetHashMarks()
 */
hash_mark_location
BSlider::HashMarks() const
{
	return fHashMarks;
}


/**
 * @brief Sets the visual style of the thumb.
 *
 * Choosing B_BLOCK_THUMB produces a rectangular button; B_TRIANGLE_THUMB
 * produces a triangular pointer. The layout is invalidated because different
 * thumb styles have different frame sizes, and the view is repainted.
 *
 * @param style The desired thumb style.
 * @see Style(), ThumbFrame()
 */
void
BSlider::SetStyle(thumb_style style)
{
	fStyle = style;
	InvalidateLayout();
	Invalidate();
}


/**
 * @brief Returns the current thumb style.
 *
 * @return B_BLOCK_THUMB or B_TRIANGLE_THUMB.
 * @see SetStyle()
 */
thumb_style
BSlider::Style() const
{
	return fStyle;
}


/**
 * @brief Sets the color used to render the slider bar (track).
 *
 * Only the bar-frame rectangle is invalidated for efficient repainting.
 *
 * @param barColor The new bar color.
 * @see BarColor(), SetFillColor()
 */
void
BSlider::SetBarColor(rgb_color barColor)
{
	fBarColor = barColor;
	Invalidate(BarFrame());
}


/**
 * @brief Returns the color used to render the slider bar.
 *
 * @return The current bar color.
 * @see SetBarColor()
 */
rgb_color
BSlider::BarColor() const
{
	return fBarColor;
}


/**
 * @brief Enables or disables the two-tone fill color for the bar.
 *
 * When \a useFill is true the portion of the bar between the minimum end
 * and the thumb is painted in the fill color instead of the bar color,
 * providing a visual progress indication. If \a barColor is not NULL the
 * fill color is also updated.
 *
 * @param useFill  true to enable the fill color, false to use a single color.
 * @param barColor New fill color, or NULL to leave the existing fill color.
 * @see FillColor(), SetFillColor()
 */
void
BSlider::UseFillColor(bool useFill, const rgb_color* barColor)
{
	fUseFillColor = useFill;

	if (useFill && barColor)
		fFillColor = *barColor;

	Invalidate(BarFrame());
}


/**
 * @brief Returns whether the fill color is enabled, and optionally retrieves it.
 *
 * @param barColor If not NULL and the fill color is enabled, receives the
 *                 current fill color.
 * @return true if the fill color is in use, false otherwise.
 * @see UseFillColor()
 */
bool
BSlider::FillColor(rgb_color* barColor) const
{
	if (barColor && fUseFillColor)
		*barColor = fFillColor;

	return fUseFillColor;
}


/**
 * @brief Returns the view used for drawing slider components.
 *
 * When off-screen rendering is compiled in (USE_OFF_SCREEN_VIEW != 0) this
 * returns the off-screen BView backed by a BBitmap. Otherwise it returns the
 * slider itself, so all DrawBar(), DrawThumb(), etc. calls operate directly on
 * the on-screen view.
 *
 * @return The BView into which component-drawing methods should render.
 */
BView*
BSlider::OffscreenView() const
{
#if USE_OFF_SCREEN_VIEW
	return fOffScreenView;
#else
	return (BView*)this;
#endif
}


/**
 * @brief Returns the current slider orientation.
 *
 * @return B_HORIZONTAL or B_VERTICAL.
 * @see SetOrientation()
 */
orientation
BSlider::Orientation() const
{
	return fOrientation;
}


/**
 * @brief Changes the axis along which the thumb travels.
 *
 * Switching between B_HORIZONTAL and B_VERTICAL invalidates the layout
 * (because the min-size geometry changes entirely) and triggers a repaint.
 * No-ops when the new orientation matches the current one.
 *
 * @param posture The new orientation (B_HORIZONTAL or B_VERTICAL).
 * @see Orientation()
 */
void
BSlider::SetOrientation(orientation posture)
{
	if (fOrientation == posture)
		return;

	fOrientation = posture;
	InvalidateLayout();
	Invalidate();
}


/**
 * @brief Returns the thickness of the slider bar in pixels.
 *
 * @return The current bar thickness, always at least 1.0.
 * @see SetBarThickness()
 */
float
BSlider::BarThickness() const
{
	return fBarThickness;
}


/**
 * @brief Sets the cross-axis thickness of the slider bar in pixels.
 *
 * Values below 1.0 are clamped to 1.0; other values are rounded to the
 * nearest pixel. When the thickness actually changes, the dirty region (bar
 * frame plus hash-mark extension) is computed before and after the update
 * and both are invalidated, and the layout is also invalidated because the
 * minimum size may change.
 *
 * @param thickness The desired bar thickness in pixels.
 * @see BarThickness()
 */
void
BSlider::SetBarThickness(float thickness)
{
	if (thickness < 1.0)
		thickness = 1.0;
	else
		thickness = roundf(thickness);

	if (thickness != fBarThickness) {
		// calculate invalid barframe and extend by hashmark size
		float hInset = 0.0;
		float vInset = 0.0;
		if (fOrientation == B_HORIZONTAL)
			vInset = -6.0;
		else
			hInset = -6.0;
		BRect invalid = BarFrame().InsetByCopy(hInset, vInset) | ThumbFrame();

		fBarThickness = thickness;

		invalid = invalid | BarFrame().InsetByCopy(hInset, vInset)
			| ThumbFrame();
		Invalidate(invalid);
		InvalidateLayout();
	}
}


/**
 * @brief Sets the font used for label and limit-label rendering.
 *
 * Updates the off-screen view's font when off-screen rendering is enabled,
 * then invalidates the layout because font metrics affect the minimum size
 * calculation.
 *
 * @param font       The new font to apply.
 * @param properties A bitmask of B_FONT_* flags indicating which properties
 *                   to copy from \a font.
 */
void
BSlider::SetFont(const BFont* font, uint32 properties)
{
	BControl::SetFont(font, properties);

#if USE_OFF_SCREEN_VIEW
	if (fOffScreenView != NULL && fOffScreenBits != NULL && fOffScreenBits->Lock()) {
		fOffScreenView->SetFont(font, properties);
		fOffScreenBits->Unlock();
	}
#endif

	InvalidateLayout();
}


/**
 * @brief Sets both ends of the slider's value range atomically.
 *
 * Only takes effect when \a minimum <= \a maximum. The current value is
 * clamped to the new range and SetValue() is called if it falls outside.
 *
 * @param minimum The new minimum value.
 * @param maximum The new maximum value.
 * @see GetLimits()
 */
void
BSlider::SetLimits(int32 minimum, int32 maximum)
{
	if (minimum <= maximum) {
		fMinValue = minimum;
		fMaxValue = maximum;

		int32 value = Value();
		value = std::max(minimum, value);
		value = std::min(maximum, value);

		if (value != Value())
			SetValue(value);
	}
}


/**
 * @brief Returns the maximum pixel width the update text will ever occupy.
 *
 * The default implementation probes the width at fMaxValue, assuming that the
 * widest string occurs at the largest value. Subclasses that generate update
 * text with different width characteristics should override this method.
 * The result is cached in fMaxUpdateTextWidth and used by _ValidateMinSize()
 * to reserve space in the preferred-size calculation.
 *
 * @return The maximum update-text width in pixels.
 * @see UpdateText(), UpdateTextChanged()
 */
float
BSlider::MaxUpdateTextWidth()
{
	// very simplistic implementation that assumes the string will be widest
	// at the maximum value
	int32 value = Value();
	SetValueNoUpdate(fMaxValue);
	float width = StringWidth(UpdateText());
	SetValueNoUpdate(value);
	// in case the derived class uses a fixed buffer, the contents
	// should be reset for the old value
	UpdateText();

	return width;
}


// #pragma mark - layout related


/**
 * @brief Returns the minimum size the layout system may assign to this slider.
 *
 * Composes the internally computed minimum size (from _ValidateMinSize()) with
 * any explicit minimum size set by the application via SetExplicitMinSize().
 *
 * @return The layout-system minimum size.
 * @see PreferredSize(), MaxSize()
 */
BSize
BSlider::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), _ValidateMinSize());
}


/**
 * @brief Returns the maximum size the layout system may assign to this slider.
 *
 * Sets the dimension along the slider axis to B_SIZE_UNLIMITED (the slider
 * may grow freely in that direction), while keeping the cross-axis dimension
 * fixed at the computed minimum. Composes the result with any explicit maximum
 * set by the application.
 *
 * @return The layout-system maximum size.
 * @see MinSize(), PreferredSize()
 */
BSize
BSlider::MaxSize()
{
	BSize maxSize = _ValidateMinSize();
	if (fOrientation == B_HORIZONTAL)
		maxSize.width = B_SIZE_UNLIMITED;
	else
		maxSize.height = B_SIZE_UNLIMITED;

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), maxSize);
}


/**
 * @brief Returns the preferred size for this slider.
 *
 * Extends the computed minimum size along the slider axis to at least 100
 * pixels, giving the thumb enough room to be draggable. Composes the result
 * with any explicit preferred size set by the application.
 *
 * @return The layout-system preferred size.
 * @see MinSize(), MaxSize(), GetPreferredSize()
 */
BSize
BSlider::PreferredSize()
{
	BSize preferredSize = _ValidateMinSize();
	if (fOrientation == B_HORIZONTAL)
		preferredSize.width = std::max(100.0f, preferredSize.width);
	else
		preferredSize.height = std::max(100.0f, preferredSize.height);

	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), preferredSize);
}


/**
 * @brief Sets a decorative icon bitmap for this slider.
 *
 * Forwards to BControl::SetIcon(). Sliders do not use icons in their default
 * rendering, but the method is provided for binary compatibility and subclass
 * use.
 *
 * @param icon  The icon bitmap, or NULL to remove the icon.
 * @param flags Icon-loading flags (B_TRIM_ICON_BITMAP, etc.).
 * @return B_OK on success, or an error code.
 */
status_t
BSlider::SetIcon(const BBitmap* icon, uint32 flags)
{
	return BControl::SetIcon(icon, flags);
}


/**
 * @brief Called by the layout system when the layout is invalidated.
 *
 * Resets the cached minimum-size sentinel so that _ValidateMinSize() will
 * recompute it on the next query.
 *
 * @param descendants true if child layouts were also invalidated.
 */
void
BSlider::LayoutInvalidated(bool descendants)
{
	// invalidate cached preferred size
	fMinSize.Set(-1, -1);
}


// #pragma mark - private


/**
 * @brief Renders the block (rectangular) thumb via BControlLook.
 *
 * Retrieves the current thumb frame and control flags and delegates to
 * BControlLook::DrawSliderThumb() using the control background color.
 */
void
BSlider::_DrawBlockThumb()
{
	BRect frame = ThumbFrame();
	BView* view = OffscreenView();

	rgb_color base = ui_color(B_CONTROL_BACKGROUND_COLOR);
	uint32 flags = be_control_look->Flags(this);
	be_control_look->DrawSliderThumb(view, frame, frame, base, flags,
		fOrientation);
}


/**
 * @brief Renders the triangular pointer thumb via BControlLook.
 *
 * Retrieves the current thumb frame and control flags and delegates to
 * BControlLook::DrawSliderTriangle(), which draws the triangle pointing
 * toward the bar along the active axis.
 */
void
BSlider::_DrawTriangleThumb()
{
	BRect frame = ThumbFrame();
	BView* view = OffscreenView();
	rgb_color base = ui_color(B_CONTROL_BACKGROUND_COLOR);
	uint32 flags = be_control_look->Flags(this);
	be_control_look->DrawSliderTriangle(view, frame, frame, base, flags,
		fOrientation);
}


/**
 * @brief Returns the current pixel location of the thumb's anchor point.
 *
 * @return The cached fLocation point in local view coordinates.
 * @see _SetLocationForValue()
 */
BPoint
BSlider::_Location() const
{
	return fLocation;
}


/**
 * @brief Updates fLocation so it corresponds to the given integer value.
 *
 * Performs a linear interpolation between _MinPosition() and _MaxPosition()
 * based on where \a value falls in [fMinValue, fMaxValue]. For horizontal
 * sliders only the x coordinate is set; for vertical sliders only y is set.
 *
 * @param value The integer value whose pixel position is to be computed.
 * @see _Location(), SetValue()
 */
void
BSlider::_SetLocationForValue(int32 value)
{
	BPoint loc;
	float range = (float)(fMaxValue - fMinValue);
	if (range == 0)
		range = 1;

	float pos = (float)(value - fMinValue) / range *
		(_MaxPosition() - _MinPosition());

	if (fOrientation == B_HORIZONTAL) {
		loc.x = ceil(_MinPosition() + pos);
		loc.y = 0;
	} else {
		loc.x = 0;
		loc.y = floor(_MaxPosition() - pos);
	}
	fLocation = loc;
}


/**
 * @brief Returns the pixel coordinate of the low end of the draggable range.
 *
 * For horizontal sliders this is BarFrame().left + 1; for vertical sliders it
 * is BarFrame().bottom - 1 (higher y means lower value in screen space).
 *
 * @return The minimum draggable pixel position along the active axis.
 * @see _MaxPosition()
 */
float
BSlider::_MinPosition() const
{
	if (fOrientation == B_HORIZONTAL)
		return BarFrame().left + 1.0f;

	return BarFrame().bottom - 1.0f;
}


/**
 * @brief Returns the pixel coordinate of the high end of the draggable range.
 *
 * For horizontal sliders this is BarFrame().right - 1; for vertical sliders
 * it is BarFrame().top + 1.
 *
 * @return The maximum draggable pixel position along the active axis.
 * @see _MinPosition()
 */
float
BSlider::_MaxPosition() const
{
	if (fOrientation == B_HORIZONTAL)
		return BarFrame().right - 1.0f;

	return BarFrame().top + 1.0f;
}


/**
 * @brief Computes and caches the minimum size required by the slider's content.
 *
 * Returns the cached value immediately when fMinSize.width >= 0. Otherwise
 * measures all visible text areas using the current font metrics, computes
 * the minimum width and height needed to display the bar, thumb, hash marks,
 * label, limit labels, and update text, stores the result in fMinSize, and
 * calls ResetLayoutInvalidation() to mark the cache as valid.
 *
 * @return The newly computed (or previously cached) minimum size.
 * @see LayoutInvalidated(), MinSize()
 */
BSize
BSlider::_ValidateMinSize()
{
	if (fMinSize.width >= 0) {
		// the preferred size is up to date
		return fMinSize;
	}

	font_height fontHeight;
	GetFontHeight(&fontHeight);

	float width = 0.0f;
	float height = 0.0f;

	if (fMaxUpdateTextWidth < 0.0f)
		fMaxUpdateTextWidth = MaxUpdateTextWidth();

	if (Orientation() == B_HORIZONTAL) {
		height = 12.0f + fBarThickness;
		int32 rows = 0;

		float labelWidth = 0;
		int32 labelRows = 0;
		float labelSpacing = StringWidth("M") * 2;
		if (Label() != NULL) {
			labelWidth = StringWidth(Label());
			labelRows = 1;
		}
		if (fMaxUpdateTextWidth > 0.0f) {
			if (labelWidth > 0)
				labelWidth += labelSpacing;

			labelWidth += fMaxUpdateTextWidth;
			labelRows = 1;
		}
		rows += labelRows;

		if (MinLimitLabel() != NULL)
			width = StringWidth(MinLimitLabel());

		if (MaxLimitLabel() != NULL) {
			// some space between the labels
			if (MinLimitLabel() != NULL)
				width += labelSpacing;

			width += StringWidth(MaxLimitLabel());
		}

		if (labelWidth > width)
			width = labelWidth;

		if (width < 32.0f)
			width = 32.0f;

		if (MinLimitLabel() || MaxLimitLabel())
			rows++;

		height += rows * (ceilf(fontHeight.ascent)
			+ ceilf(fontHeight.descent) + 4.0);
	} else {
		// B_VERTICAL
		width = 12.0f + fBarThickness;
		height = 32.0f;

		float lineHeightNoLeading = ceilf(fontHeight.ascent)
			+ ceilf(fontHeight.descent);
		float lineHeight = lineHeightNoLeading + ceilf(fontHeight.leading);

		// find largest label
		float labelWidth = 0;
		if (Label() != NULL) {
			labelWidth = StringWidth(Label());
			height += lineHeightNoLeading;
		}
		if (MaxLimitLabel() != NULL) {
			labelWidth = std::max(labelWidth, StringWidth(MaxLimitLabel()));
			height += Label() ? lineHeight : lineHeightNoLeading;
		}
		if (MinLimitLabel() != NULL) {
			labelWidth = std::max(labelWidth, StringWidth(MinLimitLabel()));
			height += lineHeightNoLeading;
		}
		if (fMaxUpdateTextWidth > 0.0f) {
			labelWidth = std::max(labelWidth, fMaxUpdateTextWidth);
			height += MinLimitLabel() ? lineHeight : lineHeightNoLeading;
		}

		width = std::max(labelWidth, width);
	}

	fMinSize.width = width;
	fMinSize.height = height;

	ResetLayoutInvalidation();

	return fMinSize;
}


// #pragma mark - FBC padding

void BSlider::_ReservedSlider6() {}
void BSlider::_ReservedSlider7() {}
void BSlider::_ReservedSlider8() {}
void BSlider::_ReservedSlider9() {}
void BSlider::_ReservedSlider10() {}
void BSlider::_ReservedSlider11() {}
void BSlider::_ReservedSlider12() {}


/** @brief Private copy-assignment operator; copying BSlider objects is not supported. */
BSlider&
BSlider::operator=(const BSlider&)
{
	return *this;
}


//	#pragma mark - BeOS compatibility


#if __GNUC__ < 3

extern "C" void
GetLimits__7BSliderPlT1(BSlider* slider, int32* minimum, int32* maximum)
{
	slider->GetLimits(minimum, maximum);
}


extern "C" void
_ReservedSlider4__7BSlider(BSlider* slider, int32 minimum, int32 maximum)
{
	slider->BSlider::SetLimits(minimum, maximum);
}

extern "C" float
_ReservedSlider5__7BSlider(BSlider* slider)
{
	return slider->BSlider::MaxUpdateTextWidth();
}


extern "C" void
_ReservedSlider1__7BSlider(BSlider* slider, orientation _orientation)
{
	slider->BSlider::SetOrientation(_orientation);
}


extern "C" void
_ReservedSlider2__7BSlider(BSlider* slider, float thickness)
{
	slider->BSlider::SetBarThickness(thickness);
}


extern "C" void
_ReservedSlider3__7BSlider(BSlider* slider, const BFont* font,
	uint32 properties)
{
	slider->BSlider::SetFont(font, properties);
}


#endif	// __GNUC__ < 3


extern "C" void
B_IF_GCC_2(InvalidateLayout__7BSliderb, _ZN7BSlider16InvalidateLayoutEb)(
	BView* view, bool descendants)
{
	perform_data_layout_invalidated data;
	data.descendants = descendants;

	view->Perform(PERFORM_CODE_LAYOUT_INVALIDATED, &data);
}
