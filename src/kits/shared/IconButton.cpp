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
 *   Copyright 2006-2011, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Axel Dörfler, axeld@pinc-software.de.
 */

/** @file IconButton.cpp
 *  @brief Implements BIconButton, a BControl subclass that renders a bitmap
 *         icon with hover, pressed, and disabled visual states, and supports
 *         multiple icon-loading paths (resource ID, file path, BBitmap, MIME
 *         type, and raw pixel data).
 */


#include "IconButton.h"

#include <new>
#include <stdio.h>

#include <Application.h>
#include <Bitmap.h>
#include <Control.h>
#include <ControlLook.h>
#include <Entry.h>
#include <IconUtils.h>
#include <Looper.h>
#include <Message.h>
#include <Mime.h>
#include <Path.h>
#include <Region.h>
#include <Resources.h>
#include <Roster.h>
#include <TranslationUtils.h>
#include <Window.h>


namespace BPrivate {


enum {
	STATE_NONE			= 0x0000,
	STATE_PRESSED		= 0x0002,
	STATE_INSIDE		= 0x0008,
	STATE_FORCE_PRESSED	= 0x0010,
};



/** @brief Constructs a BIconButton.
 *  @param name     Internal view name.
 *  @param label    Optional text label passed to BControl.
 *  @param message  The message sent when the button is activated.
 *  @param target   Initial message target; updated when attached to a window.
 */
BIconButton::BIconButton(const char* name, const char* label,
	BMessage* message, BHandler* target)
	:
	BControl(name, label, message, B_WILL_DRAW),
	fButtonState(0),
	fNormalBitmap(NULL),
	fDisabledBitmap(NULL),
	fClickedBitmap(NULL),
	fDisabledClickedBitmap(NULL),
	fTargetCache(target)
{
	SetTarget(target);
	SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	SetViewColor(B_TRANSPARENT_32_BIT);
}


/** @brief Destructor — deletes all derived bitmap variants. */
BIconButton::~BIconButton()
{
	_DeleteBitmaps();
}


/** @brief Handles messages not consumed by mouse or focus events.
 *  @param message  The incoming message.
 */
void
BIconButton::MessageReceived(BMessage* message)
{
	switch (message->what) {
		default:
			BView::MessageReceived(message);
			break;
	}
}


/** @brief Adopts parent colors and sets the message target when attached.
 *
 *  Falls back to targeting the window if no explicit target was provided.
 */
void
BIconButton::AttachedToWindow()
{
	AdoptParentColors();

	if (ViewUIColor() != B_NO_COLOR)
		SetLowUIColor(ViewUIColor());

	SetTarget(fTargetCache);
	if (!Target())
		SetTarget(Window());
}


/** @brief Draws the button border, background, and centered icon bitmap.
 *
 *  Selects the appropriate bitmap variant (normal, disabled, clicked) based
 *  on the current state flags.  Draws the border and background only when
 *  the pointer is inside or the button is being tracked or force-pressed.
 *
 *  @param updateRect  The rectangle that needs redrawing.
 */
void
BIconButton::Draw(BRect updateRect)
{
	rgb_color background = LowColor();

	BRect r(Bounds());

	uint32 flags = 0;
	BBitmap* bitmap = fNormalBitmap;
	if (!IsEnabled()) {
		flags |= BControlLook::B_DISABLED;
		bitmap = fDisabledBitmap;
	}
	if (_HasFlags(STATE_PRESSED) || _HasFlags(STATE_FORCE_PRESSED))
		flags |= BControlLook::B_ACTIVATED;

	if (ShouldDrawBorder()) {
		DrawBorder(r, updateRect, background, flags);
		DrawBackground(r, updateRect, background, flags);
	} else {
		SetHighColor(background);
		FillRect(r);
	}

	if (bitmap && bitmap->IsValid()) {
		if (bitmap->ColorSpace() == B_RGBA32
			|| bitmap->ColorSpace() == B_RGBA32_BIG) {
			SetDrawingMode(B_OP_ALPHA);
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		}
		float x = r.left + floorf((r.Width()
			- bitmap->Bounds().Width()) / 2.0 + 0.5);
		float y = r.top + floorf((r.Height()
			- bitmap->Bounds().Height()) / 2.0 + 0.5);
		DrawBitmap(bitmap, BPoint(x, y));
	}
}


/** @brief Returns whether the button border should be drawn.
 *  @return True when the pointer is inside and the button is enabled,
 *          when the button is being tracked, or when force-pressed.
 */
bool
BIconButton::ShouldDrawBorder() const
{
	return (IsEnabled() && (IsInside() || IsTracking()))
		|| _HasFlags(STATE_FORCE_PRESSED);
}


/** @brief Draws the button frame using be_control_look.
 *  @param frame            The frame rectangle (may be shrunk by the look).
 *  @param updateRect       The dirty update rectangle.
 *  @param backgroundColor  The background color for the frame.
 *  @param flags            BControlLook flags (e.g. B_DISABLED, B_ACTIVATED).
 */
void
BIconButton::DrawBorder(BRect& frame, const BRect& updateRect,
	const rgb_color& backgroundColor, uint32 flags)
{
	be_control_look->DrawButtonFrame(this, frame, updateRect, backgroundColor,
		backgroundColor, flags);
}


/** @brief Draws the button background using be_control_look.
 *  @param frame            The frame rectangle after border inset.
 *  @param updateRect       The dirty update rectangle.
 *  @param backgroundColor  The fill color.
 *  @param flags            BControlLook flags.
 */
void
BIconButton::DrawBackground(BRect& frame, const BRect& updateRect,
	const rgb_color& backgroundColor, uint32 flags)
{
	be_control_look->DrawButtonBackground(this, frame, updateRect,
		backgroundColor, flags);
}


/** @brief Handles mouse-down by marking the button as pressed and tracking.
 *  @param where  The position of the click (view coordinates).
 */
void
BIconButton::MouseDown(BPoint where)
{
	if (!IsValid())
		return;

	if (IsEnabled()) {
		if (Bounds().Contains(where)) {
			SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
			_SetFlags(STATE_PRESSED, true);
			_SetTracking(true);
		} else {
			_SetFlags(STATE_PRESSED, false);
			_SetTracking(false);
		}
	}
}


/** @brief Handles mouse-up by invoking the button if the cursor is inside.
 *  @param where  The position of the release (view coordinates).
 */
void
BIconButton::MouseUp(BPoint where)
{
	if (!IsValid())
		return;

	if (IsEnabled() && _HasFlags(STATE_PRESSED)
		&& Bounds().Contains(where)) {
		Invoke();
	} else if (Bounds().Contains(where))
		SetInside(true);

	_SetFlags(STATE_PRESSED, false);
	_SetTracking(false);
}


/** @brief Handles mouse movement to update inside/pressed state.
 *
 *  Also catches missed mouse-up events by checking the button state.
 *
 *  @param where    The current pointer position (view coordinates).
 *  @param transit  Transit code (B_ENTERED_VIEW, B_INSIDE_VIEW, etc.).
 *  @param message  Any drag message currently in flight, or NULL.
 */
void
BIconButton::MouseMoved(BPoint where, uint32 transit, const BMessage* message)
{
	if (!IsValid())
		return;

	uint32 buttons = 0;
	Window()->CurrentMessage()->FindInt32("buttons", (int32*)&buttons);
	// catch a mouse up event that we might have missed
	if (!buttons && _HasFlags(STATE_PRESSED)) {
		MouseUp(where);
		return;
	}
	if (buttons != 0 && !IsTracking())
		return;

	SetInside((transit == B_INSIDE_VIEW || transit == B_ENTERED_VIEW)
		&& IsEnabled());
	if (IsTracking())
		_SetFlags(STATE_PRESSED, Bounds().Contains(where));
}


/** @brief Computes the preferred (and minimum) display size of the button.
 *  @param width   Output: preferred width in pixels.
 *  @param height  Output: preferred height in pixels.
 */
void
BIconButton::GetPreferredSize(float* width, float* height)
{
	float minWidth = 0.0f;
	float minHeight = 0.0f;
	if (IsValid()) {
		minWidth += fNormalBitmap->Bounds().IntegerWidth() + 1.0f;
		minHeight += fNormalBitmap->Bounds().IntegerHeight() + 1.0f;
	}

	const float kMinSpace = 15.0f;
	if (minWidth < kMinSpace)
		minWidth = kMinSpace;
	if (minHeight < kMinSpace)
		minHeight = kMinSpace;

	float hPadding = max_c(6.0f, ceilf(minHeight / 4.0f));
	float vPadding = max_c(6.0f, ceilf(minWidth / 4.0f));

	if (Label() != NULL && Label()[0] != '\0') {
		font_height fh;
		GetFontHeight(&fh);
		minHeight += ceilf(fh.ascent + fh.descent) + vPadding;
		minWidth += StringWidth(Label()) + vPadding;
	}

	if (width)
		*width = minWidth + hPadding;
	if (height)
		*height = minHeight + vPadding;
}


/** @brief Returns the minimum size of the button.
 *  @return A BSize equal to GetPreferredSize().
 */
BSize
BIconButton::MinSize()
{
	BSize size;
	GetPreferredSize(&size.width, &size.height);
	return size;
}


/** @brief Returns the maximum size of the button, equal to its minimum size.
 *  @return The result of MinSize().
 */
BSize
BIconButton::MaxSize()
{
	return MinSize();
}


/** @brief Invokes the button's action, enriching the message with metadata.
 *
 *  Adds "be:when", "be:source", and "be:value" fields to a clone of the
 *  message before invoking.
 *
 *  @param message  The message to send, or NULL to use the stored message.
 *  @return Status code from BInvoker::Invoke().
 */
status_t
BIconButton::Invoke(BMessage* message)
{
	if (message == NULL)
		message = Message();
	if (message != NULL) {
		BMessage clone(*message);
		clone.AddInt64("be:when", system_time());
		clone.AddPointer("be:source", (BView*)this);
		clone.AddInt32("be:value", Value());
		return BInvoker::Invoke(&clone);
	}
	return BInvoker::Invoke(message);
}


/** @brief Forces the button into a pressed or unpressed state programmatically.
 *  @param pressed  True to lock the button in the pressed state.
 */
void
BIconButton::SetPressed(bool pressed)
{
	_SetFlags(STATE_FORCE_PRESSED, pressed);
}


/** @brief Returns whether the button is currently force-pressed.
 *  @return True if STATE_FORCE_PRESSED is set.
 */
bool
BIconButton::IsPressed() const
{
	return _HasFlags(STATE_FORCE_PRESSED);
}


/** @brief Loads a vector icon from the application's resources by ID.
 *
 *  Rasterizes the HVIF data into a 32-pixel bitmap and calls SetIcon().
 *
 *  @param resourceID  The resource ID of the B_VECTOR_ICON_TYPE resource.
 *  @return B_OK on success, or an error code (B_ERROR if resource not found).
 */
status_t
BIconButton::SetIcon(int32 resourceID)
{
	app_info info;
	status_t status = be_app->GetAppInfo(&info);
	if (status != B_OK)
		return status;

	BResources resources(&info.ref);
	status = resources.InitCheck();
	if (status != B_OK)
		return status;

	size_t size;
	const void* data = resources.LoadResource(B_VECTOR_ICON_TYPE, resourceID,
		&size);
	if (data != NULL) {
		const BRect bitmapRect(BPoint(0, 0), be_control_look->ComposeIconSize(32));
		BBitmap bitmap(bitmapRect, B_BITMAP_NO_SERVER_LINK, B_RGBA32);
		status = bitmap.InitCheck();
		if (status != B_OK)
			return status;
		status = BIconUtils::GetVectorIcon(reinterpret_cast<const uint8*>(data),
			size, &bitmap);
		if (status != B_OK)
			return status;
		return SetIcon(&bitmap);
	}
//	const void* data = resources.LoadResource(B_BITMAP_TYPE, resourceID, &size);
	return B_ERROR;
}


/** @brief Loads an icon bitmap from a file path (absolute or relative to the app).
 *  @param pathToBitmap  Path string to the image file.
 *  @return B_OK on success, B_BAD_VALUE if the path is NULL, or another error.
 */
status_t
BIconButton::SetIcon(const char* pathToBitmap)
{
	if (pathToBitmap == NULL)
		return B_BAD_VALUE;

	status_t status = B_BAD_VALUE;
	BBitmap* fileBitmap = NULL;
	// try to load bitmap from either relative or absolute path
	BEntry entry(pathToBitmap, true);
	if (!entry.Exists()) {
		app_info info;
		status = be_app->GetAppInfo(&info);
		if (status == B_OK) {
			BEntry app_entry(&info.ref, true);
			BPath path;
			app_entry.GetPath(&path);
			status = path.InitCheck();
			if (status == B_OK) {
				status = path.GetParent(&path);
				if (status == B_OK) {
					status = path.Append(pathToBitmap, true);
					if (status == B_OK)
						fileBitmap = BTranslationUtils::GetBitmap(path.Path());
					else {
						printf("BIconButton::SetIcon() - path.Append() failed: "
							"%s\n", strerror(status));
					}
				} else {
					printf("BIconButton::SetIcon() - path.GetParent() failed: "
						"%s\n", strerror(status));
				}
			} else {
				printf("BIconButton::SetIcon() - path.InitCheck() failed: "
					"%s\n", strerror(status));
			}
		} else {
			printf("BIconButton::SetIcon() - be_app->GetAppInfo() failed: "
				"%s\n", strerror(status));
		}
	} else
		fileBitmap = BTranslationUtils::GetBitmap(pathToBitmap);
	if (fileBitmap) {
		status = _MakeBitmaps(fileBitmap);
		delete fileBitmap;
	} else
		status = B_ERROR;
	return status;
}


/** @brief Sets the icon from an existing BBitmap, converting color spaces as needed.
 *
 *  B_CMAP8 bitmaps are first converted to B_RGBA32 before generating the
 *  state variants.
 *
 *  @param bitmap  Source bitmap (not adopted; a copy is made).
 *  @param flags   Reserved for future use.
 *  @return B_OK on success, or an error code.
 */
status_t
BIconButton::SetIcon(const BBitmap* bitmap, uint32 flags)
{
	if (bitmap && bitmap->ColorSpace() == B_CMAP8) {
		status_t status = bitmap->InitCheck();
		if (status >= B_OK) {
			if (BBitmap* rgb32Bitmap = _ConvertToRGB32(bitmap)) {
				status = _MakeBitmaps(rgb32Bitmap);
				delete rgb32Bitmap;
			} else
				status = B_NO_MEMORY;
		}
		return status;
	} else
		return _MakeBitmaps(bitmap);
}


/** @brief Sets the icon from a MIME type's registered icon.
 *  @param fileType  The BMimeType whose icon should be used.
 *  @param small     True to use the small (16 x 16) icon; false for large.
 *  @return B_OK on success, B_BAD_VALUE if fileType is NULL or invalid.
 */
status_t
BIconButton::SetIcon(const BMimeType* fileType, bool small)
{
	status_t status = fileType ? fileType->InitCheck() : B_BAD_VALUE;
	if (status >= B_OK) {
		BBitmap* mimeBitmap = new(std::nothrow) BBitmap(BRect(0.0, 0.0, 15.0,
			15.0), B_CMAP8);
		if (mimeBitmap && mimeBitmap->IsValid()) {
			status = fileType->GetIcon(mimeBitmap, small ? B_MINI_ICON
				: B_LARGE_ICON);
			if (status >= B_OK) {
				if (BBitmap* bitmap = _ConvertToRGB32(mimeBitmap)) {
					status = _MakeBitmaps(bitmap);
					delete bitmap;
				} else {
					printf("BIconButton::SetIcon() - B_RGB32 bitmap is not "
						"valid\n");
				}
			} else {
				printf("BIconButton::SetIcon() - fileType->GetIcon() failed: "
					"%s\n", strerror(status));
			}
		} else
			printf("BIconButton::SetIcon() - B_CMAP8 bitmap is not valid\n");
		delete mimeBitmap;
	} else {
		printf("BIconButton::SetIcon() - fileType is not valid: %s\n",
			strerror(status));
	}
	return status;
}


/** @brief Sets the icon from raw QuickRes-exported pixel data.
 *
 *  Handles colorspace conversion for non-32-bit formats and optionally
 *  converts the image to greyscale.
 *
 *  @param bitsFromQuickRes  Pointer to the raw pixel buffer.
 *  @param width             Width of the bitmap in pixels.
 *  @param height            Height of the bitmap in pixels.
 *  @param format            Color space of the raw data.
 *  @param convertToBW       If true, converts the bitmap to grey scale.
 *  @return B_OK on success, or an error code.
 */
status_t
BIconButton::SetIcon(const unsigned char* bitsFromQuickRes,
	uint32 width, uint32 height, color_space format, bool convertToBW)
{
	status_t status = B_BAD_VALUE;
	if (bitsFromQuickRes && width > 0 && height > 0) {
		BBitmap* quickResBitmap = new(std::nothrow) BBitmap(BRect(0.0, 0.0,
			width - 1.0, height - 1.0), format);
		status = quickResBitmap ? quickResBitmap->InitCheck() : B_ERROR;
		if (status >= B_OK) {
			// It doesn't look right to copy BitsLength() bytes, but bitmaps
			// exported from QuickRes still contain their padding, so it is
			// all right.
			memcpy(quickResBitmap->Bits(), bitsFromQuickRes,
				quickResBitmap->BitsLength());
			if (format != B_RGB32 && format != B_RGBA32
				&& format != B_RGB32_BIG && format != B_RGBA32_BIG) {
				// colorspace needs conversion
				BBitmap* bitmap = new(std::nothrow) BBitmap(
					quickResBitmap->Bounds(), B_RGB32, true);
				if (bitmap && bitmap->IsValid()) {
					if (bitmap->Lock()) {
						BView* helper = new BView(bitmap->Bounds(), "helper",
							B_FOLLOW_NONE, B_WILL_DRAW);
						bitmap->AddChild(helper);
						helper->SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
						helper->FillRect(helper->Bounds());
						helper->SetDrawingMode(B_OP_OVER);
						helper->DrawBitmap(quickResBitmap, BPoint(0.0, 0.0));
						helper->Sync();
						bitmap->Unlock();
					}
					status = _MakeBitmaps(bitmap);
				} else {
					printf("BIconButton::SetIcon() - B_RGB32 bitmap is not "
						"valid\n");
				}
				delete bitmap;
			} else {
				// native colorspace (32 bits)
				if (convertToBW) {
					// convert to gray scale icon
					uint8* bits = (uint8*)quickResBitmap->Bits();
					uint32 bpr = quickResBitmap->BytesPerRow();
					for (uint32 y = 0; y < height; y++) {
						uint8* handle = bits;
						uint8 gray;
						for (uint32 x = 0; x < width; x++) {
							gray = uint8((116 * handle[0] + 600 * handle[1]
								+ 308 * handle[2]) / 1024);
							handle[0] = gray;
							handle[1] = gray;
							handle[2] = gray;
							handle += 4;
						}
						bits += bpr;
					}
				}
				status = _MakeBitmaps(quickResBitmap);
			}
		} else {
			printf("BIconButton::SetIcon() - error allocating bitmap: "
				"%s\n", strerror(status));
		}
		delete quickResBitmap;
	}
	return status;
}


/** @brief Removes the current icon and redraws the button without one. */
void
BIconButton::ClearIcon()
{
	_DeleteBitmaps();
	_Update();
}


/** @brief Trims transparent padding from the icon bitmap.
 *
 *  Scans the normal bitmap's alpha channel to find the tightest bounding
 *  rectangle containing all non-transparent pixels, then optionally
 *  preserves the aspect ratio by using the smallest inset on all sides.
 *  The trimmed bitmap replaces the current icon via SetIcon().
 *
 *  @param keepAspect  If true, the trim amount is equalised on all sides to
 *                     preserve the original aspect ratio.
 */
void
BIconButton::TrimIcon(bool keepAspect)
{
	if (fNormalBitmap == NULL)
		return;

	uint8* bits = (uint8*)fNormalBitmap->Bits();
	uint32 bpr = fNormalBitmap->BytesPerRow();
	uint32 width = fNormalBitmap->Bounds().IntegerWidth() + 1;
	uint32 height = fNormalBitmap->Bounds().IntegerHeight() + 1;
	BRect trimmed(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN);
	for (uint32 y = 0; y < height; y++) {
		uint8* b = bits + 3;
		bool rowHasAlpha = false;
		for (uint32 x = 0; x < width; x++) {
			if (*b) {
				rowHasAlpha = true;
				if (x < trimmed.left)
					trimmed.left = x;
				if (x > trimmed.right)
					trimmed.right = x;
			}
			b += 4;
		}
		if (rowHasAlpha) {
			if (y < trimmed.top)
				trimmed.top = y;
			if (y > trimmed.bottom)
				trimmed.bottom = y;
		}
		bits += bpr;
	}
	if (!trimmed.IsValid())
		return;
	if (keepAspect) {
		float minInset = trimmed.left;
		minInset = min_c(minInset, trimmed.top);
		minInset = min_c(minInset, fNormalBitmap->Bounds().right
			- trimmed.right);
		minInset = min_c(minInset, fNormalBitmap->Bounds().bottom
			- trimmed.bottom);
		trimmed = fNormalBitmap->Bounds().InsetByCopy(minInset, minInset);
	}
	trimmed = trimmed & fNormalBitmap->Bounds();
	BBitmap trimmedBitmap(trimmed.OffsetToCopy(B_ORIGIN),
		B_BITMAP_NO_SERVER_LINK, B_RGBA32);
	bits = (uint8*)fNormalBitmap->Bits();
	bits += 4 * (int32)trimmed.left + bpr * (int32)trimmed.top;
	uint8* dst = (uint8*)trimmedBitmap.Bits();
	uint32 trimmedWidth = trimmedBitmap.Bounds().IntegerWidth() + 1;
	uint32 trimmedHeight = trimmedBitmap.Bounds().IntegerHeight() + 1;
	uint32 trimmedBPR = trimmedBitmap.BytesPerRow();
	for (uint32 y = 0; y < trimmedHeight; y++) {
		memcpy(dst, bits, trimmedWidth * 4);
		dst += trimmedBPR;
		bits += bpr;
	}
	SetIcon(&trimmedBitmap);
}


/** @brief Returns whether all four bitmap state variants are valid.
 *  @return True when normal, disabled, clicked, and disabled-clicked bitmaps
 *          all exist and pass IsValid().
 */
bool
BIconButton::IsValid() const
{
	return (fNormalBitmap && fDisabledBitmap && fClickedBitmap
		&& fDisabledClickedBitmap
		&& fNormalBitmap->IsValid()
		&& fDisabledBitmap->IsValid()
		&& fClickedBitmap->IsValid()
		&& fDisabledClickedBitmap->IsValid());
}


/** @brief Returns a copy of the normal bitmap with grey pixels made transparent.
 *
 *  In B_RGB32 mode pixels with the magic grey value (216, 216, 216) have
 *  their alpha set to 0.  The caller is responsible for deleting the
 *  returned bitmap.
 *
 *  @return A newly allocated BBitmap, or NULL if no valid bitmap exists.
 */
BBitmap*
BIconButton::Bitmap() const
{
	BBitmap* bitmap = NULL;
	if (fNormalBitmap && fNormalBitmap->IsValid()) {
		bitmap = new(std::nothrow) BBitmap(fNormalBitmap);
		if (bitmap != NULL && bitmap->IsValid()) {
			// TODO: remove this functionality when we use real transparent
			// bitmaps
			uint8* bits = (uint8*)bitmap->Bits();
			uint32 bpr = bitmap->BytesPerRow();
			uint32 width = bitmap->Bounds().IntegerWidth() + 1;
			uint32 height = bitmap->Bounds().IntegerHeight() + 1;
			color_space format = bitmap->ColorSpace();
			if (format == B_CMAP8) {
				// replace gray with magic transparent index
			} else if (format == B_RGB32) {
				for (uint32 y = 0; y < height; y++) {
					uint8* bitsHandle = bits;
					for (uint32 x = 0; x < width; x++) {
						if (bitsHandle[0] == 216
							&& bitsHandle[1] == 216
							&& bitsHandle[2] == 216) {
							// make this pixel completely transparent
							bitsHandle[3] = 0;
						}
						bitsHandle += 4;
					}
					bits += bpr;
				}
			}
		} else {
			delete bitmap;
			bitmap = NULL;
		}
	}
	return bitmap;
}


/** @brief Sets the control value and mirrors it in the pressed state flag.
 *  @param value  The new control value (B_CONTROL_ON or B_CONTROL_OFF).
 */
void
BIconButton::SetValue(int32 value)
{
	BControl::SetValue(value);
	_SetFlags(STATE_PRESSED, value != 0);
}


/** @brief Enables or disables the button, clearing inside/tracking state when disabled.
 *  @param enabled  True to enable; false to disable.
 */
void
BIconButton::SetEnabled(bool enabled)
{
	BControl::SetEnabled(enabled);
	if (!enabled) {
		SetInside(false);
		_SetTracking(false);
	}
}


// #pragma mark - protected


/** @brief Returns whether the pointer is currently inside the button.
 *  @return True if STATE_INSIDE is set.
 */
bool
BIconButton::IsInside() const
{
	return _HasFlags(STATE_INSIDE);
}


/** @brief Sets whether the pointer is considered to be inside the button.
 *  @param inside  True when the pointer is inside the view bounds.
 */
void
BIconButton::SetInside(bool inside)
{
	_SetFlags(STATE_INSIDE, inside);
}


// #pragma mark - private


/** @brief Converts a bitmap of any color space to B_RGBA32 via a helper view.
 *  @param bitmap  The source bitmap to convert.
 *  @return A newly allocated B_RGBA32 BBitmap, or NULL on failure.
 */
BBitmap*
BIconButton::_ConvertToRGB32(const BBitmap* bitmap) const
{
	BBitmap* convertedBitmap = new(std::nothrow) BBitmap(bitmap->Bounds(),
		B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
	if (convertedBitmap && convertedBitmap->IsValid()) {
		memset(convertedBitmap->Bits(), 0, convertedBitmap->BitsLength());
		if (convertedBitmap->Lock()) {
			BView* helper = new BView(bitmap->Bounds(), "helper",
				B_FOLLOW_NONE, B_WILL_DRAW);
			convertedBitmap->AddChild(helper);
			helper->SetDrawingMode(B_OP_OVER);
			helper->DrawBitmap(bitmap, BPoint(0.0, 0.0));
			helper->Sync();
			convertedBitmap->Unlock();
		}
	} else {
		delete convertedBitmap;
		convertedBitmap = NULL;
	}
	return convertedBitmap;
}


/** @brief Generates the four state bitmap variants from a source bitmap.
 *
 *  Creates normal, clicked (darkened), disabled (desaturated), and
 *  disabled-clicked variants.  Supports B_RGB32, B_RGB32_BIG, B_RGBA32,
 *  and B_RGBA32_BIG color spaces.
 *
 *  @param bitmap  The source bitmap from which all variants are derived.
 *  @return B_OK on success, or an error code.
 */
status_t
BIconButton::_MakeBitmaps(const BBitmap* bitmap)
{
	status_t status = bitmap ? bitmap->InitCheck() : B_BAD_VALUE;
	if (status == B_OK) {
		// make our own versions of the bitmap
		BRect b(bitmap->Bounds());
		_DeleteBitmaps();
		color_space format = bitmap->ColorSpace();
		fNormalBitmap = new(std::nothrow) BBitmap(b, format);
		fDisabledBitmap = new(std::nothrow) BBitmap(b, format);
		fClickedBitmap = new(std::nothrow) BBitmap(b, format);
		fDisabledClickedBitmap = new(std::nothrow) BBitmap(b, format);
		if (IsValid()) {
			// copy bitmaps from file bitmap
			uint8* nBits = (uint8*)fNormalBitmap->Bits();
			uint8* dBits = (uint8*)fDisabledBitmap->Bits();
			uint8* cBits = (uint8*)fClickedBitmap->Bits();
			uint8* dcBits = (uint8*)fDisabledClickedBitmap->Bits();
			uint8* fBits = (uint8*)bitmap->Bits();
			int32 nbpr = fNormalBitmap->BytesPerRow();
			int32 fbpr = bitmap->BytesPerRow();
			int32 pixels = b.IntegerWidth() + 1;
			int32 lines = b.IntegerHeight() + 1;
			// nontransparent version:
			if (format == B_RGB32 || format == B_RGB32_BIG) {
				// iterate over color components
				for (int32 y = 0; y < lines; y++) {
					for (int32 x = 0; x < pixels; x++) {
						int32 nOffset = 4 * x;
						int32 fOffset = 4 * x;
						nBits[nOffset + 0] = fBits[fOffset + 0];
						nBits[nOffset + 1] = fBits[fOffset + 1];
						nBits[nOffset + 2] = fBits[fOffset + 2];
						nBits[nOffset + 3] = 255;
						// clicked bits are darker (lame method...)
						cBits[nOffset + 0] = (uint8)((float)nBits[nOffset + 0]
							* 0.8);
						cBits[nOffset + 1] = (uint8)((float)nBits[nOffset + 1]
							* 0.8);
						cBits[nOffset + 2] = (uint8)((float)nBits[nOffset + 2]
							* 0.8);
						cBits[nOffset + 3] = 255;
						// disabled bits have less contrast (lame method...)
						uint8 grey = 216;
						float dist = (nBits[nOffset + 0] - grey) * 0.4;
						dBits[nOffset + 0] = (uint8)(grey + dist);
						dist = (nBits[nOffset + 1] - grey) * 0.4;
						dBits[nOffset + 1] = (uint8)(grey + dist);
						dist = (nBits[nOffset + 2] - grey) * 0.4;
						dBits[nOffset + 2] = (uint8)(grey + dist);
						dBits[nOffset + 3] = 255;
						// disabled bits have less contrast (lame method...)
						grey = 188;
						dist = (nBits[nOffset + 0] - grey) * 0.4;
						dcBits[nOffset + 0] = (uint8)(grey + dist);
						dist = (nBits[nOffset + 1] - grey) * 0.4;
						dcBits[nOffset + 1] = (uint8)(grey + dist);
						dist = (nBits[nOffset + 2] - grey) * 0.4;
						dcBits[nOffset + 2] = (uint8)(grey + dist);
						dcBits[nOffset + 3] = 255;
					}
					nBits += nbpr;
					dBits += nbpr;
					cBits += nbpr;
					dcBits += nbpr;
					fBits += fbpr;
				}
			// transparent version:
			} else if (format == B_RGBA32 || format == B_RGBA32_BIG) {
				// iterate over color components
				for (int32 y = 0; y < lines; y++) {
					for (int32 x = 0; x < pixels; x++) {
						int32 nOffset = 4 * x;
						int32 fOffset = 4 * x;
						nBits[nOffset + 0] = fBits[fOffset + 0];
						nBits[nOffset + 1] = fBits[fOffset + 1];
						nBits[nOffset + 2] = fBits[fOffset + 2];
						nBits[nOffset + 3] = fBits[fOffset + 3];
						// clicked bits are darker (lame method...)
						cBits[nOffset + 0] = (uint8)(nBits[nOffset + 0] * 0.8);
						cBits[nOffset + 1] = (uint8)(nBits[nOffset + 1] * 0.8);
						cBits[nOffset + 2] = (uint8)(nBits[nOffset + 2] * 0.8);
						cBits[nOffset + 3] = fBits[fOffset + 3];
						// disabled bits have less opacity

						uint8 grey = ((uint16)nBits[nOffset + 0] * 10
						    + nBits[nOffset + 1] * 60
							+ nBits[nOffset + 2] * 30) / 100;
						float dist = (nBits[nOffset + 0] - grey) * 0.3;
						dBits[nOffset + 0] = (uint8)(grey + dist);
						dist = (nBits[nOffset + 1] - grey) * 0.3;
						dBits[nOffset + 1] = (uint8)(grey + dist);
						dist = (nBits[nOffset + 2] - grey) * 0.3;
						dBits[nOffset + 2] = (uint8)(grey + dist);
						dBits[nOffset + 3] = (uint8)(fBits[fOffset + 3] * 0.3);
						// disabled bits have less contrast (lame method...)
						dcBits[nOffset + 0] = (uint8)(dBits[nOffset + 0] * 0.8);
						dcBits[nOffset + 1] = (uint8)(dBits[nOffset + 1] * 0.8);
						dcBits[nOffset + 2] = (uint8)(dBits[nOffset + 2] * 0.8);
						dcBits[nOffset + 3] = (uint8)(fBits[fOffset + 3] * 0.3);
					}
					nBits += nbpr;
					dBits += nbpr;
					cBits += nbpr;
					dcBits += nbpr;
					fBits += fbpr;
				}
			// unsupported format
			} else {
				printf("BIconButton::_MakeBitmaps() - bitmap has unsupported "
					"colorspace\n");
				status = B_MISMATCHED_VALUES;
				_DeleteBitmaps();
			}
		} else {
			printf("BIconButton::_MakeBitmaps() - error allocating local "
				"bitmaps\n");
			status = B_NO_MEMORY;
			_DeleteBitmaps();
		}
	} else
		printf("BIconButton::_MakeBitmaps() - bitmap is not valid\n");
	return status;
}


/** @brief Deletes all four state bitmap variants and sets pointers to NULL. */
void
BIconButton::_DeleteBitmaps()
{
	delete fNormalBitmap;
	fNormalBitmap = NULL;
	delete fDisabledBitmap;
	fDisabledBitmap = NULL;
	delete fClickedBitmap;
	fClickedBitmap = NULL;
	delete fDisabledClickedBitmap;
	fDisabledClickedBitmap = NULL;
}


/** @brief Invalidates the view inside a looper lock, triggering a redraw. */
void
BIconButton::_Update()
{
	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}


/** @brief Sets or clears one or more state flags and triggers a redraw if changed.
 *
 *  When STATE_PRESSED is toggled the BControl value is updated without
 *  scheduling an additional redraw.
 *
 *  @param flags  The flag bits to modify.
 *  @param set    True to set the bits; false to clear them.
 */
void
BIconButton::_SetFlags(uint32 flags, bool set)
{
	if (_HasFlags(flags) != set) {
		if (set)
			fButtonState |= flags;
		else
			fButtonState &= ~flags;

		if ((flags & STATE_PRESSED) != 0)
			SetValueNoUpdate(set ? B_CONTROL_ON : B_CONTROL_OFF);
		_Update();
	}
}


/** @brief Tests whether all specified state flags are currently set.
 *  @param flags  The flag bits to test.
 *  @return True if all bits in \a flags are set.
 */
bool
BIconButton::_HasFlags(uint32 flags) const
{
	return (fButtonState & flags) != 0;
}


//!	This one calls _Update() if needed; BControl::SetTracking() isn't virtual.
/** @brief Sets the tracking state and triggers a redraw if it changed.
 *  @param tracking  True to start tracking; false to stop.
 */
void
BIconButton::_SetTracking(bool tracking)
{
	if (IsTracking() == tracking)
		return;

	SetTracking(tracking);
	_Update();
}


}	// namespace BPrivate
