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
 *   Copyright 2005-2008, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file ring_buffer.cpp
 * @brief Lightweight lock-free ring buffer for byte streams.
 *
 * Intended as a building block: the buffer itself does no locking, allocates
 * no memory after create(), and can be used from interrupt context when the
 * kernel-only read/write variants are used. The public surface has kernel
 * (`ring_buffer_*`) and userspace-safe (`ring_buffer_user_*`) pairs that
 * differ only in whether user_memcpy() is used for the data copy.
 */


#include "ring_buffer.h"

#include <KernelExport.h>
#if 0
#include <port.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

#ifndef HAIKU_TARGET_PLATFORM_HAIKU
#define user_memcpy(x...) (memcpy(x), B_OK)
#endif

/**
 * @brief Return the number of bytes free in @a buffer.
 * @param buffer Ring buffer to inspect.
 * @return Space remaining before writes would start failing.
 */
static inline int32
space_left_in_buffer(struct ring_buffer *buffer)
{
	return buffer->size - buffer->in;
}


/**
 * @brief Consume up to @a length bytes from the buffer head.
 *
 * Handles both the no-wrap and wrap-around cases. For @a user=true the copy
 * goes through user_memcpy() and returns B_BAD_ADDRESS on fault.
 *
 * @param buffer Ring buffer to drain.
 * @param data   Destination buffer.
 * @param length Maximum bytes to read.
 * @param user   True when @a data lives in userspace.
 * @return Bytes read, or B_BAD_ADDRESS on user-copy fault.
 */
static ssize_t
read_from_buffer(struct ring_buffer *buffer, uint8 *data, ssize_t length,
	bool user)
{
	int32 available = buffer->in;

	if (length > available)
		length = available;

	if (length == 0)
		return 0;

	ssize_t bytesRead = length;

	if (buffer->first + length <= buffer->size) {
		// simple copy
		if (user) {
			if (user_memcpy(data, buffer->buffer + buffer->first, length) < B_OK)
				return B_BAD_ADDRESS;
		} else
			memcpy(data, buffer->buffer + buffer->first, length);
	} else {
		// need to copy both ends
		size_t upper = buffer->size - buffer->first;
		size_t lower = length - upper;

		if (user) {
			if (user_memcpy(data, buffer->buffer + buffer->first, upper) < B_OK
				|| user_memcpy(data + upper, buffer->buffer, lower) < B_OK)
				return B_BAD_ADDRESS;
		} else {
			memcpy(data, buffer->buffer + buffer->first, upper);
			memcpy(data + upper, buffer->buffer, lower);
		}
	}

	buffer->first = (buffer->first + bytesRead) % buffer->size;
	buffer->in -= bytesRead;

	return bytesRead;
}


/**
 * @brief Append up to @a length bytes at the buffer tail.
 *
 * Caps at available space and handles the wrap-around split. For @a user=true
 * the copy goes through user_memcpy() and returns B_BAD_ADDRESS on fault.
 *
 * @param buffer Ring buffer to fill.
 * @param data   Source buffer.
 * @param length Maximum bytes to write.
 * @param user   True when @a data lives in userspace.
 * @return Bytes written, or B_BAD_ADDRESS on user-copy fault.
 */
static ssize_t
write_to_buffer(struct ring_buffer *buffer, const uint8 *data, ssize_t length,
	bool user)
{
	int32 left = space_left_in_buffer(buffer);
	if (length > left)
		length = left;

	if (length == 0)
		return 0;

	ssize_t bytesWritten = length;
	int32 position = (buffer->first + buffer->in) % buffer->size;

	if (position + length <= buffer->size) {
		// simple copy
		if (user) {
			if (user_memcpy(buffer->buffer + position, data, length) < B_OK)
				return B_BAD_ADDRESS;
		} else
			memcpy(buffer->buffer + position, data, length);
	} else {
		// need to copy both ends
		size_t upper = buffer->size - position;
		size_t lower = length - upper;

		if (user) {
			if (user_memcpy(buffer->buffer + position, data, upper) < B_OK
				|| user_memcpy(buffer->buffer, data + upper, lower) < B_OK)
				return B_BAD_ADDRESS;
		} else {
			memcpy(buffer->buffer + position, data, upper);
			memcpy(buffer->buffer, data + upper, lower);
		}
	}

	buffer->in += bytesWritten;

	return bytesWritten;
}


