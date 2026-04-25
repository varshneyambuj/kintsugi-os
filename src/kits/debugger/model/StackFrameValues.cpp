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
 * @file StackFrameValues.cpp
 * @brief Implementation of StackFrameValues, a hash table mapping
 *        (variable, type-component path) keys to BVariant values.
 *
 * StackFrameValues caches resolved variable values inside a single stack
 * frame so the variable inspector does not have to re-evaluate
 * expressions for components it has already shown. The internal Key,
 * ValueEntry, and ValueEntryHashDefinition structs implement the
 * BOpenHashTable adapter required by the open-addressing table.
 */


#include "StackFrameValues.h"

#include <new>

#include "FunctionID.h"
#include "TypeComponentPath.h"


/**
 * @brief Composite (variable id, type-component path) lookup key for the table.
 */
struct StackFrameValues::Key {
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
 * @brief Hash-table entry binding a Key to its captured BVariant value.
 */
struct StackFrameValues::ValueEntry : Key {
	BVariant			value;
	ValueEntry*			next;

	/**
	 * @brief Constructs an entry, acquiring references to its key components.
	 *
	 * @param variable Owning variable identity.
	 * @param path     Component path inside @a variable.
	 */
	ValueEntry(ObjectID* variable, TypeComponentPath* path)
		:
		Key(variable, path)
	{
		variable->AcquireReference();
		path->AcquireReference();
	}

	/**
	 * @brief Releases the references acquired in the constructor.
	 */
	~ValueEntry()
	{
		variable->ReleaseReference();
		path->ReleaseReference();
	}
};


/**
 * @brief BOpenHashTable adapter describing key/entry hashing and linking.
 */
struct StackFrameValues::ValueEntryHashDefinition {
	typedef Key			KeyType;
	typedef	ValueEntry	ValueType;

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
	size_t Hash(const ValueEntry* value) const
	{
		return value->HashValue();
	}

	/**
	 * @brief Compares a key against an existing entry.
	 *
	 * @param key   Candidate key.
	 * @param value Existing entry.
	 * @return     True if @a key equals @a value's key components.
	 */
	bool Compare(const Key& key, const ValueEntry* value) const
	{
		return key == *value;
	}

	/**
	 * @brief Returns the chain link inside @a value used by the hash table.
	 *
	 * @param value Entry whose link to expose.
	 * @return     Reference to the next-pointer slot.
	 */
	ValueEntry*& GetLink(ValueEntry* value) const
	{
		return value->next;
	}
};


/**
 * @brief Constructs an uninitialised StackFrameValues; @c Init() must follow.
 */
StackFrameValues::StackFrameValues()
	:
	fValues(NULL)
{
}


/**
 * @brief Copy-constructs by initialising and cloning every entry.
 *
 * Throws @c std::bad_alloc on allocation failure during init or insert.
 *
 * @param other Source instance whose entries are duplicated.
 */
StackFrameValues::StackFrameValues(const StackFrameValues& other)
	:
	fValues(NULL)
{
	try {
		// init
		if (Init() != B_OK)
			throw std::bad_alloc();

		// clone all values
		for (ValueTable::Iterator it = other.fValues->GetIterator();
				ValueEntry* entry = it.Next();) {
			if (SetValue(entry->variable, entry->path, entry->value) != B_OK)
				throw std::bad_alloc();
		}
	} catch (...) {
		_Cleanup();
		throw;
	}
}


/**
 * @brief Destroys the table and releases all entry references.
 */
StackFrameValues::~StackFrameValues()
{
	_Cleanup();
}


/**
 * @brief Allocates and initialises the underlying hash table.
 *
 * @return @c B_OK on success, @c B_NO_MEMORY on allocation failure.
 */
status_t
StackFrameValues::Init()
{
	fValues = new(std::nothrow) ValueTable;
	if (fValues == NULL)
		return B_NO_MEMORY;

	return fValues->Init();
}


/**
 * @brief Looks up the value associated with (@a variable, @a path).
 *
 * @param variable Variable identity to look up.
 * @param path     Component path inside the variable.
 * @param _value   Receives the value when found.
 * @return        True if a value was found and stored in @a _value.
 */
bool
StackFrameValues::GetValue(ObjectID* variable, const TypeComponentPath* path,
	BVariant& _value) const
{
	ValueEntry* entry = fValues->Lookup(
		Key(variable, (TypeComponentPath*)path));
	if (entry == NULL)
		return false;

	_value = entry->value;
	return true;
}


/**
 * @brief Tests whether (@a variable, @a path) has a stored value.
 *
 * @param variable Variable identity.
 * @param path     Component path.
 * @return        True if an entry exists.
 */
bool
StackFrameValues::HasValue(ObjectID* variable, const TypeComponentPath* path)
	const
{
	return fValues->Lookup(Key(variable, (TypeComponentPath*)path)) != NULL;
}


/**
 * @brief Stores or replaces the value at (@a variable, @a path).
 *
 * Inserts a new entry if no matching one exists; otherwise overwrites the
 * stored BVariant.
 *
 * @param variable Variable identity (reference acquired by entry).
 * @param path     Component path (reference acquired by entry).
 * @param value    Value to store.
 * @return        @c B_OK on success, @c B_NO_MEMORY on allocation failure.
 */
status_t
StackFrameValues::SetValue(ObjectID* variable, TypeComponentPath* path,
	const BVariant& value)
{
	ValueEntry* entry = fValues->Lookup(Key(variable, path));
	if (entry == NULL) {
		entry = new(std::nothrow) ValueEntry(variable, path);
		if (entry == NULL)
			return B_NO_MEMORY;
		fValues->Insert(entry);
	}

	entry->value = value;
	return B_OK;
}


/**
 * @brief Releases every entry, deletes the table, and resets @c fValues to NULL.
 */
void
StackFrameValues::_Cleanup()
{
	if (fValues != NULL) {
		ValueEntry* entry = fValues->Clear(true);

		while (entry != NULL) {
			ValueEntry* next = entry->next;
			delete entry;
			entry = next;
		}

		delete fValues;
		fValues = NULL;
	}
}
