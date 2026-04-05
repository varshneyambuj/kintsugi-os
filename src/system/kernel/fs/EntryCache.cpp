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
 *   Copyright 2008-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file EntryCache.cpp
 * @brief VFS directory entry cache — maps (directory vnode, name) pairs to child vnode IDs for fast lookup.
 */


#include "EntryCache.h"

#include <new>
#include <vm/vm.h>
#include <slab/Slab.h>


static const int32 kEntryNotInArray = -1;
static const int32 kEntryRemoved = -2;


// #pragma mark - EntryCacheGeneration


/** @brief Default constructor for EntryCacheGeneration.
 *
 * Initialises the generation slot with a zero next_index and a NULL entries
 * pointer.  The generation is not usable until Init() has been called.
 */
EntryCacheGeneration::EntryCacheGeneration()
	:
	next_index(0),
	entries(NULL)
{
}


/** @brief Destructor for EntryCacheGeneration.
 *
 * Releases the heap-allocated entries array that was allocated by Init().
 */
EntryCacheGeneration::~EntryCacheGeneration()
{
	delete[] entries;
}


/** @brief Allocate and zero-initialise the entries array for this generation.
 *
 * @param entriesSize  Number of EntryCacheEntry pointer slots to allocate.
 * @return B_OK on success, or B_NO_MEMORY if the allocation fails.
 */
status_t
EntryCacheGeneration::Init(int32 entriesSize)
{
	entries_size = entriesSize;
	entries = new(std::nothrow) EntryCacheEntry*[entries_size];
	if (entries == NULL)
		return B_NO_MEMORY;

	memset(entries, 0, sizeof(EntryCacheEntry*) * entries_size);
	return B_OK;
}


// #pragma mark - EntryCache


/** @brief Default constructor for EntryCache.
 *
 * Sets the generation count to zero and initialises the reader/writer lock.
 * Call Init() before using any other method.
 */
EntryCache::EntryCache()
	:
	fGenerationCount(0),
	fGenerations(NULL),
	fCurrentGeneration(0)
{
	rw_lock_init(&fLock, "entry cache");

	new(&fEntries) EntryTable;
}


/** @brief Destructor for EntryCache.
 *
 * Drains the hash table, frees every cached entry, releases the generations
 * array, and destroys the reader/writer lock.
 */
EntryCache::~EntryCache()
{
	// delete entries
	EntryCacheEntry* entry = fEntries.Clear(true);
	while (entry != NULL) {
		EntryCacheEntry* next = entry->hash_link;
		free(entry);
		entry = next;
	}
	delete[] fGenerations;

	rw_lock_destroy(&fLock);
}


/** @brief Initialise the entry cache, allocating generation rings.
 *
 * Chooses the number of generations and their sizes based on the amount of
 * available physical memory, then allocates and zeroes all generation arrays.
 *
 * @return B_OK on success, or an error code if any allocation fails.
 */
