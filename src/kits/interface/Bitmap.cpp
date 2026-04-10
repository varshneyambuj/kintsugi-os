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
 *   Copyright 2001-2009, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold (bonefish@users.sf.net)
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file Bitmap.cpp
 * @brief Implementation of BBitmap, an off-screen bitmap image container
 *
 * BBitmap holds a rectangular pixel buffer in a specified color space. It can
 * be created as a child of an off-screen BWindow to allow drawing into it, or
 * used directly as a raw pixel buffer for image manipulation.
 *
 * @see BView, BWindow, BGradient
 */


#include <Bitmap.h>

#include <algorithm>
#include <limits.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <GraphicsDefs.h>
#include <Locker.h>
#include <View.h>
#include <Window.h>

#include <ApplicationPrivate.h>
#include <AppServerLink.h>
#include <ObjectList.h>
#include <ServerMemoryAllocator.h>
#include <ServerProtocol.h>
#include <locks.h>

#include "ColorConversion.h"
#include "BitmapPrivate.h"


using namespace BPrivate;


/** @brief Global list of all live BBitmap objects, used to reconnect them after an app_server restart. */
static BObjectList<BBitmap> sBitmapList;

/** @brief Mutex protecting sBitmapList against concurrent add/remove. */
static mutex sBitmapListLock = MUTEX_INITIALIZER("BBitmap list");


/**
 * @brief Reconnects all live BBitmap objects to the app_server.
 *
 * Called after an app_server restart to re-register every bitmap that was
 * previously allocated via the server.  Each bitmap in sBitmapList has its
 * _ReconnectToAppServer() helper invoked while the list lock is held.
 *
 * @see BBitmap::_ReconnectToAppServer()
 */
void
reconnect_bitmaps_to_app_server()
{
	MutexLocker _(sBitmapListLock);
	for (int32 i = 0; i < sBitmapList.CountItems(); i++) {
		BBitmap::Private bitmap(sBitmapList.ItemAt(i));
		bitmap.ReconnectToAppServer();
	}
}


/**
 * @brief Constructs a Private accessor for the given BBitmap.
 *
 * @param bitmap The BBitmap instance to wrap.
 */
BBitmap::Private::Private(BBitmap* bitmap)
	:
	fBitmap(bitmap)
{
}


/**
 * @brief Forwards the reconnect request to the wrapped BBitmap.
 *
 * @see BBitmap::_ReconnectToAppServer()
 */
void
BBitmap::Private::ReconnectToAppServer()
{
	fBitmap->_ReconnectToAppServer();
}


/**
 * @brief Returns the unpadded bytes per row for a given color space and width.
 *
 * Computes the minimum number of bytes required to store one row of pixel data
 * for the specified color space without any alignment padding.
 *
 * @param colorSpace The pixel color space.
 * @param width      The row width in pixels.
 * @return The number of bytes needed to store one row, or 0 if the color space
 *         is not supported.
 * @see get_bytes_per_row()
 */
static inline int32
get_raw_bytes_per_row(color_space colorSpace, int32 width)
{
	int32 bpr = 0;
	switch (colorSpace) {
		// supported
		case B_RGBA64: case B_RGBA64_BIG:
			bpr = 8 * width;
			break;
		case B_RGB48: case B_RGB48_BIG:
			bpr = 6 * width;
			break;
		case B_RGB32: case B_RGBA32:
		case B_RGB32_BIG: case B_RGBA32_BIG:
		case B_UVL32: case B_UVLA32:
		case B_LAB32: case B_LABA32:
		case B_HSI32: case B_HSIA32:
		case B_HSV32: case B_HSVA32:
		case B_HLS32: case B_HLSA32:
		case B_CMY32: case B_CMYA32: case B_CMYK32:
			bpr = 4 * width;
			break;
		case B_RGB24: case B_RGB24_BIG:
		case B_UVL24: case B_LAB24: case B_HSI24:
		case B_HSV24: case B_HLS24: case B_CMY24:
			bpr = 3 * width;
			break;
		case B_RGB16:		case B_RGB15:		case B_RGBA15:
		case B_RGB16_BIG:	case B_RGB15_BIG:	case B_RGBA15_BIG:
			bpr = 2 * width;
			break;
		case B_CMAP8: case B_GRAY8:
			bpr = width;
			break;
		case B_GRAY1:
			bpr = (width + 7) / 8;
			break;
		case B_YCbCr422: case B_YUV422:
			bpr = (width + 3) / 4 * 8;
			break;
		case B_YCbCr411: case B_YUV411:
			bpr = (width + 3) / 4 * 6;
			break;
		case B_YCbCr444: case B_YUV444:
			bpr = (width + 3) / 4 * 12;
			break;
		case B_YCbCr420: case B_YUV420:
			bpr = (width + 3) / 4 * 6;
			break;
		case B_YUV9:
			bpr = (width + 15) / 16 * 18;
			break;
		// unsupported
		case B_NO_COLOR_SPACE:
		case B_YUV12:
			break;
	}
	return bpr;
}


namespace BPrivate {

/**
 * @brief Returns the padded bytes per row for a given color space and width.
 *
 * Computes the number of bytes required to store one row of pixel data,
 * including alignment padding to the nearest 32-bit (int32) boundary.
 *
 * @param colorSpace The pixel color space.
 * @param width      The row width in pixels.
 * @return The number of bytes needed to store one padded row, or 0 if the
 *         color space is not supported.
 * @see get_raw_bytes_per_row()
 */
int32
get_bytes_per_row(color_space colorSpace, int32 width)
{
	int32 bpr = get_raw_bytes_per_row(colorSpace, width);
	// align to int32
	bpr = (bpr + 3) & 0x7ffffffc;
	return bpr;
}

}	// namespace BPrivate


//	#pragma mark -


/**
 * @brief Constructs a BBitmap with full control over flags and layout.
 *
 * Creates an off-screen bitmap of the specified dimensions, color space,
 * and creation flags. If @a bytesPerRow is B_ANY_BYTES_PER_ROW, the row
 * stride is chosen automatically to satisfy alignment requirements.
 *
 * @param bounds      The bitmap dimensions (top-left is typically (0,0)).
 * @param flags       Bitmap creation flags (e.g. B_BITMAP_ACCEPTS_VIEWS,
 *                    B_BITMAP_IS_CONTIGUOUS, B_BITMAP_CLEAR_TO_WHITE).
 * @param colorSpace  The pixel color space for the bitmap data.
 * @param bytesPerRow The desired row stride in bytes, or B_ANY_BYTES_PER_ROW
 *                    to let the implementation choose.
 * @param screenID    The screen this bitmap is associated with; pass
 *                    B_MAIN_SCREEN_ID for the primary display.
 *
 * @see _InitObject(), B_ANY_BYTES_PER_ROW
 */
