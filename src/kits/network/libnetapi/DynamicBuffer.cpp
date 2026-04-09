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
 *   Copyright 2009, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Bruno Albuquerque, bga@bug-br.org.br
 */

/** @file DynamicBuffer.cpp
 *  @brief FIFO-style byte buffer that grows on demand. Used as a scratch
 *         staging area by the network kit's reader/writer helpers. */

#include "DynamicBuffer.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include <Errors.h>
#include <SupportDefs.h>

#include <new>

/** @brief Constructs a DynamicBuffer with a pre-allocated initial capacity.
 *  @param initialSize Number of bytes to allocate up front. If allocation
 *                     fails, InitCheck() will return B_NO_INIT. */
DynamicBuffer::DynamicBuffer(size_t initialSize) :
	fBuffer(NULL),
	fBufferSize(0),
	fDataStart(0),
	fDataEnd(0),
	fInit(B_NO_INIT)
{
	fBuffer = new (std::nothrow) unsigned char[initialSize];
	if (fBuffer != NULL) {
		fBufferSize = initialSize;
		fInit = B_OK;
	}
}


/** @brief Destructor. Frees the underlying storage. */
DynamicBuffer::~DynamicBuffer()
{
	delete[] fBuffer;
	fBufferSize = 0;
	fDataStart = 0;
	fDataEnd = 0;
}


/** @brief Copy constructor. Produces an exact duplicate of the source buffer,
 *         including its read/write cursors and currently buffered bytes. */
DynamicBuffer::DynamicBuffer(const DynamicBuffer& buffer) :
	fBuffer(NULL),
	fBufferSize(0),
	fDataStart(0),
	fDataEnd(0),
	fInit(B_NO_INIT)
{
	fInit = buffer.fInit;
	if (fInit == B_OK) {
		status_t result = _GrowToFit(buffer.fBufferSize, true);
		if (result == B_OK) {
			memcpy(fBuffer, buffer.fBuffer, fBufferSize);
			fDataStart = buffer.fDataStart;
			fDataEnd = buffer.fDataEnd;
		} else
			fInit = result;
	}
}


/** @brief Returns the buffer's construction status.
 *  @return B_OK if allocated, or B_NO_INIT if construction failed. */
status_t
DynamicBuffer::InitCheck() const
{
	return fInit;
}


/** @brief Appends bytes to the end of the buffer, growing storage as needed.
 *  @param data Pointer to the source bytes.
 *  @param size Number of bytes to append.
 *  @return Bytes written on success, or a negative error code. */
ssize_t
DynamicBuffer::Write(const void* data, size_t size)
{
	if (fInit != B_OK)
		return fInit;

	status_t result = _GrowToFit(size);
	if (result != B_OK)
		return result;

	memcpy(fBuffer + fDataEnd, data, size);
	fDataEnd += size;

	return (ssize_t)size;
}


/** @brief Reads (consumes) bytes from the front of the buffer.
 *  @param data Destination buffer.
 *  @param size Maximum number of bytes to read.
 *  @return Number of bytes actually read, or a negative error code. */
ssize_t
DynamicBuffer::Read(void* data, size_t size)
{
	if (fInit != B_OK)
		return fInit;

	size = std::min(size, Size());
	if (size == 0)
		return 0;

	memcpy(data, fBuffer + fDataStart, size);
	fDataStart += size;

	if (fDataStart == fDataEnd)
		fDataStart = fDataEnd = 0;

	return size;
}


/** @brief Returns a pointer to the first currently buffered byte. */
unsigned char*
DynamicBuffer::Data() const
{
	return fBuffer + fDataStart;
}


/** @brief Returns the number of bytes currently stored in the buffer. */
size_t
DynamicBuffer::Size() const
{
	return fDataEnd - fDataStart;
}


/** @brief Returns the number of free bytes after the write cursor. */
size_t
DynamicBuffer::BytesRemaining() const
{
	return fBufferSize - fDataEnd;
}


/** @brief Dumps internal buffer statistics to stdout for debugging. */
void
DynamicBuffer::PrintToStream()
{
	printf("Current buffer size : %ld\n", fBufferSize);
	printf("Data start position : %ld\n", fDataStart);
	printf("Data end position   : %ld\n", fDataEnd);
	printf("Bytes wasted        : %ld\n", fDataStart);
	printf("Bytes available     : %ld\n", fBufferSize - fDataEnd);
}


/** @brief Ensures at least @a size free bytes exist after the write cursor.
 *  @param size  Number of additional bytes required.
 *  @param exact If true, resize to exactly the requested size; otherwise
 *               grow exponentially to amortise future allocations.
 *  @return B_OK on success, or B_NO_MEMORY if allocation fails. */
status_t
DynamicBuffer::_GrowToFit(size_t size, bool exact)
{
	if (size <= fBufferSize - fDataEnd)
		return B_OK;

	size_t newSize;
	if (!exact)
		newSize = (fBufferSize + size) * 2;
	else
		newSize = size;

	unsigned char* newBuffer = new (std::nothrow) unsigned char[newSize];
	if (newBuffer == NULL)
		return B_NO_MEMORY;

	if (fDataStart != fDataEnd)
		memcpy(newBuffer, fBuffer + fDataStart, fDataEnd - fDataStart);

	delete[] fBuffer;
	fBuffer = newBuffer;
	fDataEnd -= fDataStart;
	fDataStart = 0;
	fBufferSize = newSize;

	return B_OK;
}
