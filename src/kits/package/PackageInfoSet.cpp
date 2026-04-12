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
 *   Copyright 2011, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe <zooey@hirschkaefer.de>
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file PackageInfoSet.cpp
 * @brief Copy-on-write set of BPackageInfo objects, keyed by package name.
 *
 * BPackageInfoSet provides a reference-counted, copy-on-write container for
 * BPackageInfo objects. Multiple instances can share the same underlying hash
 * table until a mutating operation is performed, at which point the table is
 * cloned. Iteration is supported via the nested Iterator class.
 *
 * @see BPackageInfo, BPackageRoster
 */


#include <package/PackageInfoSet.h>

#include <new>

#include <Referenceable.h>

#include <AutoDeleter.h>

#include <util/OpenHashTable.h>

#include <package/PackageInfo.h>


namespace BPackageKit {


// #pragma mark - PackageInfo


struct BPackageInfoSet::PackageInfo : public BPackageInfo {
	PackageInfo*	hashNext;
	PackageInfo*	listNext;

	/**
	 * @brief Construct a PackageInfo from an existing BPackageInfo.
	 *
	 * @param other  The BPackageInfo to copy into this node.
	 */
	PackageInfo(const BPackageInfo& other)
		:
		BPackageInfo(other),
		listNext(NULL)
	{
	}

	/**
	 * @brief Delete this node and all subsequent nodes in the linked list.
	 */
	void DeleteList()
	{
		PackageInfo* info = this;
		while (info != NULL) {
			PackageInfo* next = info->listNext;
			delete info;
			info = next;
		}
	}
};


// #pragma mark - PackageInfoHashDefinition


struct BPackageInfoSet::PackageInfoHashDefinition {
	typedef const char*		KeyType;
	typedef	PackageInfo		ValueType;

	/**
	 * @brief Hash a raw C-string key.
	 *
	 * @param key  The package name to hash.
	 * @return Hash value of the key.
	 */
	size_t HashKey(const char* key) const
	{
		return BString::HashValue(key);
	}

	/**
	 * @brief Hash a PackageInfo node by its name.
	 *
	 * @param value  The node whose name is used as hash input.
	 * @return Hash value derived from the package name.
	 */
	size_t Hash(const PackageInfo* value) const
	{
		return value->Name().HashValue();
	}

	/**
	 * @brief Compare a raw key against a PackageInfo node.
	 *
	 * @param key    The package name to compare.
	 * @param value  The node to compare against.
	 * @return True if the node's name equals the key.
	 */
	bool Compare(const char* key, const PackageInfo* value) const
	{
		return value->Name() == key;
	}

