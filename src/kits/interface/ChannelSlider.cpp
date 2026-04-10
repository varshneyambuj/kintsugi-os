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
 *   Copyright 2005-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini (stefano.ceccherini@gmail.com)
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file ChannelSlider.cpp
 * @brief Implementation of BChannelSlider, a multi-channel slider control
 *
 * BChannelSlider renders one or more slider thumbs in a shared track, allowing
 * independent adjustment of multiple related values such as stereo audio levels.
 * Supports both horizontal and vertical orientations.
 *
 * @see BChannelControl, BSlider
 */


#include <ChannelSlider.h>

#include <new>

#include <Bitmap.h>
#include <ControlLook.h>
#include <Debug.h>
#include <PropertyInfo.h>
#include <Screen.h>
#include <Window.h>

#include <binary_compatibility/Support.h>


/** @brief Raw 8-bit indexed-colour bitmap data for the vertical slider knob (12x15 pixels). */
const static unsigned char
kVerticalKnobData[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0xcb, 0xcb, 0xcb, 0xcb, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x12,
	0xff, 0xff, 0xff, 0xff, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0xff
};


/** @brief Raw 8-bit indexed-colour bitmap data for the horizontal slider knob (16x12 pixels). */
const static unsigned char
kHorizontalKnobData[] = {
	0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0xff, 0xff, 0xff, 0x00, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0xff, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0xcb, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x00, 0x12, 0xff, 0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0xcb,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12, 0xff, 0xff, 0x00, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0xcb, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0xcb, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x00, 0x12, 0xff, 0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12, 0xff, 0xff, 0x00, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12, 0xff,
	0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x0c, 0x12, 0xff, 0xff, 0xff, 0xff, 0xff, 0x12, 0x12, 0x12, 0x12,
	0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};


/** @brief Scripting property table for BChannelSlider. */
static property_info
sPropertyInfo[] = {
	{ "Orientation",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, NULL, 0, { B_INT32_TYPE }
	},

	{ 0 }
};


/** @brief Padding (in pixels) applied around labels and the thumb track. */
const static float kPadding = 3.0;


/**
 * @brief Construct a BChannelSlider with an explicit frame rectangle.
 *
 * Orientation is inferred from the frame's aspect ratio: a frame that is
 * taller than it is wide produces a vertical slider.
 *
 * @param area       The view's frame rectangle in its parent's coordinate system.
 * @param name       The internal name of the view.
 * @param label      The user-visible label rendered above the slider track.
 * @param model      The BMessage sent when the slider is invoked, or NULL.
 * @param channels   Number of independent channel thumbs to display.
 * @param resizeMode Resizing mode flags passed to BChannelControl.
 * @param flags      View flags passed to BChannelControl.
 * @see BChannelControl::BChannelControl()
 */
BChannelSlider::BChannelSlider(BRect area, const char* name, const char* label,
	BMessage* model, int32 channels, uint32 resizeMode, uint32 flags)
	: BChannelControl(area, name, label, model, channels, resizeMode, flags)
{
	_InitData();
}


/**
 * @brief Construct a BChannelSlider with an explicit frame and orientation.
 *
 * @param area        The view's frame rectangle in its parent's coordinate system.
 * @param name        The internal name of the view.
 * @param label       The user-visible label rendered alongside the slider track.
 * @param model       The BMessage sent when the slider is invoked, or NULL.
 * @param orientation Initial orientation: B_VERTICAL or B_HORIZONTAL.
 * @param channels    Number of independent channel thumbs to display.
 * @param resizeMode  Resizing mode flags passed to BChannelControl.
 * @param flags       View flags passed to BChannelControl.
 * @see SetOrientation()
 */
BChannelSlider::BChannelSlider(BRect area, const char* name, const char* label,
	BMessage* model, orientation orientation, int32 channels,
		uint32 resizeMode, uint32 flags)
	: BChannelControl(area, name, label, model, channels, resizeMode, flags)

{
	_InitData();
	SetOrientation(orientation);
}


/**
 * @brief Construct a layout-friendly BChannelSlider without an explicit frame.
 *
 * @param name        The internal name of the view.
 * @param label       The user-visible label rendered alongside the slider track.
 * @param model       The BMessage sent when the slider is invoked, or NULL.
 * @param orientation Initial orientation: B_VERTICAL or B_HORIZONTAL.
 * @param channels    Number of independent channel thumbs to display.
 * @param flags       View flags passed to BChannelControl.
 * @see SetOrientation()
 */
BChannelSlider::BChannelSlider(const char* name, const char* label,
	BMessage* model, orientation orientation, int32 channels,
		uint32 flags)
	: BChannelControl(name, label, model, channels, flags)

{
	_InitData();
	SetOrientation(orientation);
}


/**
 * @brief Unarchive constructor — restore a BChannelSlider from a BMessage.
 *
 * Reads the "_orient" field from the archive to restore the slider's
 * orientation. All other state is restored by _InitData() and the parent
 * unarchive constructor.
 *
 * @param archive The BMessage produced by a previous call to Archive().
 * @see Archive()
 * @see Instantiate()
 */
BChannelSlider::BChannelSlider(BMessage* archive)
	: BChannelControl(archive)
{
	_InitData();

	orientation orient;
	if (archive->FindInt32("_orient", (int32*)&orient) == B_OK)
		SetOrientation(orient);
}


