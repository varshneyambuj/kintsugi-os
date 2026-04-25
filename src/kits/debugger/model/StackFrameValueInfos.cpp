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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file StackFrameValueInfos.cpp
 * @brief Implementation of StackFrameValueInfos, a hash table mapping
 *        (variable, type-component path) keys to (Type, ValueLocation) pairs.
 *
 * Where StackFrameValues caches the resolved value, StackFrameValueInfos
 * caches the resolved type and storage location used to compute that
 * value, so the inspector can re-render or re-fetch members without
 * walking the DWARF tree again.
 */


#include "StackFrameValueInfos.h"

#include <new>

#include "FunctionID.h"
#include "Type.h"
#include "TypeComponentPath.h"
#include "ValueLocation.h"


/**
 * @brief Composite (variable id, type-component path) lookup key.
 */
struct StackFrameValueInfos::Key {
	ObjectID*			variable;
	TypeComponentPath*	path;

	/**
	 * @brief Constructs a Key wrapping @a variable and @a path by reference.
	 *
	 * @param variable Owning variable identity.
	 * @param path     Component path inside @a variable.
	 */
	Key(ObjectID* variable, TypeComponentPath* path)
		:
		variable(variable),
		path(path)
	{
	}

	/**
	 * @brief Combines the per-component hashes via XOR.
	 *
	 * @return Hash value identifying the key.
	 */
	uint32 HashValue() const
	{
		return variable->HashValue() ^ path->HashValue();
	}

	/**
	 * @brief Tests two keys for equality on both components.
	 *
	 * @param other Other key.
	 * @return     True if both variables and both paths compare equal.
	 */
	bool operator==(const Key& other) const
	{
		return *variable == *other.variable && *path == *other.path;
	}
};


/**
 * @brief Hash-table entry binding a Key to a (Type, ValueLocation) pair.
 *
 * Each entry independently reference-counts the Type and ValueLocation it
 * wraps; @c SetInfo() is the single point that updates them atomically.
 */
struct StackFrameValueInfos::InfoEntry : Key {
	Type*				type;
	ValueLocation*		location;
	InfoEntry*			next;

	/**
	 * @brief Constructs an entry with NULL type and location.
	 *
	 * @param variable Owning variable identity (reference acquired).
	 * @param path     Component path (reference acquired).
	 */
	InfoEntry(ObjectID* variable, TypeComponentPath* path)
		:
		Key(variable, path),
		type(NULL),
		location(NULL)
	{
		variable->AcquireReference();
		path->AcquireReference();
	}

	/**
	 * @brief Clears type and location, then releases the key references.
	 */
	~InfoEntry()
	{
		SetInfo(NULL, NULL);
		variable->ReleaseReference();
		path->ReleaseReference();
	}


	/**
	 * @brief Atomically replaces the entry's type and location.
	 *
	 * Acquires references on the new values before releasing references
	 * on the old ones so a self-assignment is safe.
	 *
	 * @param type     Replacement type, or NULL.
	 * @param location Replacement location, or NULL.
	 */
	void SetInfo(Type* type, ValueLocation* location)
	{
		if (type != NULL)
			type->AcquireReference();
		if (location != NULL)
			location->AcquireReference();

		if (this->type != NULL)
			this->type->ReleaseReference();
		if (this->location != NULL)
			this->location->ReleaseReference();

		this->type = type;
		this->location = location;
	}
};


/**
 * @brief BOpenHashTable adapter for InfoEntry hashing and linking.
 */
struct StackFrameValueInfos::InfoEntryHashDefinition {
	typedef Key			KeyType;
	typedef	InfoEntry	ValueType;

	/**
	 * @brief Hashes a lookup key.
	 *
	 * @param key Composite key.
	 * @return   The key's hash value.
	 */
	size_t HashKey(const Key& key) const
	{
		return key.HashValue();
	}

	/**
	 * @brief Hashes an existing entry (reuses the key hash).
	 *
	 * @param value Entry to hash.
	 * @return     The entry's hash value.
	 */
	size_t Hash(const InfoEntry* value) const
	{
		return value->HashValue();
	}

