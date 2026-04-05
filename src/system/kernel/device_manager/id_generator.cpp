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
 *   Copyright 2004-2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Copyright 2002-2004, Thomas Kurschel. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file id_generator.cpp
 * @brief Monotonically-increasing ID generator for device and resource objects.
 */


#include "id_generator.h"

#include <new>
#include <stdlib.h>
#include <string.h>

#include <KernelExport.h>

#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>


//#define TRACE_ID_GENERATOR
#ifdef TRACE_ID_GENERATOR
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

#define GENERATOR_MAX_ID 64

struct id_generator : DoublyLinkedListLinkImpl<id_generator> {
	id_generator(const char* name)
		:
		ref_count(1),
		name(strdup(name)),
		num_ids(0)
	{
		memset(&alloc_map, 0, sizeof(alloc_map));
	}

	~id_generator()
	{
		free(name);
	}

	int32		ref_count;
	char*		name;
	uint32		num_ids;
	uint8		alloc_map[(GENERATOR_MAX_ID + 7) / 8];
};

typedef DoublyLinkedList<id_generator> GeneratorList;


static GeneratorList sGenerators;
static mutex sLock = MUTEX_INITIALIZER("id generator");


/**
 * @brief Allocates and inserts a new named ID generator into the global list.
 *
 * Creates a new id_generator for the given @p name and adds it to
 * sGenerators. The caller must hold sLock.
 *
 * @param name  The name to associate with the new generator.
 * @return Pointer to the newly created id_generator, or @c NULL on
 *         allocation failure.
 */
static id_generator*
create_generator(const char* name)
{
	TRACE(("create_generator(name: %s)\n", name));

	id_generator* generator = new(std::nothrow) id_generator(name);
	if (generator == NULL)
		return NULL;

	if (generator->name == NULL) {
		delete generator;
		return NULL;
	}

	sGenerators.Add(generator);
	return generator;
}


/**
 * @brief Allocates the next free ID slot from a generator's bitmap.
 *
 * Scans the generator's @c alloc_map for the first unset bit, marks it,
 * increments the ID count, and returns the corresponding ID. Acquires
 * sLock internally.
 *
 * @param generator  The generator whose bitmap is searched.
 * @return The newly allocated ID (0 .. GENERATOR_MAX_ID-1), or
 *         @c B_ERROR if all slots are exhausted.
 */
static int32
create_id_internal(id_generator* generator)
{
	uint32 id;

	TRACE(("create_id_internal(name: %s)\n", generator->name));

	// see above: we use global instead of local lock
	MutexLocker _(sLock);

	// simple bit search
	for (id = 0; id < GENERATOR_MAX_ID; ++id) {
		if ((generator->alloc_map[id / 8] & (1 << (id & 7))) == 0) {
			TRACE(("  id: %lu\n", id));

			generator->alloc_map[id / 8] |= 1 << (id & 7);
			generator->num_ids++;

			return id;
		}
	}

	return B_ERROR;
}


/**
 * @brief Looks up a generator by name and increments its reference count.
 *
 * Walks the sGenerators list for an entry whose name matches @p name.
 * When found, bumps ref_count so the generator remains live until
 * release_generator() is called. The caller must hold sLock.
 *
 * @param name  The generator name to search for.
 * @return Pointer to the matching id_generator with an elevated reference
 *         count, or @c NULL if no generator with that name exists.
 */
static id_generator*
get_generator(const char* name)
{
	TRACE(("find_generator(name: %s)\n", name));

	GeneratorList::Iterator iterator = sGenerators.GetIterator();
	while (iterator.HasNext()) {
		id_generator* generator = iterator.Next();

		if (!strcmp(generator->name, name)) {
			// increase ref_count, so it won't go away
			generator->ref_count++;
			return generator;
		}
	}

	return NULL;
}


/**
 * @brief Decrements a generator's reference count, destroying it when unused.
 *
 * Acquires sLock, decrements ref_count, and — if both ref_count and
 * num_ids reach zero — removes the generator from sGenerators and
 * deletes it.
 *
 * @param generator  The generator whose reference is being released.
 */
static void
release_generator(id_generator *generator)
{
	TRACE(("release_generator(name: %s)\n", generator->name));

	MutexLocker _(sLock);

	if (--generator->ref_count == 0) {
		// no one messes with generator
		if (generator->num_ids == 0) {
			TRACE(("  Destroy %s\n", generator->name));
			// no IDs is allocated - destroy generator
			sGenerators.Remove(generator);
			delete generator;
		}
	}
}


//	#pragma mark - Private kernel API


/**
 * @brief Initialises the global ID-generator subsystem.
 *
 * Placement-constructs the sGenerators list. Must be called once during
 * device-manager initialisation before any other dm_*_id functions.
 */
void
dm_init_id_generator(void)
{
	new(&sGenerators) GeneratorList;
}


//	#pragma mark - Public module API


/**
 * @brief Allocates a unique ID from the named generator, creating it if needed.
 *
 * Looks up (or lazily creates) the id_generator for @p name, allocates
 * the next free slot from its bitmap, and releases the generator reference
 * before returning.
 *
 * @param name  The generator namespace to allocate from.
 * @return The newly allocated ID (>= 0) on success, or @c B_NO_MEMORY if
 *         the generator could not be created, or @c B_ERROR if all IDs are
 *         exhausted.
 */
int32
dm_create_id(const char* name)
{

	// find generator, create new if not there
	mutex_lock(&sLock);

	id_generator* generator = get_generator(name);
	if (generator == NULL)
		generator = create_generator(name);

	mutex_unlock(&sLock);

	if (generator == NULL)
		return B_NO_MEMORY;

	// get ID
	int32 id = create_id_internal(generator);

	release_generator(generator);

	TRACE(("dm_create_id: name: %s, id: %ld\n", name, id));
	return id;
}


/**
 * @brief Returns a previously allocated ID to the named generator.
 *
 * Looks up the id_generator for @p name, verifies that @p id is actually
 * marked as allocated, clears the corresponding bitmap bit, decrements
 * num_ids, and releases the generator reference.
 *
 * @param name  The generator namespace the ID belongs to.
 * @param id    The ID to free; must be a value previously returned by
 *              dm_create_id() for the same @p name.
 * @retval B_OK              The ID was freed successfully.
 * @retval B_NAME_NOT_FOUND  No generator with the given @p name exists.
 * @retval B_BAD_VALUE       @p id was not marked as allocated in the generator.
 */
status_t
dm_free_id(const char* name, uint32 id)
{
	TRACE(("dm_free_id(name: %s, id: %ld)\n", name, id));

	// find generator
	mutex_lock(&sLock);

	id_generator* generator = get_generator(name);

	mutex_unlock(&sLock);

	if (generator == NULL) {
		TRACE(("  Generator %s doesn't exist\n", name));
		return B_NAME_NOT_FOUND;
	}

	// free ID

	// make sure it's really allocated
	// (very important to keep <num_ids> in sync
	if ((generator->alloc_map[id / 8] & (1 << (id & 7))) == 0) {
		dprintf("id %" B_PRIu32 " of generator %s wasn't allocated\n", id,
			generator->name);

		release_generator(generator);
		return B_BAD_VALUE;
	}

	generator->alloc_map[id / 8] &= ~(1 << (id & 7));
	generator->num_ids--;

	release_generator(generator);
	return B_OK;
}
