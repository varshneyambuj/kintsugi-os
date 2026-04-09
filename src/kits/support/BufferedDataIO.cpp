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
 *   Copyright 2011-2013, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BufferedDataIO.cpp
 * @brief Implementation of BBufferedDataIO, a buffered wrapper for BDataIO.
 *
 * BBufferedDataIO wraps any BDataIO (a non-positionable sequential stream)
 * with an internal read/write buffer. Reads are satisfied from the buffer
 * when possible, and writes are accumulated in the buffer and flushed to the
 * underlying stream either when the buffer is full or when Flush() is called
 * explicitly.
 *
 * The buffer size is clamped to a minimum of 512 bytes. When \a ownsStream
 * is \c true the wrapped BDataIO is deleted when this object is destroyed.
 * The optional \a partialReads flag controls whether Read() may return fewer
 * bytes than requested after draining the current buffer contents without
 * re-filling it.
 *
 * @see BDataIO, BBufferIO
 */


#include <BufferedDataIO.h>

#include <new>

#include <stdio.h>
#include <string.h>


//#define TRACE_DATA_IO
#ifdef TRACE_DATA_IO
#	define TRACE(x...) printf(x)
#else
#	define TRACE(x...) ;
#endif


/**
 * @brief Construct a BBufferedDataIO wrapping \a stream.
 *
 * Allocates an internal heap buffer of max(\a bufferSize, 512) bytes. If
 * the allocation fails, InitCheck() will return B_NO_MEMORY.
 *
 * @param stream       The underlying sequential data stream to wrap. Must
 *                     remain valid for the lifetime of this object unless
 *                     \a ownsStream is \c true.
 * @param bufferSize   Desired internal buffer size in bytes. Values below
 *                     512 are silently raised to 512.
 * @param ownsStream   If \c true, \a stream is deleted when this object is
 *                     destroyed.
 * @param partialReads If \c true, Read() returns as soon as it has drained
 *                     the buffered data, without attempting to re-fill the
 *                     buffer for the remainder of the request.
 */
BBufferedDataIO::BBufferedDataIO(BDataIO& stream, size_t bufferSize,
	bool ownsStream, bool partialReads)
	:
	fStream(stream),
	fPosition(0),
	fSize(0),
	fDirty(false),
	fOwnsStream(ownsStream),
	fPartialReads(partialReads)
{
	fBufferSize = max_c(bufferSize, 512);
	fBuffer = new(std::nothrow) uint8[fBufferSize];
}


/**
 * @brief Destroy the BBufferedDataIO, flushing any pending writes first.
 *
 * Calls Flush() to commit any dirty buffered data to the underlying stream,
 * frees the internal buffer, and — if OwnsStream() is \c true — deletes the
 * wrapped BDataIO.
 */
BBufferedDataIO::~BBufferedDataIO()
{
	Flush();
	delete[] fBuffer;

	if (fOwnsStream)
		delete &fStream;
}


/**
 * @brief Return the initialisation status of this object.
 *
 * @return B_OK if the internal buffer was allocated successfully,
 *         B_NO_MEMORY otherwise.
 */
status_t
BBufferedDataIO::InitCheck() const
{
	return fBuffer == NULL ? B_NO_MEMORY : B_OK;
}


/**
 * @brief Return a pointer to the wrapped BDataIO stream.
 *
 * @return The BDataIO passed to the constructor.
 */
BDataIO*
BBufferedDataIO::Stream() const
{
	return &fStream;
}


/**
 * @brief Return the capacity of the internal read/write buffer.
 *
 * @return Buffer size in bytes (always >= 512).
 */
size_t
BBufferedDataIO::BufferSize() const
{
	return fBufferSize;
}


/**
 * @brief Test whether this object owns (and will delete) the wrapped stream.
 *
 * @return \c true if the wrapped BDataIO will be deleted on destruction.
 */
bool
BBufferedDataIO::OwnsStream() const
{
	return fOwnsStream;
}


/**
 * @brief Control whether the wrapped stream is deleted on destruction.
 *
 * @param ownsStream \c true to take ownership, \c false to relinquish it.
 */
void
BBufferedDataIO::SetOwnsStream(bool ownsStream)
{
	fOwnsStream = ownsStream;
}


/**
 * @brief Write any buffered (dirty) data to the underlying stream.
 *
 * If the buffer is clean (no pending writes) this is a no-op and returns
 * B_OK immediately. On a short write B_PARTIAL_WRITE is returned and
 * fPosition/fSize are updated to reflect the bytes that remain unsent.
 *
 * @return B_OK on success (or when nothing needed flushing),
 *         B_PARTIAL_WRITE if fewer bytes than expected were written, or an
 *         error code from the underlying stream.
 */
status_t
BBufferedDataIO::Flush()
{
	if (!fDirty)
		return B_OK;

	ssize_t bytesWritten = fStream.Write(fBuffer + fPosition, fSize);
	if ((size_t)bytesWritten == fSize) {
		fDirty = false;
		fPosition = 0;
		fSize = 0;
		return B_OK;
	} else if (bytesWritten >= 0) {
		fSize -= bytesWritten;
		fPosition += bytesWritten;
		return B_PARTIAL_WRITE;
	}

	return B_OK;
}


