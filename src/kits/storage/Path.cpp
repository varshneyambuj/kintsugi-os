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
 *   Copyright 2002-2012, Haiku Inc.
 *   Authors:
 *       Tyler Dauwalder
 *       Axel Dörfler, axeld@pinc-software.de
 *       Ingo Weinhold, bonefish@users.sf.net
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file Path.cpp
 * @brief Implementation of the BPath class for filesystem path manipulation.
 *
 * BPath provides a managed representation of a filesystem path, supporting
 * construction from strings, entry references, BEntry objects, and BDirectory
 * objects. It handles path normalization, flattening (BFlattenable interface),
 * and common path operations such as appending components and retrieving the
 * leaf or parent portions of a path.
 *
 * @see BEntry, BDirectory, BFlattenable
 */

#include <Path.h>

#include <new>

#include <Directory.h>
#include <Entry.h>
#include <StorageDefs.h>
#include <String.h>

#include <syscalls.h>

#include "storage_support.h"


using namespace std;


/**
 * @brief Creates an uninitialized BPath object.
 *
 * The object's InitCheck() will return B_NO_INIT until it is explicitly
 * initialized via SetTo() or an assignment operator.
 */
BPath::BPath()
	:
	fName(NULL),
	fCStatus(B_NO_INIT)
{
}


/**
 * @brief Creates a copy of the given BPath object.
 *
 * @param path The BPath object to copy.
 */
BPath::BPath(const BPath& path)
	:
	fName(NULL),
	fCStatus(B_NO_INIT)
{
	*this = path;
}


/**
 * @brief Creates a BPath object initialized to the filesystem entry
 *        specified by the passed in entry_ref struct.
 *
 * @param ref Pointer to the entry_ref identifying the filesystem entry.
 */
BPath::BPath(const entry_ref* ref)
	:
	fName(NULL),
	fCStatus(B_NO_INIT)
{
	SetTo(ref);
}


/**
 * @brief Creates a BPath object initialized to the filesystem entry
 *        specified by the passed in BEntry object.
 *
 * @param entry Pointer to the BEntry identifying the filesystem entry.
 */
BPath::BPath(const BEntry* entry)
	:
	fName(NULL),
	fCStatus(B_NO_INIT)
{
	SetTo(entry);
}


/**
 * @brief Creates a BPath object initialized to the specified path or
 *        path and filename combination.
 *
 * @param dir   The base directory path string.
 * @param leaf  Optional leaf name to append to the base path.
 * @param normalize If true, forces normalization of the resulting path.
 */
BPath::BPath(const char* dir, const char* leaf, bool normalize)
	:
	fName(NULL),
	fCStatus(B_NO_INIT)
{
	SetTo(dir, leaf, normalize);
}


/**
 * @brief Creates a BPath object initialized to the specified directory
 *        and filename combination.
 *
 * @param dir       Pointer to the BDirectory representing the base directory.
 * @param leaf      Optional leaf name to append to the directory path.
 * @param normalize If true, forces normalization of the resulting path.
 */
BPath::BPath(const BDirectory* dir, const char* leaf, bool normalize)
	:
	fName(NULL),
	fCStatus(B_NO_INIT)
{
	SetTo(dir, leaf, normalize);
}


/**
 * @brief Destroys the BPath object and frees any of its associated resources.
 */
BPath::~BPath()
{
	Unset();
}


/**
 * @brief Checks whether or not the object was properly initialized.
 *
 * @return B_OK if the object is properly initialized, B_NO_INIT otherwise.
 */
status_t
BPath::InitCheck() const
{
	return fCStatus;
}


