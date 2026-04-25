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
 *   Copyright 2005-2012, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file HWInterface.cpp
 * @brief Shared base implementation for graphics hardware abstractions.
 *
 * HWInterface implements all the parts of the abstract graphics interface
 * that are independent of the actual transport: cursor compositing, drag
 * bitmap handling, default copy-back-to-front, listener notifications, and
 * stub overlay support. Concrete subclasses (real accelerants, VESA, virtual
 * frame buffers, off-screen bitmaps, remote desktop) override the
 * mode-setting and frame-buffer methods.
 */


#include "HWInterface.h"

#include <new>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <vesa/vesa_info.h>

#include "drawing_support.h"

#include "DrawingEngine.h"
#include "RenderingBuffer.h"
#include "SystemPalette.h"


using std::nothrow;


/**
 * @brief Default-constructs a listener; nothing to initialise.
 */
HWInterfaceListener::HWInterfaceListener()
{
}


/**
 * @brief Default destructor; nothing to release.
 */
HWInterfaceListener::~HWInterfaceListener()
{
}


// #pragma mark - HWInterface


/**
 * @brief Default-constructs the interface with no cursor / drag bitmap.
 *
 * Initialises locks, listener list, and cursor state. The actual hardware
 * resources are deferred to Initialize() in the subclass.
 */
HWInterface::HWInterface()
	:
	MultiLocker("hw interface lock"),
	fFloatingOverlaysLock("floating overlays lock"),
	fCursor(NULL),
	fDragBitmap(NULL),
	fDragBitmapOffset(0, 0),
	fCursorAndDragBitmap(NULL),
	fCursorVisible(false),
	fCursorObscured(false),
	fHardwareCursorEnabled(false),
	fCursorLocation(0, 0),
	fVGADevice(-1),
	fListeners(20)
{
}


/**
 * @brief Destructor; reference-counted members are released by their smart pointers.
 */
HWInterface::~HWInterface()
{
}


/**
 * @brief Validates the MultiLocker; subclasses extend this to set up hardware.
 *
 * @return The MultiLocker init status.
 */
status_t
HWInterface::Initialize()
{
	return MultiLocker::InitCheck();
}


/**
 * @brief Returns a fresh DrawingEngine bound to this interface.
 *
 * @return Newly allocated engine, or NULL on allocation failure. Caller owns.
 */
DrawingEngine*
HWInterface::CreateDrawingEngine()
{
	return new(std::nothrow) DrawingEngine(this);
}


/**
 * @brief Default implementation; subclasses with their own input return a stream.
 *
 * @return Always NULL (use the system default event stream).
 */
EventStream*
HWInterface::CreateEventStream()
{
	return NULL;
}


/**
 * @brief Default implementation; subclasses fill in the accelerant path.
 *
 * @retval B_ERROR Always.
 */
status_t
HWInterface::GetAccelerantPath(BString &path)
{
	return B_ERROR;
}


/**
 * @brief Default implementation; subclasses fill in the kernel driver path.
 *
 * @retval B_ERROR Always.
 */
status_t
HWInterface::GetDriverPath(BString &path)
{
	return B_ERROR;
}


/**
 * @brief Default implementation; subclasses with EDID parsing return a preferred mode.
 *
 * @retval B_NOT_SUPPORTED Always.
 */
status_t
HWInterface::GetPreferredMode(display_mode* mode)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Default implementation; subclasses with EDID parsing fill in monitor info.
 *
 * @retval B_NOT_SUPPORTED Always.
 */
status_t
HWInterface::GetMonitorInfo(monitor_info* info)
{
	return B_NOT_SUPPORTED;
}


// #pragma mark -


/**
 * @brief Replaces the cursor sprite, invalidating both the old and new positions.
 *
 * Acquires the floating-overlays lock so the cursor change is atomic with
 * respect to draw operations that hide and restore the cursor.
 *
 * @param cursor New cursor sprite (caller owns; reference counted internally).
 */
void
HWInterface::SetCursor(ServerCursor* cursor)
{
	if (!fFloatingOverlaysLock.Lock())
		return;

	if (fCursor.Get() != cursor) {
		BRect oldFrame = _CursorFrame();

		fCursor.SetTo(cursor);

		Invalidate(oldFrame);

		_AdoptDragBitmap();
		Invalidate(_CursorFrame());
	}
	fFloatingOverlaysLock.Unlock();
}


/**
 * @brief Returns the current cursor sprite (without the drag bitmap composited).
 *
 * @return Reference to the current cursor; NULL when none is set.
 */
ServerCursorReference
HWInterface::Cursor() const
{
	if (!fFloatingOverlaysLock.Lock())
		return ServerCursorReference(NULL);

	fFloatingOverlaysLock.Unlock();
	return fCursor;
}


