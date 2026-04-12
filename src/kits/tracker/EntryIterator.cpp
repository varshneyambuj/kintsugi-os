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
 *   Open Tracker License
 *
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of
 *   this software and associated documentation files (the "Software"), to deal in
 *   the Software without restriction, including without limitation the rights to
 *   use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *   of the Software, and to permit persons to whom the Software is furnished to do
 *   so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice applies to all licensees
 *   and shall be included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 *   AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
 *   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *   Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
 *   trademarks of Be Incorporated in the United States and other countries.
 *   All rights reserved.
 */


/**
 * @file EntryIterator.cpp
 * @brief Directory entry iterator classes used by the Tracker pose population logic.
 *
 * Provides TWalkerWrapper, EntryListBase, CachedEntryIterator, CachedDirectoryEntryList,
 * DirectoryEntryList, EntryIteratorList, and CachedEntryIteratorList. These classes
 * wrap BEntryList implementations with optional read-ahead caching and inode sorting
 * to improve pose-population throughput.
 *
 * @see BEntryList, BPoseView
 */


#include <Debug.h>
#include <Entry.h>
#include <ObjectList.h>
#include <Path.h>

#include <new>
#include <string.h>

#include "EntryIterator.h"


//	#pragma mark - TWalkerWrapper


/**
 * @brief Wrap a TWalker pointer and take ownership of it.
 *
 * @param walker  Heap-allocated TWalker to wrap; deleted in the destructor.
 */
TWalkerWrapper::TWalkerWrapper(BTrackerPrivate::TWalker* walker)
	:
	fWalker(walker),
	fStatus(B_OK)
{
}


/**
 * @brief Destructor; deletes the wrapped TWalker.
 */
TWalkerWrapper::~TWalkerWrapper()
{
	delete fWalker;
}


/**
 * @brief Return the initialisation status of the wrapper.
 *
 * @return B_OK if the last walker call succeeded, otherwise an error code.
 */
status_t
TWalkerWrapper::InitCheck() const
{
	return fStatus;
}


/**
 * @brief Fetch the next entry from the wrapped walker.
 *
 * @param entry     Output BEntry to receive the next result.
 * @param traverse  Whether to traverse symlinks.
 * @return B_OK on success, B_ENTRY_NOT_FOUND at end, or an error code.
 */
status_t
TWalkerWrapper::GetNextEntry(BEntry* entry, bool traverse)
{
	fStatus = fWalker->GetNextEntry(entry, traverse);

	return fStatus;
}


/**
 * @brief Fetch the next entry_ref from the wrapped walker.
 *
 * @param ref  Output entry_ref to receive the next result.
 * @return B_OK on success, B_ENTRY_NOT_FOUND at end, or an error code.
 */
status_t
TWalkerWrapper::GetNextRef(entry_ref* ref)
{
	fStatus = fWalker->GetNextRef(ref);

	return fStatus;
}


/**
 * @brief Fetch up to @a count dirents from the wrapped walker.
 *
 * @param buffer  Destination buffer for dirent data.
 * @param length  Size of @a buffer in bytes.
 * @param count   Maximum number of entries to retrieve.
 * @return Number of entries placed in @a buffer, or negative on error.
 */
int32
TWalkerWrapper::GetNextDirents(struct dirent* buffer, size_t length,
	int32 count)
{
	int32 result = fWalker->GetNextDirents(buffer, length, count);
	fStatus = result < B_OK ? result : (result ? B_OK : B_ENTRY_NOT_FOUND);

	return result;
}


/**
 * @brief Reset the walker to the beginning of the entry list.
 *
 * @return B_OK on success, or an error code.
 */
status_t
TWalkerWrapper::Rewind()
{
	return fWalker->Rewind();
}


/**
 * @brief Return the total number of entries available from the walker.
 *
 * @return Entry count reported by the underlying TWalker.
 */
int32
TWalkerWrapper::CountEntries()
{
	return fWalker->CountEntries();
}


//	#pragma mark - EntryListBase


/**
 * @brief Default constructor; sets the status to B_OK.
 */
