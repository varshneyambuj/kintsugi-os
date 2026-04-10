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
 *   Copyright 2001-2005, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Erik Jaesler (erik@cgsoftware.com)
 */


/**
 * @file MessageUtils.cpp
 * @brief Utility functions for BMessage serialization helpers.
 *
 * Provides checksum computation and flatten/unflatten/byte-swap routines for
 * entry_ref and node_ref structures used by the messaging system.
 */


#include <string.h>
#include <ByteOrder.h>

#include <MessageUtils.h>

namespace BPrivate {

/** @brief Calculate a simple additive checksum over a byte buffer.
 *
 *  Processes the buffer in 4-byte big-endian words, accumulating a running sum.
 *  Any remaining bytes (fewer than 4) are appended as a partial word.
 *
 *  @param buffer Pointer to the data to checksum.
 *  @param size Number of bytes in the buffer.
 *  @return The computed 32-bit checksum.
 */
uint32
CalculateChecksum(const uint8 *buffer, int32 size)
{
	uint32 sum = 0;
	uint32 temp = 0;

	while (size > 3) {
		sum += B_BENDIAN_TO_HOST_INT32(*(int32 *)buffer);
		buffer += 4;
		size -= 4;
	}

	while (size > 0) {
		temp = (temp << 8) + *buffer++;
		size -= 1;
	}

	return sum + temp;
}


/** @brief Flatten an entry_ref into a byte buffer.
 *
 *  Serializes the device, directory, and name fields of the entry_ref into
 *  the provided buffer. On success, @a size is updated to the number of
 *  bytes written.
 *
 *  @param buffer Destination buffer for the flattened data.
 *  @param size On entry, the available buffer size; on exit, the number of
 *              bytes actually written.
 *  @param ref The entry_ref to flatten.
 *  @return B_OK on success, or B_BUFFER_OVERFLOW if the buffer is too small.
 */
status_t
entry_ref_flatten(char *buffer, size_t *size, const entry_ref *ref)
{
	if (*size < sizeof(ref->device) + sizeof(ref->directory))
		return B_BUFFER_OVERFLOW;

	memcpy((void *)buffer, (const void *)&ref->device, sizeof(ref->device));
	buffer += sizeof(ref->device);
	memcpy((void *)buffer, (const void *)&ref->directory, sizeof(ref->directory));
	buffer += sizeof(ref->directory);
	*size -= sizeof(ref->device) + sizeof(ref->directory);

	size_t nameLength = 0;
	if (ref->name) {
		nameLength = strlen(ref->name) + 1;
		if (*size < nameLength)
			return B_BUFFER_OVERFLOW;

		memcpy((void *)buffer, (const void *)ref->name, nameLength);
	}

	*size = sizeof(ref->device) + sizeof(ref->directory) + nameLength;

	return B_OK;
}


/** @brief Reconstruct an entry_ref from a previously flattened byte buffer.
 *
 *  Deserializes device, directory, and name fields. On failure the ref is
 *  reset to a default-constructed entry_ref.
 *
 *  @param ref Pointer to the entry_ref to populate.
 *  @param buffer Source buffer containing the flattened data.
 *  @param size Number of bytes available in @a buffer.
 *  @return B_OK on success, B_BAD_VALUE if the buffer is too small, or
 *          B_NO_MEMORY if the name could not be allocated.
 */
status_t
entry_ref_unflatten(entry_ref *ref, const char *buffer, size_t size)
{
	if (size < sizeof(ref->device) + sizeof(ref->directory)) {
		*ref = entry_ref();
		return B_BAD_VALUE;
	}

	memcpy((void *)&ref->device, (const void *)buffer, sizeof(ref->device));
	buffer += sizeof(ref->device);
	memcpy((void *)&ref->directory, (const void *)buffer,
		sizeof(ref->directory));
	buffer += sizeof(ref->directory);

	if (ref->device != ~(dev_t)0 && size > sizeof(ref->device)
			+ sizeof(ref->directory)) {
		ref->set_name(buffer);
		if (ref->name == NULL) {
			*ref = entry_ref();
			return B_NO_MEMORY;
		}
	} else
		ref->set_name(NULL);

	return B_OK;
}


/** @brief Byte-swap a flattened entry_ref in place for endian conversion.
 *
 *  Swaps the device (int32) and directory (int64) fields stored at the
 *  beginning of the buffer. The name string is left untouched.
 *
 *  @param buffer Buffer containing the flattened entry_ref.
 *  @param size Number of bytes available in @a buffer.
 *  @return B_OK on success, or B_BAD_VALUE if the buffer is too small.
 */
status_t
entry_ref_swap(char *buffer, size_t size)
{
	if (size < sizeof(dev_t) + sizeof(ino_t))
		return B_BAD_VALUE;

	dev_t *dev = (dev_t *)buffer;
	*dev = B_SWAP_INT32(*dev);
	buffer += sizeof(dev_t);

	ino_t *ino = (ino_t *)buffer;
	*ino = B_SWAP_INT64(*ino);

	return B_OK;
}


/** @brief Flatten a node_ref into a byte buffer.
 *
 *  Serializes the device and node fields into the provided buffer.
 *
 *  @param buffer Destination buffer for the flattened data.
 *  @param size On entry, the available buffer size. Not modified on output.
 *  @param ref The node_ref to flatten.
 *  @return B_OK on success, or B_BUFFER_OVERFLOW if the buffer is too small.
 */
status_t
node_ref_flatten(char *buffer, size_t *size, const node_ref *ref)
{
	if (*size < sizeof(dev_t) + sizeof(ino_t))
		return B_BUFFER_OVERFLOW;

	memcpy((void *)buffer, (const void *)&ref->device, sizeof(ref->device));
	buffer += sizeof(ref->device);
	memcpy((void *)buffer, (const void *)&ref->node, sizeof(ref->node));
	buffer += sizeof(ref->node);

	return B_OK;
}


/** @brief Reconstruct a node_ref from a previously flattened byte buffer.
 *
 *  On failure the ref is reset to a default-constructed node_ref.
 *
 *  @param ref Pointer to the node_ref to populate.
 *  @param buffer Source buffer containing the flattened data.
 *  @param size Number of bytes available in @a buffer.
 *  @return B_OK on success, or B_BAD_VALUE if the buffer is too small.
 */
status_t
node_ref_unflatten(node_ref *ref, const char *buffer, size_t size)
{
	if (size < sizeof(dev_t) + sizeof(ino_t)) {
		*ref = node_ref();
		return B_BAD_VALUE;
	}

	memcpy((void *)&ref->device, (const void *)buffer, sizeof(dev_t));
	buffer += sizeof(dev_t);
	memcpy((void *)&ref->node, (const void *)buffer, sizeof(ino_t));
	buffer += sizeof(ino_t);

	return B_OK;
}


/** @brief Byte-swap a flattened node_ref in place for endian conversion.
 *
 *  Swaps the device (int32) and node (int64) fields stored at the
 *  beginning of the buffer.
 *
 *  @param buffer Buffer containing the flattened node_ref.
 *  @param size Number of bytes available in @a buffer.
 *  @return B_OK on success, or B_BAD_VALUE if the buffer is too small.
 */
status_t
node_ref_swap(char *buffer, size_t size)
{
	if (size < sizeof(dev_t) + sizeof(ino_t))
		return B_BAD_VALUE;

	dev_t *dev = (dev_t *)buffer;
	*dev = B_SWAP_INT32(*dev);
	buffer += sizeof(dev_t);

	ino_t *ino = (ino_t *)buffer;
	*ino = B_SWAP_INT64(*ino);

	return B_OK;
}

} // namespace BPrivate
