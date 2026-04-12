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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DebugEventStream.cpp
 * @brief Sequential I/O streams for the system profiler debug event format.
 *
 * Implements BDebugEventInputStream for reading and BDebugEventOutputStream
 * for writing the binary event format produced by the system profiler. The
 * format begins with a debug_event_stream_header followed by a sequence of
 * system_profiler_event_header records each immediately preceded by their
 * variable-length payload.
 *
 * @see BDataIO, system_profiler_defs.h
 */


#include <DebugEventStream.h>

#include <stdlib.h>
#include <string.h>

#include <DataIO.h>

#include <system_profiler_defs.h>


/** @brief Default size of the internal read-ahead buffer for input streams. */
#define INPUT_BUFFER_SIZE	(128 * 1024)


/**
 * @brief Default constructor — creates an uninitialised input stream.
 *
 * All fields are zeroed or set to safe sentinel values. Call SetTo() before
 * using the stream.
 */
BDebugEventInputStream::BDebugEventInputStream()
	:
	fStream(NULL),
	fFlags(0),
	fEventMask(0),
	fBuffer(NULL),
	fBufferCapacity(0),
	fBufferSize(0),
	fBufferPosition(0),
	fStreamPosition(0),
	fOwnsBuffer(false)
{
}


/**
 * @brief Destructor — calls Unset() and frees the internal buffer if owned.
 */
BDebugEventInputStream::~BDebugEventInputStream()
{
	Unset();

	if (fOwnsBuffer)
		free(fBuffer);
}


/**
 * @brief Attach this stream to a BDataIO source.
 *
 * Calls Unset() to release any previous attachment, then allocates an
 * INPUT_BUFFER_SIZE read-ahead buffer (if one does not already exist) and
 * reads the stream header via _Init().
 *
 * @param stream  The data source to read from; must remain valid for the
 *                lifetime of this stream object. Must not be NULL.
 * @return B_OK on success; B_BAD_VALUE if @a stream is NULL; B_NO_MEMORY if
 *         buffer allocation fails; or a header-validation error.
 */
status_t
BDebugEventInputStream::SetTo(BDataIO* stream)
{
	Unset();

	// set the new values
	if (stream == NULL)
		return B_BAD_VALUE;

	fStream = stream;

	// allocate a buffer
	if (fBuffer == NULL) {
		fBuffer = (uint8*)malloc(INPUT_BUFFER_SIZE);
		if (fBuffer == NULL) {
			Unset();
			return B_NO_MEMORY;
		}

		fOwnsBuffer = true;
		fBufferCapacity = INPUT_BUFFER_SIZE;
		fBufferSize = 0;
	}

	return _Init();
}


/**
 * @brief Attach this stream to an in-memory data buffer.
 *
 * The buffer replaces any previous buffer managed by this object. When
 * @a takeOverOwnership is true, the buffer will be free()'d when the stream
 * is unset or destroyed.
 *
 * @param data               Pointer to the raw event stream data.
 * @param size               Byte size of the data buffer.
 * @param takeOverOwnership  If true, this object takes ownership of @a data
 *                           and will call free() on it when appropriate.
 * @return B_OK on success; B_BAD_VALUE if @a data is NULL or @a size is zero;
 *         or a header-validation error from _Init().
 */
status_t
BDebugEventInputStream::SetTo(const void* data, size_t size,
	bool takeOverOwnership)
{
	Unset();

	if (data == NULL || size == 0)
		return B_BAD_VALUE;

	if (fBuffer != NULL) {
		if (fOwnsBuffer)
			free(fBuffer);
		fBuffer = NULL;
		fBufferCapacity = 0;
		fBufferSize = 0;
	}

	fBuffer = (uint8*)data;
	fBufferCapacity = fBufferSize = size;
	fOwnsBuffer = takeOverOwnership;

	return _Init();
}


/**
 * @brief Detach from the current source and reset all streaming state.
 *
 * If an owned buffer of the default INPUT_BUFFER_SIZE is present it is kept
 * for reuse; any other owned buffer is freed. Unowned buffers are simply
 * released (not freed).
 */
void
BDebugEventInputStream::Unset()
{
	fStream = NULL;
	fFlags = 0;
	fEventMask = 0;

	// If we have a buffer that we own and has the right size, we keep it.
	if (fOwnsBuffer) {
		if (fBuffer != NULL && fBufferSize != INPUT_BUFFER_SIZE) {
			free(fBuffer);
			fBuffer = NULL;
			fBufferCapacity = 0;
			fBufferSize = 0;
		}
	} else {
		fBuffer = NULL;
		fBufferCapacity = 0;
		fBufferSize = 0;
	}
}


/**
 * @brief Seek to an absolute byte offset within the in-memory buffer.
 *
 * Only supported for buffer-backed streams (SetTo(const void*, ...) variant).
 * Stream-backed sources always return B_UNSUPPORTED.
 *
 * @param streamOffset  Byte offset from the beginning of the stream (after the
 *                      header was consumed by _Init()).
 * @return B_OK on success; B_UNSUPPORTED for stream sources;
 *         B_BUFFER_OVERFLOW if @a streamOffset is out of range.
 */
