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
 *   Copyright 2002-2011, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Travis Smith
 *       Michael Wilber
 */


/**
 * @file BitmapStream.cpp
 * @brief BPositionIO adapter that exposes a BBitmap as a translation stream
 *
 * Implements BBitmapStream, which wraps a BBitmap in a BPositionIO interface
 * suitable for use with BTranslatorRoster::Translate(). The stream presents
 * a big-endian TranslatorBitmap header followed by the raw pixel data. Writing
 * a complete header automatically allocates or replaces the underlying BBitmap.
 *
 * @see TranslatorRoster.cpp, TranslationUtils.cpp
 */


#include <BitmapStream.h>

#include <new>

#include <string.h>

#include <Bitmap.h>
#include <Debug.h>


/**
 * @brief Constructs a BBitmapStream, optionally wrapping an existing BBitmap.
 *
 * If \a bitmap is non-NULL and valid, the stream is initialized with that
 * bitmap's geometry and its header is pre-populated. If \a bitmap is NULL,
 * the stream starts empty and a BBitmap is created automatically when a
 * complete TranslatorBitmap header is written via WriteAt().
 *
 * @param bitmap An existing BBitmap to wrap, or NULL to create a new one
 *     when data is written.
 */
BBitmapStream::BBitmapStream(BBitmap* bitmap)
{
	fBitmap = bitmap;
	fDetached = false;
	fPosition = 0;
	fSize = 0;
	fBigEndianHeader = new (std::nothrow) TranslatorBitmap;
	if (fBigEndianHeader == NULL) {
		fBitmap = NULL;
		return;
	}

	// Extract header information if bitmap is available
	if (fBitmap != NULL && fBitmap->InitCheck() == B_OK) {
		fHeader.magic = B_TRANSLATOR_BITMAP;
		fHeader.bounds = fBitmap->Bounds();
		fHeader.rowBytes = fBitmap->BytesPerRow();
		fHeader.colors = fBitmap->ColorSpace();
		fHeader.dataSize = static_cast<uint32>
			((fHeader.bounds.Height() + 1) * fHeader.rowBytes);
		fSize = sizeof(TranslatorBitmap) + fHeader.dataSize;

		if (B_HOST_IS_BENDIAN)
			*fBigEndianHeader = fHeader;
		else
			SwapHeader(&fHeader, fBigEndianHeader);
	} else
		fBitmap = NULL;
}


/**
 * @brief Destroys the BBitmapStream.
 *
 * Deletes the internal BBitmap unless DetachBitmap() was previously called,
 * then frees the big-endian header buffer.
 */
BBitmapStream::~BBitmapStream()
{
	if (!fDetached)
		delete fBitmap;

	delete fBigEndianHeader;
}


/**
 * @brief Reads bytes from the stream at an absolute position.
 *
 * The first sizeof(TranslatorBitmap) bytes are the big-endian header;
 * subsequent bytes are raw pixel data from the BBitmap.
 *
 * @param pos Absolute byte offset to read from.
 * @param buffer Destination buffer for the data.
 * @param size Maximum number of bytes to read.
 * @return The number of bytes actually read, or a negative error code.
 */
ssize_t
BBitmapStream::ReadAt(off_t pos, void* buffer, size_t size)
{
	if (fBitmap == NULL)
		return B_NO_INIT;
	if (size == 0)
		return B_OK;
	if (pos >= (off_t)fSize || pos < 0 || buffer == NULL)
		return B_BAD_VALUE;

	ssize_t toRead;
	void *source;

	if (pos < (off_t)sizeof(TranslatorBitmap)) {
		toRead = sizeof(TranslatorBitmap) - pos;
		source = (reinterpret_cast<uint8 *>(fBigEndianHeader)) + pos;
	} else {
		toRead = fSize - pos;
		source = (reinterpret_cast<uint8 *>(fBitmap->Bits())) + pos -
			sizeof(TranslatorBitmap);
	}
	if (toRead > (ssize_t)size)
		toRead = (ssize_t)size;

	memcpy(buffer, source, toRead);
	return toRead;
}


/**
 * @brief Writes bytes into the stream at an absolute position.
 *
 * Writes are split between the header region and the pixel data region.
 * Completing the header (i.e. writing all sizeof(TranslatorBitmap) bytes)
 * triggers allocation of a matching BBitmap. If the new header geometry
 * differs from an existing BBitmap, the old BBitmap is replaced.
 *
 * @param pos Absolute byte offset to write at.
 * @param data Source buffer containing the data to write.
 * @param size Number of bytes to write.
 * @return The number of bytes written, or a negative error code.
 */