	/**
	 * @brief Return a reference to the hash-table next pointer of a node.
	 *
	 * @param value  The node whose hash-chain link is needed.
	 * @return Reference to the node's hashNext pointer.
	 */
	PackageInfo*& GetLink(PackageInfo* value) const
	{
		return value->hashNext;
	}
};


// #pragma mark - PackageMap


struct BPackageInfoSet::PackageMap : public BReferenceable,
	public BOpenHashTable<PackageInfoHashDefinition> {

	/**
	 * @brief Default-construct an empty PackageMap.
	 */
	PackageMap()
		:
		fCount(0)
	{
	}

	/**
	 * @brief Destroy the map and free all PackageInfo nodes.
	 */
	~PackageMap()
	{
		DeleteAllPackageInfos();
	}

	/**
	 * @brief Allocate and initialise a new empty PackageMap.
	 *
	 * @return A pointer to the new map on success, or NULL on allocation failure.
	 */
	static PackageMap* Create()
	{
		PackageMap* map = new(std::nothrow) PackageMap;
		if (map == NULL || map->Init() != B_OK) {
			delete map;
			return NULL;
		}

		return map;
	}

	/**
	 * @brief Create a deep copy of this map containing cloned PackageInfo nodes.
	 *
	 * @return A pointer to the cloned map, or NULL on allocation failure.
	 */
	PackageMap* Clone() const
	{
		PackageMap* newMap = Create();
		if (newMap == NULL)
			return NULL;
		ObjectDeleter<PackageMap> newMapDeleter(newMap);

		for (BPackageInfoSet::Iterator it(this); it.HasNext();) {
			const BPackageInfo* info = it.Next();
			if (newMap->AddNewPackageInfo(*info) != B_OK)
				return NULL;
		}

		return newMapDeleter.Detach();
	}

	/**
	 * @brief Insert a pre-allocated PackageInfo node into the map.
	 *
	 * If a node with the same name already exists, the new node is appended
	 * to the existing node's listNext chain; otherwise it is inserted into
	 * the hash table directly.
	 *
	 * @param info  The node to insert; ownership is transferred.
	 */
	void AddPackageInfo(PackageInfo* info)
	{
		if (PackageInfo* oldInfo = Lookup(info->Name())) {
			info->listNext = oldInfo->listNext;
			oldInfo->listNext = info;
		} else
			Insert(info);

		fCount++;
	}

	/**
	 * @brief Copy a BPackageInfo and insert the copy into the map.
	 *
	 * @param oldInfo  The info to copy.
	 * @return B_OK on success, B_NO_MEMORY on allocation failure, or another
	 *         error if the copied info fails its InitCheck.
	 */
	status_t AddNewPackageInfo(const BPackageInfo& oldInfo)
	{
		PackageInfo* info = new(std::nothrow) PackageInfo(oldInfo);
		if (info == NULL)
			return B_NO_MEMORY;
		ObjectDeleter<PackageInfo> infoDeleter(info);

		status_t error = info->InitCheck();
		if (error != B_OK)
			return error;

		AddPackageInfo(infoDeleter.Detach());

		return B_OK;
	}

	/**
	 * @brief Remove and delete all PackageInfo nodes from the map.
	 */
	void DeleteAllPackageInfos()
	{
		PackageInfo* info = Clear(true);
		while (info != NULL) {
			PackageInfo* next = info->hashNext;
			info->DeleteList();
			info = next;
		}
	}

	/**
	 * @brief Return the total number of PackageInfo entries, including duplicates.
	 *
	 * @return Count of all stored PackageInfo objects.
	 */
	uint32 CountPackageInfos() const
	{
		return fCount;
	}

private:
	uint32	fCount;
};


// #pragma mark - Iterator


/**
 * @brief Construct an Iterator over the given PackageMap.
 *
 * @param map  The PackageMap to iterate; may be NULL for an empty iteration.
 */
BPackageInfoSet::Iterator::Iterator(const PackageMap* map)
	:
	fMap(map),
	fNextInfo(map != NULL ? map->GetIterator().Next() : NULL)
{
}


/**
 * @brief Check whether more elements are available.
 *
 * @return True if a call to Next() will return a valid pointer.
 */
bool
BPackageInfoSet::Iterator::HasNext() const
{
	return fNextInfo != NULL;
}


/**
 * @brief Advance the iterator and return the current element.
 *
 * @return Pointer to the current BPackageInfo, or NULL when exhausted.
 */
const BPackageInfo*
BPackageInfoSet::Iterator::Next()
{
	BPackageInfo* result = fNextInfo;

	if (fNextInfo != NULL) {
		if (fNextInfo->listNext != NULL) {
			// get next in list
			fNextInfo = fNextInfo->listNext;
		} else {
			// get next in hash table
			PackageMap::Iterator iterator
				= fMap->GetIterator(fNextInfo->Name());
			iterator.Next();
			fNextInfo = iterator.Next();
		}
	}

	return result;
}


// #pragma mark - BPackageInfoSet


/**
 * @brief Default-construct an empty BPackageInfoSet.
 */
BPackageInfoSet::BPackageInfoSet()
	:
	fPackageMap(NULL)
{
}


/**
 * @brief Destroy the set, releasing the shared PackageMap reference.
 */
BPackageInfoSet::~BPackageInfoSet()
{
	if (fPackageMap != NULL)
		fPackageMap->ReleaseReference();
}


/**
 * @brief Copy-construct a BPackageInfoSet sharing the underlying PackageMap.
 *
 * @param other  The set to copy; the PackageMap reference count is incremented.
 */
BPackageInfoSet::BPackageInfoSet(const BPackageInfoSet& other)
	:
	fPackageMap(other.fPackageMap)
{
	if (fPackageMap != NULL)
		fPackageMap->AcquireReference();
}


/**
 * @brief Add a copy of the given BPackageInfo to the set.
 *
 * Triggers a copy-on-write if the underlying PackageMap is shared.
 *
 * @param info  The package info to add.
 * @return B_OK on success, B_NO_MEMORY if allocation fails.
 */
status_t
BPackageInfoSet::AddInfo(const BPackageInfo& info)
{
	if (!_CopyOnWrite())
		return B_NO_MEMORY;

	return fPackageMap->AddNewPackageInfo(info);
}


/**
 * @brief Remove all entries from the set.
 *
 * If the map is shared, the reference is simply released and set to NULL.
 * Otherwise the map's contents are cleared in place.
 */
void
BPackageInfoSet::MakeEmpty()
{
	if (fPackageMap == NULL || fPackageMap->CountPackageInfos() == 0)
		return;

	// If our map is shared, just set it to NULL.
	if (fPackageMap->CountReferences() != 1) {
		fPackageMap->ReleaseReference();
		fPackageMap = NULL;
		return;
	}

	// Our map is not shared -- make it empty.
	fPackageMap->DeleteAllPackageInfos();
}


/**
 * @brief Return the number of package info objects stored in the set.
 *
 * @return Count of stored entries, or 0 if the set is empty.
 */
uint32
BPackageInfoSet::CountInfos() const
{
	if (fPackageMap == NULL)
		return 0;

	return fPackageMap->CountPackageInfos();
}


/**
 * @brief Return an iterator positioned at the first element of the set.
 *
 * @return An Iterator over all BPackageInfo objects in the set.
 */
BPackageInfoSet::Iterator
BPackageInfoSet::GetIterator() const
{
	return Iterator(fPackageMap);
}


/**
 * @brief Assignment operator; replaces the current set with another.
 *
 * Reference counts on both maps are adjusted appropriately.
 *
 * @param other  The set to copy.
 * @return Reference to this set.
 */
BPackageInfoSet&
BPackageInfoSet::operator=(const BPackageInfoSet& other)
{
	if (other.fPackageMap == fPackageMap)
		return *this;

	if (fPackageMap != NULL)
		fPackageMap->ReleaseReference();

	fPackageMap = other.fPackageMap;

	if (fPackageMap != NULL)
		fPackageMap->AcquireReference();

	return *this;
}


/**
 * @brief Ensure exclusive ownership of the PackageMap before mutation.
 *
 * If the map is NULL a new empty map is created. If it is shared it is
 * cloned so that this instance holds the only reference.
 *
 * @return True if the operation succeeded, false on allocation failure.
 */
bool
BPackageInfoSet::_CopyOnWrite()
{
	if (fPackageMap == NULL) {
		fPackageMap = PackageMap::Create();
		return fPackageMap != NULL;
	}

	if (fPackageMap->CountReferences() == 1)
		return true;

	PackageMap* newMap = fPackageMap->Clone();
	if (newMap == NULL)
		return false;

	fPackageMap->ReleaseReference();
	fPackageMap = newMap;
	return true;
}


}	// namespace BPackageKit
