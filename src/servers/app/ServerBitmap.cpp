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
 *   Copyright 2001-2010, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file ServerBitmap.cpp
    @brief Server-side bitmap classes used by the app_server for offscreen rendering and cursor storage. */


#include "ServerBitmap.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BitmapManager.h"
#include "ClientMemoryAllocator.h"
#include "ColorConversion.h"
#include "HWInterface.h"
#include "InterfacePrivate.h"
#include "Overlay.h"
#include "ServerApp.h"


using std::nothrow;
using namespace BPrivate;


/*!	A word about memory housekeeping and why it's implemented this way:

	The reason why this looks so complicated is to optimize the most common
	path (bitmap creation from the application), and don't cause any further
	memory allocations for maintaining memory in that case.
	If a bitmap was allocated this way, both, the fAllocator and
	fAllocationCookie members are used.

	For overlays, the allocator only allocates a small piece of client memory
	for use with the overlay_client_data structure - the actual buffer will be
	placed in the graphics frame buffer and is allocated by the graphics driver.

	If the memory was allocated on the app_server heap, neither fAllocator, nor
	fAllocationCookie are used, and the buffer is just freed in that case when
	the bitmap is destructed. This method is mainly used for cursors.
*/


/** @brief Constructor called by the BitmapManager only.
    @param rect        Size of the bitmap (width and height are derived from this).
    @param space       Color space of the bitmap.
    @param flags       Various bitmap flags as defined in Bitmap.h.
    @param bytesPerRow Number of bytes per row. -1 implies the default value. Any value
                       less than the default will be overridden; any value greater will
                       be honoured as-is.
    @param screen      Screen assigned to the bitmap. */
ServerBitmap::ServerBitmap(BRect rect, color_space space, uint32 flags,
		int32 bytesPerRow, screen_id screen)
	:
	fMemory(NULL),
	fOverlay(NULL),
	fBuffer(NULL),
	// WARNING: '1' is added to the width and height.
	// Same is done in FBBitmap subclass, so if you
	// modify here make sure to do the same under
	// FBBitmap::SetSize(...)
	fWidth(rect.IntegerWidth() + 1),
	fHeight(rect.IntegerHeight() + 1),
	fBytesPerRow(0),
	fSpace(space),
	fFlags(flags),
	fOwner(NULL)
	// fToken is initialized (if used) by the BitmapManager
{
	int32 minBytesPerRow = get_bytes_per_row(space, fWidth);

	fBytesPerRow = max_c(bytesPerRow, minBytesPerRow);
}


/** @brief Copy constructor. Does not copy the pixel buffer; only metadata is duplicated.
    @param bitmap Pointer to the source ServerBitmap. If NULL, the new bitmap has zero dimensions. */
ServerBitmap::ServerBitmap(const ServerBitmap* bitmap)
	:
	fMemory(NULL),
	fOverlay(NULL),
	fBuffer(NULL),
	fOwner(NULL)
{
	if (bitmap) {
		fWidth = bitmap->fWidth;
		fHeight = bitmap->fHeight;
		fBytesPerRow = bitmap->fBytesPerRow;
		fSpace = bitmap->fSpace;
		fFlags = bitmap->fFlags;
	} else {
		fWidth = 0;
		fHeight = 0;
		fBytesPerRow = 0;
		fSpace = B_NO_COLOR_SPACE;
		fFlags = 0;
	}
}


/** @brief Destructor. Frees the pixel buffer or client memory as appropriate. */
ServerBitmap::~ServerBitmap()
{
	if (fMemory != NULL) {
		if (fMemory != &fClientMemory)
			delete fMemory;
	} else
		delete[] fBuffer;
}


/** @brief Allocates a heap buffer for the bitmap's pixel data.

    Subclasses should call this so the buffer can automatically
    be allocated on the heap. */
void
ServerBitmap::AllocateBuffer()
{
	uint32 length = BitsLength();
	if (length > 0) {
		delete[] fBuffer;
		fBuffer = new(std::nothrow) uint8[length];
	}
}


/** @brief Imports pixel data from an external buffer, converting color spaces if necessary.
    @param bits        Pointer to the source pixel data.
    @param bitsLength  Length in bytes of the source buffer.
    @param bytesPerRow Number of bytes per row in the source buffer.
    @param colorSpace  Color space of the source data.
    @return B_OK on success, B_BAD_VALUE if any argument is invalid. */
status_t
ServerBitmap::ImportBits(const void *bits, int32 bitsLength, int32 bytesPerRow,
	color_space colorSpace)
{
	if (!bits || bitsLength < 0 || bytesPerRow <= 0)
		return B_BAD_VALUE;

	return BPrivate::ConvertBits(bits, fBuffer, bitsLength, BitsLength(),
		bytesPerRow, fBytesPerRow, colorSpace, fSpace, fWidth, fHeight);
}