status_t
BDebugEventInputStream::Seek(off_t streamOffset)
{
	// TODO: Support for streams, at least for BPositionIOs.
	if (fStream != NULL)
		return B_UNSUPPORTED;

	if (streamOffset < 0 || streamOffset > (off_t)fBufferCapacity)
		return B_BUFFER_OVERFLOW;

	fStreamPosition = 0;
	fBufferPosition = streamOffset;
	fBufferSize = fBufferCapacity - streamOffset;

	return B_OK;
}


/*!	\brief Returns the next event in the stream.

	At the end of the stream \c 0 is returned and \c *_buffer is set to \c NULL.
	For events that don't have data associated with them, \c *_buffer will still
	be non-NULL, even if dereferencing that address is not allowed.

	\param _event Pointer to a pre-allocated location where the event ID shall
		be stored.
	\param _cpu Pointer to a pre-allocated location where the CPU index shall
		be stored.
	\param _buffer Pointer to a pre-allocated location where the pointer to the
		event data shall be stored.
	\param _streamOffset Pointer to a pre-allocated location where the event
		header's offset relative to the beginning of the stream shall be stored.
		May be \c NULL.
	\return A negative error code in case an error occurred while trying to read
		the info, the size of the data associated with the event otherwise.
*/
ssize_t
BDebugEventInputStream::ReadNextEvent(uint32* _event, uint32* _cpu,
	const void** _buffer, off_t* _streamOffset)
{
	// get the next header
	status_t error = _GetData(sizeof(system_profiler_event_header));
	if (error != B_OK) {
		if (error == B_BAD_DATA && fBufferSize == 0) {
			*_buffer = NULL;
			return 0;
		}
		return error;
	}

	system_profiler_event_header header
		= *(system_profiler_event_header*)(fBuffer + fBufferPosition);

	off_t streamOffset = fStreamPosition + fBufferPosition;

	// skip the header in the buffer
	fBufferSize -= sizeof(system_profiler_event_header);
	fBufferPosition += sizeof(system_profiler_event_header);

	// get the data
	if (header.size > 0) {
		error = _GetData(header.size);
		if (error != B_OK)
			return error;
	}

	*_event = header.event;
	*_cpu = header.cpu;
	*_buffer = fBuffer + fBufferPosition;
	if (_streamOffset)
		*_streamOffset = streamOffset;

	// skip the event in the buffer
	fBufferSize -= header.size;
	fBufferPosition += header.size;

	return header.size;
}


/**
 * @brief Read and validate the stream header, then position at first event.
 *
 * Reads the debug_event_stream_header, verifies the signature, version, and
 * that the host-endian flag is set (non-host endian is not yet supported),
 * and initialises fFlags and fEventMask from the header.
 *
 * @return B_OK on success; B_BAD_DATA if the header is invalid or the stream
 *         is non-host-endian.
 */
status_t
BDebugEventInputStream::_Init()
{
	fStreamPosition = 0;
	fBufferPosition = 0;

	// get the header
	status_t error = _GetData(sizeof(debug_event_stream_header));
	if (error != B_OK) {
		Unset();
		return error;
	}
	const debug_event_stream_header& header
		= *(const debug_event_stream_header*)(fBuffer + fBufferPosition);

	fBufferPosition += sizeof(debug_event_stream_header);
	fBufferSize -= sizeof(debug_event_stream_header);

	// check the header
	if (strncmp(header.signature, B_DEBUG_EVENT_STREAM_SIGNATURE,
			sizeof(header.signature)) != 0
		|| header.version != B_DEBUG_EVENT_STREAM_VERSION
		|| (header.flags & B_DEBUG_EVENT_STREAM_FLAG_HOST_ENDIAN) == 0) {
		// TODO: Support non-host endianess!
		Unset();
		return B_BAD_DATA;
	}

	fFlags = header.flags;
	fEventMask = header.event_mask;

	return B_OK;
}


/**
 * @brief Read bytes from the underlying BDataIO stream into the internal buffer.
 *
 * Reads in a loop until @a size bytes have been transferred or the stream
 * returns 0 or an error.
 *
 * @param _buffer  Destination buffer.
 * @param size     Number of bytes to read.
 * @return Total bytes read on success (may be less than @a size at EOF), or a
 *         negative error code.
 */
ssize_t
BDebugEventInputStream::_Read(void* _buffer, size_t size)
{
	uint8* buffer = (uint8*)_buffer;
	size_t totalBytesRead = 0;
	ssize_t bytesRead = 0;

	while (size > 0 && (bytesRead = fStream->Read(buffer, size)) > 0) {
		totalBytesRead += bytesRead;
		buffer += bytesRead;
		size -= bytesRead;
	}

	if (bytesRead < 0)
		return bytesRead;

	return totalBytesRead;
}