status_t
EntryCache::Init()
{
	WriteLocker locker(fLock);
	ASSERT(fGenerationCount == 0);

	status_t error = fEntries.Init();
	if (error != B_OK)
		return error;

	int32 entriesSize = 1024;
	fGenerationCount = 8;

	// TODO: Choose generation size/count more scientifically?
	// TODO: Add low_resource handler hook?
	if (vm_available_memory() >= (1024*1024*1024)) {
		entriesSize = 8192;
		fGenerationCount = 16;
	}

	fGenerations = new(std::nothrow) EntryCacheGeneration[fGenerationCount];
	for (int32 i = 0; i < fGenerationCount; i++) {
		error = fGenerations[i].Init(entriesSize);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/** @brief Insert or update a (dirID, name) -> nodeID mapping in the cache.
 *
 * If an entry for the given key already exists its node_id and missing flag
 * are updated in place; otherwise a new entry is allocated and inserted.
 * The entry is also promoted to the current generation ring slot.
 *
 * @param dirID    Inode number of the directory that contains the entry.
 * @param name     Name of the directory entry (NUL-terminated).
 * @param nodeID   Inode number of the child node.
 * @param missing  @c true when the entry is a negative cache hit (the name
 *                 is known not to exist in the directory).
 * @return B_OK on success, or B_NO_MEMORY if allocation fails.
 */
status_t
EntryCache::Add(ino_t dirID, const char* name, ino_t nodeID, bool missing)
{
	EntryCacheKey key(dirID, name);

	const size_t nameLen = strlen(name);
	EntryCacheEntry* entry = (EntryCacheEntry*)malloc(sizeof(EntryCacheEntry) + nameLen);

	if (entry == NULL)
		return B_NO_MEMORY;

	entry->node_id = nodeID;
	entry->dir_id = dirID;
	entry->hash = key.hash;
	entry->missing = missing;
	entry->index = kEntryNotInArray;
	entry->generation = -1;
	memcpy(entry->name, name, nameLen + 1);

	ReadLocker readLocker(fLock);

	if (fGenerationCount == 0) {
		free(entry);
		return B_NO_MEMORY;
	}

	EntryCacheEntry* existingEntry = fEntries.InsertAtomic(entry);
	if (existingEntry != NULL) {
		free(entry);
		entry = existingEntry;

		entry->node_id = nodeID;
		entry->missing = missing;
	}

	readLocker.Detach();
	_AddEntryToCurrentGeneration(entry, entry == existingEntry);
	return B_OK;
}


/** @brief Remove the cache entry for a given (dirID, name) pair.
 *
 * Looks up the entry in the hash table, removes it, and — if the entry is not
 * currently being moved between generations by another thread — frees it
 * immediately.  If another thread holds a reference, the entry is marked with
 * @c kEntryRemoved and the other thread will free it.
 *
 * @param dirID  Inode number of the directory that contains the entry.
 * @param name   Name of the directory entry (NUL-terminated).
 * @return B_OK if the entry was found and removed, B_ENTRY_NOT_FOUND otherwise.
 */
status_t
EntryCache::Remove(ino_t dirID, const char* name)
{
	EntryCacheKey key(dirID, name);

	WriteLocker writeLocker(fLock);

	EntryCacheEntry* entry = fEntries.Lookup(key);
	if (entry == NULL)
		return B_ENTRY_NOT_FOUND;

	fEntries.Remove(entry);

	if (entry->index >= 0) {
		// remove the entry from its generation and delete it
		fGenerations[entry->generation].entries[entry->index] = NULL;
		writeLocker.Unlock();
		free(entry);
	} else {
		// We can't free it, since another thread is waiting to try to move it
		// to another generation. We mark it removed and the other thread will
		// take care of deleting it.
		entry->index = kEntryRemoved;
	}

	return B_OK;
}


/** @brief Look up a (dirID, name) pair and return the associated vnode ID.
 *
 * Searches the hash table for the given key.  On a cache hit the entry is
 * promoted to the current generation ring so that it ages out less quickly.
 *
 * @param dirID    Inode number of the directory to search in.
 * @param name     Name to look up (NUL-terminated).
 * @param _nodeID  On success, receives the inode number of the child node.
 * @param _missing On success, receives @c true if this is a negative cache hit.
 * @return @c true on a cache hit, @c false if the entry is not cached.
 */
bool
EntryCache::Lookup(ino_t dirID, const char* name, ino_t& _nodeID,
	bool& _missing)
{
	EntryCacheKey key(dirID, name);

	ReadLocker readLocker(fLock);

	EntryCacheEntry* entry = fEntries.Lookup(key);
	if (entry == NULL)
		return false;

	_nodeID = entry->node_id;
	_missing = entry->missing;

	readLocker.Detach();
	return _AddEntryToCurrentGeneration(entry, true);
}


/** @brief Reverse-lookup: find an entry whose node_id matches the given inode.
 *
 * Iterates the entire hash table looking for a non-dot/dotdot entry with a
 * matching node_id.  This is intended for kernel debugger use only and is
 * not lock-protected.
 *
 * @param nodeID  Inode number to search for.
 * @param _dirID  On success, receives the inode number of the parent directory.
 * @return Pointer to the cached name string on success, or NULL if not found.
 */
const char*
EntryCache::DebugReverseLookup(ino_t nodeID, ino_t& _dirID)
{
	for (EntryTable::Iterator it = fEntries.GetIterator();
			EntryCacheEntry* entry = it.Next();) {
		if (nodeID == entry->node_id && strcmp(entry->name, ".") != 0
				&& strcmp(entry->name, "..") != 0) {
			_dirID = entry->dir_id;
			return entry->name;
		}
	}

	return NULL;
}


/** @brief Place (or move) an entry into the current generation ring slot.
 *
 * If @p move is @c true the entry is relocated from its current generation
 * to the current one; otherwise it is simply inserted for the first time.
 * When the current generation ring is full the oldest generation is evicted
 * and its entries are freed.
 *
 * The caller must already hold the read lock (or the lock must have been
 * detached to this thread) before calling this function; the function may
 * temporarily upgrade to a write lock.
 *
 * @param entry  Pointer to the cache entry to place.
 * @param move   @c true if the entry already lives in another generation and
 *               must be moved, @c false if it is being inserted for the first
 *               time.
 * @return @c true if the entry is alive after the call, @c false if it was
 *         found to have been concurrently removed and has been freed.
 */
bool
EntryCache::_AddEntryToCurrentGeneration(EntryCacheEntry* entry, bool move)
{
	ReadLocker readLocker(fLock, true);

	if (move) {
		const int32 oldGeneration = atomic_get_and_set(&entry->generation,
			fCurrentGeneration);
		if (oldGeneration == fCurrentGeneration || entry->index < 0) {
			// The entry is already in the current generation or is being moved to
			// it by another thread.
			return true;
		}

		// remove from old generation array
		fGenerations[oldGeneration].entries[entry->index] = NULL;
		entry->index = kEntryNotInArray;
	} else {
		entry->generation = fCurrentGeneration;
	}

	// add to the current generation
	int32 index = atomic_add(&fGenerations[fCurrentGeneration].next_index, 1);
	if (index < fGenerations[fCurrentGeneration].entries_size) {
		fGenerations[fCurrentGeneration].entries[index] = entry;
		entry->index = index;
		return true;
	}

	// The current generation is full, so the oldest one needs to be cleared
	// in order to make room. The write lock is needed for that.
	readLocker.Unlock();
	WriteLocker writeLocker(fLock);

	// Resize the table if needed, no matter what. (The only other place it can
	// be resized is Remove(), so this is important.)
	fEntries.ResizeIfNeeded();

	if (entry->index == kEntryRemoved) {
		// The entry was removed while we were waiting. Nothing else has a
		// reference to it at this point besides us, so we free it.
		writeLocker.Unlock();
		free(entry);
		return false;
	}

	index = fGenerations[fCurrentGeneration].next_index++;
	if (index < fGenerations[fCurrentGeneration].entries_size) {
		// the current generation has already been changed
		fGenerations[fCurrentGeneration].entries[index] = entry;
		entry->generation = fCurrentGeneration;
		entry->index = index;
		return true;
	}

	// We have to clear the oldest generation.
	EntryCacheEntry* entriesToFree = NULL;
	const int32 newGeneration = (fCurrentGeneration + 1) % fGenerationCount;
	for (int32 i = 0; i < fGenerations[newGeneration].entries_size; i++) {
		EntryCacheEntry* otherEntry = fGenerations[newGeneration].entries[i];
		if (otherEntry == NULL)
			continue;

		fGenerations[newGeneration].entries[i] = NULL;
		fEntries.RemoveUnchecked(otherEntry);

		otherEntry->hash_link = entriesToFree;
		entriesToFree = otherEntry;
	}

	// set the new generation and add the entry
	fCurrentGeneration = newGeneration;
	fGenerations[newGeneration].entries[0] = entry;
	fGenerations[newGeneration].next_index = 1;
	entry->generation = newGeneration;
	entry->index = 0;

	// free the old entries
	writeLocker.Unlock();
	while (entriesToFree != NULL) {
		EntryCacheEntry* next = entriesToFree->hash_link;
		free(entriesToFree);
		entriesToFree = next;
	}

	return true;
}
