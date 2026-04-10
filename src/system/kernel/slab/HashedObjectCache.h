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
 *   Copyright 2008, Axel Dörfler. All Rights Reserved.
 *   Copyright 2007, Hugo Santos. All Rights Reserved.
 *
 *   Distributed under the terms of the MIT License.
 */

/** @file HashedObjectCache.h
 *  @brief ObjectCache implementation that locates slabs through a hash table. */

#ifndef HASHED_OBJECT_CACHE_H
#define HASHED_OBJECT_CACHE_H


#include <util/OpenHashTable.h>

#include "ObjectCache.h"
#include "slab_private.h"


/** @brief Slab descriptor used by HashedObjectCache. */
struct HashedSlab : slab {
	HashedSlab*	hash_next;  /**< Next slab in the same hash bucket. */
};


/** @brief ObjectCache for objects whose owning slab cannot be derived from the address.
 *
 * Used for caches whose objects do not live inside their slab's page run
 * (large or non-contiguous allocations). The cache maintains a hash table
 * keyed on the slab's page address so ObjectSlab() can recover the owning
 * slab from any object pointer in O(1). */
struct HashedObjectCache final : ObjectCache {
								HashedObjectCache();

	/** @brief Allocates and initialises a HashedObjectCache. See ObjectCache::Init(). */
	static	HashedObjectCache*	Create(const char* name, size_t object_size,
									size_t alignment, size_t maximum,
									size_t magazineCapacity,
									size_t maxMagazineCount,
									uint32 flags, void* cookie,
									object_cache_constructor constructor,
									object_cache_destructor destructor,
									object_cache_reclaimer reclaimer);
	/** @brief Tears down the cache and frees the underlying memory. */
	virtual	void				Delete();

	/** @brief Allocates a fresh slab and inserts it into the hash table. */
	virtual	slab*				CreateSlab(uint32 flags);
	/** @brief Removes @p slab from the hash table and frees it. */
	virtual	void				ReturnSlab(slab* slab, uint32 flags);
	/** @brief Looks @p object up in the hash table and returns its owning slab. */
	virtual slab*				ObjectSlab(void* object) const;

private:
			struct Definition {
				typedef HashedObjectCache	ParentType;
				typedef const void*			KeyType;
				typedef HashedSlab			ValueType;

				Definition(HashedObjectCache* parent)
					:
					parent(parent)
				{
				}

				Definition(const Definition& definition)
					:
					parent(definition.parent)
				{
				}

				size_t HashKey(const void* key) const
				{
					return (addr_t)::lower_boundary(key, parent->slab_size)
						>> parent->lower_boundary;
				}

				size_t Hash(HashedSlab* value) const
				{
					return HashKey(value->pages);
				}

				bool Compare(const void* key, HashedSlab* value) const
				{
					return value->pages == key;
				}

				HashedSlab*& GetLink(HashedSlab* value) const
				{
					return value->hash_next;
				}

				HashedObjectCache*	parent;
			};

			struct InternalAllocator {
				void* Allocate(size_t size) const
				{
					return slab_internal_alloc(size, 0);
				}

				void Free(void* memory) const
				{
					slab_internal_free(memory, 0);
				}
			};

			typedef BOpenHashTable<Definition, false, false,
				InternalAllocator> HashTable;

			friend struct Definition;

private:
			void				_ResizeHashTableIfNeeded(uint32 flags);

private:
			HashTable hash_table;
			size_t lower_boundary;
};



#endif	// HASHED_OBJECT_CACHE_H
