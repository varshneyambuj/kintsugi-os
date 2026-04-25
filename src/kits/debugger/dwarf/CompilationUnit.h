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

/** @file CompilationUnit.h
    @brief BaseUnit specialisation modelling a DWARF .debug_info compilation unit. */

#ifndef COMPILATION_UNIT_H
#define COMPILATION_UNIT_H

#include <ObjectList.h>
#include <String.h>

#include "BaseUnit.h"
#include "LineNumberProgram.h"
#include "Types.h"


class DIECompileUnitBase;
class TargetAddressRangeList;


/**
 * @brief One DWARF compilation unit and the data that hangs off it.
 *
 * Bundles the line number program, file and directory tables, address
 * ranges, and the unit's top-level DIE alongside the BaseUnit fields.
 */
class CompilationUnit : public BaseUnit {
public:
								CompilationUnit(off_t headerOffset,
									off_t contentOffset,
									off_t totalSize,
									off_t abbreviationOffset,
									uint8 addressSize, bool isBigEndian,
									bool isDwarf64);
	virtual						~CompilationUnit();

	inline	target_addr_t		MaxAddress() const;

			DIECompileUnitBase*	UnitEntry() const	{ return fUnitEntry; }
			void				SetUnitEntry(DIECompileUnitBase* entry);

			TargetAddressRangeList* AddressRanges() const
									{ return fAddressRanges; }
			void				SetAddressRanges(
									TargetAddressRangeList* ranges);

			target_addr_t		AddressRangeBase() const;

			LineNumberProgram&	GetLineNumberProgram()
									{ return fLineNumberProgram; }

			bool				AddDirectory(const char* directory);
			int32				CountDirectories() const;
			const char*			DirectoryAt(int32 index) const;

			bool				AddFile(const char* fileName, int32 dirIndex);
			int32				CountFiles() const;
			const char*			FileAt(int32 index,
									const char** _directory = NULL) const;

	virtual	dwarf_unit_kind		Kind() const;

private:
			struct File;
			typedef BObjectList<BString, true> DirectoryList;
			typedef BObjectList<File, true> FileList;

private:
			DIECompileUnitBase*	fUnitEntry;
			TargetAddressRangeList* fAddressRanges;
			DirectoryList		fDirectories;
			FileList			fFiles;
			LineNumberProgram	fLineNumberProgram;
};


/**
 * @brief Returns the largest representable address for the unit's ABI.
 *
 * @return @c 0xffffffff on 32-bit targets, @c 0xffffffffffffffff on 64-bit.
 */
target_addr_t
CompilationUnit::MaxAddress() const
{
	return AddressSize() == 4 ? 0xffffffffULL : 0xffffffffffffffffULL;
}


#endif	// COMPILATION_UNIT_H
