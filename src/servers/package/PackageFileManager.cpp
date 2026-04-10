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
 * Copyright 2014, Ingo Weinhold, ingo_weinhold@gmx.de.
  * Distributed under the terms of the MIT License.
 */

/** @file PackageFileManager.cpp
 *  @brief Implements the PackageFile lookup cache with reference-counted sharing */



#include "PackageFileManager.h"

#include <Locker.h>

#include <AutoLocker.h>

#include "DebugSupport.h"
#include "Package.h"


/**
 * @brief Constructs a PackageFileManager that uses the given lock for synchronization.
 *
 * @param lock External lock shared with the Volume for thread safety.
 */
PackageFileManager::PackageFileManager(BLocker& lock)
	:
	fLock(lock),
	fFilesByEntryRef()
{
}


/** @brief Destroys the PackageFileManager. */
PackageFileManager::~PackageFileManager()
{
}


/**
 * @brief Initializes the internal entry-ref hash table.
 *
 * @return B_OK on success, or an error code if hash table initialization fails.
 */
status_t
PackageFileManager::Init()
{
	return fFilesByEntryRef.Init();
}


/**
 * @brief Retrieves or creates a PackageFile for the given entry_ref.
 *
 * If a live PackageFile already exists in the cache for this entry_ref,
 * its reference count is incremented and it is returned. Otherwise a new
 * PackageFile is created, initialized from the on-disk file, and inserted
 * into the cache.
 *
 * @param entryRef The entry_ref of the .hpkg file.
 * @param _file    Output: the PackageFile with an acquired reference.
 * @return B_OK on success, or an error code on failure.
 */
status_t
PackageFileManager::GetPackageFile(const entry_ref& entryRef,
	PackageFile*& _file)
{
	AutoLocker<BLocker> locker(fLock);

	PackageFile* file = fFilesByEntryRef.Lookup(entryRef);
	if (file != NULL) {
		if (file->AcquireReference() > 0) {
			_file = file;
			return B_OK;
		}

		// File already full dereferenced. It is about to be deleted.
		fFilesByEntryRef.Remove(file);
	}

	file = new(std::nothrow) PackageFile;
	if (file == NULL)
		RETURN_ERROR(B_NO_MEMORY);

	status_t error = file->Init(entryRef, this);
	if (error != B_OK) {
		delete file;
		return error;
	}

	fFilesByEntryRef.Insert(file);

	_file = file;
	return B_OK;
}


/**
 * @brief Creates a new Package object backed by a (possibly cached) PackageFile.
 *
 * Calls GetPackageFile() to obtain the underlying PackageFile, then wraps
 * it in a new Package instance.
 *
 * @param entryRef  The entry_ref of the .hpkg file.
 * @param _package  Output: the newly created Package.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or another error.
 */
status_t
PackageFileManager::CreatePackage(const entry_ref& entryRef, Package*& _package)
{
	PackageFile* packageFile;
	status_t error = GetPackageFile(entryRef, packageFile);
	if (error != B_OK)
		RETURN_ERROR(error);
	BReference<PackageFile> packageFileReference(packageFile, true);

	_package = new(std::nothrow) Package(packageFile);
	RETURN_ERROR(_package != NULL ? B_OK : B_NO_MEMORY);
}


/**
 * @brief Updates the cache when a PackageFile moves to a new directory.
 *
 * Removes the file from the hash table under its old entry_ref, updates
 * its directory reference, then re-inserts it.
 *
 * @param file         The PackageFile that was moved.
 * @param newDirectory The node_ref of the file's new parent directory.
 */
void
PackageFileManager::PackageFileMoved(PackageFile* file,
	const node_ref& newDirectory)
{
	if (newDirectory == file->DirectoryRef())
		return;

	AutoLocker<BLocker> locker(fLock);

	fFilesByEntryRef.Remove(file);
	file->SetDirectoryRef(newDirectory);
	fFilesByEntryRef.Insert(file);
}


/**
 * @brief Removes a PackageFile from the entry-ref cache.
 *
 * Called when the last reference to a PackageFile is released, just
 * before the object is deleted.
 *
 * @param file The PackageFile to remove from the cache.
 */
void
PackageFileManager::RemovePackageFile(PackageFile* file)
{
	AutoLocker<BLocker> locker(fLock);

	fFilesByEntryRef.Remove(file);
}