/**
 * @brief Returns the cursor with the optional drag bitmap composited on top.
 *
 * @return Reference to the composed cursor; NULL when none is set.
 */
ServerCursorReference
HWInterface::CursorAndDragBitmap() const
{
	if (!fFloatingOverlaysLock.Lock())
		return ServerCursorReference(NULL);

	fFloatingOverlaysLock.Unlock();
	return fCursorAndDragBitmap;
}


/**
 * @brief Shows or hides the cursor.
 *
 * Either draws the cursor and invalidates its frame, or restores the
 * underlying framebuffer pixels and invalidates the previously occupied
 * area, depending on @a visible.
 *
 * @param visible True to show the cursor, false to hide it.
 */
void
HWInterface::SetCursorVisible(bool visible)
{
	if (!fFloatingOverlaysLock.Lock())
		return;

	if (fCursorVisible != visible) {
		// NOTE: _CursorFrame() will
		// return an invalid rect if
		// fCursorVisible == false!
		if (visible) {
			fCursorVisible = visible;
			fCursorObscured = false;
			IntRect r = _CursorFrame();

			_DrawCursor(r);
			Invalidate(r);
		} else {
			IntRect r = _CursorFrame();
			fCursorVisible = visible;

			_RestoreCursorArea();
			Invalidate(r);
		}
	}
	fFloatingOverlaysLock.Unlock();
}


/**
 * @brief Returns the current cursor visibility.
 *
 * @return True when the cursor is visible (not obscured).
 */
bool
HWInterface::IsCursorVisible()
{
	bool visible = true;
	if (fFloatingOverlaysLock.Lock()) {
		visible = fCursorVisible;
		fFloatingOverlaysLock.Unlock();
	}
	return visible;
}


/**
 * @brief Hides the cursor until the user moves the pointer (the "obscured" state).
 *
 * BeOS-style behaviour: typing hides the cursor; the next mouse motion
 * brings it back via MoveCursorTo().
 */
void
HWInterface::ObscureCursor()
{
	if (!fFloatingOverlaysLock.Lock())
		return;

	if (!fCursorObscured) {
		SetCursorVisible(false);
		fCursorObscured = true;
	}
	fFloatingOverlaysLock.Unlock();
}


/**
 * @brief Repositions the cursor to (@a x, @a y) in logical screen coordinates.
 *
 * Unhides the cursor when it was obscured, computes the union of old and
 * new cursor frames for invalidation efficiency, and re-renders the cursor
 * sprite when a software cursor is in use.
 *
 * @param x Target X coordinate.
 * @param y Target Y coordinate.
 */
void
HWInterface::MoveCursorTo(float x, float y)
{
	if (!fFloatingOverlaysLock.Lock())
		return;

	BPoint p(x, y);
	if (p != fCursorLocation) {
		// unhide cursor if it is obscured only
		if (fCursorObscured) {
			SetCursorVisible(true);
		}
		IntRect oldFrame = _CursorFrame();
		fCursorLocation = p;
		if (fCursorVisible) {
			// Invalidate and _DrawCursor would not draw
			// anything if the cursor is hidden
			// (invalid cursor frame), but explicitly
			// testing for it here saves us some cycles
			if (fCursorAreaBackup.IsSet()) {
				// means we have a software cursor which we need to draw
				_RestoreCursorArea();
				_DrawCursor(_CursorFrame());
			}
			IntRect newFrame = _CursorFrame();
			if (newFrame.Intersects(oldFrame))
				Invalidate(oldFrame | newFrame);
			else {
				Invalidate(oldFrame);
				Invalidate(newFrame);
			}
		}
	}
	fFloatingOverlaysLock.Unlock();
}


/**
 * @brief Returns the current cursor position in logical screen coordinates.
 */
BPoint
HWInterface::CursorPosition()
{
	BPoint location;
	if (fFloatingOverlaysLock.Lock()) {
		location = fCursorLocation;
		fFloatingOverlaysLock.Unlock();
	}
	return location;
}


/**
 * @brief Sets the drag bitmap composited under the cursor during drag-and-drop.
 *
 * @param bitmap            Bitmap to display attached to the cursor; pass
 *                          NULL to clear an existing drag bitmap.
 * @param offsetFromCursor  Offset from the cursor's hot-spot to the
 *                          bitmap's top-left, in pixels.
 */
void
HWInterface::SetDragBitmap(const ServerBitmap* bitmap,
	const BPoint& offsetFromCursor)
{
	if (fFloatingOverlaysLock.Lock()) {
		fDragBitmap.SetTo((ServerBitmap*)bitmap, false);
		fDragBitmapOffset = offsetFromCursor;
		_AdoptDragBitmap();
		fFloatingOverlaysLock.Unlock();
	}
}


// #pragma mark -