/**
 * @brief Read without consuming, starting @a offset bytes past the head.
 *
 * Leaves the buffer state unchanged. Handles wrap-around.
 *
 * @param buffer Ring buffer to inspect.
 * @param offset Offset into the currently buffered data.
 * @param data   Destination buffer.
 * @param length Maximum bytes to read.
 * @param user   True when @a data lives in userspace.
 * @return Bytes copied, or B_BAD_ADDRESS on user-copy fault.
 */
static ssize_t
buffer_peek(struct ring_buffer* buffer, size_t offset, void* data,
	ssize_t length, bool user)
{
	size_t available = buffer->in;

	if (offset >= available || length == 0)
		return 0;

	if (offset + length > available)
		length = available - offset;

	if ((offset += buffer->first) >= (size_t)buffer->size)
		offset -= buffer->size;

	if (offset + length <= (size_t)buffer->size) {
		// simple copy
		if (user) {
			if (user_memcpy(data, buffer->buffer + offset, length) < B_OK)
				return B_BAD_ADDRESS;
		} else
			memcpy(data, buffer->buffer + offset, length);
	} else {
		// need to copy both ends
		size_t upper = buffer->size - offset;
		size_t lower = length - upper;

		if (user) {
			if (user_memcpy(data, buffer->buffer + offset, upper) < B_OK
				|| user_memcpy((uint8*)data + upper, buffer->buffer, lower) < B_OK)
				return B_BAD_ADDRESS;
		} else {
			memcpy(data, buffer->buffer + offset, upper);
			memcpy((uint8*)data + upper, buffer->buffer, lower);
		}
	}

	return length;
}


//	#pragma mark -


/**
 * @brief Allocate a ring buffer with @a size bytes of capacity.
 * @param size Capacity in bytes.
 * @return New ring buffer, or NULL on allocation failure.
 */
struct ring_buffer*
create_ring_buffer(size_t size)
{
	return create_ring_buffer_etc(NULL, size, 0);
}


/**
 * @brief Create a ring buffer in caller-supplied memory, optionally preserving data.
 *
 * When @a memory is NULL this behaves like create_ring_buffer(). When
 * @a memory is non-NULL the buffer is placed at that address and, if
 * RING_BUFFER_INIT_FROM_BUFFER is set and the header looks consistent, the
 * existing head/in/size counters are kept. Otherwise the buffer is cleared.
 *
 * @param memory Caller-owned memory region (must be size bytes).
 * @param size   Total size of the region including the ring_buffer header.
 * @param flags  Bitwise combination of RING_BUFFER_* flags.
 * @return Pointer to the ring buffer (same as @a memory when non-NULL).
 */
struct ring_buffer*
create_ring_buffer_etc(void* memory, size_t size, uint32 flags)
{
	if (memory == NULL) {
		ring_buffer* buffer = (ring_buffer*)malloc(sizeof(ring_buffer) + size);
		if (buffer == NULL)
			return NULL;

		buffer->size = size;
		ring_buffer_clear(buffer);

		return buffer;
	}

	size -= sizeof(ring_buffer);
	ring_buffer* buffer = (ring_buffer*)memory;

	buffer->size = size;
	if ((flags & RING_BUFFER_INIT_FROM_BUFFER) != 0
		&& (size_t)buffer->size == size
		&& buffer->in >= 0 && (size_t)buffer->in <= size
		&& buffer->first >= 0 && (size_t)buffer->first < size) {
		// structure looks valid
	} else
		ring_buffer_clear(buffer);

	return buffer;
}


