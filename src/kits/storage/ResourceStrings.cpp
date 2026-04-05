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
 * @file ResourceStrings.cpp
 * @brief Implementation of BResourceStrings for fast string-resource lookup.
 *
 * BResourceStrings provides a hash-table-based interface for looking up
 * string resources (type 'CSTR') from a resource file by numeric ID. Strings
 * are loaded once from the file and cached internally. The class is
 * thread-safe via an internal BLocker.
 *
 * @see BResources
 */

#include <ResourceStrings.h>

#include <new>
#include <stdlib.h>
#include <string.h>

#include <Entry.h>
#include <File.h>
#include <Resources.h>
#include <String.h>

#include <AppMisc.h>

using namespace std;


/**
 * @brief Creates a BResourceStrings object initialized to the application's string resources.
 *
 * Calls SetStringFile(NULL) which resolves the running application's resource file.
 */
BResourceStrings::BResourceStrings()
				: _string_lock(),
				  _init_error(),
				  fFileRef(),
				  fResources(NULL),
				  fHashTable(NULL),
				  fHashTableSize(0),
				  fStringCount(0)
{
	SetStringFile(NULL);
}

/**
 * @brief Creates a BResourceStrings object initialized to a specific resource file.
 *
 * @param ref The entry_ref of the resource file to load strings from.
 */
BResourceStrings::BResourceStrings(const entry_ref &ref)
				: _string_lock(),
				  _init_error(),
				  fFileRef(),
				  fResources(NULL),
				  fHashTable(NULL),
				  fHashTableSize(0),
				  fStringCount(0)
{
	SetStringFile(&ref);
}

/**
 * @brief Destroys the BResourceStrings object and frees all cached strings.
 *
 * Acquires the internal lock before cleaning up to prevent concurrent access.
 */
BResourceStrings::~BResourceStrings()
{
	_string_lock.Lock();
	_Cleanup();
}

/**
 * @brief Returns the initialization status of this object.
 *
 * @return B_OK if the object is properly initialized, or an error code otherwise.
 */
status_t
BResourceStrings::InitCheck()
{
	return _init_error;
}

/**
 * @brief Finds a string by ID and returns a newly allocated copy.
 *
 * The caller is responsible for deleting the returned BString object.
 *
 * @param id The numeric ID of the requested string.
 * @return A new BString containing the requested string, or NULL if not found.
 */
BString *
BResourceStrings::NewString(int32 id)
{
//	_string_lock.Lock();
	BString *result = NULL;
	if (const char *str = FindString(id))
		result = new(nothrow) BString(str);
//	_string_lock.Unlock();
	return result;
}

/**
 * @brief Finds and returns the string identified by the supplied ID.
 *
 * The returned pointer belongs to this object and remains valid until
 * the object is destroyed or SetStringFile() is called again.
 *
 * @param id The numeric ID of the requested string.
 * @return The string, or NULL if the object is not initialized or the ID is not found.
 */
const char *
BResourceStrings::FindString(int32 id)
{
	_string_lock.Lock();
	const char *result = NULL;
	if (InitCheck() == B_OK) {
		if (_string_id_hash *entry = _FindString(id))
			result = entry->data;
	}
	_string_lock.Unlock();
	return result;
}