/**
 * @brief Returns the buffer that DrawingEngines should target.
 *
 * @return BackBuffer() when double-buffered, FrontBuffer() otherwise.
 */
RenderingBuffer*
HWInterface::DrawingBuffer() const
{
	if (IsDoubleBuffered())
		return BackBuffer();
	return FrontBuffer();
}


/**
 * @brief Schedules every rectangle in @a region for refresh.
 *
 * @param region Region to invalidate; rectangles are processed in order.
 * @return       B_OK on success, otherwise the first error from Invalidate().
 * @note  The interface must already be locked.
 */
status_t
HWInterface::InvalidateRegion(const BRegion& region)
{
	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		status_t result = Invalidate(region.RectAt(i));
		if (result != B_OK)
			return result;
	}

	return B_OK;
}


/**
 * @brief Schedules @a frame for refresh.
 *
 * In double-buffered mode the call is forwarded to CopyBackToFront().
 * Single-buffered mode is a no-op since the front buffer is already updated.
 *
 * @param frame Rectangle to refresh.
 * @note  The interface must already be locked.
 */
status_t
HWInterface::Invalidate(const BRect& frame)
{
	if (IsDoubleBuffered())
		return CopyBackToFront(frame);

	return B_OK;
}


/**
 * @brief Performs the actual back-to-front blit for the given @a frame.
 *
 * Clips @a frame against the back buffer bounds, excludes the area
 * currently covered by the cursor (which is restored separately), copies
 * the resulting region, and finally redraws the cursor on top.
 *
 * @param frame Rectangle to copy.
 * @retval B_OK         Copy completed.
 * @retval B_NO_INIT    Either buffer is missing.
 * @retval B_BAD_VALUE  The clipped @a frame is empty.
 * @note  The interface must already be locked.
 */
status_t
HWInterface::CopyBackToFront(const BRect& frame)
{
	RenderingBuffer* frontBuffer = FrontBuffer();
	RenderingBuffer* backBuffer = BackBuffer();

	if (!backBuffer || !frontBuffer)
		return B_NO_INIT;

	// we need to mess with the area, but it is const
	IntRect area(frame);
	IntRect bufferClip(backBuffer->Bounds());

	if (area.IsValid() && area.Intersects(bufferClip)) {

		// make sure we don't copy out of bounds
		area = bufferClip & area;

		bool cursorLocked = fFloatingOverlaysLock.Lock();

		BRegion region((BRect)area);
		if (IsDoubleBuffered())
			region.Exclude((clipping_rect)_CursorFrame());

		_CopyBackToFront(region);

		_DrawCursor(area);

		if (cursorLocked)
			fFloatingOverlaysLock.Unlock();

		return B_OK;
	}
	return B_BAD_VALUE;
}


/**
 * @brief Default copy implementation: walks @a region and dispatches each rect to _CopyToFront().
 *
 * Subclasses with native blitter support override this to use hardware
 * acceleration; the default uses CPU memcpy via _CopyToFront().
 *
 * @param region Region to copy from back buffer to front buffer.
 */
void
HWInterface::_CopyBackToFront(/*const*/ BRegion& region)
{
	RenderingBuffer* backBuffer = BackBuffer();

	uint32 srcBPR = backBuffer->BytesPerRow();
	uint8* src = (uint8*)backBuffer->Bits();

	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		clipping_rect r = region.RectAtInt(i);
		// offset to left top pixel in source buffer (always B_RGBA32)
		uint8* srcOffset = src + r.top * srcBPR + r.left * 4;
		_CopyToFront(srcOffset, srcBPR, r.left, r.top, r.right, r.bottom);
	}
}


// #pragma mark -


/**
 * @brief Default overlay channel acquisition; subclasses with overlay support override it.
 *
 * @return Always NULL (no overlay channels available).
 */
overlay_token
HWInterface::AcquireOverlayChannel()
{
	return NULL;
}


/**
 * @brief Default overlay channel release; no-op.
 */
void
HWInterface::ReleaseOverlayChannel(overlay_token token)
{
}


/**
 * @brief Default overlay restriction query.
 *
 * @retval B_NOT_SUPPORTED Always.
 */
status_t
HWInterface::GetOverlayRestrictions(const Overlay* overlay,
	overlay_restrictions* restrictions)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Default overlay capability check.
 *
 * @return Always false.
 */
bool
HWInterface::CheckOverlayRestrictions(int32 width, int32 height,
	color_space colorSpace)
{
	return false;
}


/**
 * @brief Default overlay buffer allocation.
 *
 * @return Always NULL.
 */
const overlay_buffer*
HWInterface::AllocateOverlayBuffer(int32 width, int32 height, color_space space)
{
	return NULL;
}


/**
 * @brief Default overlay buffer release; no-op.
 */
