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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/** @file LocatableFile.cpp
    @brief Locatable entry that represents a regular file in the source/target tree. */

#include "LocatableFile.h"

#include <AutoLocker.h>

#include "LocatableDirectory.h"


// #pragma mark - LocatableFile


/**
 * @brief Construct a file entry under @a directory inside @a owner.
 *
 * @param owner      Domain that arbitrates locking and lifecycle callbacks.
 * @param directory  Parent directory.
 * @param name       File name within @a directory.
 */
LocatableFile::LocatableFile(LocatableEntryOwner* owner,
	LocatableDirectory* directory, const BString& name)
	:
	LocatableEntry(owner, directory),
	fName(name),
	fLocatedPath(),
	fListeners(8)
{
}


/** @brief Virtual destructor. */
LocatableFile::~LocatableFile()
{
}


/** @brief Return the file's leaf name. */
const char*
LocatableFile::Name() const
{
	return fName.String();
}


/**
 * @brief Compose the full original path by joining the parent directory and @a fName.
 *
 * @param _path  Output BString that receives the composed path.
 */
void
LocatableFile::GetPath(BString& _path) const
{
	fParent->GetPath(_path);
	if (_path.Length() != 0)
		_path << '/';
	_path << fName;
}


/**
 * @brief Return the local located path, falling back to the parent's location plus @a fName.
 *
 * @param _path  Output BString that receives the located path on success.
 * @return true if either the file or its parent are located.
 */
bool
LocatableFile::GetLocatedPath(BString& _path) const
{
	AutoLocker<LocatableEntryOwner> locker(fOwner);

	if (fLocatedPath.Length() > 0) {
		_path = fLocatedPath;
		return true;
	}

	if (!fParent->GetLocatedPath(_path))
		return false;

	_path << '/' << fName;
	return true;
}


/**
 * @brief Record the local path that resolves this file and notify listeners.
 *
 * Implicit locations clear @c fLocatedPath so the file is resolved through
 * the parent directory chain on subsequent lookups; explicit locations are
 * stored verbatim.
 *
 * @param path      Located path.
 * @param implicit  true if the location was inferred from a sibling/ancestor.
 * @note Callers must already hold the owner's lock.
 */
void
LocatableFile::SetLocatedPath(const BString& path, bool implicit)
{
	// called with owner already locked

	if (implicit) {
		fLocatedPath = (const char*)NULL;
		fState = LOCATABLE_ENTRY_LOCATED_IMPLICITLY;
	} else {
		fLocatedPath = path;
		fState = LOCATABLE_ENTRY_LOCATED_EXPLICITLY;
	}

	_NotifyListeners();
}


/**
 * @brief Register a listener for change notifications on this file.
 *
 * @param listener  Listener to attach. Ownership is not transferred.
 * @return true on success, false on allocation failure.
 */
bool
LocatableFile::AddListener(Listener* listener)
{
	AutoLocker<LocatableEntryOwner> locker(fOwner);
	return fListeners.AddItem(listener);
}


/**
 * @brief Detach a previously registered listener.
 *
 * @param listener  Listener to remove.
 */
void
LocatableFile::RemoveListener(Listener* listener)
{
	AutoLocker<LocatableEntryOwner> locker(fOwner);
	fListeners.RemoveItem(listener);
}


/**
 * @brief Dispatch LocatableFileChanged() to every registered listener.
 *
 * Iterates in reverse so listeners that detach during the callback don't
 * skip later listeners.
 */
void
LocatableFile::_NotifyListeners()
{
	for (int32 i = fListeners.CountItems() - 1; i >= 0; i--)
		fListeners.ItemAt(i)->LocatableFileChanged(this);
}


// #pragma mark - Listener


/** @brief Virtual destructor anchor for the Listener interface. */
LocatableFile::Listener::~Listener()
{
}