BBitmap::BBitmap(BRect bounds, uint32 flags, color_space colorSpace,
		int32 bytesPerRow, screen_id screenID)
	:
	fBasePointer(NULL),
	fSize(0),
	fColorSpace(B_NO_COLOR_SPACE),
	fBounds(0, 0, -1, -1),
	fBytesPerRow(0),
	fWindow(NULL),
	fServerToken(-1),
	fAreaOffset(-1),
	fArea(-1),
	fServerArea(-1),
	fFlags(0),
	fInitError(B_NO_INIT)
{
	_InitObject(bounds, colorSpace, flags, bytesPerRow, screenID);
}


/**
 * @brief Constructs a BBitmap with optional view acceptance and contiguous memory.
 *
 * Creates an off-screen bitmap of the specified dimensions and color space.
 * The row stride is chosen automatically. Flags are derived from the two
 * boolean parameters.
 *
 * @param bounds           The bitmap dimensions.
 * @param colorSpace       The pixel color space for the bitmap data.
 * @param acceptsViews     If true, BViews can be attached and drawn into
 *                         the bitmap (implies B_BITMAP_ACCEPTS_VIEWS).
 * @param needsContiguous  If true, a physically contiguous memory block is
 *                         requested (implies B_BITMAP_IS_CONTIGUOUS).
 *
 * @see _InitObject(), BBitmap(BRect, uint32, color_space, int32, screen_id)
 */
BBitmap::BBitmap(BRect bounds, color_space colorSpace, bool acceptsViews,
		bool needsContiguous)
	:
	fBasePointer(NULL),
	fSize(0),
	fColorSpace(B_NO_COLOR_SPACE),
	fBounds(0, 0, -1, -1),
	fBytesPerRow(0),
	fWindow(NULL),
	fServerToken(-1),
	fAreaOffset(-1),
	fArea(-1),
	fServerArea(-1),
	fFlags(0),
	fInitError(B_NO_INIT)
{
	int32 flags = (acceptsViews ? B_BITMAP_ACCEPTS_VIEWS : 0)
		| (needsContiguous ? B_BITMAP_IS_CONTIGUOUS : 0);
	_InitObject(bounds, colorSpace, flags, B_ANY_BYTES_PER_ROW,
		B_MAIN_SCREEN_ID);
}


/**
 * @brief Constructs a BBitmap as a deep copy of another bitmap (pointer form).
 *
 * Allocates a new bitmap with the same bounds, color space, and row stride as
 * @a source, then copies its pixel data. Flags are derived from the two boolean
 * parameters; the source's own flags are not inherited.
 *
 * @param source           The bitmap to copy; must be valid (IsValid() == true).
 *                         If NULL or invalid, the constructor initializes to an
 *                         error state without copying any data.
 * @param acceptsViews     If true, BViews can be attached to the new bitmap.
 * @param needsContiguous  If true, a physically contiguous memory block is
 *                         requested for the new bitmap.
 *
 * @see _InitObject(), IsValid()
 */
BBitmap::BBitmap(const BBitmap* source, bool acceptsViews, bool needsContiguous)
	:
	fBasePointer(NULL),
	fSize(0),
	fColorSpace(B_NO_COLOR_SPACE),
	fBounds(0, 0, -1, -1),
	fBytesPerRow(0),
	fWindow(NULL),
	fServerToken(-1),
	fAreaOffset(-1),
	fArea(-1),
	fServerArea(-1),
	fFlags(0),
	fInitError(B_NO_INIT)
{
	if (source && source->IsValid()) {
		int32 flags = (acceptsViews ? B_BITMAP_ACCEPTS_VIEWS : 0)
			| (needsContiguous ? B_BITMAP_IS_CONTIGUOUS : 0);
		_InitObject(source->Bounds(), source->ColorSpace(), flags,
			source->BytesPerRow(), B_MAIN_SCREEN_ID);
		if (InitCheck() == B_OK) {
			memcpy(Bits(), source->Bits(), min_c(BitsLength(),
				source->BitsLength()));
		}
	}
}


/**
 * @brief Constructs a BBitmap as a deep copy of another bitmap with explicit flags.
 *
 * Copies the pixel data from @a source and initialises the new bitmap with the
 * explicitly supplied @a flags instead of inheriting the source's flags.  If
 * @a source is invalid the constructor returns an error state without copying.
 *
 * @param source  The source bitmap (reference form).  Must be valid.
 * @param flags   Creation flags for the new bitmap (e.g. B_BITMAP_ACCEPTS_VIEWS).
 *
 * @see _InitObject(), IsValid()
 */
BBitmap::BBitmap(const BBitmap& source, uint32 flags)
	:
	fBasePointer(NULL),
	fSize(0),
	fColorSpace(B_NO_COLOR_SPACE),
	fBounds(0, 0, -1, -1),
	fBytesPerRow(0),
	fWindow(NULL),
	fServerToken(-1),
	fAreaOffset(-1),
	fArea(-1),
	fServerArea(-1),
	fFlags(0),
	fInitError(B_NO_INIT)
{
	if (!source.IsValid())
		return;

	_InitObject(source.Bounds(), source.ColorSpace(), flags,
		source.BytesPerRow(), B_MAIN_SCREEN_ID);

	if (InitCheck() == B_OK)
		memcpy(Bits(), source.Bits(), min_c(BitsLength(), source.BitsLength()));
}


/**
 * @brief Copy-constructs a BBitmap from another bitmap.
 *
 * Delegates to the copy-assignment operator, which cleans up any previous
 * state and then performs a deep copy of pixel data and metadata.
 *
 * @param source  The bitmap to copy.
 *
 * @see operator=(const BBitmap&)
 */
BBitmap::BBitmap(const BBitmap& source)
	:
	fBasePointer(NULL),
	fSize(0),
	fColorSpace(B_NO_COLOR_SPACE),
	fBounds(0, 0, -1, -1),
	fBytesPerRow(0),
	fWindow(NULL),
	fServerToken(-1),
	fAreaOffset(-1),
	fArea(-1),
	fServerArea(-1),
	fFlags(0),
	fInitError(B_NO_INIT)
{
	*this = source;
}


/**
 * @brief Constructs a BBitmap backed by an existing memory area.
 *
 * Attaches the bitmap to an already-allocated area_id, starting at
 * @a areaOffset bytes into that area.  The area must be owned by the
 * current team; if it belongs to another team the constructor will fail
 * with B_BAD_VALUE.  This constructor is used internally when reconnecting
 * bitmaps to the app_server after a restart.
 *
 * @param area        The area_id of the memory region to use.
 * @param areaOffset  Byte offset within @a area where the pixel data begins.
 * @param bounds      The bitmap dimensions.
 * @param flags       Bitmap creation flags.
 * @param colorSpace  The pixel color space.
 * @param bytesPerRow The row stride in bytes.
 * @param screenID    The screen this bitmap is associated with.
 *
 * @see _InitObject(), reconnect_bitmaps_to_app_server()
 */
