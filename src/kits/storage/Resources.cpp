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
 *   Copyright 2001-2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 *   Copyright 2013 Haiku, Inc.
 *   All Rights Reserved.
 *   Authors: John Scipione, jscipione@gmail.com
 *            Ingo Weinhold, bonefish@cs.tu-berlin.de
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file Resources.cpp
 * @brief Implementation of BResources, the public API for reading and writing file resources.
 *
 * BResources provides a high-level interface for accessing typed, named
 * resources embedded in files (ELF, x86 resource, or PEF format). Resources
 * are loaded lazily from disk and cached in a ResourcesContainer. Modifications
 * are written back only when Sync() or WriteTo() is called.
 *
 * @see ResourceFile
 */

#include <Resources.h>

#include <new>
#include <stdio.h>
#include <stdlib.h>

#include "ResourceFile.h"
#include "ResourceItem.h"
#include "ResourcesContainer.h"


using namespace BPrivate::Storage;
using namespace std;


// debugging
//#define DBG(x) x
#define DBG(x)
#define OUT	printf


/**
 * @brief Creates an uninitialized BResources object.
 *
 * The object is not associated with any file. Call SetTo() before use.
 */
BResources::BResources()
	:
	fFile(),
	fContainer(NULL),
	fResourceFile(NULL),
	fReadOnly(false)
{
	fContainer = new(nothrow) ResourcesContainer;
}


/**
 * @brief Creates a BResources object representing the resources of the given file.
 *
 * @param file    The file whose resources are to be managed.
 * @param clobber If true, any existing resources in the file are overwritten.
 */
BResources::BResources(const BFile* file, bool clobber)
	:
	fFile(),
	fContainer(NULL),
	fResourceFile(NULL),
	fReadOnly(false)
{
	fContainer = new(nothrow) ResourcesContainer;
	SetTo(file, clobber);
}


/**
 * @brief Creates a BResources object representing the resources of the file at the given path.
 *
 * @param path    Path to the file whose resources are to be managed.
 * @param clobber If true, any existing resources in the file are overwritten.
 */
BResources::BResources(const char* path, bool clobber)
	:
	fFile(),
	fContainer(NULL),
	fResourceFile(NULL),
	fReadOnly(false)
{
	fContainer = new(nothrow) ResourcesContainer;
	SetTo(path, clobber);
}


/**
 * @brief Creates a BResources object representing the resources of the file referenced by ref.
 *
 * @param ref     entry_ref referring to the file whose resources are to be managed.
 * @param clobber If true, any existing resources in the file are overwritten.
 */
BResources::BResources(const entry_ref* ref, bool clobber)
	:
	fFile(),
	fContainer(NULL),
	fResourceFile(NULL),
	fReadOnly(false)
{
	fContainer = new(nothrow) ResourcesContainer;
	SetTo(ref, clobber);
}


/**
 * @brief Destroys the BResources object, syncing pending changes and freeing memory.
 *
 * Pending modifications are flushed to disk via Unset() before destruction.
 */
BResources::~BResources()
{
	Unset();
	delete fContainer;
}


