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
 *   Copyright 2008-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file EntryCache.h
 *  @brief Per-mount cache mapping (directory, name) tuples to inode numbers. */

#ifndef ENTRY_CACHE_H
#define ENTRY_CACHE_H


#include <stdlib.h>

#include <util/AutoLock.h>
#include <util/AtomicsHashTable.h>
#include <util/DoublyLinkedList.h>
#include <util/StringHash.h>


/** @brief Lookup key into the entry cache: parent directory id plus a name. */
struct EntryCacheKey {
	/** @brief Builds a key, computing the hash up front so the cache lookup is lock-free.
	 *  @param dirID The parent directory inode.
	 *  @param name  The name being looked up (not copied).
	 *  @param _hash Optional pre-computed hash; if NULL, the hash is recomputed. */
	EntryCacheKey(ino_t dirID, const char* name, const uint32* _hash = NULL)
		:
		dir_id(dirID),
		name(name)
	{
		if (_hash == NULL) {
			// We cache the hash value, so we compute it before holding any locks.
			hash = Hash(dirID, name);
		} else {
			hash = *_hash;
		}
	}

	/** @brief Hash function used by the entry cache hash table. */
	static uint32 Hash(ino_t dirID, const char* name)
	{
		return (uint32)dirID ^ (uint32)(dirID >> 32) ^ hash_hash_string(name);
	}

	ino_t		dir_id;  /**< Parent directory inode. */
	const char*	name;    /**< Entry name (not owned by the key). */
	uint32		hash;    /**< Cached hash of (dir_id, name). */
};


/** @brief Entry cache record stored inline in a flexible-array tail. */
struct EntryCacheEntry {
	EntryCacheEntry*	hash_link;   /**< Next entry in the same hash bucket. */
	ino_t				node_id;     /**< Inode number this name resolves to. */
	ino_t				dir_id;      /**< Parent directory inode. */
	uint32				hash;        /**< Cached hash, must match EntryCacheKey::Hash(dir_id, name). */
	int32				generation;  /**< Generation index this entry currently lives in. */
	int32				index;       /**< Slot inside that generation. */
	bool				missing;     /**< True if this is a negative entry (lookup miss). */
	char				name[1];     /**< Inline NUL-terminated name (flexible-array tail). */
};


/** @brief A single ring of cached entries; used for time-bounded eviction. */
struct EntryCacheGeneration {
			int32				next_index;    /**< Next free slot in @c entries. */
			int32				entries_size;  /**< Capacity of @c entries. */
			EntryCacheEntry**	entries;       /**< Slots referencing live entries in this generation. */

								EntryCacheGeneration();
								~EntryCacheGeneration();

	/** @brief Allocates the per-generation slot array.
	 *  @param entriesSize Number of slots in this generation. */
			status_t			Init(int32 entriesSize);
};


struct EntryCacheHashDefinition {
	typedef EntryCacheKey	KeyType;
	typedef EntryCacheEntry	ValueType;

	uint32 HashKey(const EntryCacheKey& key) const
	{
		return key.hash;
	}

	uint32 Hash(const EntryCacheEntry* value) const
	{
		return value->hash;
	}

	EntryCacheKey Key(const EntryCacheEntry* value) const
	{
		return EntryCacheKey(value->dir_id, value->name, &value->hash);
	}

	bool Compare(const EntryCacheKey& key, const EntryCacheEntry* value) const
	{
		if (key.hash != value->hash)
			return false;
		return value->dir_id == key.dir_id
			&& strcmp(value->name, key.name) == 0;
	}

	EntryCacheEntry*& GetLink(EntryCacheEntry* value) const
	{
		return value->hash_link;
	}
};


/** @brief Per-mount lookup cache for directory entries.
 *
 * Stores both positive (name → inode) and negative (name → "missing") results
 * keyed on (parent inode, name). Entries are organised into a small ring of
 * generations: the newest writes always go into the current generation, and
 * older generations expire as a unit, giving the cache an approximate
 * second-chance LRU policy without per-entry timestamps. */
class EntryCache {
public:
								EntryCache();
								~EntryCache();

	/** @brief Allocates the hash table and the generation ring. */
			status_t			Init();

	/** @brief Inserts (or refreshes) an entry in the cache.
	 *  @param dirID   Parent directory inode.
	 *  @param name    Entry name (copied into the cache).
	 *  @param nodeID  Inode number to associate with the name.
	 *  @param missing If true, store this as a negative cache entry.
	 *  @return B_OK on success, or an error code on failure. */
			status_t			Add(ino_t dirID, const char* name,
									ino_t nodeID, bool missing);

	/** @brief Removes the entry for (@p dirID, @p name) from the cache. */
			status_t			Remove(ino_t dirID, const char* name);

	/** @brief Looks up (@p dirID, @p name) in the cache.
	 *  @param nodeID  On hit, set to the cached inode id.
	 *  @param missing On hit, set to true if the cached result is negative.
	 *  @return True if the cache had a record for the requested key. */
			bool				Lookup(ino_t dirID, const char* name,
									ino_t& nodeID, bool& missing);

	/** @brief Debugger-only reverse lookup from inode to (name, dirID). */
			const char*			DebugReverseLookup(ino_t nodeID, ino_t& _dirID);

private:
			typedef AtomicsHashTable<EntryCacheHashDefinition> EntryTable;
			typedef DoublyLinkedList<EntryCacheEntry> EntryList;

private:
			bool				_AddEntryToCurrentGeneration(
									EntryCacheEntry* entry, bool move);

private:
			rw_lock				fLock;               /**< Reader/writer lock protecting the table and generations. */
			EntryTable			fEntries;            /**< Hash table mapping keys to entries. */
			int32				fGenerationCount;    /**< Number of generations in the ring. */
			EntryCacheGeneration* fGenerations;      /**< Ring of generations holding entries. */
			int32				fCurrentGeneration;  /**< Index of the generation currently accepting new inserts. */
};


#endif	// ENTRY_CACHE_H