/**
 * @brief Destroy the BChannelSlider and release all owned resources.
 *
 * Deletes the offscreen backing bitmap, knob bitmaps, and the initial-values
 * snapshot array used during mouse tracking.
 */
BChannelSlider::~BChannelSlider()
{
	delete fBacking;
	delete fLeftKnob;
	delete fMidKnob;
	delete fRightKnob;
	delete[] fInitialValues;
}


/**
 * @brief Instantiate a BChannelSlider from an archived BMessage.
 *
 * @param archive The archive to instantiate from.
 * @return A newly allocated BChannelSlider, or NULL if the archive is invalid
 *         or allocation fails.
 * @see Archive()
 */
BArchivable*
BChannelSlider::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BChannelSlider"))
		return new (std::nothrow) BChannelSlider(archive);

	return NULL;
}


/**
 * @brief Archive the slider's state into a BMessage.
 *
 * Stores the current orientation under the "_orient" key in addition to all
 * data stored by BChannelControl::Archive().
 *
 * @param into The message to fill with archived data.
 * @param deep If true, child views are archived as well.
 * @return B_OK on success, or a negative error code on failure.
 * @see BChannelSlider(BMessage*)
 */
status_t
BChannelSlider::Archive(BMessage* into, bool deep) const
{
	status_t status = BChannelControl::Archive(into, deep);
	if (status == B_OK)
		status = into->AddInt32("_orient", (int32)Orientation());

	return status;
}


/**
 * @brief Hook called when the view is attached to a window.
 *
 * Adopts the parent view's colours so the slider background blends
 * seamlessly, then delegates to BChannelControl::AttachedToWindow().
 */
void
BChannelSlider::AttachedToWindow()
{
	AdoptParentColors();
	BChannelControl::AttachedToWindow();
}


/**
 * @brief Hook called after all sibling views have been attached to the window.
 *
 * Delegates to BChannelControl::AllAttached().
 */
void
BChannelSlider::AllAttached()
{
	BChannelControl::AllAttached();
}


/**
 * @brief Hook called when the view is detached from its window.
 *
 * Delegates to BChannelControl::DetachedFromWindow().
 */
void
BChannelSlider::DetachedFromWindow()
{
	BChannelControl::DetachedFromWindow();
}


/**
 * @brief Hook called after all sibling views have been detached from the window.
 *
 * Delegates to BChannelControl::AllDetached().
 */
void
BChannelSlider::AllDetached()
{
	BChannelControl::AllDetached();
}


/**
 * @brief Dispatch an incoming BMessage, handling colour and orientation changes.
 *
 * Intercepts B_COLORS_UPDATED to refresh the backing bitmap's low colour, and
 * handles B_GET_PROPERTY / B_SET_PROPERTY scripting for the "Orientation"
 * property. All other messages are forwarded to BChannelControl::MessageReceived().
 *
 * @param message The message to process.
 */
void
BChannelSlider::MessageReceived(BMessage* message)
{
	if (message->what == B_COLORS_UPDATED
		&& fBacking != NULL && fBackingView != NULL) {
		rgb_color color;
		if (message->FindColor(ui_color_name(B_PANEL_BACKGROUND_COLOR), &color)
				== B_OK
			&& fBacking->Lock()) {

			if (fBackingView->LockLooper()) {
				fBackingView->SetLowColor(color);
				fBackingView->UnlockLooper();
			}
			fBacking->Unlock();
		}
	}

	switch (message->what) {
		case B_SET_PROPERTY: {
		case B_GET_PROPERTY:
			BMessage reply(B_REPLY);
			int32 index = 0;
			BMessage specifier;
			int32 what = 0;
			const char* property = NULL;
			bool handled = false;
			status_t status = message->GetCurrentSpecifier(&index, &specifier,
				&what, &property);
			BPropertyInfo propInfo(sPropertyInfo);
			if (status == B_OK
				&& propInfo.FindMatch(message, index, &specifier, what,
					property) >= 0) {
				handled = true;
				if (message->what == B_SET_PROPERTY) {
					orientation orient;
					if (specifier.FindInt32("data", (int32*)&orient) == B_OK) {
						SetOrientation(orient);
						Invalidate(Bounds());
					}
				} else if (message->what == B_GET_PROPERTY)
					reply.AddInt32("result", (int32)Orientation());
				else
					status = B_BAD_SCRIPT_SYNTAX;
			}

			if (handled) {
				reply.AddInt32("error", status);
				message->SendReply(&reply);
			} else {
				BChannelControl::MessageReceived(message);
			}
		}	break;

		default:
			BChannelControl::MessageReceived(message);
			break;
	}
}


/**
 * @brief Render the slider, including all channel thumbs, labels, and track.
 *
 * Calls _DrawThumbs() to paint the offscreen backing bitmap and then blit it
 * to the view, and draws the control label, min-limit label, and max-limit
 * label at the appropriate positions for the current orientation.
 *
 * @param updateRect The region of the view that needs repainting.
 */