/**
 * @brief Initializes the BResources object to represent the resources of the given file.
 *
 * Any previously associated state is reset first. If @p clobber is true existing
 * resources are replaced with an empty resource set.
 *
 * @param file    Pointer to an open BFile; must not be NULL.
 * @param clobber If true, overwrite existing resource data.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BResources::SetTo(const BFile* file, bool clobber)
{
	Unset();
	status_t error = B_OK;
	if (file) {
		error = file->InitCheck();
		if (error == B_OK) {
			fFile = *file;
			error = fFile.InitCheck();
		}
		if (error == B_OK) {
			fReadOnly = !fFile.IsWritable();
			fResourceFile = new(nothrow) ResourceFile;
			if (fResourceFile)
				error = fResourceFile->SetTo(&fFile, clobber);
			else
				error = B_NO_MEMORY;
		}
		if (error == B_OK) {
			if (fContainer)
				error = fResourceFile->InitContainer(*fContainer);
			else
				error = B_NO_MEMORY;
		}
	}
	if (error != B_OK) {
		delete fResourceFile;
		fResourceFile = NULL;
		if (fContainer)
			fContainer->MakeEmpty();
	}
	return error;
}


/**
 * @brief Initializes the BResources object to represent the resources of the file at path.
 *
 * Opens the file read-write if possible, falling back to read-only.
 *
 * @param path    Path string of the file to open.
 * @param clobber If true, overwrite existing resource data.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BResources::SetTo(const char* path, bool clobber)
{
	if (!path)
		return B_BAD_VALUE;

	// open file
	BFile file;
	status_t error = file.SetTo(path, B_READ_WRITE);
	if (error != B_OK && error != B_ENTRY_NOT_FOUND)
		error = file.SetTo(path, B_READ_ONLY);
	if (error != B_OK) {
		Unset();
		return error;
	}

	// delegate the actual work
	return SetTo(&file, clobber);
}


/**
 * @brief Initializes the BResources object to represent the resources of the file at ref.
 *
 * Opens the file read-write if possible, falling back to read-only.
 *
 * @param ref     entry_ref referring to the file to open.
 * @param clobber If true, overwrite existing resource data.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BResources::SetTo(const entry_ref* ref, bool clobber)
{
	if (!ref)
		return B_BAD_VALUE;

	// open file
	BFile file;
	status_t error = file.SetTo(ref, B_READ_WRITE);
	if (error != B_OK && error != B_ENTRY_NOT_FOUND)
		error = file.SetTo(ref, B_READ_ONLY);
	if (error != B_OK) {
		Unset();
		return error;
	}

	// delegate the actual work
	return SetTo(&file, clobber);
}


/**
 * @brief Initializes the BResources object to the file from which the given image was loaded.
 *
 * @param image   The image_id of the loaded image whose file to access.
 * @param clobber If true, overwrite existing resource data.
 * @return B_OK on success, B_NOT_SUPPORTED on non-Haiku platforms, or an error code.
 */