/**
 * @brief Release a ring buffer previously allocated by create_ring_buffer().
 * @param buffer Ring buffer to free; must not have been created with
 *        caller-supplied memory.
 */
void
delete_ring_buffer(struct ring_buffer *buffer)
{
	free(buffer);
}


/**
 * @brief Drop all buffered data without freeing storage.
 * @param buffer Ring buffer to empty.
 */
void
ring_buffer_clear(struct ring_buffer *buffer)
{
	buffer->in = 0;
	buffer->first = 0;
}


/**
 * @brief Bytes currently available to read.
 * @param buffer Ring buffer.
 * @return Readable byte count.
 */
size_t
ring_buffer_readable(struct ring_buffer *buffer)
{
	return buffer->in;
}


/**
 * @brief Bytes that can currently be written without blocking.
 * @param buffer Ring buffer.
 * @return Writable byte count.
 */
size_t
ring_buffer_writable(struct ring_buffer *buffer)
{
	return buffer->size - buffer->in;
}


/**
 * @brief Discard up to @a length bytes from the head.
 * @param buffer Ring buffer.
 * @param length Maximum bytes to discard (clamped to what is readable).
 */
void
ring_buffer_flush(struct ring_buffer *buffer, size_t length)
{
	// we can't flush more bytes than there are
	if (length > (size_t)buffer->in)
		length = buffer->in;

	buffer->in -= length;
	buffer->first = (buffer->first + length) % buffer->size;
}


/**
 * @brief Kernel-space read; cannot fail.
 * @param buffer Ring buffer.
 * @param data   Kernel destination buffer.
 * @param length Maximum bytes to read.
 * @return Number of bytes read.
 */
size_t
ring_buffer_read(struct ring_buffer *buffer, uint8 *data, ssize_t length)
{
	return read_from_buffer(buffer, data, length, false);
}


/**
 * @brief Kernel-space write; cannot fail.
 * @param buffer Ring buffer.
 * @param data   Kernel source buffer.
 * @param length Maximum bytes to write.
 * @return Number of bytes written.
 */
size_t
ring_buffer_write(struct ring_buffer *buffer, const uint8 *data, ssize_t length)
{
	return write_to_buffer(buffer, data, length, false);
}


/**
 * @brief Userspace-safe read; @a data must be a user pointer.
 * @param buffer Ring buffer.
 * @param data   Userspace destination buffer.
 * @param length Maximum bytes to read.
 * @return Bytes read, or B_BAD_ADDRESS on fault.
 */
ssize_t
ring_buffer_user_read(struct ring_buffer *buffer, uint8 *data, ssize_t length)
{
	return read_from_buffer(buffer, data, length, true);
}


/**
 * @brief Userspace-safe write; @a data must be a user pointer.
 * @param buffer Ring buffer.
 * @param data   Userspace source buffer.
 * @param length Maximum bytes to write.
 * @return Bytes written, or B_BAD_ADDRESS on fault.
 */
ssize_t
ring_buffer_user_write(struct ring_buffer *buffer, const uint8 *data, ssize_t length)
{
	return write_to_buffer(buffer, data, length, true);
}


/**
 * @brief Kernel-space peek; copy without consuming.
 * @param buffer Ring buffer.
 * @param offset Offset relative to the head.
 * @param data   Kernel destination buffer.
 * @param length Maximum bytes to read.
 * @return Bytes copied.
 */
size_t
ring_buffer_peek(struct ring_buffer* buffer, size_t offset, void* data,
	size_t length)
{
	return buffer_peek(buffer, offset, data, length, false);
}


/**
 * @brief Userspace-safe peek; copy without consuming.
 * @param buffer Ring buffer.
 * @param offset Offset relative to the head.
 * @param data   Userspace destination buffer.
 * @param length Maximum bytes to read.
 * @return Bytes copied, or B_BAD_ADDRESS on fault.
 */
