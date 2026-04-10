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
 *
 * Copyright 2013-2014, Haiku, Inc. All Rights Reserved.
  * Distributed under the terms of the MIT License.
  *
  * Authors:
  *		Ingo Weinhold <ingo_weinhold@gmx.de>
 */

/** @file PackageFile.cpp
 *  @brief Implements PackageFile initialization, info reading, and reference-counted cleanup */



#include "PackageFile.h"

#include <fcntl.h>

#include <File.h>

#include <AutoDeleter.h>

#include "DebugSupport.h"
#include "PackageFileManager.h"


/**
 * @brief Constructs an uninitialized PackageFile.
 *
 * Init() must be called before the object is usable.
 */
PackageFile::PackageFile()
	:
	fNodeRef(),
	fDirectoryRef(),
	fFileName(),
	fInfo(),
	fEntryRefHashTableNext(NULL),
// 	fNodeRefHashTableNext(NULL),
	fOwner(NULL),
	fIgnoreEntryCreated(0),
	fIgnoreEntryRemoved(0)
{
}


/** @brief Destroys the PackageFile. */
PackageFile::~PackageFile()
{
}


/**
 * @brief Initializes the PackageFile from a directory entry.
 *
 * Opens the file, retrieves its node_ref, reads the package info from
 * the .hpkg archive, and stores the owning PackageFileManager.
 *
 * @param entryRef The entry_ref pointing to the .hpkg file on disk.
 * @param owner    The PackageFileManager that owns this file; used for
 *                 cleanup when the last reference is released.
 * @return B_OK on success, or an error code on failure.
 */
status_t
PackageFile::Init(const entry_ref& entryRef, PackageFileManager* owner)
{
	fDirectoryRef.device = entryRef.device;
	fDirectoryRef.node = entryRef.directory;

	// init the file name
	fFileName = entryRef.name;
	if (fFileName.IsEmpty())
		RETURN_ERROR(B_NO_MEMORY);

	// open the file and get the node_ref
	BFile file;
	status_t error = file.SetTo(&entryRef, B_READ_ONLY);
	if (error != B_OK)
		RETURN_ERROR(error);

	error = file.GetNodeRef(&fNodeRef);
	if (error != B_OK)
		RETURN_ERROR(error);

	// get the package info
	FileDescriptorCloser fd(file.Dup());
	if (!fd.IsSet())
		RETURN_ERROR(error);

	error = fInfo.ReadFromPackageFile(fd.Get());
	if (error != B_OK)
		RETURN_ERROR(error);

	if (fFileName != fInfo.CanonicalFileName())
		fInfo.SetFileName(fFileName);

	fOwner = owner;

	return B_OK;
}


/**
 * @brief Returns a string combining the package name and version.
 *
 * The format is "name-version", suitable for identifying a specific
 * revision of a package.
 *
 * @return A formatted "name-version" string, or an empty string on failure.
 */
BString
PackageFile::RevisionedName() const
{
	return BString().SetToFormat("%s-%s", fInfo.Name().String(),
		fInfo.Version().ToString().String());
}


/**
 * @brief Returns the revisioned name, throwing std::bad_alloc on failure.
 *
 * @return A "name-version" string.
 * @throws std::bad_alloc If the string allocation fails.
 */
BString
PackageFile::RevisionedNameThrows() const
{
	BString result(RevisionedName());
	if (result.IsEmpty())
		throw std::bad_alloc();
	return result;
}


/**
 * @brief Called when the last reference to this PackageFile is released.
 *
 * Removes the file from its owning PackageFileManager's cache and then
 * deletes itself.
 */
void
PackageFile::LastReferenceReleased()
{
	if (fOwner != NULL)
		fOwner->RemovePackageFile(this);
	delete this;
}