	/**
	 * @brief Compares a key against an existing entry.
	 *
	 * @param key   Candidate key.
	 * @param value Existing entry.
	 * @return     True if @a key matches.
	 */
	bool Compare(const Key& key, const InfoEntry* value) const
	{
		return key == *value;
	}

	/**
	 * @brief Returns the chain link inside @a value used by the hash table.
	 *
	 * @param value Entry whose link to expose.
	 * @return     Reference to the next-pointer slot.
	 */
	InfoEntry*& GetLink(InfoEntry* value) const
	{
		return value->next;
	}
};


/**
 * @brief Constructs an uninitialised StackFrameValueInfos; @c Init() must follow.
 */
StackFrameValueInfos::StackFrameValueInfos()
	:
	fValues(NULL)
{
}


/**
 * @brief Destroys the table and releases all entry references.
 */
StackFrameValueInfos::~StackFrameValueInfos()
{
	_Cleanup();
}


/**
 * @brief Allocates and initialises the underlying hash table.
 *
 * @return @c B_OK on success, @c B_NO_MEMORY on allocation failure.
 */
status_t
StackFrameValueInfos::Init()
{
	fValues = new(std::nothrow) ValueTable;
	if (fValues == NULL)
		return B_NO_MEMORY;

	return fValues->Init();
}


/**
 * @brief Retrieves the type and location stored at (@a variable, @a path).
 *
 * Each non-NULL out parameter receives an independently-acquired reference
 * that the caller must eventually release.
 *
 * @param variable  Variable identity to look up.
 * @param path      Component path inside the variable.
 * @param _type     If non-NULL, receives a reference to the Type.
 * @param _location If non-NULL, receives a reference to the ValueLocation.
 * @return         True if an entry was found.
 */
bool
StackFrameValueInfos::GetInfo(ObjectID* variable,
	const TypeComponentPath* path, Type** _type,
	ValueLocation** _location) const
{
	InfoEntry* entry = fValues->Lookup(
		Key(variable, (TypeComponentPath*)path));
	if (entry == NULL)
		return false;

	if (_type != NULL) {
		entry->type->AcquireReference();
		*_type = entry->type;
	}

	if (_location != NULL) {
		entry->location->AcquireReference();
		*_location = entry->location;
	}

	return true;
}


/**
 * @brief Tests whether (@a variable, @a path) has stored info.
 *
 * @param variable Variable identity.
 * @param path     Component path.
 * @return        True if an entry exists.
 */
bool
StackFrameValueInfos::HasInfo(ObjectID* variable,
	const TypeComponentPath* path) const
{
	return fValues->Lookup(Key(variable, (TypeComponentPath*)path)) != NULL;
}


/**
 * @brief Stores or replaces the type and location at (@a variable, @a path).
 *
 * @param variable Variable identity (reference acquired by entry).
 * @param path     Component path (reference acquired by entry).
 * @param type     Type to store; reference acquired (may be NULL).
 * @param location ValueLocation to store; reference acquired (may be NULL).
 * @return        @c B_OK on success, @c B_NO_MEMORY on allocation failure.
 */
status_t
StackFrameValueInfos::SetInfo(ObjectID* variable, TypeComponentPath* path,
	Type* type, ValueLocation* location)
{
	InfoEntry* entry = fValues->Lookup(Key(variable, path));
	if (entry == NULL) {
		entry = new(std::nothrow) InfoEntry(variable, path);
		if (entry == NULL)
			return B_NO_MEMORY;
		fValues->Insert(entry);
	}

	entry->SetInfo(type, location);
	return B_OK;
}


/**
 * @brief Releases every entry, deletes the table, and resets @c fValues to NULL.
 */
void
StackFrameValueInfos::_Cleanup()
{
	if (fValues != NULL) {
		InfoEntry* entry = fValues->Clear(true);

		while (entry != NULL) {
			InfoEntry* next = entry->next;
			delete entry;
			entry = next;
		}

		delete fValues;
		fValues = NULL;
	}
}