/**
 * @brief Re-initializes the object to the string resources of the given file.
 *
 * If @p ref is NULL the object is initialized to the running application's
 * resource file. The previous state is cleaned up before re-initialization.
 *
 * @param ref entry_ref of the resource file, or NULL for the application file.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BResourceStrings::SetStringFile(const entry_ref *ref)
{
	_string_lock.Lock();
	// cleanup
	_Cleanup();
	// get the ref (if NULL, take the application)
	status_t error = B_OK;
	entry_ref fileRef;
	if (ref) {
		fileRef = *ref;
		fFileRef = *ref;
	} else
		error = BPrivate::get_app_ref(&fileRef);
	// get the BResources
	if (error == B_OK) {
		BFile file(&fileRef, B_READ_ONLY);
		error = file.InitCheck();
		if (error == B_OK) {
			fResources = new(nothrow) BResources;
			if (fResources)
				error = fResources->SetTo(&file);
			else
				error = B_NO_MEMORY;
		}
	}
	// read the strings
	if (error == B_OK) {
		// count them first
		fStringCount = 0;
		int32 id;
		const char *name;
		size_t length;
		while (fResources->GetResourceInfo(RESOURCE_TYPE, fStringCount, &id,
										   &name, &length)) {
			fStringCount++;
		}
		// allocate a hash table with a nice size
		// I don't have a heuristic at hand, so let's simply take the count.
		error = _Rehash(fStringCount);
		// load the resources
		for (int32 i = 0; error == B_OK && i < fStringCount; i++) {
			if (!fResources->GetResourceInfo(RESOURCE_TYPE, i, &id, &name,
											 &length)) {
				error = B_ERROR;
			}
			if (error == B_OK) {
				const void *data
					= fResources->LoadResource(RESOURCE_TYPE, id, &length);
				if (data) {
					_string_id_hash *entry = NULL;
					if (length == 0)
						entry = _AddString(NULL, id, false);
					else
						entry = _AddString((char*)data, id, false);
					if (!entry)
						error = B_ERROR;
				} else
					error = B_ERROR;
			}
		}
	}
	// if something went wrong, cleanup the mess
	if (error != B_OK)
		_Cleanup();
	_init_error = error;
	_string_lock.Unlock();
	return error;
}

/**
 * @brief Returns an entry_ref referring to the currently used resource file.
 *
 * @param outRef Output parameter filled with the entry_ref on success.
 * @return B_OK on success, B_BAD_VALUE if outRef is NULL, or another error code.
 */
status_t
BResourceStrings::GetStringFile(entry_ref *outRef)
{
	status_t error = (outRef ? B_OK : B_BAD_VALUE);
	if (error == B_OK)
		error = InitCheck();
	if (error == B_OK) {
		if (fFileRef == entry_ref())
			error = B_ENTRY_NOT_FOUND;
		else
			*outRef = fFileRef;
	}
	return error;
}


/**
 * @brief Frees all resources and resets all member variables to safe defaults.
 *
 * Empties the hash table, deletes the BResources object, and clears the file
 * reference and string count.
 */
void
BResourceStrings::_Cleanup()
{
//	_string_lock.Lock();
	_MakeEmpty();
	delete[] fHashTable;
	fHashTable = NULL;
	delete fResources;
	fResources = NULL;
	fFileRef = entry_ref();
	fHashTableSize = 0;
	fStringCount = 0;
	_init_error = B_OK;
//	_string_lock.Unlock();
}

/**
 * @brief Removes and deletes all entries from the id-to-string hash table.
 *
 * After this call the hash table buckets remain allocated but all chains
 * are cleared and the string count is reset to zero.
 */
void
BResourceStrings::_MakeEmpty()
{
	if (fHashTable) {
		for (int32 i = 0; i < fHashTableSize; i++) {
			while (_string_id_hash *entry = fHashTable[i]) {
				fHashTable[i] = entry->next;
				delete entry;
			}
		}
		fStringCount = 0;
	}
}

/**
 * @brief Resizes the id-to-string hash table to the given size.
 *
 * Existing entries are rehashed into the new table. If @p newSize equals the
 * current size, no action is taken.
 *
 * @param newSize The desired number of hash buckets.
 * @return B_OK on success, B_NO_MEMORY if allocation fails.
 */