EntryListBase::EntryListBase()
	:
	fStatus(B_OK)
{
}


/**
 * @brief Return the current status of the entry list.
 *
 * @return B_OK if the last operation succeeded, otherwise an error code.
 */
status_t
EntryListBase::InitCheck() const
{
	 return fStatus;
}


/**
 * @brief Advance a dirent pointer to the next record in a packed dirent buffer.
 *
 * @param ent  Pointer to the current dirent record.
 * @return Pointer to the following dirent record.
 */
dirent*
EntryListBase::Next(dirent* ent)
{
	return (dirent*)((char*)ent + ent->d_reclen);
}


//	#pragma mark - CachedEntryIterator


/**
 * @brief Construct a caching wrapper around @a iterator.
 *
 * @param iterator    The underlying BEntryList to read from.
 * @param numEntries  Number of entries to read ahead into the cache.
 * @param sortInodes  If true, cached dirent batches are sorted by inode number.
 */
CachedEntryIterator::CachedEntryIterator(BEntryList* iterator,
	int32 numEntries, bool sortInodes)
	:
	fIterator(iterator),
	fEntryRefBuffer(NULL),
	fCacheSize(numEntries),
	fNumEntries(0),
	fIndex(0),
	fDirentBuffer(NULL),
	fCurrentDirent(NULL),
	fSortInodes(sortInodes),
	fSortedList(NULL),
	fEntryBuffer(NULL)
{
}


/**
 * @brief Destructor; frees all cache buffers.
 */
CachedEntryIterator::~CachedEntryIterator()
{
	delete[] fEntryRefBuffer;
	free(fDirentBuffer);
	delete fSortedList;
	delete[] fEntryBuffer;
}


/**
 * @brief Retrieve the next BEntry, filling the cache when exhausted.
 *
 * @param result    Output BEntry to receive the next directory entry.
 * @param traverse  Whether to traverse symlinks.
 * @return B_OK on success, B_ENTRY_NOT_FOUND at end, or an error code.
 */
status_t
CachedEntryIterator::GetNextEntry(BEntry* result, bool traverse)
{
	ASSERT(fDirentBuffer == NULL);
	ASSERT(fEntryRefBuffer == NULL);

	if (fEntryBuffer == NULL) {
		fEntryBuffer = new BEntry [fCacheSize];
		ASSERT(fIndex == 0 && fNumEntries == 0);
	}

	if (fIndex >= fNumEntries) {
		// fill up the buffer or stop if error; keep error around
		// and return it when appropriate
		fStatus = B_OK;
		for (fNumEntries = 0; fNumEntries < fCacheSize; fNumEntries++) {
			fStatus = fIterator->GetNextEntry(&fEntryBuffer[fNumEntries],
				traverse);
			if (fStatus != B_OK)
				break;
		}
		fIndex = 0;
	}

	*result = fEntryBuffer[fIndex++];
	if (fIndex > fNumEntries) {
		// we are at the end of the cache we loaded up, time to return
		// an error, if we had one
		return fStatus;
	}

	return B_OK;
}


/**
 * @brief Retrieve the next entry_ref, filling the cache when exhausted.
 *
 * @param ref  Output entry_ref to receive the next result.
 * @return B_OK on success, B_ENTRY_NOT_FOUND at end, or an error code.
 */
status_t
CachedEntryIterator::GetNextRef(entry_ref* ref)
{
	ASSERT(fDirentBuffer == NULL);
	ASSERT(fEntryBuffer == NULL);

	if (fEntryRefBuffer == NULL) {
		fEntryRefBuffer = new entry_ref[fCacheSize];
		ASSERT(fIndex == 0 && fNumEntries == 0);
	}

	if (fIndex >= fNumEntries) {
		// fill up the buffer or stop if error; keep error around
		// and return it when appropriate
		fStatus = B_OK;
		for (fNumEntries = 0; fNumEntries < fCacheSize; fNumEntries++) {
			fStatus = fIterator->GetNextRef(&fEntryRefBuffer[fNumEntries]);
			if (fStatus != B_OK)
				break;
		}
		fIndex = 0;
	}

	*ref = fEntryRefBuffer[fIndex++];
	if (fIndex > fNumEntries) {
		// we are at the end of the cache we loaded up, time to return
		// an error, if we had one
		return fStatus;
	}

	return B_OK;
}


