/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
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
 * This file incorporates work from the Haiku project:
 *   Copyright 2009, Haiku Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author:
 *   Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file BitmapStream.h
 *  @brief BPositionIO stream wrapper around a BBitmap for use with the Translation Kit. */

#ifndef _BITMAP_STREAM_H
#define _BITMAP_STREAM_H


#include <ByteOrder.h>
#include <DataIO.h>
#include <TranslationDefs.h>
#include <TranslatorFormats.h>


class BBitmap;


/** @brief A BPositionIO implementation that reads and writes bitmap data via a BBitmap object.
 *
 *  Used to feed or extract BBitmap data through the Translation Kit translators. */
class BBitmapStream : public BPositionIO {
public:
	/** @brief Constructs a stream wrapping an existing bitmap, or an empty stream.
	 *  @param bitmap Optional BBitmap to wrap; pass NULL to create an empty stream. */
								BBitmapStream(BBitmap* bitmap = NULL);
	virtual						~BBitmapStream();

	/** @brief Reads bytes from the stream at the specified offset.
	 *  @param offset Byte offset to read from.
	 *  @param buffer Destination buffer.
	 *  @param size Number of bytes to read.
	 *  @return Number of bytes read, or a negative error code. */
	virtual	ssize_t				ReadAt(off_t offset, void* buffer, size_t size);

	/** @brief Writes bytes into the stream at the specified offset.
	 *  @param offset Byte offset to write to.
	 *  @param buffer Source data.
	 *  @param size Number of bytes to write.
	 *  @return Number of bytes written, or a negative error code. */
	virtual	ssize_t				WriteAt(off_t offset, const void* buffer,
									size_t size);

	/** @brief Moves the stream position.
	 *  @param position New position or delta.
	 *  @param seekMode SEEK_SET, SEEK_CUR, or SEEK_END.
	 *  @return New absolute position, or a negative error code. */
	virtual	off_t				Seek(off_t position, uint32 seekMode);

	/** @brief Returns the current stream position.
	 *  @return Current byte offset. */
	virtual	off_t				Position() const;

	/** @brief Returns the total size of the stream in bytes.
	 *  @return Stream size. */
	virtual	off_t				Size() const;

	/** @brief Sets the stream size (not supported; always returns B_NOT_SUPPORTED).
	 *  @param size Ignored.
	 *  @return B_NOT_SUPPORTED. */
	virtual	status_t			SetSize(off_t size);

	/** @brief Detaches and returns ownership of the underlying BBitmap.
	 *
	 *  After this call the stream no longer owns the bitmap.
	 *  @param _bitmap Set to a pointer to the detached BBitmap.
	 *  @return B_OK on success, or an error code. */
			status_t			DetachBitmap(BBitmap** _bitmap);

protected:
	/** @brief Swaps the byte order of a TranslatorBitmap header.
	 *  @param source Input header.
	 *  @param destination Output header with swapped fields. */
			void				SwapHeader(const TranslatorBitmap* source,
									TranslatorBitmap* destination);

protected:
			TranslatorBitmap	fHeader;
			BBitmap*			fBitmap;
			size_t				fPosition;
			size_t				fSize;
			bool				fDetached;

private:
	virtual	void _ReservedBitmapStream1();
	virtual void _ReservedBitmapStream2();

private:
			TranslatorBitmap*	fBigEndianHeader;
			long				_reserved[5];
};


#endif	// _BITMAP_STREAM_H
