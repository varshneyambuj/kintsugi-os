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
 *   Copyright 2013, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *   Authors:
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 */


/**
 * @file VirtualDirectoryEntryList.cpp
 * @brief EntryList implementation that iterates a merged virtual directory.
 *
 * VirtualDirectoryEntryList resolves a virtual-directory definition file to a
 * set of real filesystem paths via VirtualDirectoryManager and then presents
 * them through a BMergedDirectory so that callers can enumerate the union of
 * those paths using the standard EntryList API.  Sub-directory entries are
 * transparently redirected to their virtual counterparts.
 *
 * @see VirtualDirectoryManager, BMergedDirectory, EntryListBase
 */


#include "VirtualDirectoryEntryList.h"

#include <AutoLocker.h>
#include <storage_support.h>

#include "Model.h"
#include "VirtualDirectoryManager.h"


namespace BPrivate {

//	#pragma mark - VirtualDirectoryEntryList


/**
 * @brief Construct an entry list for the virtual directory described by \a model.
 *
 * Resolves the directory paths from \a model's definition file via the
 * VirtualDirectoryManager, then initialises the underlying merged directory.
 * Check InitCheck() after construction.
 *
 * @param model  The Model representing the virtual directory node.
 */
VirtualDirectoryEntryList::VirtualDirectoryEntryList(Model* model)
	:
	EntryListBase(),
	fDefinitionFileRef(),
	fMergedDirectory(BMergedDirectory::B_ALWAYS_FIRST)
{
	VirtualDirectoryManager* manager = VirtualDirectoryManager::Instance();
	if (manager == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}

	AutoLocker<VirtualDirectoryManager> managerLocker(manager);
	BStringList directoryPaths;
	fStatus = manager->ResolveDirectoryPaths(*model->NodeRef(),
		*model->EntryRef(), directoryPaths, &fDefinitionFileRef);
	if (fStatus != B_OK)
		return;

	fStatus = _InitMergedDirectory(directoryPaths);
}


/**
 * @brief Construct an entry list from an already-resolved set of directory paths.
 *
 * @param definitionFileRef  node_ref of the virtual-directory definition file.
 * @param directoryPaths     List of real filesystem paths to merge.
 */
VirtualDirectoryEntryList::VirtualDirectoryEntryList(
	const node_ref& definitionFileRef, const BStringList& directoryPaths)
	:
	EntryListBase(),
	fDefinitionFileRef(definitionFileRef),
	fMergedDirectory(BMergedDirectory::B_ALWAYS_FIRST)
{
	fStatus = _InitMergedDirectory(directoryPaths);
}


/**
 * @brief Destructor.
 */
VirtualDirectoryEntryList::~VirtualDirectoryEntryList()
{
}


/**
 * @brief Return the initialisation status of this entry list.
 *
 * @return B_OK if the list is ready to iterate, or an error code otherwise.
 */
status_t
VirtualDirectoryEntryList::InitCheck() const
{
	return EntryListBase::InitCheck();
}


/**
 * @brief Retrieve the next entry as a BEntry.
 *
 * @param entry     Output BEntry to populate.
 * @param traverse  If true, symbolic links are followed.
 * @return B_OK on success, B_ENTRY_NOT_FOUND when exhausted, or an error code.
 */
status_t
VirtualDirectoryEntryList::GetNextEntry(BEntry* entry, bool traverse)
{
	entry_ref ref;
	status_t error = GetNextRef(&ref);
	if (error != B_OK)
		return error;

	return entry->SetTo(&ref, traverse);
}


/**
 * @brief Retrieve the next entry as an entry_ref.
 *
 * @param ref  Output entry_ref to populate.
 * @return B_OK on success, B_ENTRY_NOT_FOUND when exhausted, or an error code.
 */
status_t
VirtualDirectoryEntryList::GetNextRef(entry_ref* ref)
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
 * @brief Retrieve up to \a count dirents from the merged directory.
 *
 * Sub-directory entries are translated to their virtual-directory equivalents
 * by the VirtualDirectoryManager before being returned.
 *
 * @param buffer  Buffer to receive dirent structures.
 * @param length  Size in bytes of \a buffer.
 * @param count   Maximum number of dirents to return (capped at 1 internally).
 * @return Number of dirents read, 0 at end-of-directory, or a negative error code.
 */
int32
VirtualDirectoryEntryList::GetNextDirents(struct dirent* buffer, size_t length,
	int32 count)
{
	if (count > 1)
		count = 1;

	int32 countRead = fMergedDirectory.GetNextDirents(buffer, length, count);
	if (countRead != 1)
		return countRead;

	// deal with directories
	entry_ref ref;
	ref.device = buffer->d_pdev;
	ref.directory = buffer->d_pino;
	if (ref.set_name(buffer->d_name) == B_OK && BEntry(&ref).IsDirectory()) {
		if (VirtualDirectoryManager* manager
				= VirtualDirectoryManager::Instance()) {
			AutoLocker<VirtualDirectoryManager> managerLocker(manager);
			manager->TranslateDirectoryEntry(fDefinitionFileRef, buffer);
		}
	}

	return countRead;
}


/**
 * @brief Rewind the iteration to the beginning of the merged directory.
 *
 * @return B_OK on success, or an error code otherwise.
 */
status_t
VirtualDirectoryEntryList::Rewind()
{
	return fMergedDirectory.Rewind();
}


/**
 * @brief Return the number of entries in the virtual directory.
 *
 * This implementation always returns 0 because counting is not supported
 * for merged directories.
 *
 * @return 0.
 */
int32
VirtualDirectoryEntryList::CountEntries()
{
	return 0;
}


/**
 * @brief Initialise the internal BMergedDirectory from a list of paths.
 *
 * @param directoryPaths  Ordered list of filesystem paths to merge.
 * @return B_OK on success, or an error code if BMergedDirectory::Init() fails.
 */
status_t
VirtualDirectoryEntryList::_InitMergedDirectory(
	const BStringList& directoryPaths)
{
	status_t error = fMergedDirectory.Init();
	if (error != B_OK)
		return error;

	int32 count = directoryPaths.CountStrings();
	for (int32 i = 0; i < count; i++)
		fMergedDirectory.AddDirectory(directoryPaths.StringAt(i));

	return B_OK;
}

} // namespace BPrivate
