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
 *   Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file AbbreviationTable.cpp
 * @brief Parser and lookup for DWARF .debug_abbrev abbreviation tables.
 *
 * Each compilation unit (CU) references a DWARF abbreviation table that
 * defines the structure of every debug information entry (DIE) it contains.
 * An abbreviation entry pairs a numeric @c code with a tag, a child flag,
 * and an ordered list of (attribute name, attribute form) pairs.  This
 * file builds an in-memory hash table keyed by code so DIE parsing can
 * resolve attribute layout in O(1).
 */

#include "AbbreviationTable.h"

#include <stdio.h>

#include <new>


/**
 * @brief Constructs an abbreviation table that lives at @a offset in .debug_abbrev.
 *
 * @param offset Byte offset of this table within the .debug_abbrev section.
 */
AbbreviationTable::AbbreviationTable(off_t offset)
	:
	fOffset(offset),
	fData(NULL),
	fSize(0)
{
}


/**
 * @brief Destroys the table, releasing every entry stored in the hash table.
 */
AbbreviationTable::~AbbreviationTable()
{
	AbbreviationTableEntry* entry = fEntryTable.Clear(true);
	while (entry != NULL) {
		AbbreviationTableEntry* next = entry->next;
		delete entry;
		entry = next;
	}
}


/**
 * @brief Parses the abbreviation table starting at @c fOffset within @a section.
 *
 * Walks the byte stream, populating an internal hash table from
 * abbreviation @c code to its (offset, size) span inside the section.
 * Stops at the first null entry (code == 0).
 *
 * @param section     Pointer to the start of the .debug_abbrev section.
 * @param sectionSize Size of the section in bytes.
 * @retval B_OK         Table parsed successfully.
 * @retval B_BAD_DATA   Offset out of range or malformed entry encountered.
 * @retval B_NO_MEMORY  Hash-table allocation failed.
 */
status_t
AbbreviationTable::Init(const void* section, off_t sectionSize)
{
	if (fOffset < 0 || fOffset >= sectionSize)
		return B_BAD_DATA;

	fData = (uint8*)section + fOffset;
	fSize = sectionSize - fOffset;
		// That's only the maximum size. Will be adjusted at the end.

	status_t error = fEntryTable.Init();
	if (error != B_OK)
		return error;

	DataReader abbrevReader(fData, fSize, 4, false);
		// address size and endianness don't matter here

	while (true) {
		bool nullEntry;
		status_t error = _ParseAbbreviationEntry(abbrevReader, nullEntry);
		if (error != B_OK)
			return error;

		if (nullEntry)
			break;
	}

	fSize -= abbrevReader.BytesRemaining();

	return B_OK;
}


/**
 * @brief Looks up an abbreviation by its numeric code and binds @a entry to it.
 *
 * @param code   Abbreviation code referenced from a DIE.
 * @param entry  Output entry whose internal pointers are aimed into the
 *               table's data buffer; valid only while this table lives.
 * @return @c true if the code is present in the table, @c false otherwise.
 */
bool
AbbreviationTable::GetAbbreviationEntry(uint32 code, AbbreviationEntry& entry)
{
	AbbreviationTableEntry* tableEntry = fEntryTable.Lookup(code);
	if (tableEntry == NULL)
		return false;

	entry.SetTo(code, fData + tableEntry->offset, tableEntry->size);
	return true;
}


/**
 * @brief Parses a single abbreviation entry from the byte stream.
 *
 * Records the entry's offset and size relative to the start of the table
 * so that AbbreviationEntry can later replay attribute parsing on demand.
 * Duplicates are reported on stderr but tolerated.
 *
 * @param abbrevReader Reader positioned at the start of the entry.
 * @param _nullEntry   Set to @c true if a null terminator was consumed,
 *                     indicating the end of the abbreviation table.
 * @retval B_OK         Entry parsed (or null terminator handled).
 * @retval B_BAD_DATA   Reader overflowed before the entry was complete.
 * @retval B_NO_MEMORY  Allocation of the new entry failed.
 */
status_t
AbbreviationTable::_ParseAbbreviationEntry(DataReader& abbrevReader,
	bool& _nullEntry)
{
	uint32 code = abbrevReader.ReadUnsignedLEB128(0);
	if (code == 0) {
		if (abbrevReader.HasOverflow()) {
			fprintf(stderr, "Invalid abbreviation table 1!\n");
			return B_BAD_DATA;
		}
		_nullEntry = true;
		return B_OK;
	}

	off_t remaining = abbrevReader.BytesRemaining();

/*	uint32 tag =*/ abbrevReader.ReadUnsignedLEB128(0);
/*	uint8 hasChildren =*/ abbrevReader.Read<uint8>(DW_CHILDREN_no);

//	printf("entry: %" B_PRIu32 ", tag: %" B_PRIu32 ", children: %d\n", code, tag,
//		hasChildren);

	// parse attribute specifications
	while (true) {
		uint32 attributeName = abbrevReader.ReadUnsignedLEB128(0);
		uint32 attributeForm = abbrevReader.ReadUnsignedLEB128(0);
		if (abbrevReader.HasOverflow()) {
			fprintf(stderr, "Invalid abbreviation table 2!\n");
			return B_BAD_DATA;
		}

		if (attributeName == 0 && attributeForm == 0)
			break;

		if (attributeForm == DW_FORM_implicit_const)
			abbrevReader.ReadSignedLEB128(0);

//		printf("  attr: name: %" B_PRIu32 ", form: %" B_PRIu32 "\n", attributeName,
//			attributeForm);
	}

	// create the entry
	if (fEntryTable.Lookup(code) == NULL) {
		AbbreviationTableEntry* entry = new(std::nothrow)
			AbbreviationTableEntry(code, fSize - remaining,
				remaining - abbrevReader.BytesRemaining());
		if (entry == NULL)
			return B_NO_MEMORY;

		fEntryTable.Insert(entry);
	} else {
		fprintf(stderr, "Duplicate abbreviation table entry %" B_PRIu32 "!\n",
			code);
	}

	_nullEntry = false;
	return B_OK;
}
