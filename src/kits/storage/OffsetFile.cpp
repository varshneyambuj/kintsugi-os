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
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file OffsetFile.cpp
 * @brief Implementation of OffsetFile, a BPositionIO view into a sub-region
 *        of a BFile.
 *
 * OffsetFile wraps a BFile and translates all positional read/write
 * operations by a fixed byte offset, making a contiguous sub-region of the
 * file appear as a standalone stream. This is used by resource-file parsers
 * and similar code that embeds data at a known offset within a larger file.
 *
 * @see OffsetFile
 */

#include <stdio.h>

#include "OffsetFile.h"

namespace BPrivate {
namespace Storage {

/**
 * @brief Default constructor; creates an uninitialised OffsetFile.
 */
OffsetFile::OffsetFile()
	: fFile(NULL),
	  fOffset(0),
	  fCurrentPosition(0)
{
}

/**
 * @brief Constructs an OffsetFile backed by the given BFile and byte offset.
 *
 * @param file   Pointer to an open BFile. Ownership is not transferred.
 * @param offset Byte offset from the beginning of the file where this view
 *               starts.
 */
OffsetFile::OffsetFile(BFile *file, off_t offset)
	: fFile(NULL),
	  fOffset(0),
	  fCurrentPosition(0)
{
	SetTo(file, offset);
}

/**
 * @brief Destructor.
 */
OffsetFile::~OffsetFile()
{
}

/**
 * @brief Associates this OffsetFile with a BFile and a starting offset.
 *
 * @param file   Pointer to an open BFile. Must remain valid for the lifetime
 *               of this OffsetFile.
 * @param offset Byte offset within @p file where position 0 of this view maps
 *               to.
 * @return B_OK if the file is properly initialised, or the file's InitCheck()
 *         error otherwise.
 */
status_t
OffsetFile::SetTo(BFile *file, off_t offset)
{
	Unset();
	fFile = file;
	fOffset = offset;
	return fFile->InitCheck();
}

/**
 * @brief Resets the OffsetFile to its default uninitialised state.
 */
void
OffsetFile::Unset()
{
	fFile = NULL;
	fOffset = 0;
	fCurrentPosition = 0;
}

/**
 * @brief Returns the initialisation status of this OffsetFile.
 *
 * @return B_OK if a valid BFile is associated, B_NO_INIT otherwise.
 */
status_t
OffsetFile::InitCheck() const
{
	return (fFile ? fFile->InitCheck() : B_NO_INIT);
}

/**
 * @brief Returns a pointer to the underlying BFile.
 *
 * @return Pointer to the BFile, or NULL if not initialised.
 */
BFile *
OffsetFile::File() const
{
	return fFile;
}

/**
 * @brief Reads data from the view at the given logical position.
 *
 * The logical position is translated to @c pos + fOffset before the
 * underlying BFile::ReadAt() is called.
 *
 * @param pos    Logical byte position within this view.
 * @param buffer Destination buffer for the read data.
 * @param size   Number of bytes to read.
 * @return Number of bytes actually read, or a negative error code.
 */
ssize_t
OffsetFile::ReadAt(off_t pos, void *buffer, size_t size)
{
	status_t error = InitCheck();
	ssize_t result = 0;
	if (error == B_OK)
		result = fFile->ReadAt(pos + fOffset, buffer, size);
	return (error == B_OK ? result : error);
}

/**
 * @brief Writes data to the view at the given logical position.
 *
 * The logical position is translated to @c pos + fOffset before the
 * underlying BFile::WriteAt() is called.
 *
 * @param pos    Logical byte position within this view.
 * @param buffer Source buffer containing the data to write.
 * @param size   Number of bytes to write.
 * @return Number of bytes actually written, or a negative error code.
 */
ssize_t
OffsetFile::WriteAt(off_t pos, const void *buffer, size_t size)
{
	status_t error = InitCheck();
	ssize_t result = 0;
	if (error == B_OK)
		result = fFile->WriteAt(pos + fOffset, buffer, size);
	return (error == B_OK ? result : error);
}

/**
 * @brief Seeks the current position within the view.
 *
 * Supports SEEK_SET, SEEK_END, and SEEK_CUR modes. All computations are
 * relative to the logical view (i.e. fOffset is not exposed to the caller).
 *
 * @param position New position value (interpretation depends on seekMode).
 * @param seekMode One of SEEK_SET, SEEK_END, or SEEK_CUR.
 * @return The new current position on success, or a negative error code.
 */
off_t
OffsetFile::Seek(off_t position, uint32 seekMode)
{
	off_t result = B_BAD_VALUE;
	status_t error = InitCheck();
	if (error == B_OK) {
		switch (seekMode) {
			case SEEK_SET:
				if (position >= 0)
					result = fCurrentPosition = position;
				break;
			case SEEK_END:
			{
				off_t size;
				error = GetSize(&size);
				if (error == B_OK) {
					if (size + position >= 0)
						result = fCurrentPosition = size + position;
				}
				break;
			}
			case SEEK_CUR:
				if (fCurrentPosition + position >= 0)
					result = fCurrentPosition += position;
				break;
			default:
				break;
		}
	}
	return (error == B_OK ? result : error);
}

/**
 * @brief Returns the current logical position within the view.
 *
 * @return The current position as a byte offset from the start of the view.
 */
off_t
OffsetFile::Position() const
{
	return fCurrentPosition;
}

/**
 * @brief Sets the logical size of the view.
 *
 * Translates the requested size to the underlying file size by adding
 * fOffset before calling BFile::SetSize().
 *
 * @param size New logical size of the view in bytes. Must be >= 0.
 * @return B_OK on success, B_BAD_VALUE if size is negative, or another
 *         error code on failure.
 */
status_t
OffsetFile::SetSize(off_t size)
{
	status_t error = (size >= 0 ? B_OK : B_BAD_VALUE );
	if (error == B_OK)
		error = InitCheck();
	if (error == B_OK)
		error = fFile->SetSize(size + fOffset);
	return error;
}

/**
 * @brief Returns the logical size of the view.
 *
 * Retrieves the underlying file's total size and subtracts fOffset. Returns
 * 0 if the underlying file is smaller than the offset.
 *
 * @param size Output parameter that receives the logical size in bytes.
 * @return B_OK on success, B_BAD_VALUE if size is NULL, or another error code.
 */
status_t
OffsetFile::GetSize(off_t *size) const
{
	status_t error = (size ? B_OK : B_BAD_VALUE );
	if (error == B_OK)
		error = InitCheck();
	if (error == B_OK)
		error = fFile->GetSize(size);
	if (error == B_OK) {
		*size -= fOffset;
		if (*size < 0)
			*size = 0;
	}
	return error;
}

/**
 * @brief Returns the byte offset at which this view starts within the file.
 *
 * @return The fixed byte offset from the beginning of the underlying BFile.
 */
off_t
OffsetFile::Offset() const
{
	return fOffset;
}

};	// namespace Storage
};	// namespace BPrivate