/**
 * @brief Reinitializes the object to the filesystem entry specified by the
 *        passed in entry_ref struct.
 *
 * @param ref Pointer to the entry_ref identifying the filesystem entry.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPath::SetTo(const entry_ref* ref)
{
	Unset();
	if (!ref)
		return fCStatus = B_BAD_VALUE;

	char path[B_PATH_NAME_LENGTH];
	fCStatus = _kern_entry_ref_to_path(ref->device, ref->directory,
		ref->name, path, sizeof(path));
	if (fCStatus != B_OK)
		return fCStatus;

	fCStatus = _SetPath(path);
		// the path is already normalized
	return fCStatus;
}


/**
 * @brief Reinitializes the object to the specified filesystem entry.
 *
 * @param entry Pointer to the BEntry identifying the filesystem entry.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPath::SetTo(const BEntry* entry)
{
	Unset();
	if (entry == NULL)
		return B_BAD_VALUE;

	entry_ref ref;
	fCStatus = entry->GetRef(&ref);
	if (fCStatus == B_OK)
		fCStatus = SetTo(&ref);

	return fCStatus;
}


/**
 * @brief Reinitializes the object to the passed in path or path and
 *        leaf combination.
 *
 * Relative paths are always normalized. If \a leaf is an absolute path,
 * B_BAD_VALUE is returned.
 *
 * @param path      The base path string.
 * @param leaf      Optional leaf name to append; must be a relative path.
 * @param normalize If true, forces normalization even for absolute paths.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPath::SetTo(const char* path, const char* leaf, bool normalize)
{
	status_t error = (path ? B_OK : B_BAD_VALUE);
	if (error == B_OK && leaf && BPrivate::Storage::is_absolute_path(leaf))
		error = B_BAD_VALUE;
	char newPath[B_PATH_NAME_LENGTH];
	if (error == B_OK) {
		// we always normalize relative paths
		normalize |= !BPrivate::Storage::is_absolute_path(path);
		// build a new path from path and leaf
		// copy path first
		uint32 pathLen = strlen(path);
		if (pathLen >= sizeof(newPath))
			error = B_NAME_TOO_LONG;
		if (error == B_OK)
			strcpy(newPath, path);
		// append leaf, if supplied
		if (error == B_OK && leaf) {
			bool needsSeparator = (pathLen > 0 && path[pathLen - 1] != '/');
			uint32 wholeLen = pathLen + (needsSeparator ? 1 : 0)
							  + strlen(leaf);
			if (wholeLen >= sizeof(newPath))
				error = B_NAME_TOO_LONG;
			if (error == B_OK) {
				if (needsSeparator) {
					newPath[pathLen] = '/';
					pathLen++;
				}
				strcpy(newPath + pathLen, leaf);
			}
		}
		// check, if necessary to normalize
		if (error == B_OK && !normalize)
			normalize = _MustNormalize(newPath, &error);

		// normalize the path, if necessary, otherwise just set it
		if (error == B_OK) {
			if (normalize) {
				// create a BEntry and initialize us with this entry
				BEntry entry;
				error = entry.SetTo(newPath, false);
				if (error == B_OK)
					return SetTo(&entry);
			} else
				error = _SetPath(newPath);
		}
	}
	// cleanup, if something went wrong
	if (error != B_OK)
		Unset();
	fCStatus = error;
	return error;
}


/**
 * @brief Reinitializes the object to the passed in dir and relative path
 *        combination.
 *
 * @param dir       Pointer to the BDirectory representing the base directory.
 * @param path      Optional relative path to append to the directory path.
 * @param normalize If true, forces normalization of the resulting path.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPath::SetTo(const BDirectory* dir, const char* path, bool normalize)
{
	status_t error = (dir && dir->InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	// get the path of the BDirectory
	BEntry entry;
	if (error == B_OK)
		error = dir->GetEntry(&entry);
	BPath dirPath;
	if (error == B_OK)
		error = dirPath.SetTo(&entry);
	// let the other version do the work
	if (error == B_OK)
		error = SetTo(dirPath.Path(), path, normalize);
	if (error != B_OK)
		Unset();
	fCStatus = error;
	return error;
}


/**
 * @brief Returns the object to an uninitialized state.
 *
 * Frees the internal path string and resets the initialization status
 * to B_NO_INIT.
 */
void
BPath::Unset()
{
	_SetPath(NULL);
	fCStatus = B_NO_INIT;
}