void
BChannelSlider::Draw(BRect updateRect)
{
	_UpdateFontDimens();
	_DrawThumbs();

	SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
	BRect bounds(Bounds());
	if (Label()) {
		float labelWidth = StringWidth(Label());
		DrawString(Label(), BPoint((bounds.Width() - labelWidth) / 2.0,
			fBaseLine));
	}

	if (MinLimitLabel()) {
		if (fIsVertical) {
			if (MinLimitLabel()) {
				float x = (bounds.Width() - StringWidth(MinLimitLabel()))
					/ 2.0;
				DrawString(MinLimitLabel(), BPoint(x, bounds.bottom
					- kPadding));
			}
		} else {
			if (MinLimitLabel()) {
				DrawString(MinLimitLabel(), BPoint(kPadding, bounds.bottom
					- kPadding));
			}
		}
	}

	if (MaxLimitLabel()) {
		if (fIsVertical) {
			if (MaxLimitLabel()) {
				float x = (bounds.Width() - StringWidth(MaxLimitLabel()))
					/ 2.0;
				DrawString(MaxLimitLabel(), BPoint(x, 2 * fLineFeed));
			}
		} else {
			if (MaxLimitLabel()) {
				DrawString(MaxLimitLabel(), BPoint(bounds.right - kPadding
					- StringWidth(MaxLimitLabel()), bounds.bottom - kPadding));
			}
		}
	}
}


/**
 * @brief Handle a mouse-button press, beginning thumb tracking.
 *
 * Determines which channel thumb the cursor is over and, if the window uses
 * asynchronous controls, starts asynchronous tracking by calling
 * _MouseMovedCommon() and setting the mouse event mask. For synchronous
 * windows a polling loop runs until all buttons are released.
 *
 * Holding the secondary mouse button moves all channel thumbs together;
 * clicking with the primary button moves only the channel under the cursor.
 *
 * @param where The cursor position in the view's local coordinates.
 */
void
BChannelSlider::MouseDown(BPoint where)
{
	if (!IsEnabled())
		BControl::MouseDown(where);
	else {
		fCurrentChannel = -1;
		fMinPoint = 0;

		// Search the channel on which the mouse was over
		int32 numChannels = CountChannels();
		for (int32 channel = 0; channel < numChannels; channel++) {
			BRect frame = ThumbFrameFor(channel);
			frame.OffsetBy(fClickDelta);

			float range = ThumbRangeFor(channel);
			if (fIsVertical) {
				fMinPoint = frame.top + frame.Height() / 2;
				frame.bottom += range;
			} else {
				// TODO: Fix this, the clickzone isn't perfect
				frame.right += range;
				fMinPoint = frame.Width();
			}

			// Click was on a slider.
			if (frame.Contains(where)) {
				fCurrentChannel = channel;
				SetCurrentChannel(channel);
				break;
			}
		}

		// Click wasn't on a slider. Bail out.
		if (fCurrentChannel == -1)
			return;

		uint32 buttons = 0;
		BMessage* currentMessage = Window()->CurrentMessage();
		if (currentMessage != NULL)
			currentMessage->FindInt32("buttons", (int32*)&buttons);

		fAllChannels = (buttons & B_SECONDARY_MOUSE_BUTTON) == 0;

		if (fInitialValues != NULL && fAllChannels) {
			delete[] fInitialValues;
			fInitialValues = NULL;
		}

		if (fInitialValues == NULL)
			fInitialValues = new (std::nothrow) int32[numChannels];

		if (fInitialValues) {
			if (fAllChannels) {
				for (int32 i = 0; i < numChannels; i++)
					fInitialValues[i] = ValueFor(i);
			} else {
				fInitialValues[fCurrentChannel] = ValueFor(fCurrentChannel);
			}
		}

		if (Window()->Flags() & B_ASYNCHRONOUS_CONTROLS) {
			if (!IsTracking()) {
				SetTracking(true);
				_DrawThumbs();
				Flush();
			}

			_MouseMovedCommon(where, B_ORIGIN);
			SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS |
				B_NO_POINTER_HISTORY);
		} else {
			do {
				snooze(30000);
				GetMouse(&where, &buttons);
				_MouseMovedCommon(where, B_ORIGIN);
			} while (buttons != 0);
			_FinishChange();
			fCurrentChannel = -1;
			fAllChannels = false;
		}
	}
}


/**
 * @brief Handle a mouse-button release, finalising a thumb drag.
 *
 * If the control was in tracking mode, calls _FinishChange() to invoke the
 * modification message and then resets tracking state. Otherwise delegates
 * to BChannelControl::MouseUp().
 *
 * @param where The cursor position in the view's local coordinates at release.
 */
void
BChannelSlider::MouseUp(BPoint where)
{
	if (IsEnabled() && IsTracking()) {
		_FinishChange();
		SetTracking(false);
		fAllChannels = false;
		fCurrentChannel = -1;
		fMinPoint = 0;
	} else {
		BChannelControl::MouseUp(where);
	}
}


/**
 * @brief Handle cursor movement, updating the tracked thumb position.
 *
 * While tracking is active, delegates to _MouseMovedCommon() to compute and
 * apply the new channel value. Otherwise forwards to
 * BChannelControl::MouseMoved().
 *
 * @param where   Current cursor position in the view's local coordinates.
 * @param code    Transit code (B_ENTERED_VIEW, B_EXITED_VIEW, etc.).
 * @param message A drag-and-drop message if a drag is in progress, or NULL.
 */