void
HWInterface::FreeOverlayBuffer(const overlay_buffer* buffer)
{
}


/**
 * @brief Default overlay configure; no-op.
 */
void
HWInterface::ConfigureOverlay(Overlay* overlay)
{
}


/**
 * @brief Default overlay hide; no-op.
 */
void
HWInterface::HideOverlay(Overlay* overlay)
{
}


// #pragma mark -


/**
 * @brief Hides the cursor / drag bitmap when it intersects @a area, in single-buffered mode.
 *
 * Used by DrawingEngine to keep cursor pixels out of an upcoming draw. In
 * double-buffered mode this is a no-op (the cursor is composited at copy
 * time).
 *
 * @param area Rectangle about to be drawn.
 * @return     True when the cursor was hidden (caller must call ShowFloatingOverlays()).
 */
bool
HWInterface::HideFloatingOverlays(const BRect& area)
{
	if (IsDoubleBuffered())
		return false;
	if (!fFloatingOverlaysLock.Lock())
		return false;
	if (fCursorAreaBackup.IsSet() && !fCursorAreaBackup->cursor_hidden) {
		BRect backupArea(fCursorAreaBackup->left, fCursorAreaBackup->top,
			fCursorAreaBackup->right, fCursorAreaBackup->bottom);
		if (area.Intersects(backupArea)) {
			_RestoreCursorArea();
			// do not unlock the cursor lock
			return true;
		}
	}
	fFloatingOverlaysLock.Unlock();
	return false;
}


/**
 * @brief Hides the cursor unconditionally in single-buffered mode.
 *
 * @return True when the cursor was hidden (caller must call ShowFloatingOverlays()).
 */
bool
HWInterface::HideFloatingOverlays()
{
	if (IsDoubleBuffered())
		return false;
	if (!fFloatingOverlaysLock.Lock())
		return false;

	_RestoreCursorArea();
	return true;
}


/**
 * @brief Restores the cursor previously hidden by HideFloatingOverlays().
 */
void
HWInterface::ShowFloatingOverlays()
{
	if (fCursorAreaBackup.IsSet() && fCursorAreaBackup->cursor_hidden)
		_DrawCursor(_CursorFrame());

	fFloatingOverlaysLock.Unlock();
}


// #pragma mark -


/**
 * @brief Adds @a listener to the set notified about framebuffer / mode changes.
 *
 * @param listener Listener to add. Duplicates are silently ignored.
 * @return True when the listener was added, false otherwise.
 */
bool
HWInterface::AddListener(HWInterfaceListener* listener)
{
	if (listener && !fListeners.HasItem(listener))
		return fListeners.AddItem(listener);
	return false;
}


/**
 * @brief Removes @a listener from the notification set.
 */
void
HWInterface::RemoveListener(HWInterfaceListener* listener)
{
	fListeners.RemoveItem(listener);
}


// #pragma mark -


/**
 * @brief Default software cursor renderer.
 *
 * Composes the cursor sprite onto the back buffer at @a area, optionally
 * saving the underlying pixels into @c fCursorAreaBackup so the area can be
 * restored when the cursor moves. The blend assumes the back buffer alpha
 * is 255 and the cursor bitmap is pre-multiplied.
 *
 * @param area Area where the cursor may be drawn; the cursor is clipped to
 *             both this area and the framebuffer bounds.
 * @note Subclasses with hardware cursor support typically override this.
 */
