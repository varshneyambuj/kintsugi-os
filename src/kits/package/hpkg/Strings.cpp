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
 *   Copyright 2009-2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Strings.cpp
 * @brief Reference-counted string cache used during HPKG attribute encoding.
 *
 * StringCache is an open-addressed hash table of CachedString entries. Each
 * unique string value is stored once; a usage counter tracks how many
 * attribute nodes currently reference the string. Writers call Get() to
 * obtain (or create) a CachedString and Put() when the node is discarded,
 * allowing the cache to free strings that are no longer referenced.
 *
 * @see WriterImplBase, PackageWriterImpl
 */


#include <package/hpkg/Strings.h>


namespace BPackageKit {

namespace BHPKG {

namespace BPrivate {


/**
 * @brief Constructs an empty StringCache.
 */
StringCache::StringCache()
{
}


/**
 * @brief Destroys the StringCache, releasing all remaining CachedString entries.
 *
 * Iterates the hash table in clear mode and deletes every CachedString node,
 * regardless of its current usage count. This is safe because the cache owner
 * is responsible for ensuring Put() has been called for every Get() before
 * the cache is destroyed.
 */
StringCache::~StringCache()
{
	CachedString* cachedString = Clear(true);
	while (cachedString != NULL) {
		CachedString* next = cachedString->next;
		delete cachedString;
		cachedString = next;
	}
}


/**
 * @brief Retrieves or creates a reference-counted entry for @a value.
 *
 * If a CachedString matching @a value already exists in the table its
 * usageCount is incremented and the existing entry is returned. Otherwise a
 * new CachedString is allocated, initialised, inserted into the hash table,
 * and returned with a usageCount of 1.
 *
 * @param value The string value to look up or insert.
 * @return Pointer to the CachedString; never NULL.
 * @throws std::bad_alloc if a new entry cannot be allocated or initialised.
 */
CachedString*
StringCache::Get(const char* value)
{
	CachedString* string = Lookup(value);
	if (string != NULL) {
		string->usageCount++;
		return string;
	}

	string = new CachedString;
	if (!string->Init(value)) {
		delete string;
		throw std::bad_alloc();
	}

	Insert(string);
	return string;
}


/**
 * @brief Releases a reference to a CachedString previously obtained via Get().
 *
 * Decrements the usageCount. When the count reaches zero the entry is removed
 * from the hash table and deleted. Passing NULL is a no-op.
 *
 * @param string CachedString to release; may be NULL.
 */
void
StringCache::Put(CachedString* string)
{
	if (string != NULL) {
		if (--string->usageCount == 0) {
			Remove(string);
			delete string;
		}
	}
}


}	// namespace BPrivate

}	// namespace BHPKG

}	// namespace BPackageKit