/**
 * @brief Read up to \a size bytes into \a buffer.
 *
 * Data is served from the internal buffer first. If \a partialReads was set
 * at construction, the call returns as soon as any buffered bytes have been
 * copied — even if fewer than \a size bytes were returned. When the buffer is
 * empty (or exhausted) and the request fits within the buffer size, the
 * internal buffer is re-filled from the stream. Requests larger than the
 * buffer bypass it entirely and read directly from the stream.
 *
 * @param buffer Destination buffer. Must not be NULL.
 * @param size   Number of bytes requested.
 * @return Number of bytes actually read (>= 0), or a negative error code.
 *         Returns B_BAD_VALUE if \a buffer is NULL.
 */
ssize_t
BBufferedDataIO::Read(void* buffer, size_t size)
{
	if (buffer == NULL)
		return B_BAD_VALUE;

	TRACE("%p::Read(size %lu)\n", this, size);

	size_t bytesRead = 0;

	if (fSize > 0) {
		// fill the part of the stream we already have
		bytesRead = min_c(size, fSize);
		TRACE("%p: read %lu bytes we already have in the buffer.\n", this,
			bytesRead);
		memcpy(buffer, fBuffer + fPosition, bytesRead);

		buffer = (void*)((uint8_t*)buffer + bytesRead);
		size -= bytesRead;
		fPosition += bytesRead;
		fSize -= bytesRead;

		if (fPartialReads)
			return bytesRead;
	}

	if (size > fBufferSize || fBuffer == NULL) {
		// request is larger than our buffer, just fill it directly
		return fStream.Read(buffer, size);
	}

	if (size > 0) {
		// retrieve next buffer

		status_t status = Flush();
		if (status != B_OK)
			return status;

		TRACE("%p: read %" B_PRIuSIZE " bytes from stream\n", this,
			fBufferSize);
		ssize_t nextRead = fStream.Read(fBuffer, fBufferSize);
		if (nextRead < 0)
			return nextRead;

		fSize = nextRead;
		TRACE("%p: retrieved %" B_PRIuSIZE " bytes from stream\n", this, fSize);
		fPosition = 0;

		// Copy the remaining part
		size_t copy = min_c(size, fSize);
		memcpy(buffer, fBuffer, copy);
		TRACE("%p: copy %" B_PRIuSIZE" bytes to buffer\n", this, copy);

		bytesRead += copy;
		fPosition = copy;
		fSize -= copy;
	}

	return bytesRead;
}


/**
 * @brief Write \a size bytes from \a buffer to the stream (possibly buffered).
 *
 * Requests larger than the internal buffer (or when the buffer has not been
 * allocated) are flushed first and then written directly to the underlying
 * stream. Smaller requests are accumulated in the internal buffer; the buffer
 * is flushed automatically whenever it becomes full.
 *
 * @param buffer Source data. Must not be NULL.
 * @param size   Number of bytes to write.
 * @return Number of bytes accepted (>= 0), or a negative error code.
 *         Returns B_BAD_VALUE if \a buffer is NULL.
 */
ssize_t
BBufferedDataIO::Write(const void* buffer, size_t size)
{
	if (buffer == NULL)
		return B_BAD_VALUE;

	TRACE("%p::Write(size %lu)\n", this, size);

	if (size > fBufferSize || fBuffer == NULL) {
		// request is larger than our buffer, just fill it directly
		status_t status = Flush();
		if (status != B_OK)
			return status;

		return fStream.Write(buffer, size);
	}

	if (!fDirty) {
		// Throw away a read-only buffer if necessary
		TRACE("%p: throw away previous buffer.\n", this);
		fPosition = 0;
		fSize = 0;
	}

	size_t bytesWritten = 0;
	while (size > 0) {
		size_t toCopy = min_c(size, fBufferSize - (fPosition + fSize));
		TRACE("%p: write %" B_PRIuSIZE " bytes to the buffer.\n", this,
			toCopy);
		memcpy(fBuffer + (fPosition + fSize), buffer, toCopy);
		fSize += toCopy;
		bytesWritten += toCopy;
		size -= toCopy;
		fDirty = true;

		if ((fPosition + fSize) == fBufferSize) {
			status_t status = Flush();
			if (status != B_OK)
				return bytesWritten;
		}
	}

	return bytesWritten;
}


//	#pragma mark - FBC


status_t BBufferedDataIO::_Reserved0(void*) { return B_ERROR; }
status_t BBufferedDataIO::_Reserved1(void*) { return B_ERROR; }
status_t BBufferedDataIO::_Reserved2(void*) { return B_ERROR; }
status_t BBufferedDataIO::_Reserved3(void*) { return B_ERROR; }
status_t BBufferedDataIO::_Reserved4(void*) { return B_ERROR; }