ssize_t
ring_buffer_user_peek(struct ring_buffer* buffer, size_t offset, void* data,
	ssize_t length)
{
	return buffer_peek(buffer, offset, data, length, true);
}


/**
 * @brief Produce iovec(s) spanning the currently buffered data.
 *
 * The caller-supplied @a vecs array must have at least 2 entries. The
 * function returns 0 for an empty buffer, 1 when the data is contiguous,
 * and 2 when it wraps around.
 *
 * @param buffer Ring buffer to describe.
 * @param vecs   Output iovec array (≥ 2 entries).
 * @return Number of iovecs written (0, 1, or 2).
 */
int32
ring_buffer_get_vecs(struct ring_buffer* buffer, struct iovec* vecs)
{
	if (buffer->in == 0)
		return 0;

	if (buffer->first + buffer->in <= buffer->size) {
		// one element
		vecs[0].iov_base = buffer->buffer + buffer->first;
		vecs[0].iov_len = buffer->in;
		return 1;
	}

	// two elements
	size_t upper = buffer->size - buffer->first;
	size_t lower = buffer->in - upper;

	vecs[0].iov_base = buffer->buffer + buffer->first;
	vecs[0].iov_len = upper;
	vecs[1].iov_base = buffer->buffer;
	vecs[1].iov_len = lower;

	return 2;
}


/**
 * @brief Splice up to @a length bytes from @a from into @a to.
 *
 * The amount actually moved is bounded by @a from's readable data and
 * @a to's free space. The split-copy case is handled, and only the bytes
 * successfully written are removed from @a from.
 *
 * @param to     Destination ring buffer.
 * @param length Maximum bytes to move.
 * @param from   Source ring buffer.
 * @return Bytes actually moved.
 */
size_t
ring_buffer_move(struct ring_buffer *to, ssize_t length,
	struct ring_buffer *from)
{
	if (length > from->in)
		length = from->in;

	if (length > (to->size - to->in))
		length = to->size - to->in;

	size_t bytesMoved = 0;

	if ((from->first + length) <= from->size) {
		// simple move
		bytesMoved = ring_buffer_write(to, from->buffer + from->first, length);
	} else {
		// need to move both ends
		size_t upper = from->size - from->first;
		size_t lower = length - upper;

		bytesMoved = ring_buffer_write(to, from->buffer + from->first, upper);
		if (bytesMoved == upper) {
			// only continue writing if the first part was completely written
			bytesMoved += ring_buffer_write(to, from->buffer, lower);
		}
	}

	from->first = (from->first + bytesMoved) % from->size;
	from->in -= bytesMoved;

	return bytesMoved;
}


#if 0
/**	Sends the contents of the ring buffer to a port.
 *	The buffer will be empty afterwards only if sending the data actually works.
 */

status_t
ring_buffer_write_to_port(struct ring_buffer *buffer, port_id port, int32 code,
	uint32 flags, bigtime_t timeout)
{
	int32 length = buffer->in;
	if (length == 0)
		return B_OK;

	status_t status;

	if (buffer->first + length <= buffer->size) {
		// simple write
		status = write_port_etc(port, code, buffer->buffer + buffer->first, length,
			flags, timeout);
	} else {
		// need to write both ends
		size_t upper = buffer->size - buffer->first;
		size_t lower = length - upper;

		iovec vecs[2];
		vecs[0].iov_base = buffer->buffer + buffer->first;
		vecs[0].iov_len = upper;
		vecs[1].iov_base = buffer->buffer;
		vecs[1].iov_len = lower;

		status = writev_port_etc(port, code, vecs, 2, length, flags, timeout);
	}

	if (status < B_OK)
		return status;

	buffer->first = (buffer->first + length) % buffer->size;
	buffer->in -= length;

	return status;
}
#endif
