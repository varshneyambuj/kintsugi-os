/*
 * Copyright 2025, Kintsugi OS Contributors.
 *
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright 2001-2010, Haiku.
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file ServerBitmap.h
 *  @brief Server-side bitmap managed by BitmapManager; base class for cursors. */

#ifndef SERVER_BITMAP_H
#define SERVER_BITMAP_H


#include <AutoDeleter.h>
#include <GraphicsDefs.h>
#include <Rect.h>
#include <OS.h>

#include <Referenceable.h>

#include "ClientMemoryAllocator.h"


class BitmapManager;
class HWInterface;
class Overlay;
class ServerApp;


/*!	\class ServerBitmap ServerBitmap.h
	\brief Bitmap class used inside the server.

	This class is not directly allocated or freed. Instead, it is
	managed by the BitmapManager class. It is also the base class for
	all cursors. Every BBitmap has a shadow ServerBitmap object.
*/
/** @brief Server-side pixel buffer managed by BitmapManager; shadows every client BBitmap. */
class ServerBitmap : public BReferenceable {
public:
	/** @brief Returns whether the bitmap has a valid pixel buffer.
	 *  @return true if the buffer is non-NULL. */
	inline	bool			IsValid() const
								{ return fBuffer != NULL; }

	/** @brief Returns a pointer to the raw pixel data.
	 *  @return Pointer to the first pixel byte. */
	inline	uint8*			Bits() const
								{ return fBuffer; }

	/** @brief Returns the total byte size of the pixel buffer.
	 *  @return BytesPerRow * Height, clamped to UINT32_MAX. */
	inline	uint32			BitsLength() const;

	/** @brief Returns the bounding rectangle of the bitmap.
	 *  @return BRect from (0,0) to (Width-1, Height-1). */
	inline	BRect			Bounds() const
								{ return BRect(0, 0, fWidth - 1, fHeight - 1); }

	/** @brief Returns the pixel width of the bitmap.
	 *  @return Width in pixels. */
	inline	int32			Width() const
								{ return fWidth; }

	/** @brief Returns the pixel height of the bitmap.
	 *  @return Height in pixels. */
	inline	int32			Height() const
								{ return fHeight; }

	/** @brief Returns the number of bytes per horizontal scan line.
	 *  @return Bytes per row including padding. */
	inline	int32			BytesPerRow() const
								{ return fBytesPerRow; }

	/** @brief Returns the color space of the bitmap.
	 *  @return color_space constant. */
	inline	color_space		ColorSpace() const
								{ return fSpace; }

	/** @brief Returns the creation flags of the bitmap.
	 *  @return Bitmap flags bitfield. */
	inline	uint32			Flags() const
								{ return fFlags; }

	//! Returns the identifier token for the bitmap
	/** @brief Returns the unique token identifying this bitmap.
	 *  @return Token value. */
	inline	int32			Token() const
								{ return fToken; }

	/** @brief Returns the area_id of the shared memory area backing the bitmap.
	 *  @return Area ID, or B_ERROR if client memory is used. */
			area_id			Area() const;

	/** @brief Returns the byte offset of the bitmap data within its shared area.
	 *  @return Byte offset. */
			uint32			AreaOffset() const;

	/** @brief Associates an Overlay object with this bitmap.
	 *  @param overlay Overlay to attach (takes ownership). */
			void			SetOverlay(::Overlay* overlay);

	/** @brief Returns the overlay attached to this bitmap, if any.
	 *  @return Pointer to the Overlay, or NULL. */
			::Overlay*		Overlay() const;

	/** @brief Sets the ServerApp that owns this bitmap.
	 *  @param owner Owning application. */
			void			SetOwner(ServerApp* owner);

	/** @brief Returns the ServerApp that owns this bitmap.
	 *  @return Pointer to the owning ServerApp. */
			ServerApp*		Owner() const;

	//! Does a shallow copy of the bitmap passed to it
	/** @brief Copies geometry and buffer fields from another ServerBitmap without deep copy.
	 *  @param from Source bitmap to copy from. */
	inline	void			ShallowCopy(const ServerBitmap *from);