void
HWInterface::_DrawCursor(IntRect area) const
{
	RenderingBuffer* backBuffer = DrawingBuffer();
	if (!backBuffer || !area.IsValid())
		return;

	IntRect cf = _CursorFrame();

	// make sure we don't copy out of bounds
	area = backBuffer->Bounds() & area;

	if (cf.IsValid() && area.Intersects(cf)) {

		// clip to common area
		area = area & cf;

		int32 width = area.right - area.left + 1;
		int32 height = area.bottom - area.top + 1;

		// make a bitmap from the backbuffer
		// that has the cursor blended on top of it

		// blending buffer
		uint8* buffer = new(std::nothrow) uint8[width * height * 4];
			// TODO: cache this buffer
		if (buffer == NULL)
			return;

		// offset into back buffer
		uint8* src = (uint8*)backBuffer->Bits();
		uint32 srcBPR = backBuffer->BytesPerRow();
		src += area.top * srcBPR + area.left * 4;

		// offset into cursor bitmap
		uint8* crs = (uint8*)fCursorAndDragBitmap->Bits();
		uint32 crsBPR = fCursorAndDragBitmap->BytesPerRow();
		// since area is clipped to cf,
		// the diff between area.top and cf.top is always positive,
		// same for diff between area.left and cf.left
		crs += (area.top - (int32)floorf(cf.top)) * crsBPR
				+ (area.left - (int32)floorf(cf.left)) * 4;

		uint8* dst = buffer;

		if (fCursorAreaBackup.IsSet() && fCursorAreaBackup->buffer
			&& fFloatingOverlaysLock.Lock()) {
			fCursorAreaBackup->cursor_hidden = false;
			// remember which area the backup contains
			fCursorAreaBackup->left = area.left;
			fCursorAreaBackup->top = area.top;
			fCursorAreaBackup->right = area.right;
			fCursorAreaBackup->bottom = area.bottom;
			uint8* bup = fCursorAreaBackup->buffer;
			uint32 bupBPR = fCursorAreaBackup->bpr;

			// blending and backup of drawing buffer
			for (int32 y = area.top; y <= area.bottom; y++) {
				uint8* s = src;
				uint8* c = crs;
				uint8* d = dst;
				uint8* b = bup;

				for (int32 x = area.left; x <= area.right; x++) {
					*(uint32*)b = *(uint32*)s;
					// assumes backbuffer alpha = 255
					// assuming pre-multiplied cursor bitmap
					int a = 255 - c[3];
					d[0] = ((int)(b[0] * a + 255) >> 8) + c[0];
					d[1] = ((int)(b[1] * a + 255) >> 8) + c[1];
					d[2] = ((int)(b[2] * a + 255) >> 8) + c[2];

					s += 4;
					c += 4;
					d += 4;
					b += 4;
				}
				crs += crsBPR;
				src += srcBPR;
				dst += width * 4;
				bup += bupBPR;
			}
			fFloatingOverlaysLock.Unlock();
		} else {
			// blending
			for (int32 y = area.top; y <= area.bottom; y++) {
				uint8* s = src;
				uint8* c = crs;
				uint8* d = dst;
				for (int32 x = area.left; x <= area.right; x++) {
					// assumes backbuffer alpha = 255
					// assuming pre-multiplied cursor bitmap
					uint8 a = 255 - c[3];
					d[0] = ((s[0] * a + 255) >> 8) + c[0];
					d[1] = ((s[1] * a + 255) >> 8) + c[1];
					d[2] = ((s[2] * a + 255) >> 8) + c[2];

					s += 4;
					c += 4;
					d += 4;
				}
				crs += crsBPR;
				src += srcBPR;
				dst += width * 4;
			}
		}
		// copy result to front buffer
		_CopyToFront(buffer, width * 4, area.left, area.top, area.right,
			area.bottom);

		delete[] buffer;
	}
}


/**
 * @brief Copies a B_RGBA32 source block to the front buffer with color space conversion.
 *
 * Handles every front buffer color space the app_server can be configured
 * to drive: B_RGB32 / B_RGBA32 (raw memcpy), B_RGB24, B_RGB16, B_RGB15 /
 * B_RGBA15, B_CMAP8 (palette lookup), and B_GRAY8 (including the VGA 16-color
 * planar fallback). Unsupported front buffer formats log a warning.
 *
 * @param src      Pointer to the first row of B_RGBA32 source pixels.
 * @param srcBPR   Source row stride in bytes.
 * @param x        Destination left coordinate.
 * @param y        Destination top coordinate.
 * @param right    Destination right coordinate (inclusive).
 * @param bottom   Destination bottom coordinate (inclusive).
 *
 * @note The source pointer is expected to already be at the right offset
 *       inside the source buffer; only the destination offset is computed
 *       from (@a x, @a y).
 */