void
BChannelSlider::MouseMoved(BPoint where, uint32 code, const BMessage* message)
{
	if (IsEnabled() && IsTracking())
		_MouseMovedCommon(where, B_ORIGIN);
	else
		BChannelControl::MouseMoved(where, code, message);
}


/**
 * @brief Hook called when the window's active state changes.
 *
 * Delegates to BChannelControl::WindowActivated().
 *
 * @param state true if the window became active, false if it deactivated.
 */
void
BChannelSlider::WindowActivated(bool state)
{
	BChannelControl::WindowActivated(state);
}


/**
 * @brief Handle a key-down event.
 *
 * Delegates to BControl::KeyDown() so that standard keyboard interaction
 * (tab navigation, etc.) is preserved.
 *
 * @param bytes    Pointer to the UTF-8 byte sequence of the key pressed.
 * @param numBytes Length of the byte sequence in @p bytes.
 */
void
BChannelSlider::KeyDown(const char* bytes, int32 numBytes)
{
	BControl::KeyDown(bytes, numBytes);
}


/**
 * @brief Handle a key-up event.
 *
 * Delegates to BView::KeyUp().
 *
 * @param bytes    Pointer to the UTF-8 byte sequence of the key released.
 * @param numBytes Length of the byte sequence in @p bytes.
 */
void
BChannelSlider::KeyUp(const char* bytes, int32 numBytes)
{
	BView::KeyUp(bytes, numBytes);
}


/**
 * @brief Hook called when the view is resized.
 *
 * Discards the cached offscreen backing bitmap so that _DrawThumbs() will
 * allocate a correctly-sized one on the next draw cycle, then invalidates the
 * entire view bounds.
 *
 * @param newWidth  New width of the view in pixels.
 * @param newHeight New height of the view in pixels.
 */
void
BChannelSlider::FrameResized(float newWidth, float newHeight)
{
	BChannelControl::FrameResized(newWidth, newHeight);

	delete fBacking;
	fBacking = NULL;

	Invalidate(Bounds());
}


/**
 * @brief Set the font used to render labels.
 *
 * Delegates to BChannelControl::SetFont().
 *
 * @param font The new font.
 * @param mask Bitmask of font attributes to change.
 */
void
BChannelSlider::SetFont(const BFont* font, uint32 mask)
{
	BChannelControl::SetFont(font, mask);
}


/**
 * @brief Give or remove keyboard focus from the slider.
 *
 * Resets the focus channel to -1 when focus is gained so the highlight
 * begins from a neutral state.
 *
 * @param focusState true to give focus, false to remove it.
 */
void
BChannelSlider::MakeFocus(bool focusState)
{
	if (focusState && !IsFocus())
		fFocusChannel = -1;
	BChannelControl::MakeFocus(focusState);
}


/**
 * @brief Return the slider's preferred width and height.
 *
 * For vertical sliders the preferred width accommodates all channel thumbs
 * side by side plus the widest label; the height accounts for three lines of
 * text, padding, and a fixed track length. For horizontal sliders the
 * preferred width accommodates the widest label pair; the height stacks all
 * channel thumbs.
 *
 * @param width  Receives the preferred width in pixels.
 * @param height Receives the preferred height in pixels.
 */
void
BChannelSlider::GetPreferredSize(float* width, float* height)
{
	_UpdateFontDimens();

	if (fIsVertical) {
		*width = 11.0 * CountChannels();
		*width = max_c(*width, ceilf(StringWidth(Label())));
		*width = max_c(*width, ceilf(StringWidth(MinLimitLabel())));
		*width = max_c(*width, ceilf(StringWidth(MaxLimitLabel())));
		*width += kPadding * 2.0;

		*height = (fLineFeed * 3.0) + (kPadding * 2.0) + 147.0;
	} else {
		*width = max_c(64.0, ceilf(StringWidth(Label())));
		*width = max_c(*width, ceilf(StringWidth(MinLimitLabel()))
			+ ceilf(StringWidth(MaxLimitLabel())) + 10.0);
		*width += kPadding * 2.0;

		*height = 11.0 * CountChannels() + (fLineFeed * 2.0)
			+ (kPadding * 2.0);
	}
}


/**
 * @brief Resolve a scripting specifier to the handler that owns the property.
 *
 * Checks the local property table (currently "Orientation") first; unrecognised
 * specifiers are forwarded to BChannelControl::ResolveSpecifier().
 *
 * @param message   The scripting message.
 * @param index     Index of the current specifier in the specifier stack.
 * @param specifier The current specifier message.
 * @param form      Specifier type constant.
 * @param property  Name of the property being addressed.
 * @return The BHandler responsible for the property.
 */
BHandler*
BChannelSlider::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 form, const char* property)
{
	BHandler* target = this;
	BPropertyInfo propertyInfo(sPropertyInfo);
	if (propertyInfo.FindMatch(message, index, specifier, form,
		property) != B_OK) {
		target = BChannelControl::ResolveSpecifier(message, index, specifier,
			form, property);
	}
	return target;
}


/**
 * @brief Fill a BMessage with the scripting suites supported by this slider.
 *
 * Adds the "suite/vnd.Be-channel-slider" suite string and its property
 * descriptor list, then delegates to BChannelControl::GetSupportedSuites().
 *
 * @param data The message to populate with suite information.
 * @return B_OK on success, B_BAD_VALUE if @p data is NULL, or a negative error
 *         code if adding suite information fails.
 */
