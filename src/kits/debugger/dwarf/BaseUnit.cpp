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
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BaseUnit.cpp
 * @brief Common implementation shared by compilation units and type units.
 *
 * BaseUnit is the abstract parent of @ref CompilationUnit (.debug_info)
 * and @ref TypeUnit (.debug_types).  It records the unit's location
 * inside the section, the abbreviation table it references, and the
 * sorted array of DIEs parsed from its body.  DIE lookup by absolute
 * offset uses a binary search over the parallel offset array.
 */


#include "BaseUnit.h"

#include <new>

#include "DebugInfoEntries.h"


/**
 * @brief Constructs a base unit with the given header geometry.
 *
 * @param headerOffset       Byte offset of the unit header in its section.
 * @param contentOffset      Byte offset of the first DIE following the header.
 * @param totalSize          Header + content size in bytes.
 * @param abbreviationOffset Offset into .debug_abbrev for the unit's table.
 * @param addressSize        Target address width in bytes (4 or 8).
 * @param isBigEndian        @c true for big-endian targets.
 * @param isDwarf64          @c true for DWARF-64 (8-byte length fields).
 */
BaseUnit::BaseUnit(off_t headerOffset, off_t contentOffset,
	off_t totalSize, off_t abbreviationOffset, uint8 addressSize,
	bool isBigEndian, bool isDwarf64)
	:
	fHeaderOffset(headerOffset),
	fContentOffset(contentOffset),
	fTotalSize(totalSize),
	fAbbreviationOffset(abbreviationOffset),
	fAbbreviationTable(NULL),
	fAddressSize(addressSize),
	fIsBigEndian(isBigEndian),
	fIsDwarf64(isDwarf64)
{
}


/**
 * @brief Destroys the unit, deleting every owned DIE.
 */
BaseUnit::~BaseUnit()
{
	for (int32 i = 0; i < fEntries.Count(); i++)
		delete fEntries[i];
}


/**
 * @brief Binds the unit to its parsed abbreviation table.
 *
 * @param abbreviationTable Table whose lifetime exceeds this unit.
 */
void
BaseUnit::SetAbbreviationTable(AbbreviationTable* abbreviationTable)
{
	fAbbreviationTable = abbreviationTable;
}


/**
 * @brief Records a parsed DIE at its absolute section offset.
 *
 * Entries are appended in order of increasing offset so that
 * @ref EntryForOffset can binary-search them.
 *
 * @param entry  Newly-parsed DIE; ownership transferred to the unit.
 * @param offset Absolute byte offset of @a entry in the .debug_info /
 *               .debug_types section.
 * @retval B_OK         Entry recorded.
 * @retval B_NO_MEMORY  Array growth failed.
 */
status_t
BaseUnit::AddDebugInfoEntry(DebugInfoEntry* entry, off_t offset)
{
	if (!fEntries.Add(entry))
		return B_NO_MEMORY;
	if (!fEntryOffsets.Add(offset)) {
		fEntries.Remove(fEntries.Count() - 1);
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Tests whether a section-absolute @a offset falls inside this unit.
 *
 * @param offset Byte offset within the unit's section.
 * @return @c true when @a offset is in [headerOffset, headerOffset + totalSize).
 */
bool
BaseUnit::ContainsAbsoluteOffset(off_t offset) const
{
	return fHeaderOffset <= offset && fHeaderOffset + fTotalSize > offset;
}


/**
 * @brief Records the source-language descriptor for this unit.
 *
 * @param language Pointer to a long-lived SourceLanguageInfo singleton.
 */
void
BaseUnit::SetSourceLanguage(const SourceLanguageInfo* language)
{
	fSourceLanguage = language;
}


/**
 * @brief Returns the number of DIEs currently held by the unit.
 */
int
BaseUnit::CountEntries() const
{
	return fEntries.Count();
}


/**
 * @brief Retrieves the DIE and its offset at a given array index.
 *
 * @param index   Position in [0, CountEntries()).
 * @param entry   Output pointer to the DIE.
 * @param offset  Output absolute section offset of the DIE.
 */
void
BaseUnit::GetEntryAt(int index, DebugInfoEntry*& entry,
	off_t& offset) const
{
	entry = fEntries[index];
	offset = fEntryOffsets[index];
}


/**
 * @brief Locates a DIE by its absolute section offset.
 *
 * Performs a binary search over the recorded offsets.
 *
 * @param offset Absolute byte offset of the desired DIE.
 * @return Pointer to the DIE, or NULL if no DIE in this unit lives at
 *         exactly @a offset.
 */
DebugInfoEntry*
BaseUnit::EntryForOffset(off_t offset) const
{
	if (fEntries.IsEmpty())
		return NULL;

	// binary search
	int lower = 0;
	int upper = fEntries.Count() - 1;
	while (lower < upper) {
		int mid = (lower + upper + 1) / 2;
		if (fEntryOffsets[mid] > offset)
			upper = mid - 1;
		else
			lower = mid;
	}

	return fEntryOffsets[lower] == offset ? fEntries[lower] : NULL;
}
