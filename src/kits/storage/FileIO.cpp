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
 *   Copyright 2009-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file FileIO.cpp
 * @brief Implementation of BFileIO, a BPositionIO backed by a C stdio FILE.
 *
 * BFileIO wraps a C stdio FILE pointer and adapts it to the BPositionIO
 * interface. Positional reads and writes are emulated by saving and restoring
 * the current file position around each call. Ownership of the FILE is
 * optionally transferred at construction time.
 *
 * @see BFileIO
 */

#include <FileIO.h>

#include <errno.h>
#include <stdio.h>


/**
 * @brief Constructs a BFileIO wrapping the given C stdio FILE.
 *
 * @param file              The FILE pointer to wrap. Must not be NULL.
 * @param takeOverOwnership If true, the FILE is closed with fclose() when
 *                          this object is destroyed.
 */
BFileIO::BFileIO(FILE* file, bool takeOverOwnership)
	:
	fFile(file),
	fOwnsFile(takeOverOwnership)
{
}


/**
 * @brief Destructor. Closes the FILE if ownership was taken and it is not NULL.
 */
BFileIO::~BFileIO()
{
	if (fOwnsFile && fFile != NULL)
		fclose(fFile);
}


/**
 * @brief Reads sequentially from the current position using fread(3).
 *
 * @param buffer Destination buffer for the data.
 * @param size   Maximum number of bytes to read.
 * @return Number of bytes read, or a negative errno value on error.
 */
ssize_t
BFileIO::Read(void* buffer, size_t size)
{
	errno = B_OK;
	ssize_t bytesRead = fread(buffer, 1, size, fFile);
	return bytesRead >= 0 ? bytesRead : errno;
}


/**
 * @brief Writes sequentially at the current position using fwrite(3).
 *
 * @param buffer Source buffer containing the data to write.
 * @param size   Number of bytes to write.
 * @return Number of bytes written, or a negative errno value on error.
 */
ssize_t
BFileIO::Write(const void* buffer, size_t size)
{
	errno = B_OK;
	ssize_t bytesRead = fwrite(buffer, 1, size, fFile);
	return bytesRead >= 0 ? bytesRead : errno;
}


/**
 * @brief Reads from an absolute position by temporarily seeking to it.
 *
 * Saves the current file position, seeks to @p position, performs the read,
 * then seeks back to the original position.
 *
 * @param position Byte offset from the beginning of the file.
 * @param buffer   Destination buffer for the data.
 * @param size     Maximum number of bytes to read.
 * @return Number of bytes read, or a negative error code on failure.
 */
ssize_t
BFileIO::ReadAt(off_t position, void* buffer, size_t size)
{
	// save the old position and seek to the requested one
	off_t oldPosition = _Seek(position, SEEK_SET);
	if (oldPosition < 0)
		return oldPosition;

	// read
	ssize_t result = BFileIO::Read(buffer, size);

	// seek back
	fseeko(fFile, oldPosition, SEEK_SET);

	return result;
}


/**
 * @brief Writes to an absolute position by temporarily seeking to it.
 *
 * Saves the current file position, seeks to @p position, performs the write,
 * then seeks back to the original position.
 *
 * @param position Byte offset from the beginning of the file.
 * @param buffer   Source buffer containing the data to write.
 * @param size     Number of bytes to write.
 * @return Number of bytes written, or a negative error code on failure.
 */
ssize_t
BFileIO::WriteAt(off_t position, const void* buffer, size_t size)
{
	// save the old position and seek to the requested one
	off_t oldPosition = _Seek(position, SEEK_SET);
	if (oldPosition < 0)
		return oldPosition;

	// write
	ssize_t result = BFileIO::Write(buffer, size);

	// seek back
	fseeko(fFile, oldPosition, SEEK_SET);

	return result;
}


/**
 * @brief Seeks the FILE using fseeko(3) and returns the new position.
 *
 * @param position New position value (interpretation depends on seekMode).
 * @param seekMode One of SEEK_SET, SEEK_END, or SEEK_CUR.
 * @return The new file position on success, or a negative errno value on error.
 */
off_t
BFileIO::Seek(off_t position, uint32 seekMode)
{
	if (fseeko(fFile, position, seekMode) < 0)
		return errno;

	return BFileIO::Position();
}


/**
 * @brief Returns the current position within the FILE using ftello(3).
 *
 * @return The current byte offset from the beginning of the file, or a
 *         negative errno value on error.
 */
off_t
BFileIO::Position() const
{
	off_t result = ftello(fFile);
	return result >= 0 ? result : errno;
}


/**
 * @brief SetSize is not supported for C stdio FILE streams.
 *
 * @param size Ignored.
 * @return Always returns B_UNSUPPORTED.
 */
status_t
BFileIO::SetSize(off_t size)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Returns the size of the file by seeking to the end.
 *
 * Saves the current position, seeks to the end to determine the size, then
 * seeks back to the original position.
 *
 * @param _size Output parameter that receives the file size in bytes.
 * @return B_OK on success, or a negative error code on failure.
 */
status_t
BFileIO::GetSize(off_t* _size) const
{
	// save the current position and seek to the end
	off_t position = _Seek(0, SEEK_END);
	if (position < 0)
		return position;

	// get the size (position at end) and seek back
	off_t size = _Seek(position, SEEK_SET);
	if (size < 0)
		return size;

	*_size = size;
	return B_OK;
}


/**
 * @brief Saves the current FILE position, seeks to a new one, and returns the
 *        saved position.
 *
 * This helper is used by ReadAt(), WriteAt(), and GetSize() to implement
 * position-preserving I/O on a non-positional FILE stream.
 *
 * @param position  New position value.
 * @param seekMode  One of SEEK_SET, SEEK_END, or SEEK_CUR.
 * @return The file position before the seek (i.e. the saved position to
 *         restore), or a negative errno value if either ftello or fseeko
 *         fails.
 */
off_t
BFileIO::_Seek(off_t position, uint32 seekMode) const
{
	// save the current position
	off_t oldPosition = ftello(fFile);
	if (oldPosition < 0)
		return errno;

	// seek to the requested position
	if (fseeko(fFile, position, seekMode) < 0)
		return errno;

	return oldPosition;
}
