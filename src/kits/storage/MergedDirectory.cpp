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
 *   Copyright 2013, Haiku, Inc. All Rights Reserved.
 *   Authors: Ingo Weinhold <ingo_weinhold@gmx.de>
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file MergedDirectory.cpp
 * @brief Implementation of BMergedDirectory, a virtual merged directory view.
 *
 * BMergedDirectory presents multiple underlying BDirectory objects as a single
 * unified BEntryList. Duplicate handling is governed by a configurable BPolicy
 * that supports allowing all duplicates, always preferring the first occurrence,
 * or comparing entries to select the best one.
 *
 * @see BMergedDirectory
 */

#include <MergedDirectory.h>

#include <string.h>

#include <new>
#include <set>
#include <string>

#include <Directory.h>
#include <Entry.h>

#include <AutoDeleter.h>

#include "storage_support.h"


struct BMergedDirectory::EntryNameSet : std::set<std::string> {
};


/**
 * @brief Constructs a BMergedDirectory with the given duplicate-handling policy.
 *
 * @param policy One of B_ALLOW_DUPLICATES, B_ALWAYS_FIRST, or B_COMPARE.
 */
BMergedDirectory::BMergedDirectory(BPolicy policy)
	:
	BEntryList(),
	fDirectories(10),
	fPolicy(policy),
	fDirectoryIndex(0),
	fVisitedEntries(NULL)
{
}


/**
 * @brief Destructor. Frees the visited-entry set.
 */
BMergedDirectory::~BMergedDirectory()
{
	delete fVisitedEntries;
}


/**
 * @brief Initialises or resets the merged directory state.
 *
 * Clears the directory list and allocates a fresh visited-entry set.
 *
 * @return B_OK on success, B_NO_MEMORY if allocation fails.
 */