ssize_t
BBitmapStream::WriteAt(off_t pos, const void* data, size_t size)
{
	if (size == 0)
		return B_OK;
	if (!data || pos < 0 || pos > (off_t)fSize)
		return B_BAD_VALUE;

	ssize_t written = 0;
	while (size > 0) {
		size_t toWrite;
		void *dest;
		// We depend on writing the header separately in detecting
		// changes to it
		if (pos < (off_t)sizeof(TranslatorBitmap)) {
			toWrite = sizeof(TranslatorBitmap) - pos;
			dest = (reinterpret_cast<uint8 *> (&fHeader)) + pos;
		} else {
			if (fBitmap == NULL || !fBitmap->IsValid())
				return B_ERROR;

			toWrite = fHeader.dataSize - pos + sizeof(TranslatorBitmap);
			dest = (reinterpret_cast<uint8 *> (fBitmap->Bits())) +
				pos - sizeof(TranslatorBitmap);
		}
		if (toWrite > size)
			toWrite = size;
		if (!toWrite && size)
			// i.e. we've been told to write too much
			return B_BAD_VALUE;

		memcpy(dest, data, toWrite);
		pos += toWrite;
		written += toWrite;
		data = (reinterpret_cast<const uint8 *> (data)) + toWrite;
		size -= toWrite;
		if (pos > (off_t)fSize)
			fSize = pos;
		// If we change the header, the rest needs to be reset
		if (pos == sizeof(TranslatorBitmap)) {
			// Setup both host and Big Endian byte order bitmap headers
			*fBigEndianHeader = fHeader;
			if (B_HOST_IS_LENDIAN)
				SwapHeader(fBigEndianHeader, &fHeader);

			if (fBitmap != NULL
				&& (fBitmap->Bounds() != fHeader.bounds
					|| fBitmap->ColorSpace() != fHeader.colors
					|| (uint32)fBitmap->BytesPerRow() != fHeader.rowBytes)) {
				if (!fDetached)
					// if someone detached, we don't delete
					delete fBitmap;
				fBitmap = NULL;
			}
			if (fBitmap == NULL) {
				if (fHeader.bounds.left > 0.0 || fHeader.bounds.top > 0.0)
					DEBUGGER("non-origin bounds!");
				fBitmap = new (std::nothrow )BBitmap(fHeader.bounds,
					0, fHeader.colors, fHeader.rowBytes);
				if (fBitmap == NULL)
					return B_ERROR;
				if (!fBitmap->IsValid()) {
					status_t error = fBitmap->InitCheck();
					delete fBitmap;
					fBitmap = NULL;
					return error;
				}
				if ((uint32)fBitmap->BytesPerRow() != fHeader.rowBytes) {
					fprintf(stderr, "BitmapStream BytesPerRow width %" B_PRId32 " does not match "
						"value declared in header %" B_PRId32 "\n",
						fBitmap->BytesPerRow(), fHeader.rowBytes);
					return B_MISMATCHED_VALUES;
				}
			}
			if (fBitmap != NULL)
				fSize = sizeof(TranslatorBitmap) + fBitmap->BitsLength();
		}
	}
	return written;
}


/**
 * @brief Sets the current stream position.
 *
 * Supports SEEK_SET, SEEK_CUR, and SEEK_END modes. The position must remain
 * within [0, Size()].
 *
 * @param position The seek offset.
 * @param seekMode One of SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return The new stream position, or B_BAD_VALUE if out of range.
 */
off_t
BBitmapStream::Seek(off_t position, uint32 seekMode)
{
	// When whence == SEEK_SET, it just falls through to
	// fPosition = position
	if (seekMode == SEEK_CUR)
		position += fPosition;
	else if (seekMode == SEEK_END)
		position += fSize;

	if (position < 0 || position > (off_t)fSize)
		return B_BAD_VALUE;

	fPosition = position;
	return fPosition;
}


/**
 * @brief Returns the current stream position.
 * @return The current byte offset within the stream.
 */
off_t
BBitmapStream::Position() const
{
	return fPosition;
}


/**
 * @brief Returns the total size of the stream in bytes.
 *
 * This is sizeof(TranslatorBitmap) plus the bitmap's pixel data size once a
 * valid header has been written or a bitmap has been supplied.
 *
 * @return The stream size in bytes.
 */
off_t
BBitmapStream::Size() const
{
	return fSize;
}


/**
 * @brief Adjusts the declared size of the stream.
 *
 * This method has limited practical use — it cannot expand the stream beyond
 * the committed bitmap data, and it has no effect if no bitmap is present.
 *
 * @param size The desired size in bytes.
 * @return B_OK on success, or B_BAD_VALUE if \a size is negative or exceeds
 *     the maximum allowed by the current bitmap.
 */
status_t
BBitmapStream::SetSize(off_t size)
{
	if (size < 0)
		return B_BAD_VALUE;
	if (fBitmap && (size > (off_t)(fHeader.dataSize + sizeof(TranslatorBitmap))))
		return B_BAD_VALUE;
	// Problem:
	// What if someone calls SetSize() before writing the header,
	// so we don't know what bitmap to create?
	// Solution:
	// We assume people will write the header before any data,
	// so SetSize() is really not going to do anything.
	if (fBitmap != NULL)
		fSize = size;

	return B_NO_ERROR;
}


/**
 * @brief Transfers ownership of the internal BBitmap to the caller.
 *
 * After this call the stream no longer owns the bitmap; it will not be
 * deleted when the BBitmapStream is destroyed. May only be called once.
 *
 * @param _bitmap Set to point to the detached BBitmap on success.
 * @return B_OK on success, B_BAD_VALUE if \a _bitmap is NULL, or B_ERROR if
 *     no bitmap exists or DetachBitmap() was already called.
 */
status_t
BBitmapStream::DetachBitmap(BBitmap** _bitmap)
{
	if (_bitmap == NULL)
		return B_BAD_VALUE;
	if (!fBitmap || fDetached)
		return B_ERROR;

	fDetached = true;
	*_bitmap = fBitmap;

	return B_OK;
}


/**
 * @brief Byte-swaps all fields of a TranslatorBitmap header.
 *
 * Copies \a source to \a destination, then swaps every field using
 * swap_data(B_SWAP_ALWAYS). Useful for converting between host byte order
 * and the stream's big-endian representation.
 *
 * @param source The header to read from.
 * @param destination The header to write the swapped result into.
 */
void
BBitmapStream::SwapHeader(const TranslatorBitmap* source,
	TranslatorBitmap* destination)
{
	if (source == NULL || destination == NULL)
		return;

	*destination = *source;
	swap_data(B_UINT32_TYPE, destination, sizeof(TranslatorBitmap),
		B_SWAP_ALWAYS);
}


//	#pragma mark - FBC protection


void BBitmapStream::_ReservedBitmapStream1() {}
void BBitmapStream::_ReservedBitmapStream2() {}