BBitmap::BBitmap(area_id area, ptrdiff_t areaOffset, BRect bounds,
	uint32 flags, color_space colorSpace, int32 bytesPerRow,
	screen_id screenID)
	:
	fBasePointer(NULL),
	fSize(0),
	fColorSpace(B_NO_COLOR_SPACE),
	fBounds(0, 0, -1, -1),
	fBytesPerRow(0),
	fWindow(NULL),
	fServerToken(-1),
	fAreaOffset(-1),
	fArea(-1),
	fServerArea(-1),
	fFlags(0),
	fInitError(B_NO_INIT)
{
	_InitObject(bounds, colorSpace, flags,
		bytesPerRow, screenID, area, areaOffset);
}


/**
 * @brief Destroys the BBitmap and releases all associated resources.
 *
 * Frees the pixel buffer, deletes the associated off-screen BWindow (if any),
 * notifies the app_server to release its server-side bitmap handle, and removes
 * the bitmap from the global reconnect list.
 *
 * @see _CleanUp()
 */
BBitmap::~BBitmap()
{
	_CleanUp();
}


/**
 * @brief Constructs a BBitmap from a BMessage archive.
 *
 * Reads the bitmap's bounds, color space, creation flags, and row stride from
 * @a data, recreates the pixel buffer, and copies the raw pixel data stored
 * under the "_data" key.  If the B_BITMAP_ACCEPTS_VIEWS flag is set, any
 * archived child views stored under "_views" are also reinstantiated and added.
 *
 * @param data  The archive message, as produced by Archive().
 *
 * @see Archive(), Instantiate(), BArchivable
 */
BBitmap::BBitmap(BMessage* data)
	:
	BArchivable(data),
	fBasePointer(NULL),
	fSize(0),
	fColorSpace(B_NO_COLOR_SPACE),
	fBounds(0, 0, -1, -1),
	fBytesPerRow(0),
	fWindow(NULL),
	fServerToken(-1),
	fAreaOffset(-1),
	fArea(-1),
	fServerArea(-1),
	fFlags(0),
	fInitError(B_NO_INIT)
{
	int32 flags;
	if (data->FindInt32("_bmflags", &flags) != B_OK) {
		// this bitmap is archived in some archaic format
		flags = 0;

		bool acceptsViews;
		if (data->FindBool("_view_ok", &acceptsViews) == B_OK && acceptsViews)
			flags |= B_BITMAP_ACCEPTS_VIEWS;

		bool contiguous;
		if (data->FindBool("_contiguous", &contiguous) == B_OK && contiguous)
			flags |= B_BITMAP_IS_CONTIGUOUS;
	}

	int32 rowBytes;
	if (data->FindInt32("_rowbytes", &rowBytes) != B_OK) {
		rowBytes = -1;
			// bytes per row are computed in InitObject(), then
	}

	BRect bounds;
	color_space cspace;
	if (data->FindRect("_frame", &bounds) == B_OK
		&& data->FindInt32("_cspace", (int32*)&cspace) == B_OK) {
		_InitObject(bounds, cspace, flags, rowBytes, B_MAIN_SCREEN_ID);
	}

	if (InitCheck() == B_OK) {
		ssize_t size;
		const void* buffer;
		if (data->FindData("_data", B_RAW_TYPE, &buffer, &size) == B_OK) {
			if (size == BitsLength()) {
				_AssertPointer();
				memcpy(fBasePointer, buffer, size);
			}
		}
	}

	if ((fFlags & B_BITMAP_ACCEPTS_VIEWS) != 0) {
		BMessage message;
		int32 i = 0;

		while (data->FindMessage("_views", i++, &message) == B_OK) {
			if (BView* view
					= dynamic_cast<BView*>(instantiate_object(&message)))
				AddChild(view);
		}
	}
}


/**
 * @brief Instantiates a BBitmap from a BMessage archive.
 *
 * Validates that @a data was produced by BBitmap::Archive(), then allocates
 * and returns a new BBitmap constructed from the archive.
 *
 * @param data  The archive message to reconstruct from.
 * @return A newly allocated BBitmap on success, or NULL if validation fails.
 *
 * @see Archive(), BBitmap(BMessage*)
 */
BArchivable*
BBitmap::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BBitmap"))
		return new BBitmap(data);

	return NULL;
}


/**
 * @brief Archives the BBitmap into a BMessage.
 *
 * Stores the bitmap's bounds, color space, creation flags, and row stride into
 * @a data, then appends the raw pixel data as a B_RAW_TYPE field.  When @a deep
 * is true and B_BITMAP_ACCEPTS_VIEWS is set, each child view is also archived
 * recursively into "_views" entries.
 *
 * @param data  The message to archive into.
 * @param deep  If true, child views are archived as well.
 * @return B_OK on success, or an error code if any field could not be added.
 *
 * @see Instantiate(), BBitmap(BMessage*)
 */
status_t
BBitmap::Archive(BMessage* data, bool deep) const
{
	status_t ret = BArchivable::Archive(data, deep);

	if (ret == B_OK)
		ret = data->AddRect("_frame", fBounds);

	if (ret == B_OK)
		ret = data->AddInt32("_cspace", (int32)fColorSpace);

	if (ret == B_OK)
		ret = data->AddInt32("_bmflags", fFlags);

	if (ret == B_OK)
		ret = data->AddInt32("_rowbytes", fBytesPerRow);

	if (ret == B_OK && deep) {
		if ((fFlags & B_BITMAP_ACCEPTS_VIEWS) != 0) {
			BMessage views;
			for (int32 i = 0; i < CountChildren(); i++) {
				if (ChildAt(i)->Archive(&views, deep))
					ret = data->AddMessage("_views", &views);
				views.MakeEmpty();
				if (ret < B_OK)
					break;
			}
		}
	}
	// Note: R5 does not archive the data if B_BITMAP_IS_CONTIGUOUS is
	// true and it does save all formats as B_RAW_TYPE and it does save
	// the data even if B_BITMAP_ACCEPTS_VIEWS is set (as opposed to
	// the BeBook)
	if (ret == B_OK) {
		const_cast<BBitmap*>(this)->_AssertPointer();
		ret = data->AddData("_data", B_RAW_TYPE, fBasePointer, fSize);
	}
	return ret;
}


/**
 * @brief Returns the initialization status of the bitmap.
 *
 * @return B_OK if the bitmap was constructed successfully, or an error code
 *         (e.g. B_NO_MEMORY, B_BAD_VALUE) if construction failed.
 *
 * @see IsValid()
 */
status_t
BBitmap::InitCheck() const
{
	return fInitError;
}


/**
 * @brief Returns whether the bitmap was successfully initialized.
 *
 * Convenience wrapper around InitCheck() == B_OK.
 *
 * @return true if the bitmap is ready for use, false otherwise.
 *
 * @see InitCheck()
 */
bool
BBitmap::IsValid() const
{
	return InitCheck() == B_OK;
}


