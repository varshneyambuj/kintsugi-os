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
 * @file GlobalTypeLookup.cpp
 * @brief Implementation of GlobalTypeCache and the GlobalTypeLookup
 *        interface.
 *
 * GlobalTypeCache stores Type objects discovered during debug-info parsing
 * and indexes them by both name and stable ID. Backends consult and
 * populate it during type construction so that a single Type instance
 * represents each type across the team. The cache is serialized through a
 * single named lock so it can be safely shared.
 *
 * @see Type, TypeLookupConstraints
 */


#include "GlobalTypeLookup.h"

#include <new>

#include <String.h>

#include <AutoLocker.h>

#include "Type.h"
#include "TypeLookupConstraints.h"


/**
 * @brief Internal cache entry pairing a Type with the two hash-table links.
 *
 * Acquires a reference on the Type at construction and releases it on
 * destruction so the cache owns one reference per stored entry.
 */
struct GlobalTypeCache::TypeEntry {
	Type*		type;
	TypeEntry*	fNextByName;
	TypeEntry*	fNextByID;

	/** @brief Stores @a type and acquires a reference on it. */
	TypeEntry(Type* type)
		:
		type(type)
	{
		type->AcquireReference();
	}

	/** @brief Releases the reference acquired in the constructor. */
	~TypeEntry()
	{
		type->ReleaseReference();
	}
};


/**
 * @brief Hash-table policy keying TypeEntry by Type::Name().
 */
struct GlobalTypeCache::TypeEntryHashDefinitionByName {
	typedef const BString	KeyType;
	typedef	TypeEntry		ValueType;

	/** @brief Hashes the lookup key. */
	size_t HashKey(const BString& key) const
	{
		return key.HashValue();
	}

	/** @brief Hashes a stored entry by its type name. */
	size_t Hash(const TypeEntry* value) const
	{
		return HashKey(value->type->Name());
	}

	/** @brief Equality test between key and stored entry name. */
	bool Compare(const BString& key, const TypeEntry* value) const
	{
		return key == value->type->Name();
	}

	/** @brief Returns the chain pointer used by the hash table. */
	TypeEntry*& GetLink(TypeEntry* value) const
	{
		return value->fNextByName;
	}
};


/**
 * @brief Hash-table policy keying TypeEntry by Type::ID().
 */
struct GlobalTypeCache::TypeEntryHashDefinitionByID {
	typedef const BString	KeyType;
	typedef	TypeEntry		ValueType;

	/** @brief Hashes the lookup key. */
	size_t HashKey(const BString& key) const
	{
		return key.HashValue();
	}

	/** @brief Hashes a stored entry by its type ID. */
	size_t Hash(const TypeEntry* value) const
	{
		return HashKey(value->type->ID());
	}

	/** @brief Equality test between key and stored entry ID. */
	bool Compare(const BString& key, const TypeEntry* value) const
	{
		return key == value->type->ID();
	}

	/** @brief Returns the chain pointer used by the hash table. */
	TypeEntry*& GetLink(TypeEntry* value) const
	{
		return value->fNextByID;
	}
};


// #pragma mark - GlobalTypeCache


/**
 * @brief Constructs an uninitialized cache; Init() must be called.
 */
GlobalTypeCache::GlobalTypeCache()
	:
	fLock("global type cache"),
	fTypesByName(NULL),
	fTypesByID(NULL)
{
}


/**
 * @brief Destroys the cache and releases all stored Type references.
 */
GlobalTypeCache::~GlobalTypeCache()
{
	if (fTypesByName != NULL)
		fTypesByName->Clear();

	// release all cached type references
	if (fTypesByID != NULL) {
		TypeEntry* entry = fTypesByID->Clear(true);
		while (entry != NULL) {
			TypeEntry* nextEntry = entry->fNextByID;
			delete entry;
			entry = nextEntry;
		}
	}

	delete fTypesByName;
	delete fTypesByID;
}


/**
 * @brief Initializes the lock and hash tables.
 *
 * @retval B_OK         Cache is ready for use.
 * @retval B_NO_MEMORY  Allocation failure.
 * @retval other        Errors from BLocker::InitCheck() or table Init().
 */
status_t
GlobalTypeCache::Init()
{
	// check lock
	status_t error = fLock.InitCheck();
	if (error != B_OK)
		return error;

	// create name table
	fTypesByName = new(std::nothrow) NameTable;
	if (fTypesByName == NULL)
		return B_NO_MEMORY;

	error = fTypesByName->Init();
	if (error != B_OK)
		return error;

	// create ID table
	fTypesByID = new(std::nothrow) IDTable;
	if (fTypesByID == NULL)
		return B_NO_MEMORY;

	error = fTypesByID->Init();
	if (error != B_OK)
		return error;

	return B_OK;
}


