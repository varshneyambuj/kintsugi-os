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
 *   Copyright 2009-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file FileDescriptorIO.cpp
 * @brief Implementation of BFileDescriptorIO, a BPositionIO backed by a POSIX
 *        file descriptor.
 *
 * BFileDescriptorIO adapts a raw POSIX file descriptor to the BPositionIO
 * interface, allowing fd-based I/O to be used in contexts that require a
 * BPositionIO. Ownership of the descriptor is optionally transferred at
 * construction time.
 *
 * @see BFileDescriptorIO
 */

#include <FileDescriptorIO.h>

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>


/**
 * @brief Constructs a BFileDescriptorIO wrapping the given file descriptor.
 *
 * @param fd                 The POSIX file descriptor to wrap.
 * @param takeOverOwnership  If true, the descriptor is closed in the
 *                           destructor.
 */
BFileDescriptorIO::BFileDescriptorIO(int fd, bool takeOverOwnership)
	:
	fFD(fd),
	fOwnsFD(takeOverOwnership)
{
}


/**
 * @brief Destructor. Closes the descriptor if ownership was taken.
 */
BFileDescriptorIO::~BFileDescriptorIO()
{
	if (fOwnsFD)
		close(fFD);
}


/**
 * @brief Reads sequentially from the current file position.
 *
 * @param buffer Destination buffer for the data.
 * @param size   Maximum number of bytes to read.
 * @return Number of bytes read, or a negative errno value on error.
 */
ssize_t
BFileDescriptorIO::Read(void* buffer, size_t size)
{
	ssize_t bytesRead = read(fFD, buffer, size);
	return bytesRead >= 0 ? bytesRead : errno;
}


/**
 * @brief Writes sequentially at the current file position.
 *
 * @param buffer Source buffer containing the data to write.
 * @param size   Number of bytes to write.
 * @return Number of bytes written, or a negative errno value on error.
 */
ssize_t
BFileDescriptorIO::Write(const void* buffer, size_t size)
{
	ssize_t bytesWritten = write(fFD, buffer, size);
	return bytesWritten >= 0 ? bytesWritten : errno;
}


/**
 * @brief Reads from an absolute position using pread(2).
 *
 * Does not affect the current file offset.
 *
 * @param position Byte offset from the beginning of the file.
 * @param buffer   Destination buffer for the data.
 * @param size     Maximum number of bytes to read.
 * @return Number of bytes read, or a negative errno value on error.
 */
ssize_t
BFileDescriptorIO::ReadAt(off_t position, void* buffer, size_t size)
{
	ssize_t bytesRead = pread(fFD, buffer, size, position);
	return bytesRead >= 0 ? bytesRead : errno;
}


/**
 * @brief Writes to an absolute position using pwrite(2).
 *
 * Does not affect the current file offset.
 *
 * @param position Byte offset from the beginning of the file.
 * @param buffer   Source buffer containing the data to write.
 * @param size     Number of bytes to write.
 * @return Number of bytes written, or a negative errno value on error.
 */
ssize_t
BFileDescriptorIO::WriteAt(off_t position, const void* buffer, size_t size)
{
	ssize_t bytesWritten = pwrite(fFD, buffer, size, position);
	return bytesWritten >= 0 ? bytesWritten : errno;
}


/**
 * @brief Seeks the file descriptor using lseek(2).
 *
 * @param position New position value (interpretation depends on seekMode).
 * @param seekMode One of SEEK_SET, SEEK_END, or SEEK_CUR.
 * @return The new file offset on success, or a negative errno value on error.
 */
off_t
BFileDescriptorIO::Seek(off_t position, uint32 seekMode)
{
	off_t result = lseek(fFD, position, seekMode);
	return result >= 0 ? result : errno;
}


/**
 * @brief Returns the current file offset.
 *
 * Implemented as a zero-offset SEEK_CUR lseek(2) call.
 *
 * @return The current file offset, or a negative errno value on error.
 */
off_t
BFileDescriptorIO::Position() const
{
	off_t result = lseek(fFD, 0, SEEK_CUR);
	return result >= 0 ? result : errno;
}


/**
 * @brief Truncates the file to the given size using ftruncate(2).
 *
 * @param size New file size in bytes.
 * @return B_OK on success, or errno on failure.
 */
status_t
BFileDescriptorIO::SetSize(off_t size)
{
	return ftruncate(fFD, size) == 0 ? B_OK : errno;
}


/**
 * @brief Returns the size of the file via fstat(2).
 *
 * @param size Output parameter that receives the file size in bytes.
 * @return B_OK on success, or errno on failure.
 */
status_t
BFileDescriptorIO::GetSize(off_t* size) const
{
	struct stat st;
	if (fstat(fFD, &st) < 0)
		return errno;

	*size = st.st_size;
	return B_OK;
}