/**
 * @brief Ensure at least @a size bytes are available starting at fBufferPosition.
 *
 * Moves any unconsumed data to the start of the buffer and reads more data from
 * the underlying stream if needed. For buffer-backed streams where no stream is
 * attached, this is a pure capacity check.
 *
 * @param size  Minimum number of contiguous bytes required.
 * @return B_OK if sufficient data is available; B_BUFFER_OVERFLOW if @a size
 *         exceeds the buffer capacity; B_BAD_DATA if the stream ended before
 *         enough data could be read.
 */
status_t
BDebugEventInputStream::_GetData(size_t size)
{
	if (fBufferSize >= size)
		return B_OK;

	if (size > fBufferCapacity)
		return B_BUFFER_OVERFLOW;

	// move remaining data to the start of the buffer
	if (fBufferSize > 0 && fBufferPosition > 0)
		memmove(fBuffer, fBuffer + fBufferPosition, fBufferSize);
	fStreamPosition += fBufferPosition;
	fBufferPosition = 0;

	// read more data
	if (fStream != NULL) {
		ssize_t bytesRead = _Read(fBuffer + fBufferSize,
			fBufferCapacity - fBufferSize);
		if (bytesRead < 0)
			return bytesRead;

		fBufferSize += bytesRead;
	}

	return fBufferSize >= size ? B_OK : B_BAD_DATA;
}


// #pragma mark - BDebugEventOutputStream


/**
 * @brief Default constructor — creates an uninitialised output stream.
 */
BDebugEventOutputStream::BDebugEventOutputStream()
	:
	fStream(NULL),
	fFlags(0)
{
}


/**
 * @brief Destructor — calls Unset() to flush and detach from the stream.
 */
BDebugEventOutputStream::~BDebugEventOutputStream()
{
	Unset();
}


/**
 * @brief Attach to a BDataIO destination and write the stream header.
 *
 * Calls Unset() to release any prior stream, then composes and writes the
 * debug_event_stream_header with the host-endian flag unconditionally set.
 * Compression is not yet implemented.
 *
 * @param stream     The destination BDataIO; must remain valid for the
 *                   lifetime of this stream object.
 * @param flags      Optional stream flags (compression not yet supported).
 * @param eventMask  Bit mask of profiler events recorded in this stream.
 * @return B_OK on success; B_BAD_VALUE if @a stream is NULL; B_FILE_ERROR if
 *         the header write is short; or a BDataIO write error.
 */
status_t
BDebugEventOutputStream::SetTo(BDataIO* stream, uint32 flags, uint32 eventMask)
{
	Unset();

	// set the new values
	if (stream == NULL)
		return B_BAD_VALUE;

	fStream = stream;
	fFlags = /*(flags & B_DEBUG_EVENT_STREAM_FLAG_ZIPPED)
		|*/ B_DEBUG_EVENT_STREAM_FLAG_HOST_ENDIAN;
		// TODO: Support zipped data!

	// init and write the header
	debug_event_stream_header header;
	memset(header.signature, 0, sizeof(header.signature));
	strlcpy(header.signature, B_DEBUG_EVENT_STREAM_SIGNATURE,
		sizeof(header.signature));
	header.version = B_DEBUG_EVENT_STREAM_VERSION;
	header.flags = fFlags;
	header.event_mask = eventMask;

	ssize_t written = fStream->Write(&header, sizeof(header));
	if (written < 0) {
		Unset();
		return written;
	}
	if ((size_t)written != sizeof(header)) {
		Unset();
		return B_FILE_ERROR;
	}

	return B_OK;
}


/**
 * @brief Flush any pending data and detach from the current stream.
 */
void
BDebugEventOutputStream::Unset()
{
	Flush();
	fStream = NULL;
	fFlags = 0;
}


/**
 * @brief Write @a size bytes from @a buffer to the output stream.
 *
 * @param buffer  Source data to write.
 * @param size    Number of bytes to write; a size of 0 is a no-op.
 * @return B_OK on success; B_BAD_VALUE if @a buffer is NULL; B_FILE_ERROR if
 *         the write is short; or a BDataIO write error.
 */
status_t
BDebugEventOutputStream::Write(const void* buffer, size_t size)
{
	if (size == 0)
		return B_OK;
	if (buffer == NULL)
		return B_BAD_VALUE;

	ssize_t written = fStream->Write(buffer, size);
	if (written < 0)
		return written;
	if ((size_t)written != size)
		return B_FILE_ERROR;

	return B_OK;
}


/**
 * @brief Flush any internally buffered output data to the underlying stream.
 *
 * The current implementation holds no internal write buffer, so this is always
 * a no-op returning B_OK.
 *
 * @return B_OK always.
 */
status_t
BDebugEventOutputStream::Flush()
{
	return B_OK;
}
