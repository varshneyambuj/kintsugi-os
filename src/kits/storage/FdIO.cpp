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
 * @file FdIO.cpp
 * @brief Implementation of BFdIO, a BPositionIO backed by a POSIX file
 *        descriptor.
 *
 * BFdIO wraps a raw integer file descriptor and exposes it as a BPositionIO,
 * enabling use of POSIX fds wherever a BPositionIO is required. Optionally
 * it can take ownership of the descriptor and close it on destruction.
 *
 * @see BFdIO
 */

#include <FdIO.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>


/**
 * @brief Default constructor; creates a BFdIO with no associated descriptor.
 */
BFdIO::BFdIO()
	:
	BPositionIO(),
	fFd(-1),
	fOwnsFd(false)
{
}


/**
 * @brief Constructs a BFdIO wrapping the given file descriptor.
 *
 * @param fd     The POSIX file descriptor to wrap.
 * @param keepFd If true, the descriptor is closed when this object is
 *               destroyed or Unset() is called.
 */
BFdIO::BFdIO(int fd, bool keepFd)
	:
	BPositionIO(),
	fFd(fd),
	fOwnsFd(keepFd)
{
}


/**
 * @brief Destructor. Calls Unset(), which closes the descriptor if owned.
 */
BFdIO::~BFdIO()
{
	Unset();
}


/**
 * @brief Associates this object with a new file descriptor.
 *
 * Any previously held descriptor is released via Unset() first.
 *
 * @param fd     The new POSIX file descriptor.
 * @param keepFd If true, the descriptor will be closed on destruction or
 *               the next call to Unset()/SetTo().
 */
void
BFdIO::SetTo(int fd, bool keepFd)
{
	Unset();

	fFd = fd;
	fOwnsFd = keepFd;
}


/**
 * @brief Releases the currently held file descriptor.
 *
 * If the descriptor is owned (keepFd was true), it is closed with close(2).
 * After this call the object is in its default (no descriptor) state.
 */
void
BFdIO::Unset()
{
	if (fOwnsFd && fFd >= 0)
		close(fFd);

	fFd = -1;
	fOwnsFd = false;
}


/**
 * @brief Reads sequentially from the current file position.
 *
 * @param buffer Destination buffer for the data.
 * @param size   Maximum number of bytes to read.
 * @return Number of bytes read, or a negative errno value on error.
 */
ssize_t
BFdIO::Read(void* buffer, size_t size)
{
	ssize_t bytesRead = read(fFd, buffer, size);
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
BFdIO::Write(const void* buffer, size_t size)
{
	ssize_t bytesWritten = write(fFd, buffer, size);
	return bytesWritten >= 0 ? bytesWritten : errno;
}


/**
 * @brief Reads from the given absolute position using pread(2).
 *
 * Does not affect the file offset.
 *
 * @param position Byte offset from the beginning of the file.
 * @param buffer   Destination buffer for the data.
 * @param size     Maximum number of bytes to read.
 * @return Number of bytes read, or a negative errno value on error.
 */
ssize_t
BFdIO::ReadAt(off_t position, void* buffer, size_t size)
{
	ssize_t bytesRead = pread(fFd, buffer, size, position);
	return bytesRead >= 0 ? bytesRead : errno;
}


/**
 * @brief Writes to the given absolute position using pwrite(2).
 *
 * Does not affect the file offset.
 *
 * @param position Byte offset from the beginning of the file.
 * @param buffer   Source buffer containing the data to write.
 * @param size     Number of bytes to write.
 * @return Number of bytes written, or a negative errno value on error.
 */
ssize_t
BFdIO::WriteAt(off_t position, const void* buffer, size_t size)
{
	ssize_t bytesWritten = pwrite(fFd, buffer, size, position);
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
BFdIO::Seek(off_t position, uint32 seekMode)
{
	off_t newPosition = lseek(fFd, position, seekMode);
	return newPosition >= 0 ? newPosition : errno;
}


/**
 * @brief Returns the current file offset.
 *
 * Implemented as a zero-offset SEEK_CUR lseek(2) call.
 *
 * @return The current file offset, or a negative errno value on error.
 */
off_t
BFdIO::Position() const
{
	return const_cast<BFdIO*>(this)->BFdIO::Seek(0, SEEK_CUR);
}


/**
 * @brief Truncates the file to the given size using ftruncate(2).
 *
 * @param size New file size in bytes.
 * @return B_OK on success, or errno on failure.
 */
status_t
BFdIO::SetSize(off_t size)
{
	return ftruncate(fFd, size) == 0 ? B_OK : errno;
}


/**
 * @brief Returns the size of the file via fstat(2).
 *
 * @param _size Output parameter that receives the file size in bytes.
 * @return B_OK on success, or errno on failure.
 */
status_t
BFdIO::GetSize(off_t* _size) const
{
	struct stat st;
	if (fstat(fFd, &st) != 0)
		return errno;

	*_size = st.st_size;
	return B_OK;
}
