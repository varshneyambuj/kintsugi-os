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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file ResourceItem.cpp
 * @brief Implementation of ResourceItem, a single typed resource entry.
 *
 * ResourceItem represents one resource stored in a resource file. It wraps a
 * BMallocIO buffer holding the raw data, and tracks identity information
 * (type, id, name) as well as load and modification state. Instances are
 * owned and managed by ResourcesContainer.
 *
 * @see ResourcesContainer
 */

#include "ResourceItem.h"

#include <stdio.h>
#include <string.h>

#include <DataIO.h>

namespace BPrivate {
namespace Storage {

/**
 * @brief Constructs an empty, uninitialized ResourceItem.
 *
 * All identity fields are zeroed and the loaded/modified flags are cleared.
 * The internal block size is set to 1 byte for fine-grained allocation.
 */
ResourceItem::ResourceItem()
			: BMallocIO(),
			  fOffset(0),
			  fInitialSize(0),
			  fType(0),
			  fID(0),
			  fName(),
			  fIsLoaded(false),
			  fIsModified(false)
{
	SetBlockSize(1);
}

/**
 * @brief Destroys the ResourceItem and releases its buffer.
 */
ResourceItem::~ResourceItem()
{
}

/**
 * @brief Writes data into the resource buffer at the given position.
 *
 * Delegates to BMallocIO::WriteAt and marks the item as modified on success.
 *
 * @param pos    Byte offset within the buffer at which to write.
 * @param buffer Pointer to the data to write.
 * @param size   Number of bytes to write.
 * @return Number of bytes written, or a negative error code on failure.
 */
ssize_t
ResourceItem::WriteAt(off_t pos, const void *buffer, size_t size)
{
	ssize_t result = BMallocIO::WriteAt(pos, buffer, size);
	if (result >= 0)
		SetModified(true);
	return result;
}

/**
 * @brief Resizes the resource data buffer.
 *
 * Delegates to BMallocIO::SetSize and marks the item as modified on success.
 *
 * @param size The new size in bytes.
 * @return B_OK on success, or an error code on failure.
 */
status_t
ResourceItem::SetSize(off_t size)
{
	status_t error = BMallocIO::SetSize(size);
	if (error == B_OK)
		SetModified(true);
	return error;
}

/**
 * @brief Sets the on-disk location of the resource data.
 *
 * Stores the file offset and the initial (on-disk) size of the resource so
 * that it can be loaded lazily from the file later.
 *
 * @param offset      Byte offset within the resource region of the file.
 * @param initialSize Size of the resource data on disk.
 */
void
ResourceItem::SetLocation(int32 offset, size_t initialSize)
{
	SetOffset(offset);
	fInitialSize = initialSize;
}

/**
 * @brief Sets the type code, numeric ID, and name of the resource.
 *
 * @param type The four-byte type code identifying the resource type.
 * @param id   The numeric resource ID.
 * @param name The resource name string (may be empty).
 */
void
ResourceItem::SetIdentity(type_code type, int32 id, const char *name)
{
	fType = type;
	fID = id;
	fName = name;
}

/**
 * @brief Sets the byte offset of the resource within the file.
 *
 * @param offset The byte offset from the start of the resource region.
 */
void
ResourceItem::SetOffset(int32 offset)
{
	fOffset = offset;
}

/**
 * @brief Returns the byte offset of the resource within the file.
 *
 * @return The offset stored by SetOffset() or SetLocation().
 */
int32
ResourceItem::Offset() const
{
	return fOffset;
}

/**
 * @brief Returns the original on-disk size of the resource data.
 *
 * @return The size value supplied to SetLocation().
 */
size_t
ResourceItem::InitialSize() const
{
	return fInitialSize;
}

/**
 * @brief Returns the effective data size of the resource.
 *
 * If the resource has been modified in memory the in-memory buffer length is
 * returned; otherwise the original on-disk size is returned.
 *
 * @return Data size in bytes.
 */
size_t
ResourceItem::DataSize() const
{
	if (IsModified())
		return BufferLength();
	return fInitialSize;
}

/**
 * @brief Sets the four-byte type code of this resource.
 *
 * @param type The type code to assign.
 */
void
ResourceItem::SetType(type_code type)
{
	fType = type;
}

/**
 * @brief Returns the four-byte type code of this resource.
 *
 * @return The type code.
 */
type_code
ResourceItem::Type() const
{
	return fType;
}

/**
 * @brief Sets the numeric ID of this resource.
 *
 * @param id The resource ID to assign.
 */
void
ResourceItem::SetID(int32 id)
{
	fID = id;
}

/**
 * @brief Returns the numeric ID of this resource.
 *
 * @return The resource ID.
 */
int32
ResourceItem::ID() const
{
	return fID;
}

/**
 * @brief Sets the name string of this resource.
 *
 * @param name Null-terminated name string; may be empty.
 */
void
ResourceItem::SetName(const char *name)
{
	fName = name;
}

/**
 * @brief Returns the name string of this resource.
 *
 * @return Null-terminated name string owned by this object.
 */
const char *
ResourceItem::Name() const
{
	return fName.String();
}

/**
 * @brief Returns a pointer to the resource data buffer.
 *
 * If the data size is zero, a unique non-NULL pointer (to the item itself) is
 * returned so that the resource can still be identified by its data pointer.
 *
 * @return Pointer to the resource data, never NULL.
 */
void *
ResourceItem::Data() const
{
	// Since MallocIO may have a NULL buffer, if the data size is 0,
	// we return a pointer to ourselves in this case. This ensures, that
	// the resource item still can be uniquely identified by its data pointer.
	if (DataSize() == 0)
		return const_cast<ResourceItem*>(this);
	return const_cast<void*>(Buffer());
}

/**
 * @brief Sets the loaded state of the resource.
 *
 * @param loaded true if the resource data has been read from file.
 */
void
ResourceItem::SetLoaded(bool loaded)
{
	fIsLoaded = loaded;
}

/**
 * @brief Returns whether the resource data has been loaded into memory.
 *
 * A resource is considered loaded if its buffer is non-empty or if
 * SetLoaded(true) has been called explicitly.
 *
 * @return true if the resource data is available in memory.
 */
bool
ResourceItem::IsLoaded() const
{
	return (BufferLength() > 0 || fIsLoaded);
}

/**
 * @brief Sets the modified flag of this resource item.
 *
 * @param modified true if the in-memory data differs from the on-disk data.
 */
void
ResourceItem::SetModified(bool modified)
{
	fIsModified = modified;
}

/**
 * @brief Returns whether the resource data has been modified since last sync.
 *
 * @return true if the resource has unsaved changes.
 */
bool
ResourceItem::IsModified() const
{
	return fIsModified;
}


};	// namespace Storage
};	// namespace BPrivate