/**
 * @brief Comparator for sorting dirent records by inode number.
 *
 * @param ent1  First dirent to compare.
 * @param ent2  Second dirent to compare.
 * @return -1 if ent1 < ent2, 0 if equal, 1 if ent1 > ent2.
 */
/*static*/ int
CachedEntryIterator::_CompareInodes(const dirent* ent1, const dirent* ent2)
{
	if (ent1->d_ino < ent2->d_ino)
		return -1;

	if (ent1->d_ino == ent2->d_ino)
		return 0;

	return 1;
}


/**
 * @brief Retrieve the next dirent from the cache, refilling and optionally sorting.
 *
 * @param ent    Destination buffer for the returned dirent.
 * @param size   Size of @a ent in bytes.
 * @param count  Number of entries requested (typically 1).
 * @return 1 if an entry was returned, 0 at end of directory.
 */
int32
CachedEntryIterator::GetNextDirents(struct dirent* ent, size_t size,
	int32 count)
{
	ASSERT(fEntryRefBuffer == NULL);
	if (fDirentBuffer == NULL) {
		fDirentBuffer = (dirent*)malloc(kDirentBufferSize);
		ASSERT(fIndex == 0 && fNumEntries == 0);
		ASSERT(size > offsetof(struct dirent, d_name) + B_FILE_NAME_LENGTH);
	}

	if (count == 0)
		return 0;

	if (fIndex >= fNumEntries) {
		// we are out of stock, cache em up
		fCurrentDirent = fDirentBuffer;
		int32 bufferRemain = kDirentBufferSize;
		for (fNumEntries = 0; fNumEntries < fCacheSize; ) {
			int32 count = fIterator->GetNextDirents(fCurrentDirent,
				bufferRemain, 1);

			if (count <= 0)
				break;

			fNumEntries += count;

			int32 currentDirentSize = fCurrentDirent->d_reclen;
			bufferRemain -= currentDirentSize;
			ASSERT(bufferRemain >= 0);

			if ((size_t)bufferRemain
					< (offsetof(struct dirent, d_name) + B_FILE_NAME_LENGTH)) {
				// cant fit a big entryRef in the buffer, just bail
				// and start from scratch
				break;
			}

			fCurrentDirent
				= (dirent*)((char*)fCurrentDirent + currentDirentSize);
		}
		fCurrentDirent = fDirentBuffer;
		if (fSortInodes) {
			if (!fSortedList)
				fSortedList = new BObjectList<dirent>(fCacheSize);
			else
				fSortedList->MakeEmpty();

			for (int32 count = 0; count < fNumEntries; count++) {
				fSortedList->AddItem(fCurrentDirent, 0);
				fCurrentDirent = Next(fCurrentDirent);
			}
			fSortedList->SortItems(&_CompareInodes);
			fCurrentDirent = fDirentBuffer;
		}
		fIndex = 0;
	}
	if (fIndex >= fNumEntries) {
		// we are done, no more dirents left
		return 0;
	}

	if (fSortInodes)
		fCurrentDirent = fSortedList->ItemAt(fIndex);

	fIndex++;
	uint32 currentDirentSize = fCurrentDirent->d_reclen;
	ASSERT(currentDirentSize <= size);
	if (currentDirentSize > size)
		return 0;

	memcpy(ent, fCurrentDirent, currentDirentSize);

	if (!fSortInodes)
		fCurrentDirent = (dirent*)((char*)fCurrentDirent + currentDirentSize);

	return 1;
}


/**
 * @brief Reset the cache and rewind the underlying iterator.
 *
 * @return The return value of the underlying iterator's Rewind().
 */
status_t
CachedEntryIterator::Rewind()
{
	fIndex = 0;
	fNumEntries = 0;
	fCurrentDirent = NULL;
	fStatus = B_OK;

	delete fSortedList;
	fSortedList = NULL;

	return fIterator->Rewind();
}


