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
 * MIT License. Copyright 2009, Ingo Weinhold; Copyright 2013, Rene Gollent.
 */

/** @file BaseUnit.h
    @brief Abstract base class for DWARF compilation and type units. */

#ifndef BASE_UNIT_H
#define BASE_UNIT_H


#include <String.h>

#include <Array.h>

#include "Types.h"


class AbbreviationTable;
class DebugInfoEntry;
struct SourceLanguageInfo;


/**
 * @brief Discriminator distinguishing compilation units from type units.
 */
enum dwarf_unit_kind {
	dwarf_unit_kind_compilation = 0,
	dwarf_unit_kind_type
};


/**
 * @brief Common state shared by .debug_info CUs and .debug_types TUs.
 *
 * Owns the array of DIEs parsed for the unit and provides offset-based
 * lookup; concrete subclasses add unit-specific fields like the line
 * number program (CU) or the type signature (TU).
 */
class BaseUnit {
public:
								BaseUnit(off_t headerOffset,
									off_t contentOffset,
									off_t totalSize,
									off_t abbreviationOffset,
									uint8 addressSize, bool isBigEndian,
									bool isDwarf64);
	virtual						~BaseUnit();

			off_t				HeaderOffset() const { return fHeaderOffset; }
			off_t				ContentOffset() const { return fContentOffset; }
			off_t 				RelativeContentOffset() const
									{ return fContentOffset - fHeaderOffset; }
			off_t				TotalSize() const	{ return fTotalSize; }
			off_t				ContentSize() const
									{ return fTotalSize
										- RelativeContentOffset(); }
			off_t				AbbreviationOffset() const
									{ return fAbbreviationOffset; }

			bool				ContainsAbsoluteOffset(off_t offset) const;

			uint8				AddressSize() const	{ return fAddressSize; }
			bool				IsBigEndian() const { return fIsBigEndian; }
			bool				IsDwarf64() const	{ return fIsDwarf64; }

			AbbreviationTable*	GetAbbreviationTable() const
									{ return fAbbreviationTable; }
			void				SetAbbreviationTable(
									AbbreviationTable* abbreviationTable);

			const SourceLanguageInfo* SourceLanguage() const
									{ return fSourceLanguage; }
			void				SetSourceLanguage(
									const SourceLanguageInfo* language);

			status_t			AddDebugInfoEntry(DebugInfoEntry* entry,
									off_t offset);
			int					CountEntries() const;
			void				GetEntryAt(int index, DebugInfoEntry*& entry,
									off_t& offset) const;
			DebugInfoEntry*		EntryForOffset(off_t offset) const;

	virtual	dwarf_unit_kind		Kind() const = 0;

private:
			off_t				fHeaderOffset;
			off_t				fContentOffset;
			off_t				fTotalSize;
			off_t				fAbbreviationOffset;
			AbbreviationTable*	fAbbreviationTable;
			const SourceLanguageInfo* fSourceLanguage;
			Array<DebugInfoEntry*> fEntries;
			Array<off_t>		fEntryOffsets;
			uint8				fAddressSize;
			bool				fIsBigEndian;
			bool				fIsDwarf64;
};


#endif	// BASE_UNIT_H
