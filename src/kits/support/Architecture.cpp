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
 *   Copyright 2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Architecture.cpp
 * @brief Convenience wrappers for querying the current and secondary
 *        CPU architectures supported by the running system.
 *
 * The kernel-level get_secondary_architectures() and get_architectures()
 * overloads that accept raw C-string arrays are wrapped here with
 * BStringList-returning variants for idiomatic C++ use.
 *
 * @see get_secondary_architectures(), get_architectures(), BStringList
 */


#include <Architecture.h>

#include <algorithm>

#include <StringList.h>


/// Maximum number of architectures the local array can hold.
static const size_t kMaxArchitectureCount = 16;


/**
 * @brief Convert a raw C-string array into a BStringList, clearing on error.
 *
 * Copies up to \a count entries from \a architectures into
 * \a _architectures. Empties the list and returns B_NO_MEMORY if any
 * string is empty or if Add() fails (indicating an allocation failure).
 *
 * @param architectures Source array of C-string pointers.
 * @param count         Number of entries to copy.
 * @param _architectures Output BStringList; emptied before populating.
 * @return B_OK on success; B_NO_MEMORY if any entry cannot be added.
 */
static status_t
string_array_to_string_list(const char* const* architectures, size_t count,
	BStringList& _architectures)
{
	_architectures.MakeEmpty();

	for (size_t i = 0; i < count; i++) {
		BString architecture(architectures[i]);
		if (architecture.IsEmpty() || !_architectures.Add(architecture)) {
			_architectures.MakeEmpty();
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Retrieve the list of secondary (compatibility) architectures.
 *
 * Queries the kernel for all secondary architecture strings (e.g. "x86"
 * on an x86_64 system that supports 32-bit binaries) and returns them as
 * a BStringList. At most kMaxArchitectureCount (16) entries are returned.
 *
 * @param[out] _architectures Populated with the secondary architecture
 *             names on success; left in an unspecified state on failure.
 * @return B_OK on success; B_NO_MEMORY if list construction fails.
 * @see get_architectures()
 */
status_t
get_secondary_architectures(BStringList& _architectures)
{
	const char* architectures[kMaxArchitectureCount];
	size_t count = get_secondary_architectures(architectures,
		kMaxArchitectureCount);
	return string_array_to_string_list(architectures,
		std::min(count, kMaxArchitectureCount), _architectures);
}


/**
 * @brief Retrieve the full list of architectures supported by the system.
 *
 * Returns both the primary (native) architecture and all secondary
 * architectures as a BStringList. The primary architecture is always the
 * first entry. At most kMaxArchitectureCount (16) entries are returned.
 *
 * @param[out] _architectures Populated with all architecture names on
 *             success; left in an unspecified state on failure.
 * @return B_OK on success; B_NO_MEMORY if list construction fails.
 * @see get_secondary_architectures()
 */
status_t
get_architectures(BStringList& _architectures)
{
	const char* architectures[kMaxArchitectureCount];
	size_t count = get_architectures(architectures, kMaxArchitectureCount);
	return string_array_to_string_list(architectures,
		std::min(count, kMaxArchitectureCount), _architectures);
}