/**
 * @brief Return the total entry count from the underlying iterator.
 *
 * @return Total number of entries in the directory.
 */
int32
CachedEntryIterator::CountEntries()
{
	return fIterator->CountEntries();
}


/**
 * @brief Replace the underlying iterator and reset the cache state.
 *
 * @param iterator  The new BEntryList to iterate over.
 */
void
CachedEntryIterator::SetTo(BEntryList* iterator)
{
	fIndex = 0;
	fNumEntries = 0;
	fStatus = B_OK;
	fIterator = iterator;
}


//	#pragma mark - CachedDirectoryEntryList


/**
 * @brief Construct a cached iterator directly over @a directory.
 *
 * @param directory  The directory whose entries will be read with inode sorting.
 */
CachedDirectoryEntryList::CachedDirectoryEntryList(const BDirectory& directory)
	:
	CachedEntryIterator(0, 40, true),
	fDirectory(directory)
{
	fStatus = fDirectory.InitCheck();
	SetTo(&fDirectory);
}


/**
 * @brief Destructor.
 */
CachedDirectoryEntryList::~CachedDirectoryEntryList()
{
}


//	#pragma mark - DirectoryEntryList


/**
 * @brief Construct a thin wrapper over a BDirectory for uncached iteration.
 *
 * @param directory  The directory whose entries will be iterated.
 */
DirectoryEntryList::DirectoryEntryList(const BDirectory& directory)
	:
	fDirectory(directory)
{
	fStatus = fDirectory.InitCheck();
}


/**
 * @brief Retrieve the next BEntry from the wrapped BDirectory.
 *
 * @param entry     Output BEntry.
 * @param traverse  Whether to traverse symlinks.
 * @return B_OK on success, B_ENTRY_NOT_FOUND at end.
 */
status_t
DirectoryEntryList::GetNextEntry(BEntry* entry, bool traverse)
{
	fStatus = fDirectory.GetNextEntry(entry, traverse);
	return fStatus;
}


/**
 * @brief Retrieve the next entry_ref from the wrapped BDirectory.
 *
 * @param ref  Output entry_ref.
 * @return B_OK on success, B_ENTRY_NOT_FOUND at end.
 */
status_t
DirectoryEntryList::GetNextRef(entry_ref* ref)
{
	fStatus = fDirectory.GetNextRef(ref);
	return fStatus;
}


/**
 * @brief Retrieve dirents from the wrapped BDirectory.
 *
 * @param buffer  Destination buffer.
 * @param length  Size of @a buffer in bytes.
 * @param count   Number of entries requested.
 * @return Number of entries written, or error code.
 */
int32
DirectoryEntryList::GetNextDirents(struct dirent* buffer, size_t length,
	int32 count)
{
	fStatus = fDirectory.GetNextDirents(buffer, length, count);
	return fStatus;
}


/**
 * @brief Rewind the wrapped BDirectory to the first entry.
 *
 * @return B_OK on success, or an error code.
 */
status_t
DirectoryEntryList::Rewind()
{
	fStatus = fDirectory.Rewind();
	return fStatus;
}


/**
 * @brief Return the number of entries in the wrapped BDirectory.
 *
 * @return Entry count.
 */
int32
DirectoryEntryList::CountEntries()
{
	return fDirectory.CountEntries();
}


//	#pragma mark - EntryIteratorList


/**
 * @brief Construct an empty list of entry iterators.
 */
EntryIteratorList::EntryIteratorList()
	:
	fList(5),
	fCurrentIndex(0)
{
}


/**
 * @brief Destructor; deletes all owned BEntryList entries.
 */
EntryIteratorList::~EntryIteratorList()
{
	int32 count = fList.CountItems();
	for (;count; count--) {
		// workaround for BEntryList not having a proper destructor
		BEntryList* entry = fList.RemoveItemAt(count - 1);
		EntryListBase* fixedEntry = dynamic_cast<EntryListBase*>(entry);
		if (fixedEntry != NULL)
			delete fixedEntry;
		else
			delete entry;
	}
}