status_t
BMergedDirectory::Init()
{
	delete fVisitedEntries;
	fDirectories.MakeEmpty(true);

	fVisitedEntries = new(std::nothrow) EntryNameSet;
	return fVisitedEntries != NULL ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Returns the current duplicate-handling policy.
 *
 * @return The active BPolicy value.
 */
BMergedDirectory::BPolicy
BMergedDirectory::Policy() const
{
	return fPolicy;
}


/**
 * @brief Changes the duplicate-handling policy.
 *
 * @param policy The new BPolicy value to apply.
 */
void
BMergedDirectory::SetPolicy(BPolicy policy)
{
	fPolicy = policy;
}


/**
 * @brief Adds an already-open BDirectory to the merge set.
 *
 * Ownership of the pointer is not transferred; the caller must keep the
 * directory alive for the lifetime of this BMergedDirectory.
 *
 * @param directory Pointer to an open BDirectory to add.
 * @return B_OK on success, B_NO_MEMORY if the internal list is full.
 */
status_t
BMergedDirectory::AddDirectory(BDirectory* directory)
{
	return fDirectories.AddItem(directory) ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Opens the directory at the given path and adds it to the merge set.
 *
 * The created BDirectory is owned by this BMergedDirectory and will be
 * deleted when the object is destroyed or Init() is called.
 *
 * @param path Absolute path of the directory to open and add.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMergedDirectory::AddDirectory(const char* path)
{
	BDirectory* directory = new(std::nothrow) BDirectory(path);
	if (directory == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<BDirectory> directoryDeleter(directory);

	if (directory->InitCheck() != B_OK)
		return directory->InitCheck();

	status_t error = AddDirectory(directory);
	if (error != B_OK)
		return error;
	directoryDeleter.Detach();

	return B_OK;
}


/**
 * @brief Retrieves the next entry from the merged view as a BEntry.
 *
 * @param entry    Output BEntry to initialise.
 * @param traverse Whether to traverse symbolic links.
 * @return B_OK on success, B_ENTRY_NOT_FOUND when exhausted, or an error code.
 */
status_t
BMergedDirectory::GetNextEntry(BEntry* entry, bool traverse)
{
	entry_ref ref;
	status_t error = GetNextRef(&ref);
	if (error != B_OK)
		return error;

	return entry->SetTo(&ref, traverse);
}


/**
 * @brief Retrieves the next entry from the merged view as an entry_ref.
 *
 * @param ref Output entry_ref to fill in.
 * @return B_OK on success, B_ENTRY_NOT_FOUND when exhausted, or an error code.
 */
status_t
BMergedDirectory::GetNextRef(entry_ref* ref)
{
	BPrivate::Storage::LongDirEntry longEntry;
	struct dirent* entry = longEntry.dirent();
	int32 result = GetNextDirents(entry, sizeof(longEntry), 1);
	if (result < 0)
		return result;
	if (result == 0)
		return B_ENTRY_NOT_FOUND;

	ref->device = entry->d_pdev;
	ref->directory = entry->d_pino;
	return ref->set_name(entry->d_name);
}


/**
 * @brief Retrieves up to maxEntries dirents from the merged view.
 *
 * Iterates across all added directories, applies the duplicate policy, and
 * optionally invokes _FindBestEntry() for B_COMPARE mode.
 *
 * @param direntBuffer Buffer to receive the dirent structures.
 * @param bufferSize   Size of direntBuffer in bytes.
 * @param maxEntries   Maximum number of entries to return.
 * @return Number of entries written (0 means end of list), or a negative
 *         error code on failure.
 */
int32
BMergedDirectory::GetNextDirents(struct dirent* direntBuffer, size_t bufferSize,
	int32 maxEntries)
{
	if (maxEntries <= 0)
		return B_BAD_VALUE;

	while (fDirectoryIndex < fDirectories.CountItems()) {
		int32 count = fDirectories.ItemAt(fDirectoryIndex)->GetNextDirents(
			direntBuffer, bufferSize, 1);
		if (count < 0)
			return count;
		if (count == 0) {
			fDirectoryIndex++;
			continue;
		}

		if (strcmp(direntBuffer->d_name, ".") == 0
			|| strcmp(direntBuffer->d_name, "..") == 0) {
			continue;
		}

		switch (fPolicy) {
			case B_ALLOW_DUPLICATES:
				return count;

			case B_ALWAYS_FIRST:
			case B_COMPARE:
				if (fVisitedEntries != NULL
					&& fVisitedEntries->find(direntBuffer->d_name)
						!= fVisitedEntries->end()) {
					continue;
				}

				if (fVisitedEntries != NULL) {
					try {
						fVisitedEntries->insert(direntBuffer->d_name);
					} catch (std::bad_alloc&) {
						return B_NO_MEMORY;
					}
				}

				if (fPolicy == B_COMPARE)
					_FindBestEntry(direntBuffer);
				return 1;
		}
	}

	return 0;
}


/**
 * @brief Rewinds all underlying directories and clears the visited-entry set.
 *
 * @return B_OK always.
 */
status_t
BMergedDirectory::Rewind()
{
	for (int32 i = 0; BDirectory* directory = fDirectories.ItemAt(i); i++)
		directory->Rewind();

	if (fVisitedEntries != NULL)
		fVisitedEntries->clear();

	fDirectoryIndex = 0;
	return B_OK;
}


/**
 * @brief Returns the total number of unique entries in the merged view.
 *
 * Note: this iterates through all entries and is therefore O(n).
 *
 * @return Total entry count.
 */
int32
BMergedDirectory::CountEntries()
{
	int32 count = 0;
	char buffer[sizeof(dirent) + B_FILE_NAME_LENGTH];
	while (GetNextDirents((dirent*)&buffer, sizeof(buffer), 1) == 1)
		count++;
	return count;
}


/**
 * @brief Decides whether the first entry should be preferred over the second
 *        when using the B_COMPARE policy.
 *
 * The default implementation always returns true (B_ALWAYS_FIRST semantics).
 * Derived classes should override this to implement custom comparison logic.
 *
 * @param entry1  entry_ref of the first (currently best) candidate.
 * @param index1  Directory index of the first candidate.
 * @param entry2  entry_ref of the second candidate.
 * @param index2  Directory index of the second candidate.
 * @return true if entry1 should be preferred, false if entry2 should win.
 */
bool
BMergedDirectory::ShallPreferFirstEntry(const entry_ref& entry1, int32 index1,
	const entry_ref& entry2, int32 index2)
{
	// That's basically B_ALWAYS_FIRST semantics. A derived class will implement
	// the desired semantics.
	return true;
}


/**
 * @brief Searches all remaining directories for the best version of the entry
 *        currently described by direntBuffer, updating it in place.
 *
 * Used internally by GetNextDirents() when the B_COMPARE policy is active.
 *
 * @param direntBuffer In/out buffer containing the initial candidate dirent;
 *                     updated to the winning entry if a better one is found.
 */
void
BMergedDirectory::_FindBestEntry(dirent* direntBuffer)
{
	entry_ref bestEntry(direntBuffer->d_pdev, direntBuffer->d_pino,
		direntBuffer->d_name);
	if (bestEntry.name == NULL)
		return;
	int32 bestIndex = fDirectoryIndex;

	int32 directoryCount = fDirectories.CountItems();
	for (int32 i = fDirectoryIndex + 1; i < directoryCount; i++) {
		BEntry entry(fDirectories.ItemAt(i), bestEntry.name);
		struct stat st;
		entry_ref ref;
		if (entry.GetStat(&st) == B_OK && entry.GetRef(&ref) == B_OK
			&& !ShallPreferFirstEntry(bestEntry, bestIndex, ref, i)) {
			direntBuffer->d_pdev = ref.device;
			direntBuffer->d_pino = ref.directory;
			direntBuffer->d_dev = st.st_dev;
			direntBuffer->d_ino = st.st_ino;
			bestEntry.device = ref.device;
			bestEntry.directory = ref.directory;
			bestIndex = i;
		}
	}
}
