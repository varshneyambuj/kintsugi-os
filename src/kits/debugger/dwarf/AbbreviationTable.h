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
 * MIT License. Copyright 2009, Ingo Weinhold.
 */

/** @file AbbreviationTable.h
    @brief Parsed .debug_abbrev abbreviation table indexed by abbreviation code. */

#ifndef ABBREVIATION_TABLE_H
#define ABBREVIATION_TABLE_H

#include <util/DoublyLinkedList.h>
#include <util/OpenHashTable.h>

#include "DataReader.h"
#include "Dwarf.h"


/** @brief Hash-table node for a single (code -> offset/size) abbreviation entry. */
struct AbbreviationTableEntry {
	uint32					code;
	off_t					offset;
	off_t					size;
	AbbreviationTableEntry*	next;

	AbbreviationTableEntry(uint32 code, off_t offset, off_t size)
		:
		code(code),
		offset(offset),
		size(size)
	{
	}
};


/**
 * @brief Lightweight cursor over a single parsed abbreviation entry.
 *
 * Bundles the abbreviation code, tag, has-children flag and an iterator
 * over its attribute specifications.  Constructed on demand from a
 * pointer/size pair owned by AbbreviationTable.
 */
struct AbbreviationEntry {
	AbbreviationEntry()
	{
	}

	AbbreviationEntry(uint32 code, const void* data, off_t size)
	{
		SetTo(code, data, size);
	}

	void SetTo(uint32 code, const void* data, off_t size)
	{
		fCode = code;
		fAttributesReader.SetTo(data, size, 4, false);
			// address size and endianness don't matter here
		fTag = fAttributesReader.ReadUnsignedLEB128(0);
		fHasChildren = fAttributesReader.Read<uint8>(0);
		fData = fAttributesReader.Data();
		fSize = fAttributesReader.BytesRemaining();
	}

	uint32	Code() const		{ return fCode; }
	uint32	Tag() const			{ return fTag; }
	bool	HasChildren() const	{ return fHasChildren == DW_CHILDREN_yes; }

	bool GetNextAttribute(uint32& name, uint32& form, int32& implicitConst)
	{
		name = fAttributesReader.ReadUnsignedLEB128(0);
		form = fAttributesReader.ReadUnsignedLEB128(0);
		if (form == DW_FORM_implicit_const)
			implicitConst = fAttributesReader.ReadSignedLEB128(0);
		return !fAttributesReader.HasOverflow() && (name != 0 || form != 0);
	}

private:
	uint32		fCode;
	const void*	fData;
	off_t		fSize;
	uint32		fTag;
	uint8		fHasChildren;
	DataReader	fAttributesReader;
};


/** @brief Hash-table policy mapping abbreviation codes to their entries. */
struct AbbreviationTableHashDefinition {
	typedef uint32					KeyType;
	typedef	AbbreviationTableEntry	ValueType;

	size_t HashKey(uint32 key) const
	{
		return (size_t)key;
	}

	size_t Hash(AbbreviationTableEntry* value) const
	{
		return HashKey(value->code);
	}

	bool Compare(uint32 key, AbbreviationTableEntry* value) const
	{
		return value->code == key;
	}

	AbbreviationTableEntry*& GetLink(AbbreviationTableEntry* value) const
	{
		return value->next;
	}
};


/**
 * @brief Parsed abbreviation table for one DWARF compilation unit.
 *
 * Owns the entries it parses and provides O(1) lookup by abbreviation
 * code.  Members of a doubly-linked list keyed by section offset so that
 * DwarfFile can share tables between units that reference the same offset.
 */
class AbbreviationTable : public DoublyLinkedListLinkImpl<AbbreviationTable> {
public:
								AbbreviationTable(off_t offset);
								~AbbreviationTable();

			status_t			Init(const void* section, off_t sectionSize);

			off_t				Offset() const	{ return fOffset; }

			bool				GetAbbreviationEntry(uint32 code,
									AbbreviationEntry& entry);

private:
			typedef BOpenHashTable<AbbreviationTableHashDefinition> EntryTable;

private:
			status_t			_ParseAbbreviationEntry(
									DataReader& abbrevReader, bool& _nullEntry);

private:
			off_t				fOffset;
			const uint8*		fData;
			off_t				fSize;
			EntryTable			fEntryTable;
};


#endif	// ABBREVIATION_TABLE_H