/** @brief Imports a sub-region of pixel data, converting color spaces if necessary.
    @param bits        Pointer to the source pixel data.
    @param bitsLength  Length in bytes of the source buffer.
    @param bytesPerRow Number of bytes per row in the source buffer.
    @param colorSpace  Color space of the source data.
    @param from        Source origin within the source buffer.
    @param to          Destination origin within this bitmap's buffer.
    @param width       Width in pixels of the region to import.
    @param height      Height in pixels of the region to import.
    @return B_OK on success, B_BAD_VALUE if any argument is invalid. */
status_t
ServerBitmap::ImportBits(const void *bits, int32 bitsLength, int32 bytesPerRow,
	color_space colorSpace, BPoint from, BPoint to, int32 width, int32 height)
{
	if (!bits || bitsLength < 0 || bytesPerRow <= 0 || width < 0 || height < 0)
		return B_BAD_VALUE;

	return BPrivate::ConvertBits(bits, fBuffer, bitsLength, BitsLength(),
		bytesPerRow, fBytesPerRow, colorSpace, fSpace, from, to, width,
		height);
}


/** @brief Returns the area_id of the shared memory area backing this bitmap.
    @return A valid area_id, or B_ERROR if the bitmap has no shared memory. */
area_id
ServerBitmap::Area() const
{
	if (fMemory != NULL)
		return fMemory->Area();

	return B_ERROR;
}


/** @brief Returns the byte offset of the pixel buffer within its shared memory area.
    @return The byte offset, or 0 if there is no shared memory. */
uint32
ServerBitmap::AreaOffset() const
{
	if (fMemory != NULL)
		return fMemory->AreaOffset();

	return 0;
}


/** @brief Assigns a hardware overlay object to this bitmap.
    @param overlay Pointer to the Overlay to associate. */
void
ServerBitmap::SetOverlay(::Overlay* overlay)
{
	fOverlay.SetTo(overlay);
}


/** @brief Returns the hardware overlay associated with this bitmap, if any.
    @return Pointer to the Overlay, or NULL if none is assigned. */
::Overlay*
ServerBitmap::Overlay() const
{
	return fOverlay.Get();
}


/** @brief Sets the owning ServerApp for this bitmap.
    @param owner Pointer to the ServerApp that owns this bitmap. */
void
ServerBitmap::SetOwner(ServerApp* owner)
{
	fOwner = owner;
}


/** @brief Returns the ServerApp that owns this bitmap.
    @return Pointer to the owning ServerApp. */
ServerApp*
ServerBitmap::Owner() const
{
	return fOwner;
}


/** @brief Prints a short description of the bitmap to standard output. */
void
ServerBitmap::PrintToStream()
{
	printf("Bitmap@%p: (%" B_PRId32 ":%" B_PRId32 "), space %" B_PRId32 ", "
		"bpr %" B_PRId32 ", buffer %p\n", this, fWidth, fHeight, (int32)fSpace,
		fBytesPerRow, fBuffer);
}


//	#pragma mark -


/** @brief Constructs a UtilityBitmap and allocates its pixel buffer on the heap.
    @param rect        Dimensions of the bitmap.
    @param space       Color space.
    @param flags       Bitmap flags as defined in Bitmap.h.
    @param bytesPerRow Bytes per row; -1 for the default.
    @param screen      Screen id. */
UtilityBitmap::UtilityBitmap(BRect rect, color_space space, uint32 flags,
		int32 bytesPerRow, screen_id screen)
	:
	ServerBitmap(rect, space, flags, bytesPerRow, screen)
{
	AllocateBuffer();
}


/** @brief Constructs a UtilityBitmap as a deep copy of another ServerBitmap.
    @param bitmap Pointer to the source bitmap whose pixel data is copied. */
UtilityBitmap::UtilityBitmap(const ServerBitmap* bitmap)
	:
	ServerBitmap(bitmap)
{
	AllocateBuffer();

	if (bitmap->Bits())
		memcpy(Bits(), bitmap->Bits(), bitmap->BitsLength());
}


/** @brief Constructs a UtilityBitmap from already-padded raw pixel data.
    @param alreadyPaddedData Pointer to the raw pixel data (must include row padding).
    @param width             Width in pixels.
    @param height            Height in pixels.
    @param format            Color space of the raw data. */
UtilityBitmap::UtilityBitmap(const uint8* alreadyPaddedData, uint32 width,
		uint32 height, color_space format)
	:
	ServerBitmap(BRect(0, 0, width - 1, height - 1), format, 0)
{
	AllocateBuffer();
	if (Bits())
		memcpy(Bits(), alreadyPaddedData, BitsLength());
}


/** @brief Destructor. */
UtilityBitmap::~UtilityBitmap()
{
}