/**
 * @brief Locks the bitmap's pixel buffer to prevent relocation.
 *
 * For overlay bitmaps (B_BITMAP_WILL_OVERLAY), the hardware frame buffer can
 * be relocated when the display resolution changes.  Callers must lock the
 * bitmap before calling Bits() and hold the lock for the entire duration of
 * buffer access to avoid reading from an invalidated pointer.
 *
 * For non-overlay bitmaps this call always succeeds immediately.
 *
 * @param state  Optional pointer that receives implementation-defined state
 *               information about the lock; may be NULL.
 * @return B_OK on success, B_BUSY if the overlay buffer is temporarily
 *         unavailable, or another error code on failure.
 *
 * @see UnlockBits(), Bits()
 */
status_t
BBitmap::LockBits(uint32* state)
{
	// TODO: how do we fill the "state"?
	//	It would be more or less useful to report what kind of bitmap
	//	we got (ie. overlay, placeholder, or non-overlay)
	if ((fFlags & B_BITMAP_WILL_OVERLAY) != 0) {
		overlay_client_data* data = (overlay_client_data*)fBasePointer;

		status_t status;
		do {
			status = acquire_sem(data->lock);
		} while (status == B_INTERRUPTED);

		if (data->buffer == NULL) {
			// the app_server does not grant us access to the frame buffer
			// right now - let's release the lock and fail
			release_sem_etc(data->lock, 1, B_DO_NOT_RESCHEDULE);
			return B_BUSY;
		}
		return status;
	}

	// NOTE: maybe this is used to prevent the app_server from
	// drawing the bitmap yet?
	// axeld: you mean for non overlays?

	return B_OK;
}


/**
 * @brief Unlocks the bitmap's pixel buffer after a LockBits() call.
 *
 * Releases the semaphore acquired by LockBits() so that the app_server is
 * free to relocate the overlay frame buffer again.  Has no effect on
 * non-overlay bitmaps.
 *
 * @see LockBits()
 */
void
BBitmap::UnlockBits()
{
	if ((fFlags & B_BITMAP_WILL_OVERLAY) == 0)
		return;

	overlay_client_data* data = (overlay_client_data*)fBasePointer;
	release_sem_etc(data->lock, 1, B_DO_NOT_RESCHEDULE);
}


/**
 * @brief Returns the area_id of the memory region backing the bitmap.
 *
 * Forces a lazy area clone if the bitmap has not yet mapped its server-side
 * area into the local address space.
 *
 * @return The area_id of the local clone of the bitmap's memory area, or a
 *         negative error code if no area has been allocated.
 *
 * @see Bits(), _AssertPointer()
 */
area_id
BBitmap::Area() const
{
	const_cast<BBitmap*>(this)->_AssertPointer();
	return fArea;
}


/**
 * @brief Returns a pointer to the bitmap's raw pixel data.
 *
 * For overlay bitmaps, returns the hardware frame buffer address obtained
 * from the overlay_client_data structure; the buffer must be locked via
 * LockBits() before accessing it.  For all other bitmaps, returns the base
 * pointer into the mapped memory area.
 *
 * @return A pointer to the start of the pixel buffer, or NULL if the bitmap
 *         is uninitialised or the area has not yet been mapped.
 *
 * @see LockBits(), BitsLength(), Area()
 */
void*
BBitmap::Bits() const
{
	const_cast<BBitmap*>(this)->_AssertPointer();

	if ((fFlags & B_BITMAP_WILL_OVERLAY) != 0) {
		overlay_client_data* data = (overlay_client_data*)fBasePointer;
		return data->buffer;
	}

	return (void*)fBasePointer;
}


/**
 * @brief Returns the total size of the bitmap's pixel buffer in bytes.
 *
 * Equals BytesPerRow() multiplied by the number of rows (height + 1).
 *
 * @return The size of the pixel buffer in bytes.
 *
 * @see Bits(), BytesPerRow()
 */
int32
BBitmap::BitsLength() const
{
	return fSize;
}


/**
 * @brief Returns the number of bytes per row in the pixel buffer.
 *
 * Includes any alignment padding added to meet hardware or protocol
 * requirements.  The stride may be larger than the minimum required by the
 * color space and width.
 *
 * @return The row stride in bytes.
 *
 * @see BitsLength(), Bits()
 */
int32
BBitmap::BytesPerRow() const
{
	return fBytesPerRow;
}


/**
 * @brief Returns the color space of the bitmap's pixel data.
 *
 * @return The color_space constant (e.g. B_RGBA32, B_CMAP8) that describes
 *         the pixel format, or B_NO_COLOR_SPACE if uninitialised.
 *
 * @see Bounds(), BytesPerRow()
 */
color_space
BBitmap::ColorSpace() const
{
	return fColorSpace;
}


/**
 * @brief Returns the bitmap's bounding rectangle.
 *
 * The returned BRect describes the pixel dimensions of the bitmap; its
 * top-left corner is typically (0, 0).
 *
 * @return The bitmap's bounds as a BRect.
 *
 * @see ColorSpace(), BytesPerRow()
 */
BRect
BBitmap::Bounds() const
{
	return fBounds;
}


/**
 * @brief Returns the creation flags used to allocate the bitmap.
 *
 * Reports which capability flags (e.g. B_BITMAP_ACCEPTS_VIEWS,
 * B_BITMAP_WILL_OVERLAY, B_BITMAP_IS_CONTIGUOUS) were requested and
 * successfully fulfilled when the bitmap was created.
 *
 * @return The bitmap's creation flags as a bitmask.
 *
 * @see BBitmap(BRect, uint32, color_space, int32, screen_id)
 */
uint32
BBitmap::Flags() const
{
	return fFlags;
}


/**
 * @brief Copies raw pixel data into the bitmap with legacy format handling.
 *
 * Writes @a length bytes from @a data into the bitmap buffer starting at
 * @a offset, converting from @a colorSpace to the bitmap's native color space
 * as needed.  This method preserves several quirks of the original BeOS API:
 *  - When @a colorSpace is B_RGB32 and @a length is divisible by 3, the
 *    source is treated as unpadded B_RGB24_BIG data.
 *  - When @a colorSpace is B_CMAP8 and the bitmap's color space differs,
 *    the source is assumed to have no row padding.
 *
 * @note Prefer ImportBits() for new code; its color space semantics are
 *       straightforward and it returns an error code.
 *
 * @param data        Pointer to the source pixel data.
 * @param length      Number of bytes to copy from @a data.
 * @param offset      Byte offset into the bitmap's buffer at which to start
 *                    writing.
 * @param colorSpace  Color space of the source @a data.
 *
 * @see ImportBits()
 */
void
BBitmap::SetBits(const void* data, int32 length, int32 offset,
	color_space colorSpace)
{
	status_t error = (InitCheck() == B_OK ? B_OK : B_NO_INIT);
	// check params
	if (error == B_OK && (data == NULL || offset > fSize || length < 0))
		error = B_BAD_VALUE;
	int32 width = 0;
	if (error == B_OK)
		width = fBounds.IntegerWidth() + 1;
	int32 inBPR = -1;
	// tweaks to mimic R5 behavior
	if (error == B_OK) {
		if (colorSpace == B_RGB32 && (length % 3) == 0) {
			// B_RGB32 could actually mean unpadded B_RGB24_BIG
			colorSpace = B_RGB24_BIG;
			inBPR = width * 3;
		} else if (colorSpace == B_CMAP8 && fColorSpace != B_CMAP8) {
			// If in color space is B_CMAP8, but the bitmap's is another one,
			// ignore source data row padding.
			inBPR = width;
		}

		// call the sane method, which does the actual work
		error = ImportBits(data, length, inBPR, offset, colorSpace);
	}
}


