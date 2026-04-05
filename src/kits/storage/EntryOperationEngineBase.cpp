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
 *   Copyright 2013-2014, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file EntryOperationEngineBase.cpp
 * @brief Implementation of BEntryOperationEngineBase::Entry, a flexible
 *        file system entry descriptor.
 *
 * The Entry inner class provides a unified representation of a file system
 * entry that may be specified as a raw path, a BEntry, an entry_ref, or a
 * directory + relative path combination. Path resolution is deferred to
 * GetPath() to avoid unnecessary stat calls.
 *
 * @see BEntryOperationEngineBase
 */

#include <EntryOperationEngineBase.h>

#include <Directory.h>
#include <Entry.h>
#include <Path.h>


namespace BPrivate {


/**
 * @brief Constructs an Entry from an absolute or relative path string.
 *
 * @param path The path string identifying the entry.
 */
BEntryOperationEngineBase::Entry::Entry(const char* path)
	:
	fDirectory(NULL),
	fPath(path),
	fEntry(NULL),
	fEntryRef(NULL),
	fDirectoryRef(NULL)
{
}


/**
 * @brief Constructs an Entry from a parent directory and a relative path.
 *
 * @param directory Reference to the parent BDirectory.
 * @param path      Relative name within the directory.
 */
BEntryOperationEngineBase::Entry::Entry(const BDirectory& directory,
	const char* path)
	:
	fDirectory(&directory),
	fPath(path),
	fEntry(NULL),
	fEntryRef(NULL),
	fDirectoryRef(NULL)
{
}


/**
 * @brief Constructs an Entry from an existing BEntry.
 *
 * @param entry Reference to the BEntry describing the entry.
 */
BEntryOperationEngineBase::Entry::Entry(const BEntry& entry)
	:
	fDirectory(NULL),
	fPath(NULL),
	fEntry(&entry),
	fEntryRef(NULL),
	fDirectoryRef(NULL)
{
}


/**
 * @brief Constructs an Entry from an entry_ref.
 *
 * @param entryRef Reference to the entry_ref identifying the entry.
 */
BEntryOperationEngineBase::Entry::Entry(const entry_ref& entryRef)
	:
	fDirectory(NULL),
	fPath(NULL),
	fEntry(NULL),
	fEntryRef(&entryRef),
	fDirectoryRef(NULL)
{
}


/**
 * @brief Constructs an Entry from a directory node_ref and a relative path.
 *
 * @param directoryRef node_ref of the parent directory.
 * @param path         Relative name within the directory.
 */
BEntryOperationEngineBase::Entry::Entry(const node_ref& directoryRef,
	const char* path)
	:
	fDirectory(NULL),
	fPath(path),
	fEntry(NULL),
	fEntryRef(NULL),
	fDirectoryRef(&directoryRef)
{
}


/**
 * @brief Destructor.
 */
BEntryOperationEngineBase::Entry::~Entry()
{
}


/**
 * @brief Resolves this entry to an absolute path.
 *
 * Uses whichever representation was provided at construction time to
 * compute the absolute path. If the entry was created from a plain path
 * string, that string is returned directly without a BPath allocation.
 *
 * @param buffer Output BPath used as a temporary buffer when resolution
 *               requires path construction.
 * @param _path  Output reference that is set to point to the resolved path
 *               string (either inside buffer or to the original path string).
 * @return B_OK on success, or an error code on failure.
 */
status_t
BEntryOperationEngineBase::Entry::GetPath(BPath& buffer, const char*& _path)
	const
{
	status_t error = B_OK;

	if (fEntry != NULL) {
		error = buffer.SetTo(fEntry);
	} else if (fDirectory != NULL) {
		error = buffer.SetTo(fDirectory, fPath);
	} else if (fEntryRef != NULL) {
		error = buffer.SetTo(fEntryRef);
	} else if (fDirectoryRef != NULL) {
		BDirectory directory;
		error = directory.SetTo(fDirectoryRef);
		if (error == B_OK)
			error = buffer.SetTo(&directory, fPath);
	} else if (fPath != NULL) {
		_path = fPath;
		return B_OK;
	}

	if (error != B_OK)
		return error;

	_path = buffer.Path();
	return B_OK;
}


/**
 * @brief Returns the resolved absolute path as a BString.
 *
 * @return The absolute path string, or an empty BString on error.
 */
BString
BEntryOperationEngineBase::Entry::Path() const
{
	BPath pathBuffer;
	const char* path;
	if (GetPath(pathBuffer, path) == B_OK)
		return BString(path);
	return BString();
}


/**
 * @brief Returns the best available path or name for this entry.
 *
 * If the full path cannot be resolved, falls back to returning just the
 * entry name or a partial directory path combined with the relative name.
 * This is useful for constructing human-readable error messages when the
 * full path is unavailable.
 *
 * @param _path Output BString that receives the path or name.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or another
 *         error code if no representation can be computed.
 */
status_t
BEntryOperationEngineBase::Entry::GetPathOrName(BString& _path) const
{
	_path.Truncate(0);

	BPath buffer;
	const char* path;
	status_t error = GetPath(buffer, path);
	if (error == B_NO_MEMORY)
		return error;

	if (error == B_OK) {
		_path = path;
	} else if (fEntry != NULL) {
		// GetPath() apparently failed, so just return the entry name.
		_path = fEntry->Name();
	} else if (fDirectory != NULL || fDirectoryRef != NULL) {
		if (fPath != NULL && fPath[0] == '/') {
			// absolute path -- just return it
			_path = fPath;
		} else {
			// get the directory path
			BEntry entry;
			if (fDirectory != NULL) {
				error = fDirectory->GetEntry(&entry);
			} else {
				BDirectory directory;
				error = directory.SetTo(fDirectoryRef);
				if (error == B_OK)
					error = directory.GetEntry(&entry);
			}

			if (error != B_OK || (error = entry.GetPath(&buffer)) != B_OK)
				return error;

			_path = buffer.Path();

			// If we additionally have a relative path, append it.
			if (!_path.IsEmpty() && fPath != NULL) {
				int32 length = _path.Length();
				_path << '/' << fPath;
				if (_path.Length() < length + 2)
					return B_NO_MEMORY;
			}
		}
	} else if (fEntryRef != NULL) {
		// Getting the actual path apparently failed, so just return the entry
		// name.
		_path = fEntryRef->name;
	} else if (fPath != NULL)
		_path = fPath;

	return _path.IsEmpty() ? B_NO_MEMORY : B_OK;
}


/**
 * @brief Returns the best available path or name as a BString.
 *
 * @return The path or name string, or an empty BString on error.
 */
BString
BEntryOperationEngineBase::Entry::PathOrName() const
{
	BString path;
	return GetPathOrName(path) == B_OK ? path : BString();
}


} // namespace BPrivate