/**
 * @brief Appends the passed in relative path to the end of the current path.
 *
 * @param path      The relative path component to append.
 * @param normalize If true, forces normalization of the resulting path.
 * @return B_OK on success, B_BAD_VALUE if the object is not initialized,
 *         or another error code on failure.
 */
status_t
BPath::Append(const char* path, bool normalize)
{
	status_t error = (InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	if (error == B_OK)
		error = SetTo(Path(), path, normalize);
	if (error != B_OK)
		Unset();
	fCStatus = error;
	return error;
}


/**
 * @brief Gets the entire path of the object as a C string.
 *
 * @return A pointer to the null-terminated path string, or NULL if the
 *         object is not initialized.
 */
const char*
BPath::Path() const
{
	return fName;
}


/**
 * @brief Gets the leaf portion of the path (the final component).
 *
 * @return A pointer to the null-terminated leaf name within the internal
 *         path string, or NULL if the object is not initialized.
 */
const char*
BPath::Leaf() const
{
	if (InitCheck() != B_OK)
		return NULL;

	const char* result = fName + strlen(fName);
	// There should be no need for the second condition, since we deal
	// with absolute paths only and those contain at least one '/'.
	// However, it doesn't harm.
	while (*result != '/' && result > fName)
		result--;
	result++;

	return result;
}


/**
 * @brief Initializes \a path with the parent directory of this BPath object.
 *
 * @param path Pointer to the BPath object that will receive the parent path.
 * @return B_OK on success, B_BAD_VALUE if \a path is NULL, B_ENTRY_NOT_FOUND
 *         if the current path is the root "/", or an initialization error.
 */
status_t
BPath::GetParent(BPath* path) const
{
	if (path == NULL)
		return B_BAD_VALUE;

	status_t error = InitCheck();
	if (error != B_OK)
		return error;

	int32 length = strlen(fName);
	if (length == 1) {
		// handle "/" (path is supposed to be absolute)
		return B_ENTRY_NOT_FOUND;
	}

	char parentPath[B_PATH_NAME_LENGTH];
	length--;
	while (fName[length] != '/' && length > 0)
		length--;
	if (length == 0) {
		// parent dir is "/"
		length++;
	}
	memcpy(parentPath, fName, length);
	parentPath[length] = '\0';

	return path->SetTo(parentPath);
}


/**
 * @brief Gets whether or not the path is absolute.
 *
 * @return true if the path is absolute (starts with '/'), false otherwise
 *         or if the object is not initialized.
 */
bool
BPath::IsAbsolute() const
{
	if (InitCheck() != B_OK)
		return false;

	return fName[0] == '/';
}


/**
 * @brief Performs a simple (string-wise) comparison of paths for equality.
 *
 * @param item The BPath object to compare against.
 * @return true if the paths are equal, false otherwise.
 */
bool
BPath::operator==(const BPath& item) const
{
	return *this == item.Path();
}


/**
 * @brief Performs a simple (string-wise) comparison of paths for equality.
 *
 * @param path The C string path to compare against.
 * @return true if the paths are equal (or both uninitialized/NULL),
 *         false otherwise.
 */
bool
BPath::operator==(const char* path) const
{
	return (InitCheck() != B_OK && path == NULL)
		|| (fName != NULL && path != NULL && strcmp(fName, path) == 0);
}


/**
 * @brief Performs a simple (string-wise) comparison of paths for inequality.
 *
 * @param item The BPath object to compare against.
 * @return true if the paths are not equal, false otherwise.
 */
bool
BPath::operator!=(const BPath& item) const
{
	return !(*this == item);
}


/**
 * @brief Performs a simple (string-wise) comparison of paths for inequality.
 *
 * @param path The C string path to compare against.
 * @return true if the paths are not equal, false otherwise.
 */
bool
BPath::operator!=(const char* path) const
{
	return !(*this == path);
}


/**
 * @brief Initializes the object as a copy of \a item.
 *
 * @param item The BPath object to copy.
 * @return A reference to this BPath object.
 */
BPath&
BPath::operator=(const BPath& item)
{
	if (this != &item)
		*this = item.Path();
	return *this;
}


/**
 * @brief Initializes the object with the passed in path string.
 *
 * @param path The C string path to set. If NULL, the object is unset.
 * @return A reference to this BPath object.
 */
BPath&
BPath::operator=(const char* path)
{
	if (path == NULL)
		Unset();
	else
		SetTo(path);
	return *this;
}


//	#pragma mark - BFlattenable functionality


// that's the layout of a flattened entry_ref
struct flattened_entry_ref {
	dev_t device;
	ino_t directory;
	char name[1];
};

// base size of a flattened entry ref
static const size_t flattened_entry_ref_size
	= sizeof(dev_t) + sizeof(ino_t);


/**
 * @brief Overrides BFlattenable::IsFixedSize().
 *
 * @return false, because the flattened size depends on the path length.
 */
bool
BPath::IsFixedSize() const
{
	return false;
}


/**
 * @brief Overrides BFlattenable::TypeCode().
 *
 * @return B_REF_TYPE, the type code used for flattened BPath data.
 */
type_code
BPath::TypeCode() const
{
	return B_REF_TYPE;
}


/**
 * @brief Gets the size of the flattened entry_ref struct that represents
 *        the path in bytes.
 *
 * @return The number of bytes required to store the flattened representation
 *         of this path.
 */
ssize_t
BPath::FlattenedSize() const
{
	ssize_t size = flattened_entry_ref_size;
	BEntry entry;
	entry_ref ref;
	if (InitCheck() == B_OK
		&& entry.SetTo(Path()) == B_OK
		&& entry.GetRef(&ref) == B_OK) {
		size += strlen(ref.name) + 1;
	}
	return size;
}


/**
 * @brief Converts the path of the object to an entry_ref and writes it
 *        into the provided buffer.
 *
 * @param buffer Pointer to the destination buffer.
 * @param size   Size in bytes of the destination buffer.
 * @return B_OK on success, B_BAD_VALUE if buffer is NULL or size is
 *         insufficient, or another error code on failure.
 */
status_t
BPath::Flatten(void* buffer, ssize_t size) const
{
	if (buffer == NULL)
		return B_BAD_VALUE;

	// ToDo: Reimplement for performance reasons: Don't call FlattenedSize().
	ssize_t flattenedSize = FlattenedSize();
	if (flattenedSize < 0)
		return flattenedSize;
	if (size < flattenedSize)
		return B_BAD_VALUE;

	// convert the path to an entry_ref
	BEntry entry;
	entry_ref ref;

	if (Path() != NULL) {
		status_t status = entry.SetTo(Path());
		if (status == B_OK)
			status = entry.GetRef(&ref);
		if (status != B_OK)
			return status;
	}

	// store the entry_ref in the buffer
	flattened_entry_ref& fref = *(flattened_entry_ref*)buffer;
	fref.device = ref.device;
	fref.directory = ref.directory;
	if (ref.name)
		strcpy(fref.name, ref.name);

	return B_OK;
}


/**
 * @brief Checks if the given type code is equal to B_REF_TYPE.
 *
 * @param code The type code to check.
 * @return true if \a code equals B_REF_TYPE, false otherwise.
 */
bool
BPath::AllowsTypeCode(type_code code) const
{
	return code == B_REF_TYPE;
}


/**
 * @brief Initializes the object with the flattened entry_ref data from the
 *        passed in buffer.
 *
 * @param code   The type code of the flattened data; must be B_REF_TYPE.
 * @param buffer Pointer to the buffer containing the flattened entry_ref.
 * @param size   Size in bytes of the buffer.
 * @return B_OK on success, B_BAD_VALUE if parameters are invalid, or another
 *         error code on failure.
 */
status_t
BPath::Unflatten(type_code code, const void* buffer, ssize_t size)
{
	Unset();
	status_t error = B_OK;
	// check params
	if (!(code == B_REF_TYPE && buffer != NULL
		  && size >= (ssize_t)flattened_entry_ref_size)) {
		error = B_BAD_VALUE;
	}
	if (error == B_OK) {
		if (size == (ssize_t)flattened_entry_ref_size) {
			// already Unset();
		} else {
			// reconstruct the entry_ref from the buffer
			const flattened_entry_ref& fref
				= *(const flattened_entry_ref*)buffer;
			BString name(fref.name, size - flattened_entry_ref_size);
			entry_ref ref(fref.device, fref.directory, name.String());
			error = SetTo(&ref);
		}
	}
	if (error != B_OK)
		fCStatus = error;
	return error;
}


void BPath::_WarPath1() {}
void BPath::_WarPath2() {}
void BPath::_WarPath3() {}


/*!	Sets the supplied path.

	The path is copied, if \a path is \c NULL the path of the object is set to
	\c NULL as well. The old path is deleted.

	\param path the path to be set

	\returns A status code.
	\retval B_OK Everything went fine.
	\retval B_NO_MEMORY Insufficient memory.
*/
/**
 * @brief Sets the internal path string, copying the supplied value.
 *
 * If \a path is NULL the internal path is freed and set to NULL.
 * The previously stored path string is always deleted.
 *
 * @param path The path string to copy and store, or NULL to clear.
 * @return B_OK on success, B_NO_MEMORY if allocation fails.
 */
status_t
BPath::_SetPath(const char* path)
{
	status_t error = B_OK;
	const char* oldPath = fName;
	// set the new path
	if (path) {
		fName = new(nothrow) char[strlen(path) + 1];
		if (fName)
			strcpy(fName, path);
		else
			error = B_NO_MEMORY;
	} else
		fName = NULL;

	// delete the old one
	delete[] oldPath;
	return error;
}


/*!	Checks a path to see if normalization is required.

	The following items require normalization:
	- Relative pathnames (after concatenation; e.g. "boot/ltj")
	- The presence of "." or ".." ("/boot/ltj/../ltj/./gwar")
	- Redundant slashes ("/boot//ltj")
	- A trailing slash ("/boot/ltj/")

	\param _error A pointer to an error variable that will be set if the input
		is not a valid path.

	\return \c true if \a path requires normalization, \c false otherwise.
*/
/**
 * @brief Checks a path to determine whether normalization is required.
 *
 * Normalization is required for relative paths, paths containing "." or ".."
 * components, redundant slashes, or a trailing slash.
 *
 * @param path   The path string to inspect.
 * @param _error Output pointer that receives B_BAD_VALUE if the path is
 *               invalid (NULL or empty).
 * @return true if normalization is required, false if the path is already
 *         in normal form or is invalid.
 */
bool
BPath::_MustNormalize(const char* path, status_t* _error)
{
	// Check for useless input
	if (path == NULL || path[0] == 0) {
		if (_error != NULL)
			*_error = B_BAD_VALUE;
		return false;
	}

	int len = strlen(path);

	/* Look for anything in the string that forces us to normalize:
			+ No leading /
			+ any occurence of /./ or /../ or //, or a trailing /. or /..
			+ a trailing /
	*/;
	if (path[0] != '/')
		return true;	//	not "/*"
	else if (len == 1)
		return false;	//	"/"
	else if (len > 1 && path[len-1] == '/')
		return true;	// 	"*/"
	else {
		enum ParseState {
			NoMatch,
			InitialSlash,
			OneDot,
			TwoDots
		} state = NoMatch;

		for (int i = 0; path[i] != 0; i++) {
			switch (state) {
				case NoMatch:
					if (path[i] == '/')
						state = InitialSlash;
					break;

				case InitialSlash:
					if (path[i] == '/')
						return true;		// "*//*"

					if (path[i] == '.')
						state = OneDot;
					else
						state = NoMatch;
					break;

				case OneDot:
					if (path[i] == '/')
						return true;		// "*/./*"

					if (path[i] == '.')
						state = TwoDots;
					else
						state = NoMatch;
					break;

				case TwoDots:
					if (path[i] == '/')
						return true;		// "*/../*"

					state = NoMatch;
					break;
			}
		}

		// If we hit the end of the string while in either
		// of these two states, there was a trailing /. or /..
		if (state == OneDot || state == TwoDots)
			return true;

		return false;
	}
}
