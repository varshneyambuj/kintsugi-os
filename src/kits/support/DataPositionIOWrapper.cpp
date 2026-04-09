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
 *   Copyright 2014, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DataPositionIOWrapper.cpp
 * @brief Adapts a sequential BDataIO stream into a BPositionIO interface.
 *
 * BDataPositionIOWrapper wraps a BDataIO (which only supports sequential
 * Read()/Write() calls) and exposes the BPositionIO interface by maintaining
 * a monotonically increasing logical position. Random-access operations
 * (ReadAt, WriteAt, Seek, SetSize, GetSize) succeed only when the requested
 * position exactly matches the current sequential position; any attempt to
 * seek backwards or skip ahead returns B_NOT_SUPPORTED.
 *
 * @see BDataIO, BPositionIO
 */


#include <DataPositionIOWrapper.h>

#include <stdio.h>


/**
 * @brief Construct a wrapper around an existing BDataIO stream.
 *
 * The wrapper does not take ownership of \a io; the caller is responsible
 * for the lifetime of the underlying stream.
 *
 * @param io Pointer to the sequential BDataIO stream to wrap. Must not be
 *           NULL.
 */
BDataPositionIOWrapper::BDataPositionIOWrapper(BDataIO* io)
	:
	BPositionIO(),
	fIO(io),
	fPosition(0)
{
}


/**
 * @brief Destructor. Does not delete the wrapped BDataIO.
 */
BDataPositionIOWrapper::~BDataPositionIOWrapper()
{
}


/**
 * @brief Read sequentially from the underlying stream.
 *
 * Forwards the read to the wrapped BDataIO and advances the internal
 * position by the number of bytes actually read.
 *
 * @param buffer Destination buffer; must be at least \a size bytes.
 * @param size   Number of bytes to read.
 * @return Number of bytes read on success; a negative error code on failure.
 * @see ReadAt(), BDataIO::Read()
 */
ssize_t
BDataPositionIOWrapper::Read(void* buffer, size_t size)
{
	ssize_t bytesRead = fIO->Read(buffer, size);
	if (bytesRead > 0)
		fPosition += bytesRead;

	return bytesRead;
}


/**
 * @brief Write sequentially to the underlying stream.
 *
 * Forwards the write to the wrapped BDataIO and advances the internal
 * position by the number of bytes actually written.
 *
 * @param buffer Source buffer; must contain at least \a size valid bytes.
 * @param size   Number of bytes to write.
 * @return Number of bytes written on success; a negative error code on failure.
 * @see WriteAt(), BDataIO::Write()
 */
ssize_t
BDataPositionIOWrapper::Write(const void* buffer, size_t size)
{
	ssize_t bytesWritten = fIO->Write(buffer, size);
	if (bytesWritten > 0)
		fPosition += bytesWritten;

	return bytesWritten;
}


/**
 * @brief Read from the stream at a specific position.
 *
 * Because the underlying stream is sequential, this call only succeeds
 * when \a position equals the current sequential position. Any other value
 * returns B_NOT_SUPPORTED.
 *
 * @param position Must equal the current stream position.
 * @param buffer   Destination buffer; must be at least \a size bytes.
 * @param size     Number of bytes to read.
 * @return Number of bytes read on success; B_NOT_SUPPORTED if
 *         \a position != current position; a negative error code otherwise.
 * @see Read(), WriteAt()
 */
ssize_t
BDataPositionIOWrapper::ReadAt(off_t position, void* buffer, size_t size)
{
	if (position != fPosition)
		return B_NOT_SUPPORTED;

	return Read(buffer, size);
}


/**
 * @brief Write to the stream at a specific position.
 *
 * Because the underlying stream is sequential, this call only succeeds
 * when \a position equals the current sequential position. Any other value
 * returns B_NOT_SUPPORTED.
 *
 * @param position Must equal the current stream position.
 * @param buffer   Source buffer; must contain at least \a size valid bytes.
 * @param size     Number of bytes to write.
 * @return Number of bytes written on success; B_NOT_SUPPORTED if
 *         \a position != current position; a negative error code otherwise.
 * @see Write(), ReadAt()
 */
ssize_t
BDataPositionIOWrapper::WriteAt(off_t position, const void* buffer,
	size_t size)
{
	if (position != fPosition)
		return B_NOT_SUPPORTED;

	return Write(buffer, size);
}


/**
 * @brief Seek to a new position in the stream.
 *
 * Only no-op seeks are supported. Specifically:
 * - SEEK_CUR with offset 0 (stay at current position) returns B_OK.
 * - SEEK_SET to the current position returns B_OK.
 * - SEEK_END is never supported.
 * - Any other movement returns B_NOT_SUPPORTED.
 *
 * @param position Desired offset relative to \a seekMode.
 * @param seekMode One of SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return B_OK for a no-op seek; B_NOT_SUPPORTED for any actual movement;
 *         B_BAD_VALUE for an unrecognised \a seekMode.
 * @see Position()
 */
off_t
BDataPositionIOWrapper::Seek(off_t position, uint32 seekMode)
{
	switch (seekMode) {
		case SEEK_CUR:
			return position == 0 ? B_OK : B_NOT_SUPPORTED;
		case SEEK_SET:
			return position == fPosition ? B_OK : B_NOT_SUPPORTED;
		case SEEK_END:
			return B_NOT_SUPPORTED;
		default:
			return B_BAD_VALUE;
	}
}


/**
 * @brief Return the current sequential position within the stream.
 *
 * The position starts at 0 and advances monotonically with each successful
 * Read() or Write() call.
 *
 * @return Current byte offset from the beginning of the stream.
 * @see Seek()
 */
off_t
BDataPositionIOWrapper::Position() const
{
	return fPosition;
}


/**
 * @brief Attempt to set the logical size of the stream.
 *
 * The wrapped BDataIO has no concept of size, so this operation only
 * succeeds when the requested \a size equals the current position (i.e.
 * a no-op truncation to the end of already-written data).
 *
 * @param size Desired stream size in bytes.
 * @return B_OK if \a size == current position; B_NOT_SUPPORTED otherwise.
 * @see GetSize()
 */
status_t
BDataPositionIOWrapper::SetSize(off_t size)
{
	return size == fPosition ? B_OK : B_NOT_SUPPORTED;
}


/**
 * @brief Query the size of the stream.
 *
 * The wrapped BDataIO does not expose a total size, so this always returns
 * B_NOT_SUPPORTED.
 *
 * @param[out] size Unused; not written on return.
 * @return B_NOT_SUPPORTED unconditionally.
 * @see SetSize()
 */
status_t
BDataPositionIOWrapper::GetSize(off_t* size) const
{
	return B_NOT_SUPPORTED;
}