/**
 * @brief Resolves a type by name subject to optional constraints.
 *
 * The lookup ignores entries whose kind, address kind, or compound kind
 * disagree with @a constraints. For address and compound types the dynamic
 * cast is used to enforce a precise match.
 *
 * @param name        Type name to look up.
 * @param constraints Optional kind/subkind constraints.
 * @return Pointer to the cached Type, or @c NULL if no match exists.
 * @note   Caller must hold the cache lock.
 */
Type*
GlobalTypeCache::GetType(const BString& name,
	const TypeLookupConstraints &constraints) const
{
	TypeEntry* typeEntry = fTypesByName->Lookup(name);
	if (typeEntry != NULL) {
		if (constraints.HasTypeKind()
			&& typeEntry->type->Kind() != constraints.TypeKind())
			typeEntry = NULL;
		else if (constraints.HasSubtypeKind()) {
			if (typeEntry->type->Kind() == TYPE_ADDRESS) {
					AddressType* type = dynamic_cast<AddressType*>(
						typeEntry->type);
					if (type == NULL)
						typeEntry = NULL;
					else if (type->AddressKind() != constraints.SubtypeKind())
						typeEntry = NULL;
			} else if (typeEntry->type->Kind() == TYPE_COMPOUND) {
					CompoundType* type = dynamic_cast<CompoundType*>(
						typeEntry->type);
					if (type == NULL)
						typeEntry = NULL;
					else if (type->CompoundKind() != constraints.SubtypeKind())
						typeEntry = NULL;
			}
		}
	}
	return typeEntry != NULL ? typeEntry->type : NULL;
}


/**
 * @brief Looks up a cached type by stable ID.
 *
 * @param id  Stable type ID (typically a DWARF DIE-derived identifier).
 * @return Pointer to the cached Type, or @c NULL if absent.
 * @note   Caller must hold the cache lock.
 */
Type*
GlobalTypeCache::GetTypeByID(const BString& id) const
{
	TypeEntry* typeEntry = fTypesByID->Lookup(id);
	return typeEntry != NULL ? typeEntry->type : NULL;
}


/**
 * @brief Inserts a Type into both indexes.
 *
 * Refuses insertion when the ID is already present, or when a name
 * collision exists. Acquires a reference on @a type via the TypeEntry
 * constructor.
 *
 * @param type  Type to add.
 * @retval B_OK         Insertion succeeded.
 * @retval B_BAD_VALUE  ID or name already present in the cache.
 * @retval B_NO_MEMORY  Allocation failure.
 * @note   Caller must hold the cache lock.
 */
status_t
GlobalTypeCache::AddType(Type* type)
{
	const BString& id = type->ID();
	const BString& name = type->Name();

	if (fTypesByID->Lookup(id) != NULL
		|| (name.Length() > 0 && fTypesByID->Lookup(name) != NULL)) {
		return B_BAD_VALUE;
	}

	TypeEntry* typeEntry = new(std::nothrow) TypeEntry(type);
	if (typeEntry == NULL)
		return B_NO_MEMORY;

	fTypesByID->Insert(typeEntry);

	if (name.Length() > 0)
		fTypesByName->Insert(typeEntry);

	return B_OK;
}


/**
 * @brief Removes a specific Type instance from the cache.
 *
 * Only removes the entry whose stored Type pointer matches @a type.
 * Releases the reference held by the entry.
 *
 * @param type  Type instance to remove.
 * @note   Caller must hold the cache lock.
 */
void
GlobalTypeCache::RemoveType(Type* type)
{
	if (TypeEntry* typeEntry = fTypesByID->Lookup(type->ID())) {
		if (typeEntry->type == type) {
			fTypesByID->Remove(typeEntry);

			if (type->Name().Length() > 0)
				fTypesByName->Remove(typeEntry);

			delete typeEntry;
		}
	}
}


/**
 * @brief Drops every cached type that originated from a given image.
 *
 * Used when an image is unloaded to evict its types in bulk. Acquires the
 * cache lock internally.
 *
 * @param imageID  Image identifier to evict.
 */
void
GlobalTypeCache::RemoveTypes(image_id imageID)
{
	AutoLocker<GlobalTypeCache> locker(this);

	for (IDTable::Iterator it = fTypesByID->GetIterator();
			TypeEntry* typeEntry = it.Next();) {
		if (typeEntry->type->ImageID() == imageID) {
			fTypesByID->RemoveUnchecked(typeEntry);

			if (typeEntry->type->Name().Length() > 0)
				fTypesByName->Remove(typeEntry);

			delete typeEntry;
		}
	}
}


// #pragma mark - GlobalTypeLookup


/**
 * @brief Virtual destructor for safe polymorphic deletion of the lookup
 *        interface.
 */
GlobalTypeLookup::~GlobalTypeLookup()
{
}