void
HWInterface::_CopyToFront(uint8* src, uint32 srcBPR, int32 x, int32 y,
	int32 right, int32 bottom) const
{
	RenderingBuffer* frontBuffer = FrontBuffer();

	uint8* dst = (uint8*)frontBuffer->Bits();
	uint32 dstBPR = frontBuffer->BytesPerRow();

	// transfer, handle colorspace conversion
	switch (frontBuffer->ColorSpace()) {
		case B_RGB32:
		case B_RGBA32:
		{
			int32 bytes = (right - x + 1) * 4;

			if (bytes > 0) {
				// offset to left top pixel in dest buffer
				dst += y * dstBPR + x * 4;
				// copy
				for (; y <= bottom; y++) {
					// bytes is guaranteed to be multiple of 4
					memcpy(dst, src, bytes);
					dst += dstBPR;
					src += srcBPR;
				}
			}
			break;
		}

		case B_RGB24:
		{
			// offset to left top pixel in dest buffer
			dst += y * dstBPR + x * 3;
			int32 left = x;
			// copy
			for (; y <= bottom; y++) {
				uint8* srcHandle = src;
				uint8* dstHandle = dst;
				for (x = left; x <= right; x++) {
					dstHandle[0] = srcHandle[0];
					dstHandle[1] = srcHandle[1];
					dstHandle[2] = srcHandle[2];
					dstHandle += 3;
					srcHandle += 4;
				}
				dst += dstBPR;
				src += srcBPR;
			}
			break;
		}

		case B_RGB16:
		{
			// offset to left top pixel in dest buffer
			dst += y * dstBPR + x * 2;
			int32 left = x;
			// copy
			// TODO: assumes BGR order, does this work on big endian as well?
			for (; y <= bottom; y++) {
				uint8* srcHandle = src;
				uint16* dstHandle = (uint16*)dst;
				for (x = left; x <= right; x++) {
					*dstHandle = (uint16)(((srcHandle[2] & 0xf8) << 8)
						| ((srcHandle[1] & 0xfc) << 3) | (srcHandle[0] >> 3));
					dstHandle ++;
					srcHandle += 4;
				}
				dst += dstBPR;
				src += srcBPR;
			}
			break;
		}

		case B_RGB15:
		case B_RGBA15:
		{
			// offset to left top pixel in dest buffer
			dst += y * dstBPR + x * 2;
			int32 left = x;
			// copy
			// TODO: assumes BGR order, does this work on big endian as well?
			for (; y <= bottom; y++) {
				uint8* srcHandle = src;
				uint16* dstHandle = (uint16*)dst;
				for (x = left; x <= right; x++) {
					*dstHandle = (uint16)(((srcHandle[2] & 0xf8) << 7)
						| ((srcHandle[1] & 0xf8) << 2) | (srcHandle[0] >> 3));
					dstHandle ++;
					srcHandle += 4;
				}
				dst += dstBPR;
				src += srcBPR;
			}
			break;
		}

		case B_CMAP8:
		{
			const color_map *colorMap = SystemColorMap();
			// offset to left top pixel in dest buffer
			dst += y * dstBPR + x;
			int32 left = x;
			uint16 index;
			// copy
			// TODO: assumes BGR order again
			for (; y <= bottom; y++) {
				uint8* srcHandle = src;
				uint8* dstHandle = dst;
				for (x = left; x <= right; x++) {
					index = ((srcHandle[2] & 0xf8) << 7)
						| ((srcHandle[1] & 0xf8) << 2) | (srcHandle[0] >> 3);
					*dstHandle = colorMap->index_map[index];
					dstHandle ++;
					srcHandle += 4;
				}
				dst += dstBPR;
				src += srcBPR;
			}

			break;
		}

		case B_GRAY8:
			if (frontBuffer->Width() > dstBPR) {
				// VGA 16 color grayscale planar mode
				if (fVGADevice >= 0) {
					vga_planar_blit_args args;
					args.source = src;
					args.source_bytes_per_row = srcBPR;
					args.left = x;
					args.top = y;
					args.right = right;
					args.bottom = bottom;
					if (ioctl(fVGADevice, VGA_PLANAR_BLIT, &args, sizeof(args))
							== 0)
						break;
				}

				// Since we cannot set the plane, we do monochrome output
				dst += y * dstBPR + x / 8;
				int32 left = x;

				// TODO: this is awfully slow...
				// TODO: assumes BGR order
				for (; y <= bottom; y++) {
					uint8* srcHandle = src;
					uint8* dstHandle = dst;
					uint8 current8 = dstHandle[0];
						// we store 8 pixels before writing them back

					for (x = left; x <= right; x++) {
						uint8 pixel = (308 * srcHandle[2] + 600 * srcHandle[1]
							+ 116 * srcHandle[0]) / 1024;
						srcHandle += 4;

						if (pixel > 128)
							current8 |= 0x80 >> (x & 7);
						else
							current8 &= ~(0x80 >> (x & 7));

						if ((x & 7) == 7) {
							// last pixel in 8 pixel group
							dstHandle[0] = current8;
							dstHandle++;
							current8 = dstHandle[0];
						}
					}

					if (x & 7) {
						// last pixel has not been written yet
						dstHandle[0] = current8;
					}
					dst += dstBPR;
					src += srcBPR;
				}
			} else {
				// offset to left top pixel in dest buffer
				dst += y * dstBPR + x;
				int32 left = x;
				// copy
				// TODO: assumes BGR order, does this work on big endian as well?
				for (; y <= bottom; y++) {
					uint8* srcHandle = src;
					uint8* dstHandle = dst;
					for (x = left; x <= right; x++) {
						*dstHandle = (308 * srcHandle[2] + 600 * srcHandle[1]
							+ 116 * srcHandle[0]) / 1024;
						dstHandle ++;
						srcHandle += 4;
					}
					dst += dstBPR;
					src += srcBPR;
				}
			}
			break;

		default:
			fprintf(stderr, "HWInterface::CopyBackToFront() - unsupported "
				"front buffer format! (0x%x)\n", frontBuffer->ColorSpace());
			break;
	}
}