status_t
BChannelSlider::GetSupportedSuites(BMessage* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t err = data->AddString("suites", "suite/vnd.Be-channel-slider");

	BPropertyInfo propInfo(sPropertyInfo);
	if (err == B_OK)
		err = data->AddFlat("messages", &propInfo);

	if (err == B_OK)
		return BChannelControl::GetSupportedSuites(data);
	return err;
}


/**
 * @brief Enable or disable the slider.
 *
 * Delegates to BChannelControl::SetEnabled() and triggers a redraw.
 *
 * @param on true to enable the slider, false to disable it.
 */
void
BChannelSlider::SetEnabled(bool on)
{
	BChannelControl::SetEnabled(on);
}


/**
 * @brief Return the current orientation of the slider.
 *
 * @return B_VERTICAL if the slider tracks run top-to-bottom, B_HORIZONTAL
 *         if they run left-to-right.
 * @see SetOrientation()
 */
orientation
BChannelSlider::Orientation() const
{
	return fIsVertical ? B_VERTICAL : B_HORIZONTAL;
}


/**
 * @brief Set the orientation of the slider.
 *
 * If the new orientation differs from the current one, the layout is
 * invalidated and the view is redrawn.
 *
 * @param orientation B_VERTICAL or B_HORIZONTAL.
 * @see Orientation()
 */
void
BChannelSlider::SetOrientation(orientation orientation)
{
	bool isVertical = orientation == B_VERTICAL;
	if (isVertical != fIsVertical) {
		fIsVertical = isVertical;
		InvalidateLayout();
		Invalidate(Bounds());
	}
}


/**
 * @brief Return the maximum number of channels this slider supports.
 *
 * @return 32 (the hard-coded limit for BChannelSlider).
 * @see BChannelControl::MaxChannelCount()
 */
int32
BChannelSlider::MaxChannelCount() const
{
	return 32;
}


/**
 * @brief Report whether this slider supports per-channel limit configuration.
 *
 * @return false — BChannelSlider uses a single shared limit for all channels.
 * @see BChannelControl::SetLimitsFor()
 */
bool
BChannelSlider::SupportsIndividualLimits() const
{
	return false;
}


/**
 * @brief Draw a single channel's groove and thumb into an offscreen view.
 *
 * Computes the groove start and end points from @p area and the current
 * orientation, calls DrawGroove() to paint the track, then calls DrawThumb()
 * to paint the knob at the position corresponding to the channel's current
 * value.
 *
 * @param into    The offscreen BView to draw into.
 * @param channel 0-based index of the channel to draw.
 * @param area    The bounding rectangle allocated to this channel.
 * @param pressed true if the thumb is currently being dragged by the user.
 * @see DrawGroove()
 * @see DrawThumb()
 */
void
BChannelSlider::DrawChannel(BView* into, int32 channel, BRect area,
	bool pressed)
{
	float hCenter = area.Width() / 2;
	float vCenter = area.Height() / 2;

	BPoint leftTop;
	BPoint bottomRight;
	if (fIsVertical) {
		leftTop.Set(area.left + hCenter, area.top + vCenter);
		bottomRight.Set(leftTop.x, leftTop.y + ThumbRangeFor(channel));
	} else {
		leftTop.Set(area.left, area.top + vCenter);
		bottomRight.Set(area.left + ThumbRangeFor(channel), leftTop.y);
	}

	DrawGroove(into, channel, leftTop, bottomRight);

	BPoint thumbLocation = leftTop;
	if (fIsVertical)
		thumbLocation.y += ThumbDeltaFor(channel);
	else
		thumbLocation.x += ThumbDeltaFor(channel);

	DrawThumb(into, channel, thumbLocation, pressed);
}


/**
 * @brief Draw the slider groove (track) between two points.
 *
 * Inflates the groove rectangle slightly and delegates to
 * be_control_look->DrawSliderBar() to paint the recessed track.
 *
 * @param into        The BView to draw into (must not be NULL).
 * @param channel     0-based channel index (currently unused, reserved for
 *                    per-channel groove styling).
 * @param leftTop     Start point of the groove centre line.
 * @param bottomRight End point of the groove centre line.
 */
void
BChannelSlider::DrawGroove(BView* into, int32 channel, BPoint leftTop,
	BPoint bottomRight)
{
	ASSERT(into != NULL);
	BRect rect(leftTop, bottomRight);

	rect.InsetBy(-2.5, -2.5);
	rect.left = floorf(rect.left);
	rect.top = floorf(rect.top);
	rect.right = floorf(rect.right);
	rect.bottom = floorf(rect.bottom);
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color barColor = be_control_look->SliderBarColor(base);
	uint32 flags = 0;
	be_control_look->DrawSliderBar(into, rect, rect, base,
		barColor, flags, Orientation());
}


/**
 * @brief Draw a single channel's thumb knob at the given position.
 *
 * Retrieves the thumb bitmap via ThumbFor(), centres the bitmap over
 * @p where, and delegates to be_control_look->DrawSliderThumb() for the
 * actual rendering.
 *
 * @param into    The BView to draw into (must not be NULL).
 * @param channel 0-based index of the channel whose thumb is being drawn.
 * @param where   Centre point of the thumb in the view's local coordinates.
 * @param pressed true if the thumb is currently pressed/dragged.
 * @see ThumbFor()
 */