/**
 * @brief Copies raw pixel data into the bitmap at a byte offset.
 *
 * Writes @a length bytes from @a data (in @a colorSpace format) into the
 * bitmap's pixel buffer starting at @a offset bytes from the buffer's base,
 * converting between color spaces as required.  Unlike SetBits(), the
 * @a colorSpace argument is interpreted literally: the source buffer must
 * contain data in exactly that color space, row-padded to @a bpr bytes (or
 * to an int32 boundary when B_ANY_BYTES_PER_ROW is supplied).
 *
 * Supported source/target color spaces: B_RGB{32,24,16,15}[_BIG], B_CMAP8,
 * B_GRAY{8,1}.
 *
 * @param data        Pointer to the source pixel data.
 * @param length      Number of source bytes to read.
 * @param bpr         Bytes per row in the source buffer, or B_ANY_BYTES_PER_ROW
 *                    to use the standard int32-aligned stride.
 * @param offset      Byte offset into the bitmap buffer at which to start
 *                    writing converted data.
 * @param colorSpace  Color space of the source @a data.
 * @return B_OK on success.
 * @retval B_NO_INIT   The bitmap was not initialised successfully.
 * @retval B_BAD_VALUE NULL @a data, invalid @a bpr or @a offset, or
 *                     unsupported @a colorSpace.
 *
 * @see SetBits(), ImportBits(const void*, int32, int32, color_space, BPoint, BPoint, BSize)
 */
status_t
BBitmap::ImportBits(const void* data, int32 length, int32 bpr, int32 offset,
	color_space colorSpace)
{
	_AssertPointer();

	if (InitCheck() != B_OK)
		return B_NO_INIT;

	if (!data || offset > fSize || length < 0)
		return B_BAD_VALUE;

	int32 width = fBounds.IntegerWidth() + 1;
	if (bpr <= 0) {
		if (bpr == B_ANY_BYTES_PER_ROW)
			bpr = get_bytes_per_row(colorSpace, width);
		else
			return B_BAD_VALUE;
	}

	return BPrivate::ConvertBits(data, (uint8*)fBasePointer + offset, length,
		fSize - offset, bpr, fBytesPerRow, colorSpace, fColorSpace, width,
		fBounds.IntegerHeight() + 1);
}


/**
 * @brief Copies a sub-region of raw pixel data into a sub-region of the bitmap.
 *
 * Reads a @a size rectangle starting at @a from within @a data (at stride
 * @a bpr) and writes it into the bitmap at @a to, converting between
 * @a colorSpace and the bitmap's native color space as needed.
 *
 * Supported source/target color spaces: B_RGB{32,24,16,15}[_BIG], B_CMAP8,
 * B_GRAY{8,1}.
 *
 * @param data        Pointer to the source pixel buffer.
 * @param length      Total size of @a data in bytes.
 * @param bpr         Bytes per row in the source buffer, or B_ANY_BYTES_PER_ROW.
 * @param colorSpace  Color space of the source @a data.
 * @param from        Top-left pixel coordinate within the source to read from.
 * @param to          Top-left pixel coordinate within the bitmap to write to.
 * @param size        Width and height of the region to copy (in pixels).
 * @return B_OK on success.
 * @retval B_NO_INIT   The bitmap was not initialised successfully.
 * @retval B_BAD_VALUE NULL @a data, invalid @a bpr, unsupported @a colorSpace,
 *                     or negative width/height in @a size.
 *
 * @see ImportBits(const void*, int32, int32, int32, color_space)
 */
status_t
BBitmap::ImportBits(const void* data, int32 length, int32 bpr,
	color_space colorSpace, BPoint from, BPoint to, BSize size)
{
	_AssertPointer();

	if (InitCheck() != B_OK)
		return B_NO_INIT;

	if (!data || length < 0 || size.IntegerWidth() < 0 || size.IntegerHeight() < 0)
		return B_BAD_VALUE;

	if (bpr <= 0) {
		if (bpr == B_ANY_BYTES_PER_ROW)
			bpr = get_bytes_per_row(colorSpace, fBounds.IntegerWidth() + 1);
		else
			return B_BAD_VALUE;
	}

	return BPrivate::ConvertBits(data, fBasePointer, length, fSize, bpr,
		fBytesPerRow, colorSpace, fColorSpace, from, to,
		size.IntegerWidth() + 1, size.IntegerHeight() + 1);
}


/**
 * @brief Copies a sub-region of raw pixel data into a sub-region of the bitmap (integer size).
 *
 * Convenience overload that accepts the region dimensions as separate integer
 * @a width and @a height parameters rather than a BSize.  Delegates to the
 * BSize variant after converting to BSize(width - 1, height - 1).
 *
 * @param data        Pointer to the source pixel buffer.
 * @param length      Total size of @a data in bytes.
 * @param bpr         Bytes per row in the source buffer, or B_ANY_BYTES_PER_ROW.
 * @param colorSpace  Color space of the source @a data.
 * @param from        Top-left pixel coordinate within the source to read from.
 * @param to          Top-left pixel coordinate within the bitmap to write to.
 * @param width       Width of the region to copy in pixels.
 * @param height      Height of the region to copy in pixels.
 * @return B_OK on success, or an error code on failure.
 *
 * @see ImportBits(const void*, int32, int32, color_space, BPoint, BPoint, BSize)
 */
status_t
BBitmap::ImportBits(const void* data, int32 length, int32 bpr,
	color_space colorSpace, BPoint from, BPoint to, int32 width, int32 height)
{
	return ImportBits(data, length, bpr, colorSpace, from, to, BSize(width - 1, height - 1));
}


/**
 * @brief Copies another bitmap's pixel data into this bitmap.
 *
 * The source @a bitmap must have exactly the same bounds as this bitmap.
 * Pixel data is converted from the source's color space to this bitmap's
 * color space as required.
 *
 * Supported source/target color spaces: B_RGB{32,24,16,15}[_BIG], B_CMAP8,
 * B_GRAY{8,1}.
 *
 * @param bitmap  The source bitmap to copy from; must be valid and have
 *                the same bounds as this bitmap.
 * @return B_OK on success.
 * @retval B_NO_INIT   This bitmap was not initialised successfully.
 * @retval B_BAD_VALUE NULL or invalid @a bitmap, or @a bitmap has different
 *                     dimensions, or the color space conversion is unsupported.
 *
 * @see ImportBits(const BBitmap*, BPoint, BPoint, BSize)
 */