/**
 * @brief Returns the rectangle currently occupied by the visible software cursor.
 *
 * @return Cursor frame in framebuffer coordinates, or an invalid rect when
 *         no cursor is set, the cursor is hidden, or a hardware cursor is
 *         in use.
 * @note   The interface must already be locked.
 */
IntRect
HWInterface::_CursorFrame() const
{
	IntRect frame(0, 0, -1, -1);
	if (fCursorAndDragBitmap && fCursorVisible && !fHardwareCursorEnabled) {
		frame = fCursorAndDragBitmap->Bounds();
		frame.OffsetTo(fCursorLocation - fCursorAndDragBitmap->GetHotSpot());
	}
	return frame;
}


/**
 * @brief Restores the framebuffer pixels saved when the software cursor was drawn.
 */
void
HWInterface::_RestoreCursorArea() const
{
	if (fCursorAreaBackup.IsSet() && !fCursorAreaBackup->cursor_hidden) {
		_CopyToFront(fCursorAreaBackup->buffer, fCursorAreaBackup->bpr,
			fCursorAreaBackup->left, fCursorAreaBackup->top,
			fCursorAreaBackup->right, fCursorAreaBackup->bottom);

		fCursorAreaBackup->cursor_hidden = true;
	}
}


/**
 * @brief Composes the drag bitmap onto the cursor sprite.
 *
 * Builds @c fCursorAndDragBitmap as the union of the cursor and the drag
 * bitmap, blends them into a single sprite using non-premultiplied alpha,
 * and finally pre-multiplies the result so subsequent _DrawCursor() calls
 * can use the cheap pre-multiplied blend.
 *
 * @note Currently only supports B_RGB32 / B_RGBA32 drag bitmaps; others log
 *       an error and are ignored.
 */
