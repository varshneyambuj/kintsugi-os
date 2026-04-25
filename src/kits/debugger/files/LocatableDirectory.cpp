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
 *   Distributed under the terms of the MIT License.
 */


/** @file LocatableDirectory.cpp
    @brief Locatable entry that represents a directory in the source/target tree. */

#include "LocatableDirectory.h"


/**
 * @brief Construct a directory entry under @a owner inside @a parent.
 *
 * @param owner   Domain that arbitrates locking and reference notifications.
 * @param parent  Parent directory, or NULL for the root.
 * @param path    Original (target) directory path.
 */
LocatableDirectory::LocatableDirectory(LocatableEntryOwner* owner,
	LocatableDirectory* parent, const BString& path)
	:
	LocatableEntry(owner, parent),
	fPath(path),
	fLocatedPath()
{
}


/** @brief Virtual destructor. */
LocatableDirectory::~LocatableDirectory()
{
}


/**
 * @brief Return the leaf component of the directory's original path.
 *
 * For the root directory ("/" or shorter) the entire path is returned.
 *
 * @return Pointer into fPath identifying the leaf name.
 */
const char*
LocatableDirectory::Name() const
{
	if (fPath.Length() <= 1)
		return fPath;

	int32 lastSlash = fPath.FindLast('/');
		// return -1, if not found
	return fPath.String() + (lastSlash + 1);
}


/** @brief Return the original (target-side) path. */
const char*
LocatableDirectory::Path() const
{
	return fPath.String();
}


/**
 * @brief Copy the original path into @a _path.
 *
 * @param _path  Output BString.
 */
void
LocatableDirectory::GetPath(BString& _path) const
{
	_path = fPath;
}


/**
 * @brief Return the local located path, if any.
 *
 * @param _path  Output BString that receives the located path on success.
 * @return true if the directory has been located, false otherwise.
 */
bool
LocatableDirectory::GetLocatedPath(BString& _path) const
{
	if (fLocatedPath.Length() == 0)
		return false;
	_path = fLocatedPath;
	return true;
}


/**
 * @brief Record the local path that resolves this directory.
 *
 * @param path      Located path.
 * @param implicit  true if the location was inferred (from a sibling/ancestor),
 *                  false when the user explicitly supplied it.
 */
void
LocatableDirectory::SetLocatedPath(const BString& path, bool implicit)
{
	fLocatedPath = path;
	fState = implicit
		? LOCATABLE_ENTRY_LOCATED_IMPLICITLY
		: LOCATABLE_ENTRY_LOCATED_EXPLICITLY;
}


/**
 * @brief Append @a entry to the list of children.
 *
 * @param entry  Child entry to add.
 */
void
LocatableDirectory::AddEntry(LocatableEntry* entry)
{
	fEntries.Add(entry);
}


/**
 * @brief Detach @a entry from the list of children.
 *
 * @param entry  Child entry to remove.
 */
void
LocatableDirectory::RemoveEntry(LocatableEntry* entry)
{
	fEntries.Remove(entry);
}