status_t
BBitmap::ImportBits(const BBitmap* bitmap)
{
	if (InitCheck() != B_OK)
		return B_NO_INIT;

	if (!bitmap || bitmap->InitCheck() != B_OK || bitmap->Bounds() != fBounds)
		return B_BAD_VALUE;

	return ImportBits(bitmap->Bits(), bitmap->BitsLength(),
		bitmap->BytesPerRow(), 0, bitmap->ColorSpace());
}


/**
 * @brief Copies a sub-region of another bitmap into a sub-region of this bitmap.
 *
 * Reads the @a size rectangle starting at @a from within @a bitmap and writes
 * it into this bitmap at @a to, clipping as necessary.  The two bitmaps do not
 * need to have the same dimensions.  Pixel data is color-space-converted as
 * required.
 *
 * Supported source/target color spaces: B_RGB{32,24,16,15}[_BIG], B_CMAP8,
 * B_GRAY{8,1}.
 *
 * @param bitmap  The source bitmap; must be valid.
 * @param from    Top-left coordinate within @a bitmap to start reading from.
 * @param to      Top-left coordinate within this bitmap to start writing to.
 * @param size    Width and height of the region to copy (in pixels).
 * @return B_OK on success.
 * @retval B_NO_INIT   This bitmap was not initialised successfully.
 * @retval B_BAD_VALUE NULL or invalid @a bitmap, unsupported color space
 *                     conversion, or negative width/height in @a size.
 *
 * @see ImportBits(const BBitmap*)
 */
status_t
BBitmap::ImportBits(const BBitmap* bitmap, BPoint from, BPoint to, BSize size)
{
	if (InitCheck() != B_OK)
		return B_NO_INIT;

	if (!bitmap || bitmap->InitCheck() != B_OK)
		return B_BAD_VALUE;

	return ImportBits(bitmap->Bits(), bitmap->BitsLength(),
		bitmap->BytesPerRow(), bitmap->ColorSpace(), from, to, size);
}


/**
 * @brief Copies a sub-region of another bitmap into this bitmap (integer size).
 *
 * Convenience overload that accepts the region size as separate integer
 * @a width and @a height parameters.  Delegates to the BSize variant.
 *
 * @param bitmap  The source bitmap; must be valid.
 * @param from    Top-left coordinate within @a bitmap to start reading from.
 * @param to      Top-left coordinate within this bitmap to start writing to.
 * @param width   Width of the region to copy in pixels.
 * @param height  Height of the region to copy in pixels.
 * @return B_OK on success, or an error code on failure.
 *
 * @see ImportBits(const BBitmap*, BPoint, BPoint, BSize)
 */
status_t
BBitmap::ImportBits(const BBitmap* bitmap, BPoint from, BPoint to, int32 width, int32 height)
{
	return ImportBits(bitmap, from, to, BSize(width - 1, height - 1));
}


/**
 * @brief Retrieves the hardware overlay placement and alignment restrictions.
 *
 * Queries the app_server for the overlay_restrictions applicable to this
 * bitmap.  Only valid for bitmaps created with the B_BITMAP_WILL_OVERLAY flag.
 *
 * @param restrictions  Output parameter filled with the overlay restriction
 *                      data returned by the server.
 * @return B_OK on success.
 * @retval B_BAD_TYPE   The bitmap was not created with B_BITMAP_WILL_OVERLAY.
 *
 * @see Flags()
 */
status_t
BBitmap::GetOverlayRestrictions(overlay_restrictions* restrictions) const
{
	if ((fFlags & B_BITMAP_WILL_OVERLAY) == 0)
		return B_BAD_TYPE;

	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_BITMAP_OVERLAY_RESTRICTIONS);
	link.Attach<int32>(fServerToken);

	status_t status;
	if (link.FlushWithReply(status) < B_OK)
		return status;

	link.Read(restrictions, sizeof(overlay_restrictions));
	return B_OK;
}


/**
 * @brief Adds a BView to the bitmap's off-screen view hierarchy.
 *
 * The bitmap must have been created with B_BITMAP_ACCEPTS_VIEWS and @a view
 * must not already be attached to another parent window or bitmap.
 *
 * @param view  The view to attach.
 *
 * @see RemoveChild(), Lock(), CountChildren()
 */
void
BBitmap::AddChild(BView* view)
{
	if (fWindow != NULL)
		fWindow->AddChild(view);
}


/**
 * @brief Removes a BView from the bitmap's off-screen view hierarchy.
 *
 * @param view  The view to detach.
 * @return true if @a view was found and removed, false otherwise.
 *
 * @see AddChild()
 */
bool
BBitmap::RemoveChild(BView* view)
{
	return fWindow != NULL ? fWindow->RemoveChild(view) : false;
}


/**
 * @brief Returns the number of BViews attached to the bitmap.
 *
 * @return The child view count, or 0 if the bitmap does not accept views.
 *
 * @see AddChild(), ChildAt()
 */
int32
BBitmap::CountChildren() const
{
	return fWindow != NULL ? fWindow->CountChildren() : 0;
}


/**
 * @brief Returns the child BView at the given index.
 *
 * @param index  Zero-based index into the bitmap's child view list.
 * @return The BView at @a index, or NULL if @a index is out of range or the
 *         bitmap does not accept views.
 *
 * @see CountChildren(), AddChild()
 */
BView*
BBitmap::ChildAt(int32 index) const
{
	return fWindow != NULL ? fWindow->ChildAt(index) : NULL;
}


/**
 * @brief Finds a child BView by name.
 *
 * Searches the bitmap's off-screen window for a view whose name matches
 * @a viewName.
 *
 * @param viewName  The name to search for.
 * @return The matching BView, or NULL if no view has that name or the bitmap
 *         does not accept views.
 *
 * @see FindView(BPoint), ChildAt()
 */
BView*
BBitmap::FindView(const char* viewName) const
{
	return fWindow != NULL ? fWindow->FindView(viewName) : NULL;
}


/**
 * @brief Finds the deepest child BView that contains the given point.
 *
 * @param point  A point in the bitmap's coordinate system.
 * @return The BView containing @a point, or NULL if no view contains it or
 *         the bitmap does not accept views.
 *
 * @see FindView(const char*), ChildAt()
 */
BView*
BBitmap::FindView(BPoint point) const
{
	return fWindow != NULL ? fWindow->FindView(point) : NULL;
}


/**
 * @brief Locks the bitmap's off-screen BWindow for drawing.
 *
 * Must be called before attaching or detaching views or issuing drawing
 * commands.  The bitmap must have been created with B_BITMAP_ACCEPTS_VIEWS.
 *
 * @return true if the lock was acquired, false if the bitmap has no
 *         associated window or the lock could not be obtained.
 *
 * @see Unlock(), IsLocked(), AddChild()
 */
bool
BBitmap::Lock()
{
	return fWindow != NULL ? fWindow->Lock() : false;
}


/**
 * @brief Unlocks the bitmap's off-screen BWindow.
 *
 * Releases the lock previously acquired by Lock().  Has no effect if the
 * bitmap does not accept views.
 *
 * @see Lock(), IsLocked()
 */