	/** @brief Imports raw pixel data into the bitmap, converting color space if needed.
	 *  @param bits         Source pixel data.
	 *  @param bitsLength   Byte length of source data.
	 *  @param bytesPerRow  Row stride of source data.
	 *  @param colorSpace   Color space of source data.
	 *  @return B_OK on success, an error code otherwise. */
			status_t		ImportBits(const void *bits, int32 bitsLength,
								int32 bytesPerRow, color_space colorSpace);

	/** @brief Imports a sub-rectangle of raw pixel data into the bitmap.
	 *  @param bits         Source pixel data.
	 *  @param bitsLength   Byte length of source data.
	 *  @param bytesPerRow  Row stride of source data.
	 *  @param colorSpace   Color space of source data.
	 *  @param from         Source origin point in the source buffer.
	 *  @param to           Destination origin point in this bitmap.
	 *  @param width        Width of the sub-rectangle in pixels.
	 *  @param height       Height of the sub-rectangle in pixels.
	 *  @return B_OK on success, an error code otherwise. */
			status_t		ImportBits(const void *bits, int32 bitsLength,
								int32 bytesPerRow, color_space colorSpace,
								BPoint from, BPoint to, int32 width,
								int32 height);

	/** @brief Prints bitmap properties to standard output for debugging. */
			void			PrintToStream();

protected:
	friend class BitmapManager;

							ServerBitmap(BRect rect, color_space space,
								uint32 flags, int32 bytesPerRow = -1,
								screen_id screen = B_MAIN_SCREEN_ID);
							ServerBitmap(const ServerBitmap* bmp);
	virtual					~ServerBitmap();

			void			AllocateBuffer();

protected:
			ClientMemory	fClientMemory;
			AreaMemory*		fMemory;
			ObjectDeleter< ::Overlay>
							fOverlay;
			uint8*			fBuffer;

			int32			fWidth;
			int32			fHeight;
			int32			fBytesPerRow;
			color_space		fSpace;
			uint32			fFlags;

			ServerApp*		fOwner;
			int32			fToken;
};

/** @brief Standalone ServerBitmap that can be allocated directly without BitmapManager. */
class UtilityBitmap : public ServerBitmap {
public:
	/** @brief Constructs a utility bitmap with the given geometry.
	 *  @param rect         Bounding rectangle (determines width and height).
	 *  @param space        Color space.
	 *  @param flags        Bitmap creation flags.
	 *  @param bytesperline Bytes per row (-1 for auto-calculation).
	 *  @param screen       Target screen ID. */
							UtilityBitmap(BRect rect, color_space space,
								uint32 flags, int32 bytesperline = -1,
								screen_id screen = B_MAIN_SCREEN_ID);

	/** @brief Copy-constructs from an existing ServerBitmap.
	 *  @param bmp Source bitmap. */
							UtilityBitmap(const ServerBitmap* bmp);

	/** @brief Constructs from already-padded raw pixel data.
	 *  @param alreadyPaddedData Pre-padded pixel data buffer.
	 *  @param width             Width in pixels.
	 *  @param height            Height in pixels.
	 *  @param format            Color space of the data. */
							UtilityBitmap(const uint8* alreadyPaddedData,
								uint32 width, uint32 height,
								color_space format);

	virtual					~UtilityBitmap();
};


uint32
ServerBitmap::BitsLength() const
{
	int64 length = fBytesPerRow * fHeight;
	return (length > 0 && length <= UINT32_MAX) ? (uint32)length : 0;
}


//! (only for server bitmaps)
void
ServerBitmap::ShallowCopy(const ServerBitmap* from)
{
	if (!from)
		return;

	fBuffer = from->fBuffer;
	fWidth = from->fWidth;
	fHeight = from->fHeight;
	fBytesPerRow = from->fBytesPerRow;
	fSpace = from->fSpace;
	fFlags = from->fFlags;
	fToken = from->fToken;
}

#endif	// SERVER_BITMAP_H