void
BChannelSlider::DrawThumb(BView* into, int32 channel, BPoint where,
	bool pressed)
{
	ASSERT(into != NULL);

	const BBitmap* thumb = ThumbFor(channel, pressed);
	if (thumb == NULL)
		return;

	BRect bitmapBounds(thumb->Bounds());
	where.x -= bitmapBounds.right / 2.0;
	where.y -= bitmapBounds.bottom / 2.0;

	BRect rect(bitmapBounds.OffsetToCopy(where));
	rect.InsetBy(1, 1);
	rect.left = floorf(rect.left);
	rect.top = floorf(rect.top);
	rect.right = ceilf(rect.right + 0.5);
	rect.bottom = ceilf(rect.bottom + 0.5);
	rgb_color base = ui_color(B_CONTROL_BACKGROUND_COLOR);
	uint32 flags = 0;
	be_control_look->DrawSliderThumb(into, rect, rect, base,
		flags, Orientation());
}


/**
 * @brief Return the thumb bitmap for a given channel and pressed state.
 *
 * Lazily creates fLeftKnob on first call by decoding the appropriate knob
 * data constant (kVerticalKnobData or kHorizontalKnobData) into a BBitmap.
 * The same bitmap is returned for every channel; @p pressed is currently
 * unused but reserved for future visual differentiation.
 *
 * @param channel 0-based channel index (currently unused).
 * @param pressed true if the thumb is pressed (currently unused).
 * @return A pointer to the shared thumb BBitmap, or NULL if allocation failed.
 * @see DrawThumb()
 */
const BBitmap*
BChannelSlider::ThumbFor(int32 channel, bool pressed)
{
	if (fLeftKnob != NULL)
		return fLeftKnob;

	if (fIsVertical) {
		fLeftKnob = new (std::nothrow) BBitmap(BRect(0, 0, 11, 14),
			B_CMAP8);
		if (fLeftKnob != NULL) {
			fLeftKnob->SetBits(kVerticalKnobData,
					sizeof(kVerticalKnobData), 0, B_CMAP8);
		}
	} else {
		fLeftKnob = new (std::nothrow) BBitmap(BRect(0, 0, 14, 11),
			B_CMAP8);
		if (fLeftKnob != NULL) {
			fLeftKnob->SetBits(kHorizontalKnobData,
					sizeof(kHorizontalKnobData), 0, B_CMAP8);
		}
	}

	return fLeftKnob;
}


/**
 * @brief Return the bounding rectangle of a channel's thumb in its rest position.
 *
 * The frame is computed from the thumb bitmap dimensions offset by the
 * channel index and the current font metrics. It represents the thumb's
 * position when the channel value is at its minimum (for horizontal sliders)
 * or maximum (for vertical sliders).
 *
 * @param channel 0-based index of the channel to query.
 * @return The thumb's bounding BRect in the view's local coordinate system.
 * @see ThumbRangeFor()
 * @see ThumbDeltaFor()
 */
BRect
BChannelSlider::ThumbFrameFor(int32 channel)
{
	_UpdateFontDimens();

	BRect frame(0.0, 0.0, 0.0, 0.0);
	const BBitmap* thumb = ThumbFor(channel, false);
	if (thumb != NULL) {
		frame = thumb->Bounds();
		if (fIsVertical) {
			frame.OffsetBy(channel * frame.Width(), (frame.Height() / 2.0) -
				(kPadding * 2.0) - 1.0);
		} else {
			frame.OffsetBy(frame.Width() / 2.0, channel * frame.Height()
				+ 1.0);
		}
	}
	return frame;
}


/**
 * @brief Return the pixel offset of a channel's thumb from its start position.
 *
 * Converts the channel's current value (relative to its min/max range) into
 * a pixel distance along the track. For vertical sliders the delta is
 * measured from the top, so higher values produce smaller deltas.
 *
 * @param channel 0-based index of the channel to query.
 * @return Pixel offset of the thumb from the start of the track, or 0.0 if
 *         @p channel is out of range.
 * @see ThumbRangeFor()
 * @see ThumbFrameFor()
 */
float
BChannelSlider::ThumbDeltaFor(int32 channel)
{
	float delta = 0.0;
	if (channel >= 0 && channel < MaxChannelCount()) {
		float range = ThumbRangeFor(channel);
		int32 limitRange = MaxLimitList()[channel] - MinLimitList()[channel];
		delta = (ValueList()[channel] - MinLimitList()[channel]) * range
			/ limitRange;

		if (fIsVertical)
			delta = range - delta;
	}

	return delta;
}


/**
 * @brief Return the total pixel travel distance available to a channel's thumb.
 *
 * The range is the track length minus one thumb height/width and text-label
 * space. It equals the maximum value ThumbDeltaFor() can return for this
 * channel.
 *
 * @param channel 0-based index of the channel to query.
 * @return Available travel in pixels.
 * @see ThumbDeltaFor()
 * @see ThumbFrameFor()
 */