void
BBitmap::Unlock()
{
	if (fWindow != NULL)
		fWindow->Unlock();
}


/**
 * @brief Returns whether the calling thread holds the bitmap's window lock.
 *
 * @return true if the current thread owns the lock, false otherwise or if
 *         the bitmap does not accept views.
 *
 * @see Lock(), Unlock()
 */
bool
BBitmap::IsLocked() const
{
	return fWindow != NULL ? fWindow->IsLocked() : false;
}


/**
 * @brief Copy-assigns another bitmap to this bitmap.
 *
 * Releases all resources held by this bitmap, then performs a deep copy of
 * @a source: dimensions, color space, flags, row stride, and pixel data.
 * If @a source is invalid, this bitmap is left in an uninitialised state.
 *
 * @param source  The bitmap to copy from.
 * @return A reference to this bitmap.
 *
 * @see _CleanUp(), _InitObject()
 */
BBitmap&
BBitmap::operator=(const BBitmap& source)
{
	_CleanUp();
	fInitError = B_NO_INIT;

	if (!source.IsValid())
		return *this;

	_InitObject(source.Bounds(), source.ColorSpace(), source.Flags(),
		source.BytesPerRow(), B_MAIN_SCREEN_ID);
	if (InitCheck() == B_OK)
		memcpy(Bits(), source.Bits(), min_c(BitsLength(), source.BitsLength()));

	return *this;
}


/**
 * @brief Dispatches a perform code to the base class implementation.
 *
 * Used internally by the BArchivable framework to handle future binary
 * compatibility extensions without breaking the vtable layout.
 *
 * @param d    The perform code identifying the operation.
 * @param arg  Operation-specific argument data.
 * @return The result from BArchivable::Perform().
 *
 * @see BArchivable::Perform()
 */
status_t
BBitmap::Perform(perform_code d, void* arg)
{
	return BArchivable::Perform(d, arg);
}

// FBC
void BBitmap::_ReservedBitmap1() {}
void BBitmap::_ReservedBitmap2() {}
void BBitmap::_ReservedBitmap3() {}


#if 0
// get_shared_pointer
/**
 * @brief Returns a shared pointer to the bitmap buffer (not implemented).
 *
 * @return Always NULL; this method is not implemented.
 */
char*
BBitmap::get_shared_pointer() const
{
	return NULL;	// not implemented
}
#endif

/**
 * @brief Returns the server-side token identifying this bitmap.
 *
 * The token is used to address the bitmap in messages sent to the app_server
 * (e.g. AS_DELETE_BITMAP, AS_GET_BITMAP_OVERLAY_RESTRICTIONS).
 *
 * @return The server token, or -1 if no server-side bitmap has been allocated.
 */
int32
BBitmap::_ServerToken() const
{
	return fServerToken;
}


/**
 * @brief Core initialisation routine shared by all constructors.
 *
 * Validates parameters, computes the row stride, and allocates the pixel
 * buffer either directly via malloc (B_BITMAP_NO_SERVER_LINK) or by
 * communicating with the app_server.  When @a area is a valid area_id the
 * bitmap is reconnected to an existing memory region rather than requesting
 * a fresh allocation.  On success the bitmap is registered in sBitmapList
 * for reconnection after an app_server restart.
 *
 * @param bounds      The bitmap dimensions.
 * @param colorSpace  The pixel color space.
 * @param flags       Bitmap creation flags.
 * @param bytesPerRow Desired row stride, or B_ANY_BYTES_PER_ROW.
 * @param screenID    The target screen.
 * @param area        Existing area_id to attach to, or a negative value to
 *                    allocate a new one via the server.
 * @param areaOffset  Byte offset within @a area where the pixel data begins.
 *
 * @see _CleanUp(), reconnect_bitmaps_to_app_server()
 */
