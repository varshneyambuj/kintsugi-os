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
 *     Ambuj Varshney, varshney@ambuj.se
 */

/**
 * @file UserDataWriter.cpp
 * @brief Utility class for serializing disk device manager data into user-space buffers.
 *
 * UserDataWriter manages a fixed-size user-space buffer into which kernel disk
 * device and partition structures are sequentially allocated and packed.  It
 * also tracks pointer-sized fields that must be relocated when the buffer is
 * copied to a different base address in user space.
 */

#include <util/kernel_cpp.h>
#include <ddm_userland_interface.h>
#include <Vector.h>

#include "UserDataWriter.h"

/** @brief Internal type alias for the list of addresses that need relocation. */
struct UserDataWriter::RelocationEntryList : Vector<addr_t*> {};


/**
 * @brief Default constructor. Creates an uninitialized writer with no backing buffer.
 *
 * Call SetTo() before using any allocation methods.
 */
UserDataWriter::UserDataWriter()
	: fBuffer(NULL),
	  fBufferSize(0),
	  fAllocatedSize(0),
	  fRelocationEntries(NULL)
{
}


/**
 * @brief Convenience constructor that immediately calls SetTo().
 * @param buffer     Pointer to the user-space destination buffer.
 * @param bufferSize Size of @a buffer in bytes.
 */
UserDataWriter::UserDataWriter(user_disk_device_data *buffer,
							   size_t bufferSize)
	: fBuffer(NULL),
	  fBufferSize(0),
	  fAllocatedSize(0),
	  fRelocationEntries(NULL)
{
	SetTo(buffer, bufferSize);
}


/** @brief Destructor. Releases the internal relocation entry list. */
UserDataWriter::~UserDataWriter()
{
	delete fRelocationEntries;
}


/**
 * @brief Attaches the writer to a user-space buffer and resets internal state.
 *
 * Any previous buffer association is discarded via Unset() before the new one
 * is applied.  When @a buffer is non-NULL and @a bufferSize is greater than
 * zero, a fresh RelocationEntryList is allocated for tracking pointer fields.
 *
 * @param buffer     Pointer to the user-space destination buffer.
 * @param bufferSize Size of @a buffer in bytes.
 * @return B_OK on success, B_NO_MEMORY if the relocation list could not be
 *         allocated.
 */
status_t
UserDataWriter::SetTo(user_disk_device_data *buffer, size_t bufferSize)
{
	Unset();
	fBuffer = buffer;
	fBufferSize = bufferSize;
	fAllocatedSize = 0;
	if (fBuffer && fBufferSize > 0) {
		fRelocationEntries = new(std::nothrow) RelocationEntryList;
		if (!fRelocationEntries)
			return B_NO_MEMORY;
	}
	return B_OK;
}


/**
 * @brief Detaches the writer from its buffer and resets all bookkeeping fields.
 *
 * After this call the object is in the same state as after the default
 * constructor.  The relocation entry list is freed if present.
 */
void
UserDataWriter::Unset()
{
	delete fRelocationEntries;
	fBuffer = NULL;
	fBufferSize = 0;
	fAllocatedSize = 0;
	fRelocationEntries = NULL;
}


/**
 * @brief Sequentially allocates @a size bytes from the backing buffer with
 *        optional alignment.
 *
 * The internal allocated-size cursor is always advanced even when the buffer
 * is too small or NULL, allowing callers to perform a dry run to determine
 * the required buffer size before making the actual allocation.
 *
 * @param size  Number of bytes to allocate.  A value of 0 is handled
 *              gracefully and returns NULL without advancing the cursor.
 * @param align Alignment requirement in bytes.  Values <= 1 mean no padding.
 * @return Pointer into the buffer on success, or NULL if the buffer is
 *         insufficient or not set.
 */
