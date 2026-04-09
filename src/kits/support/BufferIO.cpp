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
 *   Copyright 2001-2008 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT license
 *
 *   Authors:
 *       Stefano Ceccherini, burton666@libero.it
 */


/**
 * @file BufferIO.cpp
 * @brief Implementation of BBufferIO, a buffered wrapper for BPositionIO.
 *
 * BBufferIO layers a single read/write-through cache buffer over any
 * BPositionIO (a positionable stream). Random-access reads and writes that
 * fit within the buffer size are served from the cache; larger I/O bypasses
 * it. Dirty (written) cache content is flushed to the stream automatically
 * on destruction or on an explicit Flush() call.
 *
 * The buffer is allocated with malloc() and is sized to at least 512 bytes.
 * When \a ownsStream is \c true the wrapped BPositionIO is deleted when this
 * object is destroyed.
 *
 * @see BPositionIO, BBufferedDataIO
 */


#include <BufferIO.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/**
 * @brief Construct a BBufferIO wrapping \a stream.
 *
 * Allocates a cache buffer of max(\a bufferSize, 512) bytes via malloc().
 * The current stream position is read from \a stream and stored internally.
 * If malloc() fails the application may abort (matching R5 behaviour; the
 * exception from operator new is not caught).
 *
 * @param stream     The positionable stream to wrap. Must remain valid for
 *                   the lifetime of this object unless \a ownsStream is
 *                   \c true.
 * @param bufferSize Desired cache size in bytes. Values below 512 are
 *                   silently raised to 512.
 * @param ownsStream If \c true, \a stream is deleted when this object is
 *                   destroyed.
 */
BBufferIO::BBufferIO(BPositionIO* stream, size_t bufferSize, bool ownsStream)
	:
	fBufferStart(0),
	fStream(stream),
	fBuffer(NULL),
	fBufferUsed(0),
	fBufferIsDirty(false),
	fOwnsStream(ownsStream)

{
	fBufferSize = max_c(bufferSize, 512);
	fPosition = stream->Position();

	// What can we do if this malloc fails ?
	// I think R5 uses new, but doesn't catch the thrown exception
	// (if you specify a very big buffer, the application just
	// terminates with abort).
	fBuffer = (char*)malloc(fBufferSize);
}


/**
 * @brief Destroy the BBufferIO, flushing any dirty cache data first.
 *
 * If the cache contains uncommitted writes, Flush() is called before
 * freeing the buffer. If OwnsStream() is \c true the wrapped BPositionIO is
 * also deleted.
 */
BBufferIO::~BBufferIO()
{
	if (fBufferIsDirty) {
		// Write pending changes to the stream
		Flush();
	}

	free(fBuffer);

	if (fOwnsStream)
		delete fStream;
}


/**
 * @brief Read up to \a size bytes from position \a pos in the stream.
 *
 * When the request fits within the buffer size the cache is consulted first.
 * If the desired range is not cached, any dirty data is flushed and a new
 * cache window is filled from the stream. Requests larger than the buffer
 * (or when the buffer is NULL) skip the cache and read directly from the
 * stream, flushing dirty data beforehand.
 *
 * @param pos    Byte offset within the stream from which to read.
 * @param buffer Destination buffer. Must not be NULL.
 * @param size   Number of bytes to read.
 * @return Number of bytes actually read (>= 0), B_NO_INIT if the stream
 *         pointer is NULL, B_BAD_VALUE if \a buffer is NULL, or another
 *         negative error code on failure.
 */