void
BBitmap::_InitObject(BRect bounds, color_space colorSpace, uint32 flags,
	int32 bytesPerRow, screen_id screenID, area_id area, ptrdiff_t areaOffset)
{
//printf("BBitmap::InitObject(bounds: BRect(%.1f, %.1f, %.1f, %.1f), format: %ld, flags: %ld, bpr: %ld\n",
//	   bounds.left, bounds.top, bounds.right, bounds.bottom, colorSpace, flags, bytesPerRow);

	// TODO: Should we handle rounding of the "bounds" here? How does R5 behave?

	status_t error = B_OK;

#ifdef RUN_WITHOUT_APP_SERVER
	flags |= B_BITMAP_NO_SERVER_LINK;
#endif	// RUN_WITHOUT_APP_SERVER

	_CleanUp();

	// check params
	if (!bounds.IsValid() || !bitmaps_support_space(colorSpace, NULL)) {
		error = B_BAD_VALUE;
	} else {
		// bounds is in floats and might be valid but much larger than what we
		// can handle the size could not be expressed in int32
		double realSize = bounds.Width() * bounds.Height();
		if (realSize > (double)(INT_MAX / 4)) {
			fprintf(stderr, "bitmap bounds is much too large: "
				"BRect(%.1f, %.1f, %.1f, %.1f)\n",
				bounds.left, bounds.top, bounds.right, bounds.bottom);
			error = B_BAD_VALUE;
		}
	}
	if (error == B_OK) {
		int32 bpr = get_bytes_per_row(colorSpace, bounds.IntegerWidth() + 1);
		if (bytesPerRow < 0)
			bytesPerRow = bpr;
		else if (bytesPerRow < bpr)
// NOTE: How does R5 behave?
			error = B_BAD_VALUE;
	}
	// allocate the bitmap buffer
	if (error == B_OK) {
		// TODO: Let the app_server return the size when it allocated the bitmap
		int32 size = bytesPerRow * (bounds.IntegerHeight() + 1);

		if ((flags & B_BITMAP_NO_SERVER_LINK) != 0) {
			fBasePointer = (uint8*)malloc(size);
			if (fBasePointer) {
				fSize = size;
				fColorSpace = colorSpace;
				fBounds = bounds;
				fBytesPerRow = bytesPerRow;
				fFlags = flags;
			} else
				error = B_NO_MEMORY;
		} else {
			BPrivate::AppServerLink link;

			if (area >= B_OK) {
				// Use area provided by client

				area_info info;
				get_area_info(area, &info);

				// Area should be owned by current team. Client should clone area if needed.
				if (info.team != getpid())
					error = B_BAD_VALUE;
				else {
					link.StartMessage(AS_RECONNECT_BITMAP);
					link.Attach<BRect>(bounds);
					link.Attach<color_space>(colorSpace);
					link.Attach<uint32>(flags);
					link.Attach<int32>(bytesPerRow);
					link.Attach<int32>(0);
					link.Attach<int32>(area);
					link.Attach<int32>(areaOffset);

					if (link.FlushWithReply(error) == B_OK && error == B_OK) {
						link.Read<int32>(&fServerToken);
						link.Read<area_id>(&fServerArea);

						if (fServerArea >= B_OK) {
							fSize = size;
							fColorSpace = colorSpace;
							fBounds = bounds;
							fBytesPerRow = bytesPerRow;
							fFlags = flags;
							fArea = area;
							fAreaOffset = areaOffset;

							fBasePointer = (uint8*)info.address + areaOffset;
						} else
							error = fServerArea;
					}
				}
			} else {
				// Ask the server (via our owning application) to create a bitmap.

				// Attach Data:
				// 1) BRect bounds
				// 2) color_space space
				// 3) int32 bitmap_flags
				// 4) int32 bytes_per_row
				// 5) int32 screen_id::id
				link.StartMessage(AS_CREATE_BITMAP);
				link.Attach<BRect>(bounds);
				link.Attach<color_space>(colorSpace);
				link.Attach<uint32>(flags);
				link.Attach<int32>(bytesPerRow);
				link.Attach<int32>(screenID.id);

				if (link.FlushWithReply(error) == B_OK && error == B_OK) {
					// server side success
					// Get token
					link.Read<int32>(&fServerToken);

					uint8 allocationFlags;
					link.Read<uint8>(&allocationFlags);
					link.Read<area_id>(&fServerArea);
					link.Read<int32>(&fAreaOffset);

					BPrivate::ServerMemoryAllocator* allocator
						= BApplication::Private::ServerAllocator();

					error = allocator->AddArea(fServerArea, fArea,
						fBasePointer, size);
					if (error == B_OK)
						fBasePointer += fAreaOffset;

					if ((allocationFlags & kFramebuffer) != 0) {
						// The base pointer will now point to an overlay_client_data
						// structure bytes per row might be modified to match
						// hardware constraints
						link.Read<int32>(&bytesPerRow);
						size = bytesPerRow * (bounds.IntegerHeight() + 1);
					}

					if (fServerArea >= B_OK) {
						fSize = size;
						fColorSpace = colorSpace;
						fBounds = bounds;
						fBytesPerRow = bytesPerRow;
						fFlags = flags;
					} else
						error = fServerArea;
				}
			}


			if (error < B_OK) {
				fBasePointer = NULL;
				fServerToken = -1;
				fArea = -1;
				fServerArea = -1;
				fAreaOffset = -1;
				// NOTE: why not "0" in case of error?
				fFlags = flags;
			} else {
				MutexLocker _(sBitmapListLock);
				sBitmapList.AddItem(this);
			}
		}
		fWindow = NULL;
	}

	fInitError = error;

	if (fInitError == B_OK) {
		// clear to white if the flags say so.
		if (flags & B_BITMAP_CLEAR_TO_WHITE) {
			if (fColorSpace == B_CMAP8) {
				// "255" is the "transparent magic" index for B_CMAP8 bitmaps
				// use the correct index for "white"
				memset(fBasePointer, 65, fSize);
			} else {
				// should work for most colorspaces
				memset(fBasePointer, 0xff, fSize);
			}
		}
		// TODO: Creating an offscreen window with a non32 bit bitmap
		// copies the current content of the bitmap to a back buffer.
		// So at this point the bitmap has to be already cleared to white.
		// Better move the above code to the server so the problem looks more
		// clear.
		if (flags & B_BITMAP_ACCEPTS_VIEWS) {
			fWindow = new(std::nothrow) BWindow(Bounds(), fServerToken);
			if (fWindow) {
				// A BWindow starts life locked and is unlocked
				// in Show(), but this window is never shown and
				// it's message loop is never started.
				fWindow->Unlock();
			} else
				fInitError = B_NO_MEMORY;
		}
	}
}


/**
 * @brief Releases all resources owned by the bitmap.
 *
 * Destroys the off-screen BWindow (if any), frees or unmaps the pixel buffer,
 * sends AS_DELETE_BITMAP to the app_server to release the server-side handle,
 * removes the server memory area from the allocator, and deregisters the
 * bitmap from sBitmapList.
 *
 * @see _InitObject(), reconnect_bitmaps_to_app_server()
 */
void
BBitmap::_CleanUp()
{
	if (fWindow != NULL) {
		if (fWindow->Lock())
			delete fWindow;
		fWindow = NULL;
			// this will leak fWindow if it couldn't be locked
	}

	if (fBasePointer == NULL)
		return;

	if ((fFlags & B_BITMAP_NO_SERVER_LINK) != 0) {
		free(fBasePointer);
	} else if (fServerToken != -1) {
		BPrivate::AppServerLink link;
		// AS_DELETE_BITMAP:
		// Attached Data:
		//	1) int32 server token
		link.StartMessage(AS_DELETE_BITMAP);
		link.Attach<int32>(fServerToken);
		link.Flush();

		if (fServerArea >= B_OK) {
			BPrivate::ServerMemoryAllocator* allocator
				= BApplication::Private::ServerAllocator();

			allocator->RemoveArea(fServerArea);
		}

		fArea = -1;
		fServerToken = -1;
		fAreaOffset = -1;

		MutexLocker _(sBitmapListLock);
		sBitmapList.RemoveItem(this);
	}
	fBasePointer = NULL;
}


/**
 * @brief Ensures the local pixel buffer pointer is valid, cloning the area if needed.
 *
 * Bitmaps that share the common server memory allocator area already have
 * fBasePointer set at construction time.  Bitmaps that were created in a
 * separate server area (fAreaOffset == -1) are lazily mapped here on first
 * access by cloning fServerArea into the local address space.
 *
 * @see Bits(), Area(), _InitObject()
 */
void
BBitmap::_AssertPointer()
{
	if (fBasePointer == NULL && fServerArea >= B_OK && fAreaOffset == -1) {
		// We lazily clone our own areas - if the bitmap is part of the usual
		// server memory area, or is a B_BITMAP_NO_SERVER_LINK bitmap, it
		// already has its data.
		fArea = clone_area("shared bitmap area", (void**)&fBasePointer,
			B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, fServerArea);
	}
}


/**
 * @brief Re-registers this bitmap with the app_server after a server restart.
 *
 * Sends AS_RECONNECT_BITMAP to the app_server with the bitmap's existing
 * memory area and metadata so the server can rebuild its internal state for
 * this bitmap.  Updates fServerToken and fServerArea from the server's reply.
 *
 * @note Called exclusively from reconnect_bitmaps_to_app_server() while
 *       sBitmapListLock is held.
 *
 * @see reconnect_bitmaps_to_app_server(), _InitObject()
 */
void
BBitmap::_ReconnectToAppServer()
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_RECONNECT_BITMAP);
	link.Attach<BRect>(fBounds);
	link.Attach<color_space>(fColorSpace);
	link.Attach<uint32>(fFlags);
	link.Attach<int32>(fBytesPerRow);
	link.Attach<int32>(0);
	link.Attach<int32>(fArea);
	link.Attach<int32>(fAreaOffset);

	status_t error;
	if (link.FlushWithReply(error) == B_OK && error == B_OK) {
		// server side success
		// Get token
		link.Read<int32>(&fServerToken);

		link.Read<area_id>(&fServerArea);
	}
}