void *
UserDataWriter::AllocateData(size_t size, size_t align)
{
	// handles size == 0 gracefully
	// get a properly aligned offset
	size_t offset = fAllocatedSize;
	if (align > 1)
		offset = (fAllocatedSize + align - 1) / align * align;
	// get the result pointer
	void *result = NULL;
	if (fBuffer && offset + size <= fBufferSize)
		result = (uint8*)fBuffer + offset;
	// always update the allocated size, even if there wasn't enough space
	fAllocatedSize = offset + size;
	return result;
}


/**
 * @brief Allocates a user_partition_data struct sized for @a childCount children.
 *
 * The struct includes a flexible trailing array of child pointers; the
 * allocation is padded accordingly.
 *
 * @param childCount Number of child partition pointers to reserve space for.
 * @return Pointer to the allocated user_partition_data, or NULL if the buffer
 *         is full.
 */
user_partition_data *
UserDataWriter::AllocatePartitionData(size_t childCount)
{
	return (user_partition_data*)AllocateData(
		sizeof(user_partition_data)
		+ sizeof(user_partition_data*) * ((int32)childCount - 1),
		sizeof(int));
}


/**
 * @brief Allocates a user_disk_device_data struct sized for @a childCount partitions.
 *
 * Similar to AllocatePartitionData() but targets the top-level device
 * descriptor, which also embeds a trailing array of partition pointers.
 *
 * @param childCount Number of child partition pointers to reserve space for.
 * @return Pointer to the allocated user_disk_device_data, or NULL if the
 *         buffer is full.
 */
user_disk_device_data *
UserDataWriter::AllocateDeviceData(size_t childCount)
{
	return (user_disk_device_data*)AllocateData(
		sizeof(user_disk_device_data)
		+ sizeof(user_partition_data*) * ((int32)childCount - 1),
		sizeof(int));
}


/**
 * @brief Copies @a str (including its NUL terminator) into the backing buffer.
 *
 * @param str Source string to copy, or NULL.
 * @return Pointer to the stored string within the buffer, or NULL if @a str
 *         is NULL or the buffer has insufficient space.
 */
char *
UserDataWriter::PlaceString(const char *str)
{
	if (!str)
		return NULL;
	size_t len = strlen(str) + 1;
	char *data = (char*)AllocateData(len);
	if (data)
		memcpy(data, str, len);
	return data;
}


/**
 * @brief Returns the total number of bytes allocated so far (including dry-run
 *        overflows).
 * @return Cumulative allocated size in bytes.
 */
size_t
UserDataWriter::AllocatedSize() const
{
	return fAllocatedSize;
}


/**
 * @brief Registers a pointer-sized field inside the buffer that must be
 *        adjusted when the buffer is relocated to a new base address.
 *
 * The field at @a address must lie entirely within the current buffer.
 *
 * @param address Address of the pointer field to register.
 * @return B_OK on success, B_ERROR if the address is out of range or no
 *         relocation list is active.
 */
status_t
UserDataWriter::AddRelocationEntry(void *address)
{
	if (fRelocationEntries && (addr_t)address >= (addr_t)fBuffer
		&& (addr_t)address < (addr_t)fBuffer + fBufferSize - sizeof(void*)) {
		return fRelocationEntries->PushBack((addr_t*)address);
	}
	return B_ERROR;
}


/**
 * @brief Adjusts all registered pointer fields by the delta between @a address
 *        and the original buffer base.
 *
 * Each non-NULL pointer value stored in the registered fields is incremented
 * by (@a address - fBuffer), making them valid when the buffer is accessed
 * from the new base.
 *
 * @param address New base address to which the buffer will be mapped.
 * @return B_OK on success, B_BAD_VALUE if the writer has no active buffer or
 *         relocation list.
 */
status_t
UserDataWriter::Relocate(void *address)
{
	if (!fRelocationEntries || !fBuffer)
		return B_BAD_VALUE;
	int32 count = fRelocationEntries->Count();
	for (int32 i = 0; i < count; i++) {
		addr_t *entry = fRelocationEntries->ElementAt(i);
		if (*entry)
			*entry += (addr_t)address - (addr_t)fBuffer;
	}
	return B_OK;
}