/**
 * @brief Append @a walker to the list of iterators.
 *
 * @param walker  A heap-allocated BEntryList; ownership is transferred.
 */
void
EntryIteratorList::AddItem(BEntryList* walker)
{
	fList.AddItem(walker);
}


/**
 * @brief Retrieve the next BEntry, advancing through iterators as each is exhausted.
 *
 * @param entry     Output BEntry.
 * @param traverse  Whether to traverse symlinks.
 * @return B_OK on success, B_ENTRY_NOT_FOUND when all iterators are exhausted.
 */
status_t
EntryIteratorList::GetNextEntry(BEntry* entry, bool traverse)
{
	while (true) {
		if (fCurrentIndex >= fList.CountItems()) {
			fStatus = B_ENTRY_NOT_FOUND;
			break;
		}

		fStatus = fList.ItemAt(fCurrentIndex)->GetNextEntry(entry, traverse);
		if (fStatus != B_ENTRY_NOT_FOUND)
			break;

		fCurrentIndex++;
	}
	return fStatus;
}


/**
 * @brief Retrieve the next entry_ref, advancing through iterators as each is exhausted.
 *
 * @param ref  Output entry_ref.
 * @return B_OK on success, B_ENTRY_NOT_FOUND when all iterators are exhausted.
 */
status_t
EntryIteratorList::GetNextRef(entry_ref* ref)
{
	while (true) {
		if (fCurrentIndex >= fList.CountItems()) {
			fStatus = B_ENTRY_NOT_FOUND;
			break;
		}

		fStatus = fList.ItemAt(fCurrentIndex)->GetNextRef(ref);
		if (fStatus != B_ENTRY_NOT_FOUND)
			break;

		fCurrentIndex++;
	}
	return fStatus;
}


/**
 * @brief Retrieve dirents, advancing through iterators as each is exhausted.
 *
 * @param buffer  Destination buffer.
 * @param length  Size of @a buffer in bytes.
 * @param count   Number of entries requested.
 * @return Number of entries written.
 */
int32
EntryIteratorList::GetNextDirents(struct dirent* buffer, size_t length,
	int32 count)
{
	int32 result = 0;
	while (true) {
		if (fCurrentIndex >= fList.CountItems()) {
			fStatus = B_ENTRY_NOT_FOUND;
			break;
		}

		result = fList.ItemAt(fCurrentIndex)->GetNextDirents(buffer, length,
			count);
		if (result > 0) {
			fStatus = B_OK;
			break;
		}

		fCurrentIndex++;
	}
	return result;
}


/**
 * @brief Rewind all contained iterators and reset the current-index cursor.
 *
 * @return The status of the last Rewind() call made across all iterators.
 */
status_t
EntryIteratorList::Rewind()
{
	fCurrentIndex = 0;
	int32 count = fList.CountItems();
	for (int32 index = 0; index < count; index++)
		fStatus = fList.ItemAt(index)->Rewind();

	return fStatus;
}


/**
 * @brief Return the sum of entries across all contained iterators.
 *
 * @return Total entry count.
 */
int32
EntryIteratorList::CountEntries()
{
	int32 result = 0;

	int32 count = fList.CountItems();
	for (int32 index = 0; index < count; index++)
		result += fList.ItemAt(fCurrentIndex)->CountEntries();

	return result;
}


//	#pragma mark - CachedEntryIteratorList


/**
 * @brief Construct a cached iterator list, optionally sorting batches by inode.
 *
 * @param sortInodes  If true, each dirent batch is sorted by inode number.
 */
CachedEntryIteratorList::CachedEntryIteratorList(bool sortInodes)
	:
	CachedEntryIterator(NULL, 10, sortInodes)
{
	fStatus = B_OK;
	SetTo(&fIteratorList);
}


/**
 * @brief Add @a walker to the underlying EntryIteratorList source.
 *
 * @param walker  A heap-allocated BEntryList; ownership is transferred.
 */
void
CachedEntryIteratorList::AddItem(BEntryList* walker)
{
	fIteratorList.AddItem(walker);
}