ssize_t
BBufferIO::ReadAt(off_t pos, void* buffer, size_t size)
{
	// We refuse to crash, even if
	// you were lazy and didn't give a valid
	// stream on construction.
	if (fStream == NULL)
		return B_NO_INIT;
	if (buffer == NULL)
		return B_BAD_VALUE;

	// If the amount of data we want doesn't fit in the buffer, just
	// read it directly from the disk (and don't touch the buffer).
	if (size > fBufferSize || fBuffer == NULL) {
		if (fBufferIsDirty)
			Flush();
		return fStream->ReadAt(pos, buffer, size);
	}

	// If the data we are looking for is not in the buffer...
	if (size > fBufferUsed
		|| pos < fBufferStart
		|| pos > fBufferStart + (off_t)fBufferUsed
		|| pos + size > fBufferStart + fBufferUsed) {
		if (fBufferIsDirty) {
			// If there are pending writes, do them.
			Flush();
		}

		// ...cache as much as we can from the stream
		ssize_t sizeRead = fStream->ReadAt(pos, fBuffer, fBufferSize);
		if (sizeRead < 0)
			return sizeRead;

		fBufferUsed = sizeRead;
		if (fBufferUsed > 0) {
			// The data is buffered starting from this offset
			fBufferStart = pos;
		}
	}

	size = min_c(size, fBufferUsed);

	// copy data from the cache to the given buffer
	memcpy(buffer, fBuffer + pos - fBufferStart, size);

	return size;
}


/**
 * @brief Write \a size bytes to position \a pos in the stream (cached).
 *
 * Writes that fit within the buffer size are written into the cache and
 * marked dirty. The cache is extended or re-loaded from the stream as
 * needed so that the written region is contiguous with already-cached data.
 * Writes larger than the buffer bypass the cache and go directly to the
 * stream.
 *
 * @param pos    Byte offset within the stream at which to write.
 * @param buffer Source data. Must not be NULL.
 * @param size   Number of bytes to write.
 * @return Number of bytes written (>= 0), B_NO_INIT if the stream pointer
 *         is NULL, B_BAD_VALUE if \a buffer is NULL, or a negative error
 *         code on failure.
 */
ssize_t
BBufferIO::WriteAt(off_t pos, const void* buffer, size_t size)
{
	if (fStream == NULL)
		return B_NO_INIT;
	if (buffer == NULL)
		return B_BAD_VALUE;

	// If data doesn't fit into the buffer, write it directly to the stream
	if (size > fBufferSize || fBuffer == NULL)
		return fStream->WriteAt(pos, buffer, size);

	// If we have cached data in the buffer, whose offset into the stream
	// is > 0, and the buffer isn't dirty, drop the data.
	if (!fBufferIsDirty && fBufferStart > pos) {
		fBufferStart = 0;
		fBufferUsed = 0;
	}

	// If we want to write beyond the cached data...
	if (pos > fBufferStart + (off_t)fBufferUsed
		|| pos < fBufferStart) {
		ssize_t read;
		off_t where = pos;

		// Can we just cache from the beginning?
		if (pos + size <= fBufferSize)
			where = 0;

		// ...cache more.
		read = fStream->ReadAt(where, fBuffer, fBufferSize);
		if (read > 0) {
			fBufferUsed = read;
			fBufferStart = where;
		}
	}

	memcpy(fBuffer + pos - fBufferStart, buffer, size);

	fBufferIsDirty = true;
	fBufferUsed = max_c((size + pos), fBufferUsed);

	return size;
}


/**
 * @brief Move the logical stream position.
 *
 * Updates the internal position used by the BPositionIO interface (inherited
 * Read/Write). Seeking does not affect the cache contents.
 *
 * @param position Amount or absolute offset to seek to, depending on
 *                 \a seekMode.
 * @param seekMode One of SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return The new stream position on success, B_NO_INIT if the stream
 *         pointer is NULL, B_BAD_VALUE if the computed position would be
 *         negative, or a stream error code for SEEK_END failures.
 */
off_t
BBufferIO::Seek(off_t position, uint32 seekMode)
{
	if (fStream == NULL)
		return B_NO_INIT;

	off_t newPosition = fPosition;

	switch (seekMode) {
		case SEEK_CUR:
			newPosition += position;
			break;
		case SEEK_SET:
			newPosition = position;
			break;
		case SEEK_END:
		{
			off_t size;
			status_t status = fStream->GetSize(&size);
			if (status != B_OK)
				return status;

			newPosition = size - position;
			break;
		}
	}

	if (newPosition < 0)
		return B_BAD_VALUE;

	fPosition = newPosition;
	return newPosition;
}


