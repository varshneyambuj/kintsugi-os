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
 */

/** @file UserDataWriter.h
 *  @brief Serializer that packs kernel disk-device data into a user-space buffer. */

#ifndef _K_DISK_DEVICE_USER_DATA_WRITER_H
#define _K_DISK_DEVICE_USER_DATA_WRITER_H

#include <SupportDefs.h>

struct user_disk_device_data;
struct user_partition_data;

namespace BPrivate {
namespace DiskDevice {

/** @brief Bump-allocator that lays out disk_device data structures into a
 *         user-space buffer and records the pointer fields that need
 *         relocating when the buffer is later mapped at a different address. */
class UserDataWriter {
public:
	/** @brief Constructs an empty writer not bound to any buffer. */
	UserDataWriter();
	/** @brief Constructs a writer bound to the given user buffer.
	 *  @param buffer     Pointer to the user-space destination buffer.
	 *  @param bufferSize Total size of the buffer in bytes. */
	UserDataWriter(user_disk_device_data *buffer, size_t bufferSize);
	/** @brief Releases the relocation list; does not free the user buffer. */
	~UserDataWriter();

	/** @brief Rebinds the writer to a new user buffer.
	 *  @param buffer     Pointer to the user-space destination buffer.
	 *  @param bufferSize Total size of the buffer in bytes.
	 *  @return B_OK on success, or an error code on failure. */
	status_t SetTo(user_disk_device_data *buffer, size_t bufferSize);
	/** @brief Detaches from the current buffer and clears all state. */
	void Unset();

	/** @brief Reserves @p size bytes of buffer space at the requested alignment.
	 *  @param size  Number of bytes to allocate.
	 *  @param align Alignment requirement; must be a power of two.
	 *  @return Pointer to the allocated region inside the buffer, or NULL if
	 *          the buffer is exhausted. */
	void *AllocateData(size_t size, size_t align = 1);
	/** @brief Allocates a user_partition_data with space for @p childCount children.
	 *  @return Pointer to the allocated partition record, or NULL on overflow. */
	user_partition_data *AllocatePartitionData(size_t childCount);
	/** @brief Allocates a user_disk_device_data with space for @p childCount children.
	 *  @return Pointer to the allocated device record, or NULL on overflow. */
	user_disk_device_data *AllocateDeviceData(size_t childCount);

	/** @brief Copies @p str into the buffer and returns a pointer to the copy.
	 *  @return Pointer to the placed string, or NULL if @p str does not fit. */
	char *PlaceString(const char *str);

	/** @brief Returns the total number of bytes consumed so far.
	 *
	 * The value may exceed the buffer size when the writer is being used to
	 * size a future buffer; in that case AllocateData() returns NULL. */
	size_t AllocatedSize() const;

	/** @brief Records that @p address is a pointer that must be relocated.
	 *  @return B_OK on success, or B_NO_MEMORY if the relocation list cannot grow. */
	status_t AddRelocationEntry(void *address);
	/** @brief Adjusts the pointer at @p address by the buffer base offset.
	 *  @return B_OK on success, or an error code if @p address is out of range. */
	status_t Relocate(void *address);

private:
	struct RelocationEntryList;

	user_disk_device_data	*fBuffer;             /**< Destination user-space buffer. */
	size_t					fBufferSize;          /**< Total size of @c fBuffer in bytes. */
	size_t					fAllocatedSize;       /**< Bytes consumed so far (may exceed @c fBufferSize when sizing). */
	RelocationEntryList		*fRelocationEntries;  /**< List of pointer fields awaiting relocation. */
};

} // namespace DiskDevice
} // namespace BPrivate

using BPrivate::DiskDevice::UserDataWriter;

#endif	// _K_DISK_DEVICE_USER_DATA_WRITER_H