float
BChannelSlider::ThumbRangeFor(int32 channel)
{
	_UpdateFontDimens();

	float range = 0;
	BRect bounds = Bounds();
	BRect frame = ThumbFrameFor(channel);
	if (fIsVertical) {
		// *height = (fLineFeed * 3.0) + (kPadding * 2.0) + 100.0;
		range = bounds.Height() - frame.Height() - (fLineFeed * 3.0) -
			(kPadding * 2.0);
	} else {
		// *width = some width + kPadding * 2.0;
		range = bounds.Width() - frame.Width() - (kPadding * 2.0);
	}
	return range;
}


/**
 * @brief Update the tooltip text to display the current channel value.
 *
 * Formats @p currentValue as a decimal string and passes it to SetToolTip().
 *
 * @param currentValue The value to display in the tooltip.
 */
void
BChannelSlider::UpdateToolTip(int32 currentValue)
{
	BString valueString;
	valueString.SetToFormat("%" B_PRId32, currentValue);
	SetToolTip(valueString);
}


// #pragma mark -


/**
 * @brief Initialise all slider-specific member variables to safe defaults.
 *
 * Sets knob and backing-bitmap pointers to NULL, infers the initial
 * orientation from the view's aspect ratio, and resets all tracking state.
 * Called by every constructor.
 */
void
BChannelSlider::_InitData()
{
	_UpdateFontDimens();

	fLeftKnob = NULL;
	fMidKnob = NULL;
	fRightKnob = NULL;
	fBacking = NULL;
	fBackingView = NULL;
	fIsVertical = Bounds().Width() / Bounds().Height() < 1;
	fClickDelta = B_ORIGIN;

	fCurrentChannel = -1;
	fAllChannels = false;
	fInitialValues = NULL;
	fMinPoint = 0;
	fFocusChannel = -1;
}


/**
 * @brief Finalise a thumb drag, optionally sending the modification message.
 *
 * Builds a per-channel change mask (all channels or just the active one),
 * calls InvokeChannel() with or without the modification message, and
 * optionally stops tracking and invalidates the view.
 *
 * @param update If true, the modification message is sent and tracking
 *               continues. If false, tracking is stopped and the view is
 *               fully invalidated.
 */
void
BChannelSlider::_FinishChange(bool update)
{
	if (fInitialValues != NULL) {
		bool* inMask = NULL;
		int32 numChannels = CountChannels();
		if (!fAllChannels) {
			inMask = new (std::nothrow) bool[CountChannels()];
			if (inMask) {
				for (int i = 0; i < numChannels; i++)
					inMask[i] = false;
				inMask[fCurrentChannel] = true;
			}
		}
		InvokeChannel(update ? ModificationMessage() : NULL, 0, numChannels,
			inMask);

		delete[] inMask;
	}

	if (!update) {
		SetTracking(false);
		Invalidate();
	}
}


/**
 * @brief Recompute the cached font ascent, descent, and line-feed values.
 *
 * Calls GetFontHeight() and stores the results in fBaseLine and fLineFeed so
 * that drawing code does not repeatedly query the font metrics.
 */
void
BChannelSlider::_UpdateFontDimens()
{
	font_height height;
	GetFontHeight(&height);
	fBaseLine = height.ascent + height.leading;
	fLineFeed = fBaseLine + height.descent;
}


/**
 * @brief Render all channel thumbs into the offscreen backing bitmap and blit to the view.
 *
 * On the first call (or after a resize) allocates a BBitmap and attaches a
 * BView child to it. On every call, clears the backing view, draws each
 * channel via DrawChannel(), updates the tooltip when a thumb is active, and
 * blits the result to the screen with DrawBitmapAsync(). Also records
 * fClickDelta so MouseDown() can compensate for the bitmap's offset.
 *
 * @note The backing bitmap's child BView (fBackingView) is used as the drawing
 *       target for all channel rendering, isolating channel graphics from the
 *       main view's coordinate system.
 */
void
BChannelSlider::_DrawThumbs()
{
	if (fBacking == NULL) {
		// This is the idea: we build a bitmap by taking the coordinates
		// of the first and last thumb frames (top/left and bottom/right)
		BRect first = ThumbFrameFor(0);
		BRect last = ThumbFrameFor(CountChannels() - 1);
		BRect rect(first.LeftTop(), last.RightBottom());

		if (fIsVertical)
			rect.top -= ThumbRangeFor(0);
		else
			rect.right += ThumbRangeFor(0);

		rect.OffsetTo(B_ORIGIN);
		fBacking = new (std::nothrow) BBitmap(rect, B_RGB32, true);
		if (fBacking) {
			fBackingView = new (std::nothrow) BView(rect, "", 0, B_WILL_DRAW);
			if (fBackingView) {
				if (fBacking->Lock()) {
					fBacking->AddChild(fBackingView);
					fBackingView->SetFontSize(10.0);
					fBackingView->SetLowColor(
						ui_color(B_PANEL_BACKGROUND_COLOR));
					fBackingView->SetViewColor(
						ui_color(B_PANEL_BACKGROUND_COLOR));
					fBacking->Unlock();
				}
			} else {
				delete fBacking;
				fBacking = NULL;
			}
		}
	}

	if (fBacking && fBackingView) {
		BPoint drawHere;

		BRect bounds(fBacking->Bounds());
		drawHere.x = (Bounds().Width() - bounds.Width()) / 2.0;
		drawHere.y = (Bounds().Height() - bounds.Height()) - kPadding
			- fLineFeed;

		if (fBacking->Lock()) {
			// Clear the view's background
			fBackingView->FillRect(fBackingView->Bounds(), B_SOLID_LOW);

			BRect channelArea;
			// draw the entire control
			for (int32 channel = 0; channel < CountChannels(); channel++) {
				channelArea = ThumbFrameFor(channel);
				bool pressed = IsTracking()
					&& (channel == fCurrentChannel || fAllChannels);
				DrawChannel(fBackingView, channel, channelArea, pressed);
			}

			// draw some kind of current value tool tip
			if (fCurrentChannel != -1 && fMinPoint != 0) {
				UpdateToolTip(ValueFor(fCurrentChannel));
				ShowToolTip(ToolTip());
			} else {
				HideToolTip();
			}

			fBackingView->Sync();
			fBacking->Unlock();
		}

		DrawBitmapAsync(fBacking, drawHere);

		// fClickDelta is used in MouseMoved()
		fClickDelta = drawHere;
	}
}