status_t
BResources::SetToImage(image_id image, bool clobber)
{
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
	// get an image info
	image_info info;
	status_t error = get_image_info(image, &info);
	if (error != B_OK) {
		Unset();
		return error;
	}

	// delegate the actual work
	return SetTo(info.name, clobber);
#else	// HAIKU_TARGET_PLATFORM_HAIKU
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Initializes the BResources object to the file from which the given pointer was loaded.
 *
 * Iterates all loaded images to find the one containing the supplied address.
 * If @p codeOrDataPointer is NULL, the application image is selected.
 *
 * @param codeOrDataPointer An address in the code or data segment of the desired image.
 * @param clobber           If true, overwrite existing resource data.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no matching image exists,
 *         B_NOT_SUPPORTED on non-Haiku platforms.
 */
status_t
BResources::SetToImage(const void* codeOrDataPointer, bool clobber)
{
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
	// iterate through the images and find the one in question
	addr_t address = (addr_t)codeOrDataPointer;
	image_info info;
	int32 cookie = 0;

	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		if (address == 0
			? info.type == B_APP_IMAGE
			: (((addr_t)info.text <= address
					&& address - (addr_t)info.text < (addr_t)info.text_size)
				|| ((addr_t)info.data <= address
					&& address - (addr_t)info.data < (addr_t)info.data_size))) {
			return SetTo(info.name, clobber);
		}
	}

	return B_ENTRY_NOT_FOUND;
#else	// HAIKU_TARGET_PLATFORM_HAIKU
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Resets the BResources object to an uninitialized state.
 *
 * If the resource container has been modified, Sync() is called first to
 * flush changes to disk. The internal ResourceFile and container are then
 * reset.
 */
void
BResources::Unset()
{
	if (fContainer && fContainer->IsModified())
		Sync();
	delete fResourceFile;
	fResourceFile = NULL;
	fFile.Unset();
	if (fContainer)
		fContainer->MakeEmpty();
	else
		fContainer = new(nothrow) ResourcesContainer;
	fReadOnly = false;
}


/**
 * @brief Returns the initialization status of the object.
 *
 * @return B_OK if the internal container was successfully allocated, B_NO_MEMORY otherwise.
 */
status_t
BResources::InitCheck() const
{
	return (fContainer ? B_OK : B_NO_MEMORY);
}


/**
 * @brief Returns a const reference to the internal BFile object.
 *
 * @return The BFile currently associated with this object.
 */
const BFile&
BResources::File() const
{
	return fFile;
}


/**
 * @brief Loads and returns the resource identified by type and numeric ID.
 *
 * The resource data is read from disk on first access and cached. The
 * returned pointer is valid until the BResources object is modified or
 * destroyed.
 *
 * @param type  The four-byte type code of the resource.
 * @param id    The numeric resource ID.
 * @param _size Output parameter filled with the resource size in bytes.
 * @return Pointer to the resource data, or NULL on failure.
 */
const void*
BResources::LoadResource(type_code type, int32 id, size_t* _size)
{
	// find the resource
	status_t error = InitCheck();
	ResourceItem* resource = NULL;
	if (error == B_OK) {
		resource = fContainer->ResourceAt(fContainer->IndexOf(type, id));
		if (!resource)
			error = B_ENTRY_NOT_FOUND;
	}
	// load it, if necessary
	if (error == B_OK && !resource->IsLoaded() && fResourceFile)
		error = fResourceFile->ReadResource(*resource);
	// return the result
	const void *result = NULL;
	if (error == B_OK) {
		result = resource->Data();
		if (_size)
			*_size = resource->DataSize();
	}
	return result;
}


/**
 * @brief Loads and returns the resource identified by type and name.
 *
 * The resource data is read from disk on first access and cached.
 *
 * @param type  The four-byte type code of the resource.
 * @param name  The resource name string.
 * @param _size Output parameter filled with the resource size in bytes.
 * @return Pointer to the resource data, or NULL on failure.
 */
const void*
BResources::LoadResource(type_code type, const char* name, size_t* _size)
{
	// find the resource
	status_t error = InitCheck();
	ResourceItem* resource = NULL;
	if (error == B_OK) {
		resource = fContainer->ResourceAt(fContainer->IndexOf(type, name));
		if (!resource)
			error = B_ENTRY_NOT_FOUND;
	}
	// load it, if necessary
	if (error == B_OK && !resource->IsLoaded() && fResourceFile)
		error = fResourceFile->ReadResource(*resource);
	// return the result
	const void* result = NULL;
	if (error == B_OK) {
		result = resource->Data();
		if (_size)
			*_size = resource->DataSize();
	}
	return result;
}


/**
 * @brief Loads all resources of the specified type into memory.
 *
 * If @p type is 0 all resources are loaded regardless of type.
 *
 * @param type The type code to preload, or 0 to preload all types.
 * @return B_OK if all requested resources loaded successfully, or a negative
 *         count of failures.
 */
status_t
BResources::PreloadResourceType(type_code type)
{
	status_t error = InitCheck();
	if (error == B_OK && fResourceFile) {
		if (type == 0)
			error = fResourceFile->ReadResources(*fContainer);
		else {
			int32 count = fContainer->CountResources();
			int32 errorCount = 0;
			for (int32 i = 0; i < count; i++) {
				ResourceItem *resource = fContainer->ResourceAt(i);
				if (resource->Type() == type) {
					if (fResourceFile->ReadResource(*resource) != B_OK)
						errorCount++;
				}
			}
			error = -errorCount;
		}
	}
	return error;
}


/**
 * @brief Writes all pending resource changes back to the file.
 *
 * All resources are first loaded from disk to ensure a complete in-memory
 * copy before the updated resource set is written back.
 *
 * @return B_OK on success, B_NOT_ALLOWED if the file is read-only, or an error code.
 */
status_t
BResources::Sync()
{
	status_t error = InitCheck();
	if (error == B_OK)
		error = fFile.InitCheck();
	if (error == B_OK) {
		if (fReadOnly)
			error = B_NOT_ALLOWED;
		else if (!fResourceFile)
			error = B_FILE_ERROR;
	}
	if (error == B_OK)
		error = fResourceFile->ReadResources(*fContainer);
	if (error == B_OK)
		error = fResourceFile->WriteResources(*fContainer);
	return error;
}


/**
 * @brief Merges the resources from the given file into this object's file.
 *
 * All resources from @p fromFile are loaded and added to the current
 * resource container.
 *
 * @param fromFile Pointer to the source file; must not be NULL.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BResources::MergeFrom(BFile* fromFile)
{
	status_t error = (fromFile ? B_OK : B_BAD_VALUE);
	if (error == B_OK)
		error = InitCheck();
	if (error == B_OK) {
		ResourceFile resourceFile;
		error = resourceFile.SetTo(fromFile);
		ResourcesContainer container;
		if (error == B_OK)
			error = resourceFile.InitContainer(container);
		if (error == B_OK)
			error = resourceFile.ReadResources(container);
		if (error == B_OK)
			fContainer->AssimilateResources(container);
	}
	return error;
}


/**
 * @brief Writes the current resource set to a different file.
 *
 * All resources are fully loaded before being written to @p file. The
 * object's current file is not altered.
 *
 * @param file Pointer to the destination file; must not be NULL.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BResources::WriteTo(BFile* file)
{
	status_t error = (file ? B_OK : B_BAD_VALUE);
	if (error == B_OK)
		error = InitCheck();
	// make sure, that all resources are loaded
	if (error == B_OK && fResourceFile) {
		error = fResourceFile->ReadResources(*fContainer);
		fResourceFile->Unset();
	}
	// set the new file, but keep the old container
	if (error == B_OK) {
		ResourcesContainer *container = fContainer;
		fContainer = new(nothrow) ResourcesContainer;
		if (fContainer) {
			error = SetTo(file, false);
			delete fContainer;
		} else
			error = B_NO_MEMORY;
		fContainer = container;
	}
	// write the resources
	if (error == B_OK && fResourceFile)
		error = fResourceFile->WriteResources(*fContainer);
	return error;
}


/**
 * @brief Adds a new resource to the file.
 *
 * The resource is stored in the in-memory container and will be written to
 * disk on the next Sync() call.
 *
 * @param type   The four-byte type code.
 * @param id     The numeric resource ID.
 * @param data   Pointer to the resource data; must not be NULL.
 * @param length Size of the resource data in bytes.
 * @param name   Optional resource name string.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BResources::AddResource(type_code type, int32 id, const void* data,
						size_t length, const char* name)
{
	status_t error = (data ? B_OK : B_BAD_VALUE);
	if (error == B_OK)
		error = InitCheck();
	if (error == B_OK)
		error = (fReadOnly ? B_NOT_ALLOWED : B_OK);
	if (error == B_OK) {
		ResourceItem* item = new(nothrow) ResourceItem;
		if (!item)
			error = B_NO_MEMORY;
		if (error == B_OK) {
			item->SetIdentity(type, id, name);
			ssize_t written = item->WriteAt(0, data, length);
			if (written < 0)
				error = written;
			else if (written != (ssize_t)length)
				error = B_ERROR;
		}
		if (error == B_OK) {
			if (!fContainer->AddResource(item))
				error = B_NO_MEMORY;
		}
		if (error != B_OK)
			delete item;
	}
	return error;
}


/**
 * @brief Returns true if the file contains a resource with the given type and numeric ID.
 *
 * @param type The resource type code.
 * @param id   The numeric resource ID.
 * @return true if the resource exists, false otherwise.
 */
bool
BResources::HasResource(type_code type, int32 id)
{
	return (InitCheck() == B_OK && fContainer->IndexOf(type, id) >= 0);
}


/**
 * @brief Returns true if the file contains a resource with the given type and name.
 *
 * @param type The resource type code.
 * @param name The resource name string.
 * @return true if the resource exists, false otherwise.
 */
bool
BResources::HasResource(type_code type, const char* name)
{
	return (InitCheck() == B_OK && fContainer->IndexOf(type, name) >= 0);
}


/**
 * @brief Returns information about the resource at the given container index.
 *
 * @param byIndex     Zero-based index into the resource list.
 * @param typeFound   Output: type code of the resource.
 * @param idFound     Output: numeric ID of the resource.
 * @param nameFound   Output: name of the resource.
 * @param lengthFound Output: data size of the resource in bytes.
 * @return true if a resource exists at @p byIndex, false otherwise.
 */
bool
BResources::GetResourceInfo(int32 byIndex, type_code* typeFound,
	int32* idFound, const char** nameFound, size_t* lengthFound)
{
	ResourceItem* item = NULL;
	if (InitCheck() == B_OK)
		item = fContainer->ResourceAt(byIndex);
	if (item) {
		if (typeFound)
			*typeFound = item->Type();
		if (idFound)
			*idFound = item->ID();
		if (nameFound)
			*nameFound = item->Name();
		if (lengthFound)
			*lengthFound = item->DataSize();
	}
	return item;
}


/**
 * @brief Returns information about the Nth resource of the given type.
 *
 * @param byType      The type code to search for.
 * @param andIndex    Zero-based index among resources of the specified type.
 * @param idFound     Output: numeric ID of the found resource.
 * @param nameFound   Output: name of the found resource.
 * @param lengthFound Output: data size of the found resource in bytes.
 * @return true if a matching resource exists, false otherwise.
 */
bool
BResources::GetResourceInfo(type_code byType, int32 andIndex, int32* idFound,
	const char** nameFound, size_t* lengthFound)
{
	ResourceItem* item = NULL;
	if (InitCheck() == B_OK) {
		item = fContainer->ResourceAt(fContainer->IndexOfType(byType,
															  andIndex));
	}
	if (item) {
		if (idFound)
			*idFound = item->ID();
		if (nameFound)
			*nameFound = item->Name();
		if (lengthFound)
			*lengthFound = item->DataSize();
	}
	return item;
}


/**
 * @brief Returns information about the resource identified by type and numeric ID.
 *
 * @param byType      The resource type code.
 * @param andID       The numeric resource ID.
 * @param nameFound   Output: name of the resource.
 * @param lengthFound Output: data size of the resource in bytes.
 * @return true if the resource exists, false otherwise.
 */
bool
BResources::GetResourceInfo(type_code byType, int32 andID,
	const char** nameFound, size_t* lengthFound)
{
	ResourceItem* item = NULL;
	if (InitCheck() == B_OK)
		item = fContainer->ResourceAt(fContainer->IndexOf(byType, andID));
	if (item) {
		if (nameFound)
			*nameFound = item->Name();
		if (lengthFound)
			*lengthFound = item->DataSize();
	}
	return item;
}


/**
 * @brief Returns information about the resource identified by type and name.
 *
 * @param byType      The resource type code.
 * @param andName     The resource name string.
 * @param idFound     Output: numeric ID of the resource.
 * @param lengthFound Output: data size of the resource in bytes.
 * @return true if the resource exists, false otherwise.
 */
bool
BResources::GetResourceInfo(type_code byType, const char* andName,
	int32* idFound, size_t* lengthFound)
{
	ResourceItem* item = NULL;
	if (InitCheck() == B_OK)
		item = fContainer->ResourceAt(fContainer->IndexOf(byType, andName));
	if (item) {
		if (idFound)
			*idFound = item->ID();
		if (lengthFound)
			*lengthFound = item->DataSize();
	}
	return item;
}


/**
 * @brief Returns information about the resource identified by its data pointer.
 *
 * @param byPointer   The data pointer previously returned by LoadResource().
 * @param typeFound   Output: type code of the resource.
 * @param idFound     Output: numeric ID of the resource.
 * @param lengthFound Output: data size of the resource in bytes.
 * @param nameFound   Output: name of the resource.
 * @return true if a resource with the given data pointer exists, false otherwise.
 */
bool
BResources::GetResourceInfo(const void* byPointer, type_code* typeFound,
	int32* idFound, size_t* lengthFound, const char** nameFound)
{
	ResourceItem* item = NULL;
	if (InitCheck() == B_OK)
		item = fContainer->ResourceAt(fContainer->IndexOf(byPointer));
	if (item) {
		if (typeFound)
			*typeFound = item->Type();
		if (idFound)
			*idFound = item->ID();
		if (nameFound)
			*nameFound = item->Name();
		if (lengthFound)
			*lengthFound = item->DataSize();
	}
	return item;
}


/**
 * @brief Removes the resource whose data pointer matches the given pointer.
 *
 * @param resource The data pointer of the resource to remove.
 * @return B_OK on success, B_BAD_VALUE if not found or @p resource is NULL,
 *         B_NOT_ALLOWED if read-only.
 */
status_t
BResources::RemoveResource(const void* resource)
{
	status_t error = (resource ? B_OK : B_BAD_VALUE);
	if (error == B_OK)
		error = InitCheck();
	if (error == B_OK)
		error = (fReadOnly ? B_NOT_ALLOWED : B_OK);
	if (error == B_OK) {
		ResourceItem* item
			= fContainer->RemoveResource(fContainer->IndexOf(resource));
		if (item)
			delete item;
		else
			error = B_BAD_VALUE;
	}
	return error;
}


/**
 * @brief Removes the resource identified by type and numeric ID.
 *
 * @param type The resource type code.
 * @param id   The numeric resource ID.
 * @return B_OK on success, B_BAD_VALUE if not found, B_NOT_ALLOWED if read-only.
 */
status_t
BResources::RemoveResource(type_code type, int32 id)
{
	status_t error = InitCheck();
	if (error == B_OK)
		error = (fReadOnly ? B_NOT_ALLOWED : B_OK);
	if (error == B_OK) {
		ResourceItem* item
			= fContainer->RemoveResource(fContainer->IndexOf(type, id));
		if (item)
			delete item;
		else
			error = B_BAD_VALUE;
	}
	return error;
}


// #pragma mark - deprecated methods


/**
 * @brief Writes data into an existing resource at the given offset (deprecated).
 *
 * Use AddResource() instead. This method overwrites a portion of the resource
 * data and loads the resource from disk if not already in memory.
 *
 * @param type   The resource type code.
 * @param id     The numeric resource ID.
 * @param data   Pointer to the data to write.
 * @param offset Byte offset within the resource at which to write.
 * @param length Number of bytes to write.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BResources::WriteResource(type_code type, int32 id, const void* data,
	off_t offset, size_t length)
{
	status_t error = (data && offset >= 0 ? B_OK : B_BAD_VALUE);
	if (error == B_OK)
		error = InitCheck();
	if (error == B_OK)
		error = (fReadOnly ? B_NOT_ALLOWED : B_OK);

	if (error != B_OK)
		return error;

	ResourceItem *item = fContainer->ResourceAt(fContainer->IndexOf(type, id));
	if (!item)
		return B_BAD_VALUE;

	if (fResourceFile) {
		error = fResourceFile->ReadResource(*item);
		if (error != B_OK)
			return error;
	}

	ssize_t written = item->WriteAt(offset, data, length);

	if (written < 0)
		error = written;
	else if (written != (ssize_t)length)
		error = B_ERROR;

	return error;
}


/**
 * @brief Reads data from an existing resource at the given offset (deprecated).
 *
 * Use LoadResource() instead. The resource is loaded from disk if not already
 * in memory.
 *
 * @param type   The resource type code.
 * @param id     The numeric resource ID.
 * @param data   Buffer to receive the data.
 * @param offset Byte offset within the resource from which to read.
 * @param length Number of bytes to read.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BResources::ReadResource(type_code type, int32 id, void* data, off_t offset,
	size_t length)
{
	status_t error = (data && offset >= 0 ? B_OK : B_BAD_VALUE);
	if (error == B_OK)
		error = InitCheck();
	ResourceItem* item = NULL;
	if (error == B_OK) {
		item = fContainer->ResourceAt(fContainer->IndexOf(type, id));
		if (!item)
			error = B_BAD_VALUE;
	}
	if (error == B_OK && fResourceFile)
		error = fResourceFile->ReadResource(*item);
	if (error == B_OK) {
		if (item) {
			ssize_t read = item->ReadAt(offset, data, length);
			if (read < 0)
				error = read;
		} else
			error = B_BAD_VALUE;
	}
	return error;
}


/**
 * @brief Finds a resource by type and numeric ID and returns a malloc'd copy (deprecated).
 *
 * Use LoadResource() instead. The caller is responsible for free()ing the
 * returned pointer.
 *
 * @param type        The resource type code.
 * @param id          The numeric resource ID.
 * @param lengthFound Output parameter filled with the resource size.
 * @return Pointer to a malloc'd copy of the resource data, or NULL on failure.
 */
void*
BResources::FindResource(type_code type, int32 id, size_t* lengthFound)
{
	void* result = NULL;
	size_t size = 0;
	const void* data = LoadResource(type, id, &size);
	if (data != NULL) {
		if ((result = malloc(size)))
			memcpy(result, data, size);
	}
	if (lengthFound)
		*lengthFound = size;
	return result;
}


/**
 * @brief Finds a resource by type and name and returns a malloc'd copy (deprecated).
 *
 * Use LoadResource() instead. The caller is responsible for free()ing the
 * returned pointer.
 *
 * @param type        The resource type code.
 * @param name        The resource name string.
 * @param lengthFound Output parameter filled with the resource size.
 * @return Pointer to a malloc'd copy of the resource data, or NULL on failure.
 */
void*
BResources::FindResource(type_code type, const char* name, size_t* lengthFound)
{
	void* result = NULL;
	size_t size = 0;
	const void *data = LoadResource(type, name, &size);
	if (data != NULL) {
		if ((result = malloc(size)))
			memcpy(result, data, size);
	}
	if (lengthFound)
		*lengthFound = size;
	return result;
}


// FBC
void BResources::_ReservedResources1() {}
void BResources::_ReservedResources2() {}
void BResources::_ReservedResources3() {}
void BResources::_ReservedResources4() {}
void BResources::_ReservedResources5() {}
void BResources::_ReservedResources6() {}
void BResources::_ReservedResources7() {}
void BResources::_ReservedResources8() {}