status_t
BResourceStrings::_Rehash(int32 newSize)
{
	status_t error = B_OK;
	if (newSize > 0 && newSize != fHashTableSize) {
		// alloc a new table and fill it with NULL
		_string_id_hash **newHashTable
			= new(nothrow) _string_id_hash*[newSize];
		if (newHashTable) {
			memset(newHashTable, 0, sizeof(_string_id_hash*) * newSize);
			// move the entries to the new table
			if (fHashTable && fHashTableSize > 0 && fStringCount > 0) {
				for (int32 i = 0; i < fHashTableSize; i++) {
					while (_string_id_hash *entry = fHashTable[i]) {
						fHashTable[i] = entry->next;
						int32 newPos = entry->id % newSize;
						entry->next = newHashTable[newPos];
						newHashTable[newPos] = entry;
					}
				}
			}
			// set the new table
			delete[] fHashTable;
			fHashTable = newHashTable;
			fHashTableSize = newSize;
		} else
			error = B_NO_MEMORY;
	}
	return error;
}

/**
 * @brief Adds a string entry to the hash table, replacing any existing entry with the same ID.
 *
 * @param str         The string data (may be NULL for an empty string resource).
 * @param id          The numeric string ID.
 * @param wasMalloced If true, the entry takes ownership of @p str and free()s it on destruction.
 * @return Pointer to the new hash table entry, or NULL on allocation failure.
 */
BResourceStrings::_string_id_hash *
BResourceStrings::_AddString(char *str, int32 id, bool wasMalloced)
{
	_string_id_hash *entry = NULL;
	if (fHashTable && fHashTableSize > 0)
		entry = new(nothrow) _string_id_hash;
	if (entry) {
		entry->assign_string(str, false);
		entry->id = id;
		entry->data_alloced = wasMalloced;
		int32 pos = id % fHashTableSize;
		entry->next = fHashTable[pos];
		fHashTable[pos] = entry;
	}
	return entry;
}

/**
 * @brief Looks up a hash table entry by numeric string ID.
 *
 * @param id The string ID to find.
 * @return Pointer to the matching _string_id_hash entry, or NULL if not found.
 */
BResourceStrings::_string_id_hash *
BResourceStrings::_FindString(int32 id)
{
	_string_id_hash *entry = NULL;
	if (fHashTable && fHashTableSize > 0) {
		int32 pos = id % fHashTableSize;
		entry = fHashTable[pos];
		while (entry != NULL && entry->id != id)
			entry = entry->next;
	}
	return entry;
}


// FBC
status_t BResourceStrings::_Reserved_ResourceStrings_0(void *) { return 0; }
status_t BResourceStrings::_Reserved_ResourceStrings_1(void *) { return 0; }
status_t BResourceStrings::_Reserved_ResourceStrings_2(void *) { return 0; }
status_t BResourceStrings::_Reserved_ResourceStrings_3(void *) { return 0; }
status_t BResourceStrings::_Reserved_ResourceStrings_4(void *) { return 0; }
status_t BResourceStrings::_Reserved_ResourceStrings_5(void *) { return 0; }


// _string_id_hash

/**
 * @brief Constructs an uninitialized hash table entry.
 *
 * All fields are set to NULL/zero defaults.
 */
BResourceStrings::_string_id_hash::_string_id_hash()
	: next(NULL),
	  id(0),
	  data(NULL),
	  data_alloced(false)
{
}

/**
 * @brief Destroys the hash table entry, freeing the string if it was malloc'd.
 *
 * The string is only freed if data_alloced is true.
 */
BResourceStrings::_string_id_hash::~_string_id_hash()
{
	if (data_alloced)
		free(data);
}

/**
 * @brief Sets the string pointer for this hash table entry.
 *
 * If @p makeCopy is true the string is duplicated via strdup() and the copy
 * is freed on destruction. If false, the entry merely points to the supplied
 * string without taking ownership.
 *
 * @param str      The string to assign (may be NULL).
 * @param makeCopy If true, the string is copied; if false, the pointer is stored directly.
 */
void
BResourceStrings::_string_id_hash::assign_string(const char *str,
												 bool makeCopy)
{
	if (data_alloced)
		free(data);
	data = NULL;
	data_alloced = false;
	if (str) {
		if (makeCopy) {
			data = strdup(str);
			data_alloced = true;
		} else
			data = const_cast<char*>(str);
	}
}