/**
 * @brief Return the current logical stream position.
 *
 * @return The current position in bytes from the beginning of the stream.
 */
off_t
BBufferIO::Position() const
{
	return fPosition;
}


/**
 * @brief Resize the underlying stream to \a size bytes.
 *
 * Delegates directly to the wrapped stream's SetSize(). The cache is not
 * modified; callers may want to Flush() first if there is dirty data near
 * the truncation point.
 *
 * @param size New stream size in bytes.
 * @return B_OK on success, B_NO_INIT if the stream pointer is NULL, or an
 *         error code from the underlying stream.
 */
status_t
BBufferIO::SetSize(off_t size)
{
	if (fStream == NULL)
		return B_NO_INIT;

	return fStream->SetSize(size);
}


/**
 * @brief Commit any dirty cache contents to the underlying stream.
 *
 * If the cache is clean this is a no-op and returns B_OK. On success the
 * dirty flag is cleared.
 *
 * @return B_OK on success (including when nothing needed flushing), or a
 *         negative error code from WriteAt() if the write failed.
 */
status_t
BBufferIO::Flush()
{
	if (!fBufferIsDirty)
		return B_OK;

	// Write the cached data to the stream
	ssize_t bytesWritten = fStream->WriteAt(fBufferStart, fBuffer, fBufferUsed);
	if (bytesWritten > 0)
		fBufferIsDirty = false;

	return (bytesWritten < 0) ? bytesWritten : B_OK;
}


/**
 * @brief Return a pointer to the wrapped BPositionIO stream.
 *
 * @return The BPositionIO passed to the constructor, or NULL if none was
 *         provided.
 */
BPositionIO*
BBufferIO::Stream() const
{
	return fStream;
}


/**
 * @brief Return the capacity of the internal cache buffer.
 *
 * @return Cache size in bytes (always >= 512).
 */
size_t
BBufferIO::BufferSize() const
{
	return fBufferSize;
}


/**
 * @brief Test whether this object owns (and will delete) the wrapped stream.
 *
 * @return \c true if the wrapped BPositionIO will be deleted on destruction.
 */
bool
BBufferIO::OwnsStream() const
{
	return fOwnsStream;
}


/**
 * @brief Control whether the wrapped stream is deleted on destruction.
 *
 * @param ownsStream \c true to take ownership, \c false to relinquish it.
 */
void
BBufferIO::SetOwnsStream(bool ownsStream)
{
	fOwnsStream = ownsStream;
}


/**
 * @brief Print internal state to stdout for debugging.
 *
 * Outputs the stream and buffer pointers, the cache window start and used
 * byte count, the physical buffer size, and the dirty / owns-stream flags.
 */
void
BBufferIO::PrintToStream() const
{
	printf("stream %p\n", fStream);
	printf("buffer %p\n", fBuffer);
	printf("start  %" B_PRId64 "\n", fBufferStart);
	printf("used   %ld\n", fBufferUsed);
	printf("phys   %ld\n", fBufferSize);
	printf("dirty  %s\n", (fBufferIsDirty) ? "true" : "false");
	printf("owns   %s\n", (fOwnsStream) ? "true" : "false");
}


//	#pragma mark - FBC padding


// These functions are here to maintain future binary
// compatibility.
status_t BBufferIO::_Reserved_BufferIO_0(void*) { return B_ERROR; }
status_t BBufferIO::_Reserved_BufferIO_1(void*) { return B_ERROR; }
status_t BBufferIO::_Reserved_BufferIO_2(void*) { return B_ERROR; }
status_t BBufferIO::_Reserved_BufferIO_3(void*) { return B_ERROR; }
status_t BBufferIO::_Reserved_BufferIO_4(void*) { return B_ERROR; }
