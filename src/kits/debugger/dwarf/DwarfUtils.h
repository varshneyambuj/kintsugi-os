/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2009, Ingo Weinhold; Copyright 2013-2014, Rene
 * Gollent.
 */

/** @file DwarfUtils.h
    @brief Static helpers that walk DIE chains to extract names and locations. */

#ifndef DWARF_UTILS_H
#define DWARF_UTILS_H

#include "DebugInfoEntries.h"


class BString;
class DebugInfoEntry;
class DwarfFile;


/**
 * @brief Pure-static utility class with DIE-walking helpers.
 */
class DwarfUtils {
public:
	static	void				GetDIEName(const DebugInfoEntry* entry,
									BString& _name);
	static	void				GetDIETypeName(const DebugInfoEntry* entry,
									BString& _name,
									const DebugInfoEntry*
										requestingEntry = NULL);
	static	void				GetFullDIEName(const DebugInfoEntry* entry,
									BString& _name);
	static	void				GetFullyQualifiedDIEName(
									const DebugInfoEntry* entry,
									BString& _name,
									const DebugInfoEntry*
										requestingEntry = NULL);

	static	bool				GetDeclarationLocation(DwarfFile* dwarfFile,
									const DebugInfoEntry* entry,
									const char*& _directory,
									const char*& _file,
									int32& _line, int32& _column);

	template<typename EntryType, typename Predicate>
	static	EntryType*			GetDIEByPredicate(EntryType* entry,
									const Predicate& predicate);
};


/**
 * @brief Returns the first DIE in @a entry's chain that satisfies @a predicate.
 *
 * Examines @a entry, then its abstract-origin, specification, and
 * signature-type DIEs in turn.  Returns NULL if none of them match.
 *
 * @tparam EntryType  Concrete DIE type the caller cares about.
 * @tparam Predicate  Callable accepting a const @c EntryType pointer.
 * @param entry       Starting DIE.
 * @param predicate   Predicate evaluated against each candidate DIE.
 * @return Matching DIE pointer, or NULL.
 */
template<typename EntryType, typename Predicate>
/*static*/ EntryType*
DwarfUtils::GetDIEByPredicate(EntryType* entry, const Predicate& predicate)
{
	if (predicate(entry))
		return entry;

	// try the abstract origin
	if (EntryType* abstractOrigin = dynamic_cast<EntryType*>(
			entry->AbstractOrigin())) {
		entry = abstractOrigin;
		if (predicate(entry))
			return entry;
	}

	// try the specification
	if (EntryType* specification = dynamic_cast<EntryType*>(
			entry->Specification())) {
		entry = specification;
		if (predicate(entry))
			return entry;
	}

	// try the type unit signature
	if (EntryType* signature = dynamic_cast<EntryType*>(
			entry->SignatureType())) {
		entry = signature;
		if (predicate(entry))
			return entry;
	}

	return NULL;
}


#endif	// DWARF_UTILS_H