/**
 * @brief Draw a bevelled inset frame around a slider groove rectangle.
 *
 * Renders two layers of highlight and shadow lines to give the groove a
 * recessed, three-dimensional appearance. Restores the view's original high
 * colour on exit.
 *
 * @param into The BView to draw into; does nothing if NULL.
 * @param area The rectangle that defines the groove's outer boundary.
 */
void
BChannelSlider::_DrawGrooveFrame(BView* into, const BRect& area)
{
	if (into) {
		rgb_color oldColor = into->HighColor();

		into->SetHighColor(255, 255, 255);
		into->StrokeRect(area);
		into->SetHighColor(tint_color(into->ViewColor(), B_DARKEN_1_TINT));
		into->StrokeLine(area.LeftTop(), BPoint(area.right, area.top));
		into->StrokeLine(area.LeftTop(), BPoint(area.left, area.bottom - 1));
		into->SetHighColor(tint_color(into->ViewColor(), B_DARKEN_2_TINT));
		into->StrokeLine(BPoint(area.left + 1, area.top + 1),
			BPoint(area.right - 1, area.top + 1));
		into->StrokeLine(BPoint(area.left + 1, area.top + 1),
			BPoint(area.left + 1, area.bottom - 2));

		into->SetHighColor(oldColor);
	}
}


/**
 * @brief Compute a new channel value from the current cursor position and apply it.
 *
 * Translates the cursor position relative to fMinPoint into a proportional
 * value within the active channel's [min, max] range, then stores it via
 * SetAllValue() or SetValueFor() depending on whether all channels are being
 * dragged together. If a modification message is present, _FinishChange(true)
 * is called to dispatch it. Redraws the thumbs after every update.
 *
 * @param point  Current cursor position in the view's local coordinate system.
 * @param point2 Reserved — currently unused (always passed as B_ORIGIN).
 */
void
BChannelSlider::_MouseMovedCommon(BPoint point, BPoint point2)
{
	float floatValue = 0;
	int32 limitRange = MaxLimitList()[fCurrentChannel] -
			MinLimitList()[fCurrentChannel];
	float range = ThumbRangeFor(fCurrentChannel);
	if (fIsVertical)
		floatValue = range - (point.y - fMinPoint);
	else
		floatValue = range + (point.x - fMinPoint);

	int32 value = (int32)(floatValue / range * limitRange) +
		MinLimitList()[fCurrentChannel];
	if (fAllChannels)
		SetAllValue(value);
	else
		SetValueFor(fCurrentChannel, value);

	if (ModificationMessage())
		_FinishChange(true);

	_DrawThumbs();
}


// #pragma mark - FBC padding


/** @brief Reserved virtual slot 1 for future binary compatibility. */
void BChannelSlider::_Reserved_BChannelSlider_1(void*, ...) {}
/** @brief Reserved virtual slot 2 for future binary compatibility. */
void BChannelSlider::_Reserved_BChannelSlider_2(void*, ...) {}
/** @brief Reserved virtual slot 3 for future binary compatibility. */
void BChannelSlider::_Reserved_BChannelSlider_3(void*, ...) {}
/** @brief Reserved virtual slot 4 for future binary compatibility. */
void BChannelSlider::_Reserved_BChannelSlider_4(void*, ...) {}
/** @brief Reserved virtual slot 5 for future binary compatibility. */
void BChannelSlider::_Reserved_BChannelSlider_5(void*, ...) {}
/** @brief Reserved virtual slot 6 for future binary compatibility. */
void BChannelSlider::_Reserved_BChannelSlider_6(void*, ...) {}
/** @brief Reserved virtual slot 7 for future binary compatibility. */
void BChannelSlider::_Reserved_BChannelSlider_7(void*, ...) {}


//	#pragma mark - binary compatibility


extern "C" void
B_IF_GCC_2(_Reserved_BChannelSlider_0__14BChannelSliderPve,
	_ZN14BChannelSlider26_Reserved_BChannelSlider_0EPvz)(
	BChannelSlider* channelSlider, int32 currentValue)
{
	channelSlider->BChannelSlider::UpdateToolTip(currentValue);
}
