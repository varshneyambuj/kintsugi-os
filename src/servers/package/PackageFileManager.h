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
   /*
    * Copyright 2014, Ingo Weinhold, ingo_weinhold@gmx.de.
    * Distributed under the terms of the MIT License.
    */
 */

/** @file PackageFileManager.h
 *  @brief Manages a shared pool of PackageFile objects keyed by entry ref */

#ifndef PACKAGE_FILE_MANAGER_H
#define PACKAGE_FILE_MANAGER_H


#include "PackageFile.h"


class BLocker;

class Package;


/** @brief Maintains a reference-counted cache of PackageFile objects indexed by entry reference */
class PackageFileManager {
public:
	/** @brief Construct the manager with the given lock for thread safety */
								PackageFileManager(BLocker& lock);
	/** @brief Destructor */
								~PackageFileManager();

	/** @brief Initialize the internal hash table */
			status_t			Init();

	/** @brief Look up or create a PackageFile for the given entry ref, returning a reference */
			status_t			GetPackageFile(const entry_ref& entryRef,
									PackageFile*& _file);
									// returns a reference
	/** @brief Create a new Package backed by the PackageFile for the given entry ref */
			status_t			CreatePackage(const entry_ref& entryRef,
									Package*& _package);

	/** @brief Update the hash table when a package file is moved to a new directory */
			void				PackageFileMoved(PackageFile* file,
									const node_ref& newDirectory);

	/** @brief Remove a package file from the cache (called by PackageFile on last release) */
			void				RemovePackageFile(PackageFile* file);

private:
			typedef PackageFileEntryRefHashTable EntryRefTable;

private:
			BLocker&			fLock;             /**< External lock for thread safety */
			EntryRefTable		fFilesByEntryRef;  /**< Hash table of cached PackageFile objects */
};


#endif	// PACKAGE_FILE_MANAGER_H