void
HWInterface::_AdoptDragBitmap()
{
	// TODO: support other colorspaces/convert bitmap
	if (fDragBitmap && !(fDragBitmap->ColorSpace() == B_RGB32
		|| fDragBitmap->ColorSpace() == B_RGBA32)) {
		fprintf(stderr, "HWInterface::_AdoptDragBitmap() - bitmap has yet "
			"unsupported colorspace\n");
		return;
	}

	_RestoreCursorArea();
	BRect oldCursorFrame = _CursorFrame();

	if (fDragBitmap != NULL && fDragBitmap->Bounds().Width() > 0 && fDragBitmap->Bounds().Height() > 0) {
		BRect bitmapFrame = fDragBitmap->Bounds();
		if (fCursor) {
			// put bitmap frame and cursor frame into the same
			// coordinate space (the cursor location is the origin)
			bitmapFrame.OffsetTo(BPoint(-fDragBitmapOffset.x, -fDragBitmapOffset.y));

			BRect cursorFrame(fCursor->Bounds());
			BPoint hotspot(fCursor->GetHotSpot());
				// the hotspot is at the origin
			cursorFrame.OffsetTo(-hotspot.x, -hotspot.y);

			BRect combindedBounds = bitmapFrame | cursorFrame;

			BPoint shift;
			shift.x = -combindedBounds.left;
			shift.y = -combindedBounds.top;

			combindedBounds.OffsetBy(shift);
			cursorFrame.OffsetBy(shift);
			bitmapFrame.OffsetBy(shift);

			fCursorAndDragBitmap.SetTo(new(std::nothrow) ServerCursor(combindedBounds,
				fDragBitmap->ColorSpace(), 0, shift), true);

			uint8* dst = fCursorAndDragBitmap ? (uint8*)fCursorAndDragBitmap->Bits() : NULL;
			if (dst == NULL) {
				// Oops, we could not allocate memory for the drag bitmap.
				// Let's show the cursor only.
				fCursorAndDragBitmap = fCursor;
			} else {
				// clear the combined buffer
				uint32 dstBPR = fCursorAndDragBitmap->BytesPerRow();

				memset(dst, 0, fCursorAndDragBitmap->BitsLength());

				// put drag bitmap into combined buffer
				uint8* src = (uint8*)fDragBitmap->Bits();
				uint32 srcBPR = fDragBitmap->BytesPerRow();

				dst += (int32)bitmapFrame.top * dstBPR
					+ (int32)bitmapFrame.left * 4;

				uint32 width = bitmapFrame.IntegerWidth() + 1;
				uint32 height = bitmapFrame.IntegerHeight() + 1;

				for (uint32 y = 0; y < height; y++) {
					memcpy(dst, src, srcBPR);
					dst += dstBPR;
					src += srcBPR;
				}

				// compose cursor into combined buffer
				dst = (uint8*)fCursorAndDragBitmap->Bits();
				dst += (int32)cursorFrame.top * dstBPR
					+ (int32)cursorFrame.left * 4;

				src = (uint8*)fCursor->Bits();
				srcBPR = fCursor->BytesPerRow();

				width = cursorFrame.IntegerWidth() + 1;
				height = cursorFrame.IntegerHeight() + 1;

				for (uint32 y = 0; y < height; y++) {
					uint8* d = dst;
					uint8* s = src;
					for (uint32 x = 0; x < width; x++) {
						// takes two semi-transparent pixels
						// with unassociated alpha (not pre-multiplied)
						// and stays within non-premultiplied color space
						if (s[3] > 0) {
							if (s[3] == 255) {
								d[0] = s[0];
								d[1] = s[1];
								d[2] = s[2];
								d[3] = 255;
							} else {
								uint8 alphaRest = 255 - s[3];
								uint32 alphaTemp
									= (65025 - alphaRest * (255 - d[3]));
								uint32 alphaDest = d[3] * alphaRest;
								uint32 alphaSrc = 255 * s[3];
								d[0] = (d[0] * alphaDest + s[0] * alphaSrc)
									/ alphaTemp;
								d[1] = (d[1] * alphaDest + s[1] * alphaSrc)
									/ alphaTemp;
								d[2] = (d[2] * alphaDest + s[2] * alphaSrc)
									/ alphaTemp;
								d[3] = alphaTemp / 255;
							}
						}
						// TODO: make sure the alpha is always upside down,
						// then it doesn't need to be done when drawing the cursor
						// (see _DrawCursor())
						//					d[3] = 255 - d[3];
						d += 4;
						s += 4;
					}
					dst += dstBPR;
					src += srcBPR;
				}

				// handle pre-multiplication with alpha
				// for faster compositing during cursor drawing
				width = combindedBounds.IntegerWidth() + 1;
				height = combindedBounds.IntegerHeight() + 1;

				dst = (uint8*)fCursorAndDragBitmap->Bits();

				for (uint32 y = 0; y < height; y++) {
					uint8* d = dst;
					for (uint32 x = 0; x < width; x++) {
						d[0] = (d[0] * d[3]) >> 8;
						d[1] = (d[1] * d[3]) >> 8;
						d[2] = (d[2] * d[3]) >> 8;
						d += 4;
					}
					dst += dstBPR;
				}
			}
		} else {
			fCursorAndDragBitmap.SetTo(new ServerCursor(fDragBitmap->Bits(),
				bitmapFrame.IntegerWidth() + 1, bitmapFrame.IntegerHeight() + 1,
				fDragBitmap->ColorSpace()), true);
			fCursorAndDragBitmap->SetHotSpot(BPoint(-fDragBitmapOffset.x, -fDragBitmapOffset.y));
		}
	} else {
		fCursorAndDragBitmap = fCursor;
	}

	Invalidate(oldCursorFrame);

	fCursorAreaBackup.Unset();

	if (!fCursorAndDragBitmap)
		return;

	if (fCursorAndDragBitmap && !IsDoubleBuffered()) {
		BRect cursorBounds = fCursorAndDragBitmap->Bounds();
		fCursorAreaBackup.SetTo(new buffer_clip(cursorBounds.IntegerWidth() + 1,
			cursorBounds.IntegerHeight() + 1));
		if (fCursorAreaBackup->buffer == NULL)
			fCursorAreaBackup.Unset();
	}
 	_DrawCursor(_CursorFrame());
}


/**
 * @brief Calls FrameBufferChanged() on every registered listener.
 *
 * The listener list is snapshotted into a local copy first so that
 * notifications can re-enter Add/Remove safely.
 */
void
HWInterface::_NotifyFrameBufferChanged()
{
	BList listeners(fListeners);
	int32 count = listeners.CountItems();
	for (int32 i = 0; i < count; i++) {
		HWInterfaceListener* listener
			= (HWInterfaceListener*)listeners.ItemAtFast(i);
		listener->FrameBufferChanged();
	}
}


/**
 * @brief Calls ScreenChanged() on every registered listener.
 */
void
HWInterface::_NotifyScreenChanged()
{
	BList listeners(fListeners);
	int32 count = listeners.CountItems();
	for (int32 i = 0; i < count; i++) {
		HWInterfaceListener* listener
			= (HWInterfaceListener*)listeners.ItemAtFast(i);
		listener->ScreenChanged(this);
	}
}


/**
 * @brief Sanity-checks @a mode against the minimum supported screen size.
 *
 * @param mode Mode to validate.
 * @return     True when the mode is at least 320x200 (more checks may follow).
 * @todo More of those!
 */
/*static*/ bool
HWInterface::_IsValidMode(const display_mode& mode)
{
	// TODO: more of those!
	if (mode.virtual_width < 320
		|| mode.virtual_height < 200)
		return false;

	return true;
}

